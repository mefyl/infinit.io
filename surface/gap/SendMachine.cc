#include <boost/filesystem.hpp>

#include <elle/AtomicFile.hh>
#include <elle/archive/zip.hh>
#include <elle/container/vector.hh>
#include <elle/os/environ.hh>
#include <elle/os/path.hh>
#include <elle/os/file.hh>
#include <elle/serialization/json.hh>
#include <elle/system/system.hh>

#include <elle/serialize/extract.hh>
#include <elle/serialize/insert.hh>

#include <reactor/thread.hh>
#include <reactor/exception.hh>

#include <common/common.hh>

#include <frete/Frete.hh>
#include <frete/RPCFrete.hh>
#include <frete/TransferSnapshot.hh>

#include <papier/Identity.hh>

#include <station/Station.hh>

#include <aws/Credentials.hh>

#include <surface/gap/FilesystemTransferBufferer.hh>
#include <surface/gap/S3TransferBufferer.hh>
#include <surface/gap/SendMachine.hh>

ELLE_LOG_COMPONENT("surface.gap.SendMachine");

namespace surface
{
  namespace gap
  {
    using TransactionStatus = infinit::oracles::Transaction::Status;

    // Common factored constructor.
    SendMachine::SendMachine(Transaction& transaction,
                             uint32_t id,
                             std::shared_ptr<TransactionMachine::Data> data,
                             bool):
      TransactionMachine(transaction, id, std::move(data)),
      _create_transaction_state(
        this->_machine.state_make(
          "create transaction", std::bind(&SendMachine::_create_transaction, this))),
      _wait_for_accept_state(
        this->_machine.state_make(
          "wait for accept", std::bind(&SendMachine::_wait_for_accept, this))),
      _accepted("accepted barrier"),
      _rejected("rejected barrier")
    {
      this->_machine.transition_add(
        this->_create_transaction_state,
        this->_wait_for_accept_state);
      this->_machine.transition_add(
        this->_wait_for_accept_state,
        this->_finish_state,
        reactor::Waitables{&this->finished()},
        true);
      this->_machine.transition_add(
        this->_wait_for_accept_state,
        this->_transfer_core_state,
        reactor::Waitables{&this->_accepted},
        true,
        [&] () -> bool
        {
          // Pre Trigger the condition if the accepted barrier has already been
          // opened.
          return this->state().transactions().at(this->id())->data()->status ==
            TransactionStatus::accepted;
        }
        );
      this->_machine.transition_add(
        this->_wait_for_accept_state,
        this->_reject_state,
        reactor::Waitables{&this->_rejected},
        false,
        [&] () -> bool
        {
          // Pre Trigger the condition if the rejected barrier has already been
          // opened.
          return this->state().transactions().at(this->id())->data()->status ==
            TransactionStatus::rejected;
        }
        );
      // Cancel.
      this->_machine.transition_add(this->_create_transaction_state,
                                    this->_cancel_state,
                                    reactor::Waitables{&this->canceled()}, true);
      this->_machine.transition_add(this->_wait_for_accept_state,
                                    this->_cancel_state,
                                    reactor::Waitables{&this->canceled()}, true);
      // Exception.
      this->_machine.transition_add_catch(
        this->_create_transaction_state, this->_fail_state)
        .action_exception(
          [this] (std::exception_ptr e)
          {
            ELLE_WARN("%s: error while waiting for accept: %s",
                      *this, elle::exception_string(e));
          });
      this->_machine.transition_add_catch(
        this->_wait_for_accept_state, this->_fail_state)
        .action_exception(
          [this] (std::exception_ptr e)
          {
            ELLE_WARN("%s: error while creating transaction: %s",
                      *this, elle::exception_string(e));
          });
      this->_machine.state_changed().connect(
        [this] (reactor::fsm::State& state)
        {
          ELLE_LOG_COMPONENT("surface.gap.SendMachine.State");
          ELLE_TRACE("%s: entering %s", *this, state);
        });
      this->_machine.transition_triggered().connect(
        [this] (reactor::fsm::Transition& transition)
        {
          ELLE_LOG_COMPONENT("surface.gap.SendMachine.Transition");
          ELLE_TRACE("%s: %s triggered", *this, transition);
        });
    }

    SendMachine::~SendMachine()
    {
      this->_stop();
    }

    // Construct from server data.
    SendMachine::SendMachine(Transaction& transaction,
                             uint32_t id,
                             std::shared_ptr<TransactionMachine::Data> data):
      SendMachine(transaction, id, std::move(data), true)
    {
      ELLE_ASSERT(this->data() != nullptr);
      ELLE_WARN_SCOPE("%s: constructing machine for transaction data %s "
                      "(not found on local snapshots)", *this, *this->data());
      for (auto const& f: this->data()->files)
        this->_files.push_back(f);
      this->_run_from_snapshot();
    }

    // Construct for send.
    SendMachine::SendMachine(Transaction& transaction,
                             uint32_t id,
                             std::string const& recipient,
                             std::vector<std::string> files,
                             std::string const& message,
                             std::shared_ptr<TransactionMachine::Data> data):
      SendMachine(transaction, id, std::move(data), true)
    {
      ELLE_TRACE_SCOPE("%s: construct to send %s to %s",
                       *this, files, recipient);
      this->_files = std::move(files);
      this->_message = message;
      if (this->_files.empty())
        throw Exception(gap_no_file, "no files to send");
      ELLE_ASSERT_NEQ(this->_files.size(), 0u);
      // Copy filenames into data structure to be sent to meta.
      this->data()->files.resize(this->_files.size());
      std::transform(
        this->_files.begin(),
        this->_files.end(),
        this->data()->files.begin(),
        [] (std::string const& el)
        {
          return boost::filesystem::path(el).filename().string();
        });
      this->data()->is_directory = boost::filesystem::is_directory(
        *this->_files.begin());
      ELLE_ASSERT_EQ(this->data()->files.size(), this->_files.size());
      this->peer_id(recipient);
      this->_run(this->_create_transaction_state);
    }

    // Construct from snapshot.
    SendMachine::SendMachine(Transaction& transaction,
                             uint32_t id,
                             std::vector<std::string> files,
                             std::string const& message,
                             std::shared_ptr<TransactionMachine::Data> data):
      SendMachine(transaction, id, std::move(data), true)
    {
      ELLE_TRACE_SCOPE("%s: construct from snapshot", *this);
      this->_files = std::move(files);
      ELLE_ASSERT_NEQ(this->_files.size(), 0u);
      // Copy filenames into data structure to be sent to meta.
      this->data()->files.resize(this->_files.size());
      std::transform(
        this->_files.begin(),
        this->_files.end(),
        this->data()->files.begin(),
        [] (std::string const& el)
        {
          return boost::filesystem::path(el).filename().string();
        });
      ELLE_ASSERT_EQ(this->data()->files.size(), this->_files.size());
      this->_message = message;
      this->_run_from_snapshot();
    }

    void
    SendMachine::_run_from_snapshot()
    {
      bool started = false;
      boost::filesystem::path path = Snapshot::path(*this);
      if (exists(path))
      {
        ELLE_TRACE_SCOPE("%s: restore from snapshot: %s", *this, path);
        elle::AtomicFile source(path);
        source.read() << [&] (elle::AtomicFile::Read& read)
        {
          elle::serialization::json::SerializerIn input(read.stream());
          Snapshot snapshot(input);
          started = true;
          if (snapshot.current_state() == "cancel")
            this->_run(this->_cancel_state);
          else if (snapshot.current_state() == "create transaction")
            this->_run(this->_create_transaction_state);
          else if (snapshot.current_state() == "end")
            this->_run(this->_end_state);
          else if (snapshot.current_state() == "fail")
            this->_run(this->_fail_state);
          else if (snapshot.current_state() == "finish")
            this->_run(this->_finish_state);
          else if (snapshot.current_state() == "reject")
            this->_run(this->_reject_state);
          else if (snapshot.current_state() == "transfer core")
            this->_run(this->_transfer_core_state);
          else if (snapshot.current_state() == "wait for accept")
            this->_run(this->_wait_for_accept_state);
          else
          {
            ELLE_WARN("%s: unkown state in snapshot: %s",
                      *this, snapshot.current_state());
            started = false;
          }
        };
      }
      else
        ELLE_WARN("%s: missing snapshot: %s", *this, path);
      // Try to guess a decent starting state from the transaction status.
      if (started)
        return;
      ELLE_TRACE_SCOPE(
        "%s: deduce starting state from the transaction status: %s",
        *this, this->data()->status);
      switch (this->data()->status)
      {
        case TransactionStatus::initialized:
        case TransactionStatus::created:
          if (this->data()->id.empty())
            this->_run(this->_create_transaction_state);
          else
            this->_run(this->_wait_for_accept_state);
          break;
        case TransactionStatus::accepted:
          this->_run(this->_transfer_core_state);
          break;
        case TransactionStatus::finished:
          this->_run(this->_finish_state);
          break;
        case TransactionStatus::canceled:
          this->_run(this->_cancel_state);
          break;
        case TransactionStatus::failed:
          this->_run(this->_fail_state);
          break;
        case TransactionStatus::rejected:
          break;
        case TransactionStatus::started:
        case TransactionStatus::none:
          elle::unreachable();
      }
    }

    void
    SendMachine::transaction_status_update(TransactionStatus status)
    {
      ELLE_TRACE_SCOPE("%s: update with new transaction status %s",
                       *this, status);

      ELLE_ASSERT(reactor::Scheduler::scheduler() != nullptr);

      switch (status)
      {
        case TransactionStatus::accepted:
          ELLE_DEBUG("%s: open accepted barrier", *this)
            this->accepted().open();
          break;
        case TransactionStatus::canceled:
          ELLE_DEBUG("%s: open canceled barrier", *this)
            this->canceled().open();
          break;
        case TransactionStatus::failed:
          ELLE_DEBUG("%s: open failed barrier", *this)
            this->failed().open();
          break;
        case TransactionStatus::finished:
          ELLE_DEBUG("%s: open finished barrier", *this)
            this->finished().open();
          break;
        case TransactionStatus::rejected:
          ELLE_DEBUG("%s: open rejected barrier", *this)
            this->rejected().open();
          break;
        case TransactionStatus::initialized:
        case TransactionStatus::created:
          ELLE_TRACE("%s: ignoring status update to %s", *this, status);
          break;
        case TransactionStatus::none:
        case TransactionStatus::started:
          ELLE_ABORT("%s: invalid status update to %s", *this, status);
      }
    }

    void
    SendMachine::_create_transaction()
    {
      this->gap_state(gap_transaction_new);
      ELLE_TRACE_SCOPE("%s: create transaction", *this);
      int64_t size = 0;
      for (auto const& file: this->_files)
      {
        auto _size = elle::os::file::size(file);
        size += _size;
      }
      ELLE_DEBUG("%s: total file size: %s", *this, size);
      this->data()->total_size = size;

      auto first_file = boost::filesystem::path(*(this->_files.cbegin()));

      std::list<std::string> file_list{this->_files.size()};
      std::transform(
        this->_files.begin(),
        this->_files.end(),
        file_list.begin(),
        [] (std::string const& el) {
          return boost::filesystem::path(el).filename().string();
        });
      ELLE_ASSERT_EQ(file_list.size(), this->_files.size());

      // Change state to SenderCreateTransaction once we've calculated the file
      // size and have the file list.
      ELLE_TRACE("%s: Creating transaction, first_file=%s, dir=%s",
                 *this, first_file,
                 boost::filesystem::is_directory(first_file));
      this->transaction_id(
        this->state().meta().create_transaction(
          this->peer_id(),
          this->data()->files,
          this->data()->files.size(),
          this->data()->total_size,
          boost::filesystem::is_directory(first_file),
          this->state().device().id,
          this->_message
          ).created_transaction_id
        );

      ELLE_TRACE("%s: created transaction %s", *this, this->transaction_id());

      // XXX: Ensure recipient is an id.
      this->peer_id(this->state().user(this->peer_id(), true).id);

      auto const& peer = this->state().user(this->peer_id());

      bool ghost = false;

      if (peer.ghost())
        ghost = true;

      if (this->state().metrics_reporter())
        this->state().metrics_reporter()->transaction_created(
          this->transaction_id(),
          this->state().me().id,
          this->peer_id(),
          this->data()->files.size(),
          size,
          this->_message.length(),
          ghost);

      // Populate the frete.
      this->frete().save_snapshot();

      this->state().meta().update_transaction(this->transaction_id(),
                                              TransactionStatus::initialized);
    }

    void
    SendMachine::_wait_for_accept()
    {
      ELLE_TRACE_SCOPE("%s: waiting for peer to accept or reject", *this);

      auto peer = this->state().user(this->peer_id());
      if (peer.ghost())
        this->_ghost_cloud_upload();
      else if (!peer.ghost() && !peer.online())
      {
        this->_cloud_operation();
      }
      else
        this->gap_state(gap_transaction_waiting_accept);
    }

    void
    SendMachine::_transfer_operation(frete::RPCFrete& frete)
    {
      auto start_time = boost::posix_time::microsec_clock::universal_time();
      _fetch_peer_key(true);
      // save snapshot to get correct filepaths
      this->_save_transfer_snapshot();
      ELLE_TRACE_SCOPE("%s: transfer operation, resuming at %s",
                       *this, this->frete().progress());
      metrics::TransferExitReason exit_reason = metrics::TransferExitReasonUnknown;
      std::string exit_message;
      frete::Frete::FileSize total_bytes_transfered = 0;
      frete::Frete::FileSize total_size = this->frete().full_size();
      float initial_progress = this->frete().progress();
      if (auto& mr = state().metrics_reporter())
      {
        auto now = boost::posix_time::microsec_clock::universal_time();
        mr->transaction_transfer_begin(
          this->transaction_id(),
          infinit::metrics::TransferMethodP2P,
          float((now - start_time).total_milliseconds()) / 1000.0f);
      }
      elle::SafeFinally write_end_message([&,this]
        {
          if (auto& mr = state().metrics_reporter())
          {
            auto now = boost::posix_time::microsec_clock::universal_time();
            float duration =
              float((now - start_time).total_milliseconds()) / 1000.0f;
            mr->transaction_transfer_end(this->transaction_id(),
                                         metrics::TransferMethodP2P,
                                         duration,
                                         total_bytes_transfered,
                                         exit_reason,
                                         exit_message);
          }
        });
      try
      {
        elle::With<reactor::Scope>() << [&] (reactor::Scope& scope)
        {
          scope.run_background(
            elle::sprintf("frete get %s", this->id()),
            [&frete] ()
            {
              frete.run();
            });
          scope.wait();
        };
        exit_reason = metrics::TransferExitReasonFinished;
        total_bytes_transfered =
          total_size * (this->frete().progress() - initial_progress);
      }
      catch(reactor::Terminate const&)
      {
        total_bytes_transfered =
          total_size * (this->frete().progress() - initial_progress);
        exit_reason = metrics::TransferExitReasonTerminated;
        throw;
      }
      catch(...)
      {
        total_bytes_transfered =
         total_size * (this->frete().progress() - initial_progress);
        exit_reason = metrics::TransferExitReasonError;
        exit_message = elle::exception_string();
        throw;
      }
    }

    static std::streamsize const chunk_size = 1 << 18;

    typedef std::pair<frete::Frete::FileSize, frete::Frete::FileID> Position;
    static frete::Frete::FileSize
    progress_from(std::vector<std::pair<std::string, frete::Frete::FileSize>> const& infos, const Position& p)
    {
      frete::Frete::FileSize result = 0;
      for (unsigned i=0; i<p.first; ++i)
        result += infos[i].second;
      result += p.second;
      return result;
    }

    void
    SendMachine::_ghost_cloud_upload()
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
        this->gap_state(gap_transaction_transferring);
        ELLE_TRACE_SCOPE("%s: ghost_cloud_upload", *this);
        this->_save_transfer_snapshot();
        typedef boost::filesystem::path path;
        path source_file_path;
        FileSize source_file_size;
        if (this->frete().count() > 1)
        { // Our users might not appreciate downloading zillion of files from
          // their browser: make an archive
          // make an archive name from data
          path archive_name = archive_info().first;
          // Use transfer data information to archive the files. This is
          // what was passed by the user, and what we will flatten.
          // That way if user selects a directory it will be preserved.
          std::vector<boost::filesystem::path> sources(
            this->_files.begin(),
            this->_files.end());
          this->_save_transfer_snapshot();
          auto tmpdir = boost::filesystem::temp_directory_path() / transaction_id();
          boost::filesystem::create_directories(tmpdir);
          path archive_path = path(tmpdir) / archive_name;
          if (!boost::filesystem::exists(archive_path) ||
              !this->frete().transfer_snapshot()->archived())
          {
            ELLE_DEBUG("%s: archiving transfer files into %s", *this, archive_path);
            elle::archive::zip(sources, archive_path, [](boost::filesystem::path const& path)
              {
                std::string p(path.string());
                // check if p maches our renaming scheme
                size_t pos_beg = p.find_last_of('(');
                size_t pos_end = p.find_last_of(')');
                if (pos_beg != p.npos && pos_end != p.npos &&
                  (pos_end == p.size()-1 || p[pos_end+1] == '.'))
                {
                  try
                  {
                    std::string sequence =  p.substr(pos_beg + 1, pos_end-pos_beg-1);
                    unsigned int v = boost::lexical_cast<unsigned int>(sequence);
                    std::string result = p.substr(0, pos_beg+1)
                      + boost::lexical_cast<std::string>(v+1)
                      + p.substr(pos_end);
                    return result;
                  }
                  catch(const boost::bad_lexical_cast& blc)
                  {// go on
                  }
                }
                return path.stem().string() + " (1)" + path.extension().string();
              });

            this->frete().transfer_snapshot()->archived(true);
            this->_save_transfer_snapshot();
          }
          else
          {
            ELLE_DEBUG("%s: archive already present at %s", *this, archive_path);
          }
          source_file_path = archive_path;
        }
        else
        {
          source_file_path = *this->_files.begin();
        }
        source_file_size = boost::filesystem::file_size(source_file_path);
        std::string source_file_name = source_file_path.filename().string();
        ELLE_TRACE("%s: will ghost-cloud-upload %s of size %s",
                   *this, source_file_path, source_file_size);

        aws::S3 handler(this->make_aws_credentials_getter());

        typedef frete::Frete::FileSize FileSize;

        // AWS constraints: no more than 10k chunks, at least 5Mo block size
        FileSize chunk_size = std::max(FileSize(5*1024*1024), source_file_size / 9500);
        int chunk_count = source_file_size / chunk_size + ((source_file_size % chunk_size)? 1:0);
        ELLE_TRACE("%s: using chunk size of %s, with %s chunks",
                   *this, chunk_size, chunk_count);
        // Load our own snapshot that contains the upload uid
        auto raw_snapshot_path =
          this->transaction().snapshots_directory() /  "ghost_id.snapshot";
        std::string upload_id;
        {
          std::ifstream ifs(raw_snapshot_path.string());
          ifs >> upload_id;
        }
        ELLE_DEBUG("%s: tried to reload id from %s, got %s",
                   *this, raw_snapshot_path, upload_id);
        std::vector<aws::S3::MultiPartChunk> chunks;
        int next_chunk = 0;
        int max_check_id = 0; // check for chunk presence up to that id
        int start_check_index = 0; // check for presence from that chunks index
        if (upload_id.empty())
        {
          //FIXME: pass correct mime type for non-zip case
          upload_id = handler.multipart_initialize(source_file_name);
          std::ofstream ofs(raw_snapshot_path.string());
          ofs << upload_id;
          ELLE_DEBUG("%s: saved id %s to %s",
                     *this, upload_id, raw_snapshot_path);
        }
        else
        { // Fetch block list
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
            for (int i=0; i<int(chunks.size()); ++i)
              if (chunks[i].first != i)
                break;
              else
                next_chunk = i+1;
            start_check_index = next_chunk;
            max_check_id = chunks.back().first;
          }
          ELLE_DEBUG("Will resume at chunk %s", next_chunk);
        }
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
              for (unsigned i=start_check_index;
                   i<chunks.size() && chunks[i].first <= max_check_id;
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
              local_chunk*chunk_size, chunk_size);
            std::string etag = handler.multipart_upload(
              source_file_name, upload_id,
              buffer,
              local_chunk);
            // Now, totally fake progress on the original frete by
            // updating the global progress, and not individual files
            // progress. That way we don't produce fake snapshot state data
            // for further cloud upload.
            auto& snapshot = this->frete().transfer_snapshot();
            // Be careful about setting progress back, chunks finish in parallel
            float p = float(local_chunk) / float(chunk_count);
            total_bytes_transfered += buffer.size();
            auto progress_maybe = local_chunk * snapshot->total_size() / chunk_count;
            if (snapshot->progress() < progress_maybe)
            {
              ELLE_DEBUG("Setting progress to %s", p);
              snapshot->progress(progress_maybe);
            }
            chunks.push_back(std::make_pair(local_chunk, etag));
          }
        };
        int num_threads = 4;
        std::string env_num_threads =
          elle::os::getenv("INFINIT_NUM_GHOST_CLOUD_UPLOAD_THREAD", "");
        if (!env_num_threads.empty())
          num_threads = boost::lexical_cast<unsigned>(env_num_threads);
        elle::With<reactor::Scope>() << [&] (reactor::Scope& scope)
        {
          for (int i=0; i<num_threads; ++i)
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
        this->frete().transfer_snapshot()->progress(
          this->frete().transfer_snapshot()->total_size());
        // mark transfer as raw-finished
        this->gap_state(gap_transaction_finished);
        this->finished().open();
        exit_reason = metrics::TransferExitReasonFinished;
      } // try
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
    SendMachine::_cloud_operation()
    {
      if (!elle::os::getenv("INFINIT_NO_CLOUD_BUFFERING", "").empty())
      {
        ELLE_DEBUG("%s: cloud buffering disabled by configuration", *this);
        return;
      }
      this->gap_state(gap_transaction_transferring);
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
                                         metrics::TransferMethodCloud,
                                         duration,
                                         total_bytes_transfered,
                                         exit_reason,
                                         exit_message);
          }
        });
      try
      {
        ELLE_TRACE_SCOPE("%s: upload to the cloud", *this);
        auto& frete = this->frete();
        _fetch_peer_key(true);

        auto& snapshot = *frete.transfer_snapshot();
        // Save snapshot of what eventual direct upload already did right now.
        this->_save_transfer_snapshot();
        FilesystemTransferBufferer::Files files;
        for (frete::Frete::FileID file_id = 0;
             file_id < snapshot.count();
             ++file_id)
        {
          auto& file = snapshot.file(file_id);
          files.push_back(std::make_pair(file.path(), file.size()));
        }
        bool cloud_debug =
          !elle::os::getenv("INFINIT_CLOUD_FILEBUFFERER", "").empty();
        std::unique_ptr<TransferBufferer> bufferer;
        if (cloud_debug)
        {
          bufferer.reset(
            new FilesystemTransferBufferer(*this->data(),
                                           "/tmp/infinit-buffering",
                                           snapshot.count(),
                                           snapshot.total_size(),
                                           files,
                                           frete.key_code()));
        }
        else
        {
          auto get_creds = this->make_aws_credentials_getter();
          bufferer.reset(new S3TransferBufferer(*this->data(),
                                                get_creds,
                                                snapshot.count(),
                                                snapshot.total_size(),
                                                files,
                                                frete.key_code()));
        }
        if (auto& mr = state().metrics_reporter())
        {
          auto now = boost::posix_time::microsec_clock::universal_time();
          mr->transaction_transfer_begin(
            this->transaction_id(),
            infinit::metrics::TransferMethodCloud,
            float((now - start_time).total_milliseconds()) / 1000.0f);
        }
        /* Pipelined cloud upload with periodic local snapshot update
        */
        int num_threads = 8;
        std::string snum_threads =
          elle::os::getenv("INFINIT_NUM_CLOUD_UPLOAD_THREAD", "");
        if (!snum_threads.empty())
          num_threads = boost::lexical_cast<int>(snum_threads);
        typedef frete::Frete::FileSize FileSize;
        typedef frete::Frete::FileID FileID;
        FileSize transfer_since_snapshot = 0;
        FileID current_file = FileID(-1);
        FileSize current_position = 0;
        FileSize current_file_size = 0;
        // per-thread last pos
        std::vector<Position> last_acknowledge_block(
          num_threads, std::make_pair(0,0));
        FileSize acknowledge_position = 0;
        bool save_snapshot = false; // reqest one-shot snapshot save
        auto pipeline_cloud_upload = [&, this](int id)
        {
          while(true)
          {
            while (current_position >= current_file_size)
            {
              ++current_file;
              if (current_file >= snapshot.count())
                return;
              auto& file = snapshot.file(current_file);
              current_file_size = file.size();
              current_position = file.progress();
            }
            ELLE_DEBUG_SCOPE("%s: buffer file %s at offset %s/%s in the cloud",
                             *this, current_file, current_position, current_file_size);
            FileSize local_file = current_file;
            FileSize local_position = current_position;
            current_position += chunk_size;
            auto block = frete.encrypted_read_acknowledge(
              local_file, local_position, chunk_size, acknowledge_position);
            if (save_snapshot)
            {
              this->_save_transfer_snapshot();
              save_snapshot = false;
            }
            auto& buffer = block.buffer();
            bufferer->put(local_file, local_position, buffer.size(), buffer);
            transfer_since_snapshot += buffer.size();
            total_bytes_transfered += buffer.size();
            last_acknowledge_block[id] = std::make_pair(local_file, local_position);
            if (transfer_since_snapshot >= 1000000)
            {
              // Update acknowledge position
              // First find the smallest value in per-thread last_ack
              // Since each thread is fetching blocks monotonically,
              // we know all blocks before that are acknowledged
              Position pmin = *std::min_element(
                last_acknowledge_block.begin(),
                last_acknowledge_block.end(),
                [](const Position& a, const Position&b)
                {
                  if (a.first != b.first)
                    return a.first < b.first;
                  else
                    return a.second < b.second;
                });
              acknowledge_position = progress_from(this->frete().files_info(), pmin);
              // need one call to read_acknowledge for save to have effect:async
              save_snapshot = true;
            }
          }
        };
        elle::With<reactor::Scope>() << [&] (reactor::Scope& scope)
        {
          for (int i=0; i<num_threads; ++i)
            scope.run_background(elle::sprintf("cloud %s", i),
                                 std::bind(pipeline_cloud_upload, i));
          scope.wait();
        };
        // acknowledge last block and save snapshot
        frete.encrypted_read_acknowledge(0, 0, 0, this->frete().full_size());
        this->_save_transfer_snapshot();
        this->gap_state(gap_transaction_cloud_buffered);
        exit_reason = metrics::TransferExitReasonFinished;
      } // try
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
    SendMachine::_save_transfer_snapshot()
    {
      this->frete().save_snapshot();
    }

    float
    SendMachine::progress() const
    {
      if (this->_frete != nullptr)
        return this->_frete->progress();
      return 0.0f;
    }

    bool
    SendMachine::_fetch_peer_key(bool assert_success)
    {
      auto& frete = this->frete();
      if (frete.has_peer_key())
        return true;
      auto k = this->state().user(this->peer_id(), true).public_key;
      if (k.empty() && !assert_success)
        return false;
      ELLE_ASSERT(!k.empty());
      ELLE_DEBUG("restoring key from %s", k);
      infinit::cryptography::PublicKey peer_K;
      peer_K.Restore(k);
      frete.set_peer_key(peer_K);
      return true;
    }

    frete::Frete&
    SendMachine::frete()
    {
      if (this->_frete == nullptr)
      {
        ELLE_TRACE_SCOPE("%s: initialize frete", *this);
        this->_frete = elle::make_unique<frete::Frete>(
          this->transaction_id(),
          this->state().identity().pair(),
          this->transaction().snapshots_directory() / "frete.snapshot");
         _fetch_peer_key(false);

        if (this->_frete->count())
        { // Reloaded from snapshot
          //... not much to validate here, use previously snapshoted
          // directory content
          // We could recreate a 2nd frete, reperforming the add task,
          // and compare the resulting effective file lists.
        }
        else
        { // No snapshot yet, fill file list
          ELLE_DEBUG("%s: No snapshot loaded, populating files", *this);
          for (std::string const& file: this->_files)
            this->_frete->add(file);
        }
      }
      return *this->_frete;
    }

    std::unique_ptr<frete::RPCFrete>
    SendMachine::rpcs(infinit::protocol::ChanneledStream& channels)
    {
      return elle::make_unique<frete::RPCFrete>(this->frete(), channels);
    }

    /*----------.
    | Printable |
    `----------*/

    void
    SendMachine::cleanup()
    {
      if (this->data()->id.empty())
      { // Early failure, no transaction_id -> nothing to clean up
        return;
      }
      if (this->_frete)
        this->frete().remove_snapshot();
      // clear temporary session directory
      std::string tid = transaction_id();
      ELLE_ASSERT(!tid.empty());
      ELLE_ASSERT(tid.find('/') == tid.npos);
      auto tmpdir = boost::filesystem::temp_directory_path() / tid;
      ELLE_LOG("%s: clearing temporary directory %s",
               *this, tmpdir);
      boost::filesystem::remove_all(tmpdir);
    }

    void
    SendMachine::_cloud_synchronize()
    {
      // Nothing to do, don't keep uploading if the user is downloading
    }
  }
}
