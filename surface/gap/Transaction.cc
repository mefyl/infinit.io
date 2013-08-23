#include <surface/gap/Transaction.hh>

#include <surface/gap/TransferMachine.hh>
#include <surface/gap/ReceiveMachine.hh>
#include <surface/gap/SendMachine.hh>

ELLE_LOG_COMPONENT("surface.gap.Transaction");

namespace surface
{
  namespace gap
  {
    /// Generate a id for local user.
    static
    uint32_t
    generate_id()
    {
      static uint32_t id = null_id;
      return ++id;
    }

    // - Exception -------------------------------------------------------------
    Transaction::BadOperation::BadOperation(Type type):
      Exception(gap_error, elle::sprintf("%s", type)),
      _type(type)
    {}

    Transaction::Transaction(State const& state,
                             Data&& data,
                             bool history):
      _id(generate_id()),
      _data(new Data{std::move(data)}),
      _machine()
    {
      ELLE_TRACE_SCOPE("%s: construct from data %s", *this, *this->_data);
      ELLE_ASSERT(state.me().id == this->_data->sender_id ||
                  state.me().id == this->_data->recipient_id);
      if (history)
      {
        ELLE_DEBUG("%s: history", *this);
        return;
      }
      if (state.me().id == this->_data->sender_id &&
          state.device().id == this->_data->sender_device_id)
      {
        ELLE_TRACE("%s: create send machine from data: %s", *this, *this->_data);
        this->_machine.reset(new SendMachine{state, this->_id, this->_data});
      }
      else if (state.me().id == this->_data->recipient_id &&
               (this->_data->recipient_device_id.empty() ||
                state.device().id == this->_data->recipient_device_id))
      {
        ELLE_TRACE("%s: create receive machine from data: %s", *this, *this->_data);
        this->_machine.reset(new ReceiveMachine{state, this->_id, this->_data});
      }
      else
      {
        ELLE_WARN("%s: no machine can be launch for data %s cause it's not "\
                  "your device (%s)", *this, this->_data, state.device().id);
      }
    }

    Transaction::Transaction(State const& state,
                             TransferMachine::Snapshot snapshot):
      _id(generate_id()),
      _data(new Data{std::move(snapshot.data)}),
      _machine()
    {
      ELLE_TRACE_SCOPE("%s: construct from snapshot (%s) with data %s",
                       *this, snapshot.state, *this->_data);

      if (state.me().id == this->_data->sender_id &&
          state.device().id == this->_data->sender_device_id)
      {
        ELLE_TRACE("%s: create send machine from data: %s", *this, *this->_data);
        this->_machine.reset(new SendMachine{state, this->_id, snapshot.files, snapshot.state, snapshot.message, this->_data});
      }
      else if (state.me().id == this->_data->recipient_id &&
               (this->_data->recipient_device_id.empty() ||
                state.device().id == this->_data->recipient_device_id))
      {
        ELLE_TRACE("%s: create receive machine from data: %s", *this, *this->_data);
        this->_machine.reset(new ReceiveMachine{state, this->_id, snapshot.state, this->_data});
      }
      else
      {
        throw Exception(gap_internal_error, "invalid snapshot");
      }
    }

    Transaction::Transaction(surface::gap::State const& state,
                             std::string const& peer_id,
                             std::unordered_set<std::string>&& files,
                             std::string const& message):
      _id(generate_id()),
      _data{new Data{state.me().id, state.me().fullname, state.device().id}},
      _machine(new SendMachine{state, this->_id, peer_id, std::move(files), message, this->_data})
    {
      ELLE_TRACE_SCOPE("%s: created transaction for a new send: %s", *this, this->_data);
    }

    Transaction::~Transaction()
    {
      this->_machine.reset();
    }

    void
    Transaction::accept()
    {
      ELLE_TRACE_SCOPE("%s: accepting transaction", *this);

      if (this->_machine == nullptr)
      {
        ELLE_WARN("%s: machine is empty (it doesn't concern your device)", *this);
        throw BadOperation(BadOperation::Type::accept);
      }

      if (!dynamic_cast<ReceiveMachine*>(this->_machine.get()))
      {
        ELLE_ERR("%s: accepting on a send machine", *this);
        throw BadOperation(BadOperation::Type::accept);
      }
      static_cast<ReceiveMachine*>(this->_machine.get())->accept();
    }

    void
    Transaction::reject()
    {
      ELLE_TRACE_SCOPE("%s: rejecting transaction", *this);
      ELLE_ASSERT(this->_machine != nullptr);

      if (!dynamic_cast<ReceiveMachine*>(this->_machine.get()))
      {
        ELLE_ERR("%s: reject on a send machine", *this);
        throw BadOperation(BadOperation::Type::reject);
      }

      static_cast<ReceiveMachine*>(this->_machine.get())->reject();
    }

    void
    Transaction::cancel()
    {
      ELLE_TRACE_SCOPE("%s: canceling transaction", *this);
      if (this->_machine == nullptr)
      {
        ELLE_WARN("%s: machine is empty (it doesn't concern your device)", *this);
        throw BadOperation(BadOperation::Type::cancel);
      }

      this->_machine->cancel();
    }

    void
    Transaction::join()
    {
      ELLE_TRACE_SCOPE("%s: joining transaction", *this);
      if (this->_machine == nullptr)
      {
        ELLE_WARN("%s: machine is empty (it doesn't concern your device)", *this);
        throw BadOperation(BadOperation::Type::join);
      }

      this->_machine->join();
    }

    float
    Transaction::progress() const
    {
      ELLE_TRACE_SCOPE("%s: progress transaction", *this);

      if (this->_machine == nullptr)
      {
        ELLE_WARN("%s: machine is empty (it doesn't concern your device)", *this);
        throw BadOperation(BadOperation::Type::progress);
      }

      return this->_machine->progress();
    }

    static
    std::vector<plasma::TransactionStatus> const&
    final_status()
    {
      static std::vector<plasma::TransactionStatus> final{
        plasma::TransactionStatus::rejected,
          plasma::TransactionStatus::finished,
        plasma::TransactionStatus::canceled,
        plasma::TransactionStatus::failed};
      return final;
    }

    void
    Transaction::on_transaction_update(Data const& data)
    {
      ELLE_TRACE_SCOPE("%s: update transaction data with %s", *this, data);

      if (this->_machine == nullptr)
      {
        ELLE_WARN("%s: machine is empty (it doesn't concern your device)", *this);
        return;
      }

      if (this->_data->status == data.status)
      {
        ELLE_WARN("%s: transaction already has the status %s",
                  *this, this->_data->status);
      }
      if (std::find(final_status().begin(), final_status().end(),
                    this->_data->status) != final_status().end())
      {
        ELLE_WARN("%s: transaction already has a final status %s, can't "\
                  "change to %s", *this, this->_data->status, data.status);
      }
      // XXX: This is totaly wrong, the > is not overload, so the transaction
      // status as to be ordered in the right order.
      else if (this->_data->status > data.status)
      {
        ELLE_WARN("%s: receive a status update (%s) that is lower than the "\
                  " current %s", *this, this->_data->status, data.status);
      }

      *(this->_data) = data;

      this->_data->status = data.status;

      ELLE_DEBUG("%s: updating machine", *this);
      this->_machine->transaction_status_update(this->_data->status);
    }

    void
    Transaction::on_peer_connection_update(
      plasma::trophonius::PeerConnectionUpdateNotification const& update)
    {
      ELLE_TRACE_SCOPE("%s: update peer status with %s", *this, update);

      if (this->_machine == nullptr)
      {
        ELLE_WARN("%s: machine is empty (it doesn't concern your device)", *this);
        return;
      }

      ELLE_ASSERT_EQ(this->_data->network_id, update.network_id);

      // XXX.
      // ELLE_ASSERT(
      //   std::find(update.devices.begin(), update.devices.end(),
      //             this->_data->sender_device_id) != update.devices.end());

      // ELLE_ASSERT(
      //   std::find(update.devices.begin(), update.devices.end(),
      //             this->_data->recipient_device_id) != update.devices.end());

      this->_machine->peer_connection_update(update.status);
    }

    bool
    Transaction::concerns_user(std::string const& peer_id) const
    {
      return this->_data->sender_id == peer_id ||
             this->_data->recipient_id == peer_id;
    }

    bool
    Transaction::concerns_device(std::string const& device_id) const
    {
      return this->_data->sender_device_id == device_id ||
             this->_data->recipient_device_id == device_id;
    }

    bool
    Transaction::has_transaction_id(std::string const& id) const
    {
      return this->_data->id == id;
    }

    bool
    Transaction::concerns_network(std::string const& network_id) const
    {
      return this->_data->network_id == network_id;
    }
  }
}
