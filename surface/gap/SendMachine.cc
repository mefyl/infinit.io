#include <boost/filesystem.hpp>

#include <elle/os/environ.hh>
#include <elle/os/path.hh>
#include <elle/os/file.hh>

#include <elle/serialize/extract.hh>
#include <elle/serialize/insert.hh>

#include <reactor/thread.hh>
#include <reactor/exception.hh>

#include <common/common.hh>

#include <frete/Frete.hh>
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
    SendMachine::SendMachine(surface::gap::State const& state,
                             uint32_t id,
                             std::shared_ptr<TransactionMachine::Data> data,
                             bool):
      TransactionMachine(state, id, std::move(data)),
      _create_transaction_state(
        this->_machine.state_make(
          "create transaction", std::bind(&SendMachine::_create_transaction, this))),
      _wait_for_accept_state(
        this->_machine.state_make(
          "wait for accept", std::bind(&SendMachine::_wait_for_accept, this))),
      _accepted("accepted barrier"),
      _rejected("rejected barrier")
    {
      ELLE_TRACE("Creating SendMachine: id %s sid %s sdid %s rid %s rdid %s",
                this->data()->id,
                this->data()->sender_id,
                this->data()->sender_device_id,
                this->data()->recipient_id,
                this->data()->recipient_device_id);
      this->_machine.transition_add(
        this->_create_transaction_state,
        this->_wait_for_accept_state);

      this->_machine.transition_add(
        this->_wait_for_accept_state,
        this->_transfer_core_state,
        reactor::Waitables{&this->_accepted},
        true,
        [&] () -> bool
        {
          // Pre Trigger the condition if the accepted barrier has already been
          // opened.
          return this->state().transactions().at(this->id()).data()->status ==
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
          return this->state().transactions().at(this->id()).data()->status ==
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
        this->_create_transaction_state, this->_fail_state);
      this->_machine.transition_add_catch(
        this->_wait_for_accept_state, this->_fail_state);
    }

    SendMachine::~SendMachine()
    {
      this->_stop();
    }

    SendMachine::Snapshot
    SendMachine::_make_snapshot() const
    {
      return Snapshot{*this->data(), this->_current_state, this->_files, this->_message};
    }

    SendMachine::SendMachine(surface::gap::State const& state,
                             uint32_t id,
                             std::shared_ptr<TransactionMachine::Data> data):
      SendMachine(state, id, std::move(data), true)
    {
      ELLE_WARN_SCOPE("%s: constructing machine for transaction data %s "
                      "(not found on local snapshots)",
                       *this, this->data());
      // set _files from data
      for (auto const& f: this->data()->files)
      {
        this->_files.insert(f);
      }
      switch (this->data()->status)
      {
        case TransactionStatus::initialized:
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
        case TransactionStatus::created:
          break;
        case TransactionStatus::started:
        case TransactionStatus::none:
          elle::unreachable();
      }
    }

    SendMachine::SendMachine(surface::gap::State const& state,
                             uint32_t id,
                             std::string const& recipient,
                             std::unordered_set<std::string>&& files,
                             std::string const& message,
                             std::shared_ptr<TransactionMachine::Data> data):
      SendMachine(state, id, std::move(data), true)
    {
      ELLE_TRACE_SCOPE("%s: construct to send %s to %s",
                       *this, files, recipient);

      this->_message = message;

      if (files.empty())
        throw Exception(gap_no_file, "no files to send");

      std::swap(this->_files, files);

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

      this->peer_id(recipient);
      this->_run(this->_create_transaction_state);
    }

    SendMachine::SendMachine(surface::gap::State const& state,
                             uint32_t id,
                             std::unordered_set<std::string> files,
                             TransactionMachine::State current_state,
                             std::string const& message,
                             std::shared_ptr<TransactionMachine::Data> data):
      SendMachine(state, id, std::move(data), true)
    {
      ELLE_TRACE_SCOPE("%s: construct from snapshot starting at %s",
                       *this, current_state);
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

      this->_current_state = current_state;
      this->_message = message;
      switch (current_state)
      {
        case TransactionMachine::State::NewTransaction:
          elle::unreachable();
        case TransactionMachine::State::SenderCreateTransaction:
          this->_run(this->_create_transaction_state);
          break;
        case TransactionMachine::State::SenderWaitForDecision:
        case TransactionMachine::State::CloudBufferingBeforeAccept:
          this->_run(this->_wait_for_accept_state);
          break;
        case TransactionMachine::State::RecipientWaitForDecision:
        case TransactionMachine::State::RecipientAccepted:
          elle::unreachable();
        case TransactionMachine::State::PublishInterfaces:
        case TransactionMachine::State::Connect:
        case TransactionMachine::State::PeerDisconnected:
        case TransactionMachine::State::PeerConnectionLost:
        case TransactionMachine::State::Transfer:
        case TransactionMachine::State::CloudBuffered:
          this->_run(this->_transfer_core_state);
          break;
        case TransactionMachine::State::Finished:
          this->_run(this->_finish_state);
          break;
        case TransactionMachine::State::Rejected:
          this->_run(this->_reject_state);
          break;
        case TransactionMachine::State::Canceled:
          this->_run(this->_cancel_state);
          break;
        case TransactionMachine::State::Failed:
          this->_run(this->_fail_state);
          break;
        default:
          elle::unreachable();
      }
      this->current_state(current_state);
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
          break;
        case TransactionStatus::created:
        case TransactionStatus::none:
        case TransactionStatus::started:
          elle::unreachable();
      }
    }

    void
    SendMachine::_create_transaction()
    {
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
      this->current_state(TransactionMachine::State::SenderCreateTransaction);
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

      if (this->state().metrics_reporter())
        this->state().metrics_reporter()->transaction_created(
          this->transaction_id(),
          this->state().me().id,
          this->peer_id(),
          this->data()->files.size(),
          size,
          this->_message.length()
          );

      this->state().meta().update_transaction(this->transaction_id(),
                                              TransactionStatus::initialized);
    }

    void
    SendMachine::_wait_for_accept()
    {
      ELLE_TRACE_SCOPE("%s: waiting for peer to accept or reject", *this);

      auto peer = this->state().user(this->peer_id());
      if (peer.ghost())
        this->_bare_cloud_upload();
      else if (!peer.ghost() && !peer.online())
      {
        this->current_state(
          TransactionMachine::State::CloudBufferingBeforeAccept);
        this->_cloud_operation();
      }
      else
        this->current_state(TransactionMachine::State::SenderWaitForDecision);
    }

    void
    SendMachine::_transfer_operation(frete::RPCFrete& frete)
    {
      ELLE_TRACE_SCOPE("%s: transfer operation", *this);
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
    SendMachine::_bare_cloud_upload()
    {
      ELLE_TRACE_SCOPE("%s: bare_cloud_upload", *this);
      auto& meta = this->state().meta();
      auto token = meta.get_cloud_buffer_token(this->transaction_id());
      auto credentials = aws::Credentials(token.access_key_id,
                                          token.secret_access_key,
                                          token.session_token,
                                          token.expiration);
      aws::S3 handler("us-east-1-buffer-infinit-io", this->transaction_id(),
                      credentials);
      // Since we zip, we support only one file, which simplifies the logic.
      ELLE_ASSERT_EQ(this->data()->files.size(), 1);
      typedef frete::Frete::FileSize FileSize;
      std::pair<std::string, FileSize> file = frete().files_info().front();
      const std::string& object = file.first;
      FileSize size = file.second;
      // AWS constraints: no more than 10k chunks, at least 5Mo block size
      FileSize chunk_size = std::max(FileSize(5*1024*1024), size / 9500);
      int chunk_count = size / chunk_size + ((size % chunk_size)? 1:0);
      ELLE_TRACE("%s: using chunk size of %s, with %s chunks",
                 *this, chunk_size, chunk_count);
      // Load our own snapshot that contains the upload uid
      std::string raw_snapshot_path = common::infinit::frete_snapshot_path(
        this->data()->sender_id,
        this->data()->id + "_raw");
      std::string upload_id;
      std::ifstream ifs(raw_snapshot_path);
      ifs >> upload_id;
      ELLE_DEBUG("%s: tried to reload id from %s, got %s",
                 *this, raw_snapshot_path, upload_id);
      std::vector<aws::S3::MultiPartChunk> chunks;
      int next_chunk = 0;
      int max_check_id = 0; // check for chunk presence up to that id
      int start_check_index = 0; // check for presence from that chunks index
      if (upload_id.empty())
      {
        upload_id = handler.multipart_initialize(object);
        std::ofstream ofs(raw_snapshot_path);
        ofs << upload_id;
        ELLE_DEBUG("%s: saved id %s to %s",
                   *this, upload_id, raw_snapshot_path);
      }
      else
      { // Fetch block list
        chunks = handler.multipart_list(object, upload_id);
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
      auto& frete = this->frete();
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
          auto buffer = frete.cleartext_read(0, local_chunk*chunk_size, chunk_size);
          std::string etag = handler.multipart_upload(
            object, upload_id,
            buffer,
            local_chunk);
          chunks.push_back(std::make_pair(local_chunk, etag));
        }
      };
      int num_threads = 4;
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
      handler.multipart_finalize(object, upload_id, chunks);
      // mark transfer as raw-finished
    }

    void
    SendMachine::_cloud_operation()
    {
      if (elle::os::getenv("INFINIT_CLOUD_BUFFERING", "").empty())
      {
        ELLE_DEBUG("%s: cloud buffering disabled by configuration", *this);
        return;
      }
      ELLE_TRACE_SCOPE("%s: upload to the cloud", *this);
      auto& frete = this->frete();
      auto& snapshot = *frete.transfer_snapshot();
      // Save snapshot of what eventual direct upload already did right now.
      this->frete().save_snapshot();
      FilesystemTransferBufferer::Files files;
      for (frete::Frete::FileID file_id = 0;
           file_id < snapshot.count();
           ++file_id)
      {
        auto& file = snapshot.file(file_id);
        files.push_back(std::make_pair(file.path(), file.size()));
      }
      bool cloud_debug = !elle::os::getenv("INFINIT_CLOUD_FILEBUFFERER", "").empty();
      std::unique_ptr<TransferBufferer> bufferer;
      if (cloud_debug)
        bufferer.reset(new FilesystemTransferBufferer(*this->data(),
                                                      "/tmp/infinit-buffering",
                                                      snapshot.count(),
                                                      snapshot.total_size(),
                                                      files,
                                                      frete.key_code()));
      else
      {
        auto& meta = this->state().meta();
        auto token = meta.get_cloud_buffer_token(this->transaction_id());
        auto credentials = aws::Credentials(token.access_key_id,
                                            token.secret_access_key,
                                            token.session_token,
                                            token.expiration);
        bufferer.reset(new S3TransferBufferer(*this->data(),
                                              credentials,
                                              snapshot.count(),
                                              snapshot.total_size(),
                                              files,
                                              frete.key_code()));
      }

      /* Pipelined cloud upload with periodic local snapshot update
      */
      int num_threads = 8;
      std::string snum_threads = elle::os::getenv("INFINIT_NUM_CLOUD_UPLOAD_THREAD", "");
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
            this->frete().save_snapshot();
            save_snapshot = false;
          }
          auto& buffer = block.buffer();
          bufferer->put(local_file, local_position, buffer.size(), buffer);
          transfer_since_snapshot += buffer.size();
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
      this->frete().save_snapshot();
      this->current_state(State::CloudBuffered);
    }

    float
    SendMachine::progress() const
    {
      if (this->_frete != nullptr)
        return this->_frete->progress();
      return 0.0f;
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
          common::infinit::frete_snapshot_path(
            this->data()->sender_id,
            this->data()->id));
        auto k = this->state().user(this->peer_id(), true).public_key;
        if (!k.empty())
        {
          infinit::cryptography::PublicKey peer_K;
          ELLE_DEBUG("restoring key from %s", k);
          peer_K.Restore(k);
          this->_frete->set_peer_key(peer_K);
        }
        else
          ELLE_DEBUG("%s: peer id unavailable, frete has no peer key", *this);
        if (this->_frete->count())
        { // Reloaded from snapshot, validate it
          if (this->_files.size() != this->_frete->count())
          {
            ELLE_ERR("%s: snapshot data mismatch: count %s vs %s",
                     *this,
                     this->_frete->count(),
                     this->_files.size()
                     );
            throw elle::Exception("invalid transfer data");
          }
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

    std::string
    SendMachine::type() const
    {
      return "SendMachine";
    }

    void
    SendMachine::cleanup()
    {
      this->frete().remove_snapshot();
    }
  }
}
