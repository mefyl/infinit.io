#include <surface/gap/Transaction.hh>

#include <memory>

#include <elle/AtomicFile.hh>
#include <elle/Error.hh>
#include <elle/serialization/json.hh>

#include <surface/gap/enums.hh>
#include <surface/gap/Exception.hh>
#include <surface/gap/GhostReceiveMachine.hh>
#include <surface/gap/LinkSendMachine.hh>
#include <surface/gap/PeerReceiveMachine.hh>
#include <surface/gap/PeerSendMachine.hh>
#include <surface/gap/ReceiveMachine.hh>
#include <surface/gap/TransactionMachine.hh>
#include <papier/Authority.hh>

ELLE_LOG_COMPONENT("surface.gap.Transaction");

namespace surface
{
  namespace gap
  {
    std::set<infinit::oracles::Transaction::Status>
    Transaction::recipient_final_statuses({
      infinit::oracles::Transaction::Status::rejected,
      infinit::oracles::Transaction::Status::finished,
      infinit::oracles::Transaction::Status::canceled,
      infinit::oracles::Transaction::Status::failed,
      infinit::oracles::Transaction::Status::deleted
    });

    std::set<infinit::oracles::Transaction::Status>
    Transaction::sender_final_statuses({
      infinit::oracles::Transaction::Status::rejected,
      infinit::oracles::Transaction::Status::finished,
      infinit::oracles::Transaction::Status::canceled,
      infinit::oracles::Transaction::Status::failed,
      infinit::oracles::Transaction::Status::deleted,
      infinit::oracles::Transaction::Status::ghost_uploaded
    });

    // - Exception -------------------------------------------------------------
    Transaction::BadOperation::BadOperation(Type type):
      Exception(gap_error, elle::sprintf("%s", type)),
      _type(type)
    {}

    /*---------.
    | Snapshot |
    `---------*/

    void
    Transaction::_snapshot_save() const
    {
      create_directories(this->_snapshots_directory);
      ELLE_TRACE_SCOPE("%s: save snapshot to %s", *this, this->_snapshot_path);
      Snapshot snapshot(this->_sender,
                        this->_data,
                        this->_archived,
                        this->_files,
                        this->_message,
                        this->_plain_upload_uid);
      ELLE_DUMP("%s: snapshot data: %s", *this, snapshot);
      elle::AtomicFile destination(this->_snapshot_path);
      destination.write() << [&] (elle::AtomicFile::Write& write)
      {
        elle::serialization::json::SerializerOut output(write.stream(), false);
        snapshot.serialize(output);
      };
    }

    Transaction::Snapshot::Snapshot(
      bool sender,
      std::shared_ptr<Data> data,
      bool archived,
      boost::optional<std::vector<std::string>> files,
      boost::optional<std::string> message,
      boost::optional<std::string> plain_upload_uid
      )
      : _sender(sender)
      , _data(data)
      , _files(files)
      , _message(message)
      , _plain_upload_uid(plain_upload_uid)
      , _archived(archived)
    {}

    Transaction::Snapshot::Snapshot(
      elle::serialization::SerializerIn& serializer)
    {
      this->serialize(serializer);
    }

    void
    Transaction::Snapshot::serialize(
      elle::serialization::Serializer& serializer)
    {
      serializer.serialize("data", this->_data);
      serializer.serialize("sender", this->_sender);
      serializer.serialize("files", this->_files);
      serializer.serialize("message", this->_message);
      serializer.serialize("archived", this->_archived);
      serializer.serialize("plain_upload_uid", this->_plain_upload_uid);
    }

    void
    Transaction::Snapshot::print(std::ostream& stream) const
    {
      stream << "Snapshot(";
      if (this->_files)
        stream << "files: " << this->_files.get();
      if (this->_plain_upload_uid)
        stream << ", plain_upload_uid: " << this->_plain_upload_uid.get();
      stream << ")";
    }

    /*-------------.
    | Construction |
    `-------------*/

    // Construct to send files.
    Transaction::Transaction(State& state,
                             uint32_t id,
                             std::string const& peer_id,
                             std::vector<std::string> files,
                             std::string const& message,
                             papier::Authority const& authority)
      : Transaction(state, id, peer_id, elle::UUID(), std::move(files), message, authority)
    {}

    // Construct to send files to a specific device.
    Transaction::Transaction(State& state,
                             uint32_t id,
                             std::string const& peer_id,
                             elle::UUID const& peer_device_id,
                             std::vector<std::string> files,
                             std::string const& message,
                             papier::Authority const& authority)
      // FIXME: ensure better uniqueness.
      : _snapshots_directory(
        boost::filesystem::path(
          state.local_configuration().transactions_directory(state.me().id))
          / boost::filesystem::unique_path())
      , _snapshot_path(this->_snapshots_directory / "transaction.snapshot")
      , _state(state)
      , _files(std::move(files))
      , _message(message)
      , _archived(false)
      , _status(gap_transaction_new)
      , _id(id)
      , _sender(true)
      , _data(nullptr)
      , _canceled_by_user(false)
      , _authority(authority)
      , _machine(nullptr)
      , _over(false)
    {
      auto data = std::make_shared<infinit::oracles::PeerTransaction>(
        state.me().id,
        state.me().fullname,
        state.device().id,
        peer_id);
      data->recipient_device_id = peer_device_id;
      this->_data = data;
      this->_machine.reset(
        new PeerSendMachine(*this, this->_id, peer_id,
                            this->_files.get(), message, data,
                            this->_authority));
      ELLE_TRACE_SCOPE(\
        "%s: construct to send %s files to %s%s",
        *this,
        this->_files.get().size(),
        peer_id,
        peer_device_id.is_nil()
        ? "" : elle::sprintf(" on device %s", peer_device_id));
      this->_snapshot_save();
    }

    // Construct to create link.
    Transaction::Transaction(State& state,
                             uint32_t id,
                             std::vector<std::string> files,
                             std::string const& message,
                             papier::Authority const& authority)
      // FIXME: ensure better uniqueness.
      : _snapshots_directory(
        boost::filesystem::path(
          state.local_configuration().transactions_directory(state.me().id))
          / boost::filesystem::unique_path())
      , _snapshot_path(this->_snapshots_directory / "transaction.snapshot")
      , _state(state)
      , _files(std::move(files))
      , _message(message)
      , _archived(false)
      , _status(gap_transaction_new)
      , _id(id)
      , _sender(true)
      , _data(nullptr)
      , _canceled_by_user(false)
      , _authority(authority)
      , _machine(nullptr)
      , _over(false)
    {
      ELLE_TRACE_SCOPE("%s: construct to generate link for %s",
                       *this, this->_files.get());
      auto data = std::make_shared<infinit::oracles::LinkTransaction>();
      data->sender_id = state.me().id;
      data->sender_device_id = state.device().id;
      this->_data = data;
      this->_machine.reset(
        new LinkSendMachine(
          *this, this->_id, this->_files.get(), message, data));
      // Update UI.
      this->state().enqueue(
        this->state().link_to_gap_link(
          this->id(), *data, this->status()));
      this->_snapshot_save();
    }

    static
    gap_TransactionStatus
    status_gap_from_meta(infinit::oracles::Transaction::Status status)
    {
      typedef infinit::oracles::Transaction::Status Status;
      switch (status)
      {
        case Status::accepted:
        case Status::created:
        case Status::initialized:
        case Status::none:
        case Status::started:
        case Status::cloud_buffered:
          return gap_transaction_on_other_device;
        // Final states.
        case Status::canceled:
          return gap_transaction_canceled;
        case Status::failed:
          return gap_transaction_failed;
        case Status::finished:
        case Status::ghost_uploaded:
          return gap_transaction_finished;
        case Status::rejected:
          return gap_transaction_rejected;
        case Status::deleted:
          return gap_transaction_deleted;
      }
      elle::unreachable();
    }

    // FIXME: Split history transactions.
    Transaction::Transaction(State& state,
                             uint32_t id,
                             std::shared_ptr<Data> data,
                             papier::Authority const& authority,
                             bool history,
                             bool login)
      : _snapshots_directory(
        boost::filesystem::path(
          state.local_configuration().transactions_directory(state.me().id))
          / boost::filesystem::unique_path())
      , _snapshot_path(this->_snapshots_directory / "transaction.snapshot")
      , _state(state)
      , _files()
      , _message()
      , _archived(false)
      , _status(status_gap_from_meta(data->status))
      , _id(id)
      , _sender(state.me().id == data->sender_id &&
                state.device().id == data->sender_device_id)
      , _data(data)
      , _canceled_by_user(false)
      , _authority(authority)
      , _machine()
      , _over(history)
    {
      ELLE_TRACE_SCOPE("%s: constructed from data", *this);
      if (history)
      {
        ELLE_DEBUG("%s: part of history", *this);
        // During login phase, it's not usefull to send notifications to the
        // frontend.
        if (!login)
        {
          if (auto peer_data =
              std::dynamic_pointer_cast<infinit::oracles::PeerTransaction>(
                this->_data))
          {
            this->state().enqueue(
              this->state().transaction_to_gap_transaction(
                this->id(), *peer_data, this->status()));
          }
          else if (auto link_data =
                   std::dynamic_pointer_cast<infinit::oracles::LinkTransaction>(
                     this->_data))
          {
            this->state().enqueue(
              this->state().link_to_gap_link(
                this->id(), *link_data, this->status()));
          }
        }
        return;
      }
      auto me = state.me().id;
      auto device = state.device().id;
      auto sender = this->_data->sender_id;
      auto sender_device = this->_data->sender_device_id;
      if (auto peer_data =
          std::dynamic_pointer_cast<infinit::oracles::PeerTransaction>(
            this->_data))
      {
        auto recipient = peer_data->recipient_id;
        if (me == sender)
        {
          if (this->_data->is_ghost && this->_data->status ==
            infinit::oracles::Transaction::Status::ghost_uploaded)
          {
            ELLE_TRACE("%s is sender and status is ghost_upload, nothing to do",
                       *this);
            return;
          }
          if (me == recipient && device != sender_device)
          {
            if (this->_data->is_ghost)
              ELLE_WARN("Impossible state, restoring a ghost transaction to self");
            this->_machine.reset(
              new PeerReceiveMachine(*this, this->_id, peer_data));
          }
          else if (device == sender_device)
          {
            ELLE_WARN("%s: can't restore peer send transaction from server (%s)",
                      *this, *peer_data);
            bool run_to_fail = true;
            this->failure_reason(
              "sender device missing peer transaction snapshot");
            this->_machine.reset(
              new PeerSendMachine(*this, this->_id, peer_data,
                                  this->_authority, run_to_fail));
          }
          else
          { // The sender is an other device
            ELLE_DEBUG("%s: start send machine", *this);
            this->_machine.reset(
              new PeerSendMachine(*this, this->_id, peer_data, this->_authority));
          }
        }
        else if (me == recipient)
        {
          // Because we are not sure we are going to receive the new swagger
          // notification first, let's ensure sure we have it our model.
          this->state().user(peer_data->sender_id);
          if (this->_data->is_ghost)
          {
            this->_machine.reset(
              new GhostReceiveMachine(*this, this->_id, peer_data));
          }
          else
          {
            ELLE_DEBUG("%s: start receive machine", *this);
            this->_machine.reset(
              new PeerReceiveMachine(*this, this->_id, peer_data));
          }
        }
        this->_snapshot_save();
      }
      else if (auto link_data =
               std::dynamic_pointer_cast<infinit::oracles::LinkTransaction>(
                 this->_data))
      {
        // Can be a link we forgot everything about. In that case new
        // notifications can happen only if it's finished.
        if (device == sender_device
          && this->_data->status != infinit::oracles::Transaction::Status::finished)
        {
          ELLE_WARN("%s: can't restore link transaction from server (%s)",
                    *this, *link_data);
          bool run_to_fail = true;
          this->failure_reason(
            "sender device missing link transaction snapshot");
          this->_machine.reset(
            new LinkSendMachine(*this, this->_id, link_data, run_to_fail));
        }
        else
        {
          this->_machine.reset(
            new LinkSendMachine(*this, this->_id, link_data));
          this->state().enqueue(
            this->state().link_to_gap_link(
              this->id(), *link_data, this->status()));
          this->_snapshot_save();
        }
      }
      else
      {
        ELLE_ERR("%s: unkown transaction type: %s",
                 *this, elle::demangle(typeid(*this->_data).name()));
      }
    }

    Transaction::Transaction(State& state,
                             uint32_t id,
                             Snapshot snapshot,
                             boost::filesystem::path snapshots_directory,
                             papier::Authority const& authority)
      : _snapshots_directory(std::move(snapshots_directory))
      , _snapshot_path(this->_snapshots_directory / "transaction.snapshot")
      , _state(state)
      , _files(snapshot.files())
      , _message(snapshot.message())
      , _plain_upload_uid(snapshot.plain_upload_uid())
      , _archived(snapshot.archived())
      , _status(status_gap_from_meta(snapshot.data()->status))
      , _id(id)
      , _sender(snapshot.sender())
      , _data(snapshot.data())
      , _canceled_by_user(false)
      , _authority(authority)
      , _machine()
      , _over(false)
    {
      ELLE_TRACE_SCOPE("%s: constructed from snapshot %s",
                       *this, this->_snapshot_path);
      if (auto peer_data =
          std::dynamic_pointer_cast<infinit::oracles::PeerTransaction>(
            this->_data))
      {
        // Check for an updated final status from meta
        ELLE_ASSERT(state.synchronize_response() != nullptr);
        using TransactionStatus = infinit::oracles::Transaction::Status;
        if (this->_data->is_ghost &&
            this->_data->status == TransactionStatus::ghost_uploaded &&
          (this->_sender || peer_data->sender_id == this->state().me().id))
        {
          ELLE_TRACE("%s sender of ghost_uploaded transaction", *this);
          return;
        }
        if (!snapshot.data()->id.empty())
        {
          auto it = state.synchronize_response()->transactions.find(snapshot.data()->id);
          auto data = (it != state.synchronize_response()->transactions.end())
            ? it->second
            :  _state.meta().transaction(snapshot.data()->id);
          this->on_transaction_update(
            std::make_shared<infinit::oracles::PeerTransaction>(data));
        }
        // this->_sender is set true when creating a transaction on this device.
        if (this->_sender)
        {
          if (this->_files == boost::none)
          {
            ELLE_ERR("%s: sender snapshot contains no files section", *this);
            cancel();
          }
          else
          {
            ELLE_TRACE("%s: create peer send machine (this device)", *this)
              this->_machine.reset(
                new PeerSendMachine(*this, this->_id, this->_files.get(),
                                    this->_message.get(), peer_data,
                                    this->_authority));
          }
        }
        // Only create a send machine if the transaction is not to self.
        else if (peer_data->sender_id == this->state().me().id &&
                 peer_data->recipient_id != this->state().me().id)
        {
          ELLE_TRACE("%s: create peer send machine (another device)", *this)
            this->_machine.reset(
              new PeerSendMachine(*this, this->_id, peer_data,
                                  this->_authority));
        }
        // Otherwise we're the recipient.
        else
        {
          if (this->_data->is_ghost)
          {
            ELLE_TRACE("%s: create ghost receive machine", *this)
              this->_machine.reset(
                  new GhostReceiveMachine(*this, this->_id, peer_data));

          }
          else
          {
            ELLE_TRACE("%s: create peer receive machine", *this)
              this->_machine.reset(
                new PeerReceiveMachine(*this, this->_id, peer_data));
          }
        }
      }
      else if (auto link_data =
               std::dynamic_pointer_cast<infinit::oracles::LinkTransaction>(
                 this->_data))
      {
        if (this->_sender)
        {
          ELLE_ASSERT_NEQ(this->_files, boost::none);
          ELLE_TRACE("%s: create link send machine (this device)", *this)
            this->_machine.reset(
              new LinkSendMachine(*this, this->_id, this->_files.get(),
                                  this->_message.get(), link_data));
        }
        else
        {
          ELLE_TRACE("%s: create link send machine (another device)", *this)
            this->_machine.reset(
              new LinkSendMachine(*this, this->_id, link_data));
        }
      }
      else
        ELLE_ERR("%s: don't know what to do with a %s",
                 *this, elle::demangle(typeid(*this->_data).name()));
    }

    Transaction::~Transaction()
    {
      this->_machine.reset();
    }

    void
    Transaction::accept()
    {
      boost::optional<std::string> relative_output_dir;
#if defined(INFINIT_IOS)
      relative_output_dir = this->_data->id;
#elif defined(INFINIT_ANDROID)
      relative_output_dir =
        elle::sprintf("%s/%s", this->state().me().id, this->_data->id);
#endif
      ELLE_TRACE_SCOPE("%s: accepting transaction", *this);
      if (this->_machine == nullptr)
      {
        ELLE_WARN("%s: machine is empty (it doesn't concern your device)",
                  *this);
        throw BadOperation(BadOperation::Type::accept);
      }
      // FIXME
      if (auto machine = dynamic_cast<ReceiveMachine*>(this->_machine.get()))
        machine->accept(relative_output_dir);
      else
      {
        ELLE_ERR("%s: accepting on a send machine", *this);
        throw BadOperation(BadOperation::Type::accept);
      }
    }

    void
    Transaction::reject()
    {
      ELLE_TRACE_SCOPE("%s: rejecting transaction", *this);
      ELLE_ASSERT(this->_machine != nullptr);
      if (auto machine = dynamic_cast<ReceiveMachine*>(this->_machine.get()))
        machine->reject();
      else
      {
        ELLE_ERR("%s: reject on a send machine", *this);
        throw BadOperation(BadOperation::Type::reject);
      }
    }

    void
    Transaction::cancel(bool user_request)
    {
      ELLE_TRACE_SCOPE("%s: canceling transaction", *this);
      if (this->_machine == nullptr)
      {
        ELLE_WARN("%s: machine is empty (it doesn't concern your device)",
                  *this);
        throw BadOperation(BadOperation::Type::cancel);
      }
      _canceled_by_user = true;
      this->_machine->cancel("user canceled");
    }

    void
    Transaction::delete_()
    {
      ELLE_TRACE_SCOPE("%s: delete transaction", *this);
      if (auto data =
          std::dynamic_pointer_cast<infinit::oracles::LinkTransaction>(
            this->_data))
      {
        // If the transaction is running, cancel it.
        if (this->_machine != nullptr && !this->final())
        {
          _canceled_by_user = true;
          this->_machine->cancel("user deleted transaction");
          // Set the machine's gap status as we don't have a separate state for
          // deleted.
          this->status(gap_transaction_deleted);
        }
        // Update Meta.
        this->_data->status = infinit::oracles::Transaction::Status::deleted;
        this->state().meta().update_link(data->id, 0.f, this->_data->status);
        // Update UI.
        this->state().enqueue(
          this->state().link_to_gap_link(
            this->id(), *data, this->status()));
        this->state().metrics_reporter()->transaction_deleted(data->id);
      }
      else
      {
        ELLE_WARN("%s: can only delete LinkTransaction");
        throw BadOperation(BadOperation::Type::delete_);
      }
    }

    void
    Transaction::join()
    {
      ELLE_TRACE_SCOPE("%s: joining transaction", *this);
      if (this->_machine == nullptr)
      {
        ELLE_WARN("%s: machine is empty (it doesn't concern your device)",
                  *this);
        throw BadOperation(BadOperation::Type::join);
      }
      this->_machine->join();
    }

    void
    Transaction::status(gap_TransactionStatus status)
    {
      if (status != this->_status)
      {
        ELLE_TRACE_SCOPE("%s: change GAP status to %s", *this, status);
        this->_status = status;
        if (auto peer_data =
          std::dynamic_pointer_cast<infinit::oracles::PeerTransaction>(
            this->_data))
        {
          this->state().enqueue(
            this->state().transaction_to_gap_transaction(
              this->id(), *peer_data, this->status()));
        }
        else if (auto link_data =
          std::dynamic_pointer_cast<infinit::oracles::LinkTransaction>(
            this->_data))
        {
          this->state().enqueue(
            this->state().link_to_gap_link(this->id(), *link_data, status));
        }
        this->_status_changed(status);
      }
    }

    float
    Transaction::progress() const
    {
      ELLE_DUMP_SCOPE("%s: progress transaction", *this);
      if (this->_machine == nullptr)
      {
        ELLE_WARN("%s: machine is empty (it doesn't concern your device)",
                  *this);
        throw BadOperation(BadOperation::Type::progress);
      }
      return this->_machine->progress();
    }

    void
    Transaction::on_transaction_update(std::shared_ptr<Data> data)
    {
      ELLE_TRACE_SCOPE("%s: update data with %s", *this, *data);
      ELLE_ASSERT_EQ(typeid(*data), typeid(*this->_data));
      typedef infinit::oracles::Transaction::Status Status;
      if (this->_data->status != Status::deleted &&
          data->status == Status::deleted &&
          this->failure_reason().empty())
      {
        this->failure_reason("fail on other side");
      }
      // FIXME: Ugly, but ensures we don't get sliced.
      {
        using infinit::oracles::LinkTransaction;
        using infinit::oracles::PeerTransaction;
        if (auto link = std::dynamic_pointer_cast<LinkTransaction>(data))
          *std::dynamic_pointer_cast<LinkTransaction>(this->_data) = *link;
        else if (auto peer = std::dynamic_pointer_cast<PeerTransaction>(data))
        {
          // XXX: 0.9.23 fix.
          // Because users with an old client (< 0.9.23) have no notion of
          // cloud_buffered, 0.9.23 servers will still return initialized
          // instead of cloud buffered to smooth the transition.
          // To avoid weird rollbacks in status, just ignore that case.
          auto rollback_status = (this->_data->status == Status::cloud_buffered &&
                                  peer->status == Status::initialized);
          auto data = std::dynamic_pointer_cast<PeerTransaction>(this->_data);
          std::string previous_recipient_id{data->recipient_id};
          *std::dynamic_pointer_cast<PeerTransaction>(this->_data) = *peer;
          if (rollback_status)
            this->_data->status = Status::cloud_buffered;
          if (peer->recipient_id != previous_recipient_id)
          {
            ELLE_TRACE_SCOPE("recipient id changed: %s -> %s",
                             peer->recipient_id, previous_recipient_id);
            // Merge recipient if the new one is not in your list.
            // XXX: Because we should always receive a new_swagger notification
            // this code is just a security.
            this->state().enqueue(
              this->state().transaction_to_gap_transaction(
                this->id(), *peer, this->status()));
          }
          if (peer->paused)
            this->paused().open();
        }
        else
          ELLE_ERR("%s: unknown transaction type: %s",
                   *this, elle::demangle(typeid(*data).name()));
      }
      if (this->_machine && !this->_over)
      {
        ELLE_DEBUG("%s: updating machine", *this)
          this->_machine->transaction_status_update(this->_data->status);
        this->_snapshot_save();
      }
      else
        this->status(status_gap_from_meta(data->status));
      if (auto link_data =
          std::dynamic_pointer_cast<infinit::oracles::LinkTransaction>(data))
      {
        // There's still a machine when deleting a link that was created during
        // this session. We need to handle this case explicitly.
        if (this->_machine && link_data->status == Status::deleted)
          this->status(gap_transaction_deleted);
        this->state().enqueue(
          this->state().link_to_gap_link(this->id(), *link_data, this->status()));
      }
    }

    void
    Transaction::notify_user_connection_status(std::string const& user_id,
                                               bool user_status,
                                               elle::UUID const& device_id,
                                               bool device_status)
    {
      if (this->_machine == nullptr)
        return;
      this->_machine->notify_user_connection_status(user_id, user_status,
                                                    device_id, device_status);
    }

    void
    Transaction::notify_peer_reachable(
      std::vector<std::pair<std::string, int>> const& local_endpoints,
      std::vector<std::pair<std::string, int>> const& public_endpoints)
    {
      if (!this->_machine)
      {
        ELLE_DEBUG(
          "%s: got reachability notification for transaction on another device %s",
          *this, this->data()->id);
        return;
      }
      this->_machine->peer_available(local_endpoints,
                                     public_endpoints);
    }

    void
    Transaction::notify_peer_unreachable()
    {
      if (!this->_machine)
      {
        // FIXME: meta send those notifications to all devices, so this is
        // triggered.
        ELLE_DEBUG(
          "%s: got reachability notification for transaction on another device %s",
          *this, this->data()->id);
        return;
      }
      this->_machine->peer_unavailable();
    }

    bool
    Transaction::has_transaction_id(std::string const& id) const
    {
      return this->_data->id == id;
    }

    bool
    Transaction::final() const
    {
      ELLE_ASSERT(this->_data.get());
      if (_sender)
        return std::find(Transaction::sender_final_statuses.begin(),
                         Transaction::sender_final_statuses.end(),
                         this->_data->status) != Transaction::sender_final_statuses.end();
     else
       return std::find(Transaction::recipient_final_statuses.begin(),
                        Transaction::recipient_final_statuses.end(),
                         this->_data->status) != Transaction::recipient_final_statuses.end();
    }

    void
    Transaction::print(std::ostream& stream) const
    {
      if (this->_data != nullptr)
        stream << *this->_data;
      else
        stream << "Transaction(null)";
      stream << "(" << this->_id << ")";
    }

    void
    Transaction::reset()
    {
      ELLE_TRACE_SCOPE("reseting %s", *this);
      if (this->final())
      {
        // Finalized (current session or history transaction): nothing to do.
        ELLE_DEBUG("transaction already finalized");
        return;
      }
      if (this->_machine != nullptr)
        this->_machine->reset_transfer();
    }

    void
    Transaction::archived(bool v)
    {
      this->_archived = v;
      this->_snapshot_save();
    }
  }
}
