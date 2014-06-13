#include <surface/gap/SendMachine.hh>

#include <boost/filesystem.hpp>

#include <elle/archive/zip.hh>
#include <elle/container/vector.hh>
#include <elle/os/environ.hh>
#include <elle/serialize/extract.hh>
#include <elle/serialize/insert.hh>
#include <elle/system/system.hh>

#include <reactor/thread.hh>
#include <reactor/exception.hh>

#include <aws/Credentials.hh>
#include <aws/S3.hh>

#include <common/common.hh>
#include <papier/Identity.hh>
#include <station/Station.hh>

ELLE_LOG_COMPONENT("surface.gap.SendMachine");

namespace surface
{
  namespace gap
  {
    using TransactionStatus = infinit::oracles::Transaction::Status;

    /*-------------.
    | Construction |
    `-------------*/

    SendMachine::SendMachine(Transaction& transaction,
                             uint32_t id,
                             std::vector<std::string> files,
                             std::shared_ptr<Data> data)
      : Super(transaction, id, std::move(data))
      , _plain_progress(0)
      , _create_transaction_state(
        this->_machine.state_make(
          "create transaction",
          std::bind(&SendMachine::_create_transaction, this)))
      , _files(files)
    {
      if (this->files().empty())
        throw Exception(gap_no_file, "no files to send");
      // Cancel.
      this->_machine.transition_add(this->_create_transaction_state,
                                    this->_cancel_state,
                                    reactor::Waitables{&this->canceled()}, true);
      // Exception.
      this->_machine.transition_add_catch(
        this->_create_transaction_state, this->_fail_state)
        .action_exception(
          [this] (std::exception_ptr e)
          {
            ELLE_WARN("%s: error while creating transaction: %s",
                      *this, elle::exception_string(e));
          });
    }

    SendMachine::~SendMachine()
    {}

    /*-------------.
    | Plain upload |
    `-------------*/

    float
    SendMachine::progress() const
    {
      auto progress = this->_plain_progress;
      for (auto const& chunk: this->_plain_progress_chunks)
        progress += chunk.second;
      return progress;
    }

    void
    SendMachine::_plain_upload()
    {
      // exit information for factored metrics writer.
      // needs to stay out of the try, as catch clause will fill those
      auto start_time = boost::posix_time::microsec_clock::universal_time();
      metrics::TransferExitReason exit_reason = metrics::TransferExitReasonUnknown;
      std::string exit_message;
      uint64_t total_bytes_transfered = 0;
      elle::SafeFinally write_end_message([&,this]
        {
          if (auto& mr = state().metrics_reporter())
          {
            auto now = boost::posix_time::microsec_clock::universal_time();
            float duration =
              float((now - start_time).total_milliseconds()) / 1000.0f;
            mr->transaction_transfer_end(this->transaction_id(),
                                         metrics::TransferMethodGhostCloud,
                                         duration,
                                         total_bytes_transfered,
                                         exit_reason,
                                         exit_message);
          }
        });
      try
      {
        typedef frete::Frete::FileSize FileSize;
        this->gap_status(gap_transaction_transferring);
        ELLE_TRACE_SCOPE("%s: start plain upload", *this);
        typedef boost::filesystem::path path;
        path source_file_path;
        FileSize file_size;
        auto archive = this->archive_info();
        if (archive.second)
        {
          // Our users might not appreciate downloading zillion of files from
          // their browser: make an archive make an archive name from data
          path archive_name = archive.first;
          // Use transfer data information to archive the files. This is what
          // was passed by the user, and what we will flatten.  That way if user
          // selects a directory it will be preserved.
          std::vector<boost::filesystem::path> sources(
            this->_files.begin(),
            this->_files.end());
          auto tmpdir = boost::filesystem::temp_directory_path() / transaction_id();
          boost::filesystem::create_directories(tmpdir);
          path archive_path = path(tmpdir) / archive_name;
          if (!exists(archive_path) || !this->transaction().archived())
          {
            ELLE_DEBUG("%s: archiving transfer files into %s", *this, archive_path);
            auto renaming_callback =
              [](boost::filesystem::path const& path)
              { // File renaming callback
                std::string p(path.string());
                // Check if p maches our renaming scheme.
                size_t pos_beg = p.find_last_of('(');
                size_t pos_end = p.find_last_of(')');
                if (pos_beg != p.npos && pos_end != p.npos &&
                  (pos_end == p.size()-1 || p[pos_end+1] == '.'))
                {
                  try
                  {
                    std::string sequence = p.substr(pos_beg + 1, pos_end-pos_beg-1);
                    unsigned int v = boost::lexical_cast<unsigned int>(sequence);
                    std::string result = p.substr(0, pos_beg+1)
                      + boost::lexical_cast<std::string>(v+1)
                      + p.substr(pos_end);
                    return result;
                  }
                  catch(const boost::bad_lexical_cast& blc)
                  {
                    // Go on.
                  }
                }
                return path.stem().string() + " (1)" + path.extension().string();
              };
            ELLE_TRACE("%s: Begin archiving thread", *this);
            reactor::background(
              [&]
              {
                elle::archive::zip(sources, archive_path, renaming_callback);
              });
            ELLE_TRACE("%s: Join archiving thread", *this);
            this->transaction().archived(true);
          }
          else
          {
            ELLE_DEBUG("%s: archive already present at %s", *this, archive_path);
          }
          source_file_path = archive_path;
        }
        else
          source_file_path = *this->_files.begin();
        file_size = boost::filesystem::file_size(source_file_path);
        std::string source_file_name = source_file_path.filename().string();
        ELLE_TRACE("%s: will ghost-cloud-upload %s of size %s",
                   *this, source_file_path, file_size);
        auto get_credentials = [this] (bool first_time)
          {
            return this->_aws_credentials(first_time);
          };
        aws::S3 handler(get_credentials);
        typedef frete::Frete::FileSize FileSize;
        auto const& config = this->transaction().state().configuration();
        // AWS constraints: no more than 10k chunks, at least 5Mo block size
        FileSize default_chunk_size(
          std::max(config.s3.multipart_upload.chunk_size, 5 * 1024 * 1024));
        FileSize chunk_size = std::max(default_chunk_size, file_size / 9500);
        int chunk_count =
          file_size / chunk_size + ((file_size % chunk_size) ? 1 : 0);
        ELLE_TRACE("%s: using chunk size of %s, with %s chunks",
                   *this, chunk_size, chunk_count);
        std::vector<aws::S3::MultiPartChunk> chunks;
        int next_chunk = 0;
        int max_check_id = 0; // check for chunk presence up to that id
        int start_check_index = 0; // check for presence from that chunks index
        std::string upload_id;
        if (!transaction().plain_upload_uid())
        {
          //FIXME: pass correct mime type for non-zip case
          upload_id = handler.multipart_initialize(source_file_name);
          transaction().plain_upload_uid(upload_id);
          transaction()._snapshot_save();
          ELLE_TRACE("%s: saved upload ID %s to snapshot",
                     *this, *transaction().plain_upload_uid());
        }
        else
        { // Fetch block list
          upload_id = *transaction().plain_upload_uid();
          chunks = handler.multipart_list(source_file_name, upload_id);
          std::sort(chunks.begin(), chunks.end(),
            [](aws::S3::MultiPartChunk const& a, aws::S3::MultiPartChunk const& b)
            {
              return a.first < b.first;
            });
          if (!chunks.empty())
          {
            // We expect missing blocks potentially, but only at the end
            //, ie we expect contiguous blocks 0 to (num_blocks - pipeline_size)
            for (int i = 0; i < int(chunks.size()); ++i)
              if (chunks[i].first != i)
                break;
              else
                next_chunk = i+1;
            start_check_index = next_chunk;
            max_check_id = chunks.back().first;
          }
          ELLE_DEBUG("Will resume at chunk %s", next_chunk);
        }
        auto chunk_uploaded = next_chunk;
        this->_plain_progress =
          float(chunk_uploaded) / float(chunk_count);
        if (auto& mr = state().metrics_reporter())
        {
          auto now = boost::posix_time::microsec_clock::universal_time();
          mr->transaction_transfer_begin(
            this->transaction_id(),
            infinit::metrics::TransferMethodGhostCloud,
            float((now - start_time).total_milliseconds()) / 1000.0f);
        }
        // start pipelined upload
        auto pipeline_upload = [&, this](int id)
        {
          while (true)
          {
            // fetch a chunk number
            if (next_chunk >= chunk_count)
              return;
            int local_chunk = next_chunk++;
            if (local_chunk <= max_check_id)
            { // maybe we have it
              bool has_it = false;
              for (unsigned i = start_check_index;
                   i < chunks.size() && chunks[i].first <= max_check_id;
                   ++i)
              {
                if (chunks[i].first == local_chunk)
                {
                  has_it = true;
                  break;
                }
              }
              if (has_it)
                continue;
            }
            // upload it
            ELLE_DEBUG("%s: uploading chunk %s", *this, local_chunk);
            auto buffer = elle::system::read_file_chunk(
              source_file_path,
              local_chunk * chunk_size, chunk_size);
            auto size = buffer.size();
            this->_plain_progress_chunks[local_chunk] = 0;
            std::string etag = handler.multipart_upload(
              source_file_name, upload_id,
              buffer,
              local_chunk,
              std::function<void (int)>(
                [this, size, local_chunk, chunk_count] (int uploaded)
                {
                  this->_plain_progress_chunks[local_chunk] =
                    float(uploaded) / size / chunk_count;
                }));
            ++chunk_uploaded;
            this->_plain_progress =
              float(chunk_uploaded) / float(chunk_count);
            total_bytes_transfered += size;
            chunks.push_back(std::make_pair(local_chunk, etag));
            this->_plain_progress_chunks.erase(local_chunk);
          }
        };
        int num_threads = config.s3.multipart_upload.parallelism;
        std::string env_num_threads =
          elle::os::getenv("INFINIT_NUM_PLAIN_UPLOAD_THREAD", "");
        if (!env_num_threads.empty())
          num_threads = boost::lexical_cast<unsigned>(env_num_threads);
        elle::With<reactor::Scope>() << [&] (reactor::Scope& scope)
        {
          for (int i = 0; i < num_threads; ++i)
            scope.run_background(elle::sprintf("cloud %s", i),
                                 std::bind(pipeline_upload, i));
          scope.wait();
        };
        std::sort(chunks.begin(), chunks.end(),
          [](aws::S3::MultiPartChunk const& a, aws::S3::MultiPartChunk const& b)
            {
              return a.first < b.first;
            });
        handler.multipart_finalize(source_file_name, upload_id, chunks);
        // mark transfer as raw-finished
        this->gap_status(gap_transaction_finished);
        this->finished().open();
        exit_reason = metrics::TransferExitReasonFinished;
      } // try
      catch (boost::filesystem::filesystem_error const& e)
      {
        exit_message = e.what();
        exit_reason = infinit::metrics::TransferExitReasonError;
        if (e.code() == boost::system::errc::no_such_file_or_directory)
        {
          ELLE_WARN("%s: source file disappeared, cancel : %s",
                    *this, e.what());
          this->cancel();
          throw;
        }
        else
        {
          ELLE_WARN("%s: source file corrupted (%s), cancel",
                    *this, e.what());
          this->cancel();
          throw;
        }
      }
      catch(reactor::Terminate const&)
      {
        exit_reason = metrics::TransferExitReasonTerminated;
        throw;
      }
      catch(...)
      {
        exit_reason = metrics::TransferExitReasonError;
        exit_message = elle::exception_string();
        throw;
      }
    }

    void
    SendMachine::cleanup()
    {
      if (this->data()->id.empty())
      { // Early failure, no transaction_id -> nothing to clean up
        return;
      }
      // clear temporary session directory
      std::string tid = transaction_id();
      ELLE_ASSERT(!tid.empty());
      ELLE_ASSERT(tid.find('/') == tid.npos);
      auto tmpdir = boost::filesystem::temp_directory_path() / tid;
      ELLE_LOG("%s: clearing temporary directory %s",
               *this, tmpdir);
      boost::filesystem::remove_all(tmpdir);
    }

    std::pair<std::string, bool>
    SendMachine::archive_info()
    {
      auto const& files = this->files();
      if (files.size() == 1)
      {
        boost::filesystem::path file(*files.begin());
        if (is_directory(status(file)))
          return std::make_pair(
            file.filename().replace_extension("zip").string(), true);
        else
          return std::make_pair(file.filename().string(), false);
      }
      else
        return std::make_pair(elle::sprintf("%s files.zip", files.size()),
                              true);
    }
  }
}
