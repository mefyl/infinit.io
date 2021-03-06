#include <elle/log.hh>

#include <frete/RPCFrete.hh>
#include <papier/Authority.hh>

#include <surface/gap/onboarding/ReceiveMachine.hh>
#include <surface/gap/onboarding/Transaction.hh>
#include <surface/gap/onboarding/TransferMachine.hh>

ELLE_LOG_COMPONENT("surface.gap.onboarding.ReceiveMachine");

namespace surface
{
  namespace gap
  {
    namespace onboarding
    {
      using TransactionStatus = infinit::oracles::Transaction::Status;
      ReceiveMachine::ReceiveMachine(Transaction& transaction,
                                     uint32_t id,
                                     std::shared_ptr<Data> data,
                                     std::string const& file_path,
                                     papier::Authority const& authority,
                                     reactor::Duration duration)
        : surface::gap::TransactionMachine(transaction, id, data)
        , surface::gap::ReceiveMachine(transaction, id, data)
        , surface::gap::PeerMachine(transaction, id, std::move(data), authority)
        , _file_path(file_path)
      {
        ELLE_TRACE_SCOPE("%s: construction", *this);
        this->_transfer_machine.reset(
          new TransferMachine(
            *this, this->_file_path, this->state().output_dir(), duration));
        if (this->state().metrics_reporter())
        {
          bool onboarding = true;
          this->state().metrics_reporter()->peer_transaction_created(
            this->transaction_id(),
            "onboarder",
            this->state().me().id,
            1,
            0,
            0,
            false,
            onboarding,
            {boost::filesystem::path(this->_file_path).extension().string().substr(1)});
        }
        this->_run(this->_wait_for_decision_state);
      }

      ReceiveMachine::~ReceiveMachine()
      {
        this->_stop();
      }

      void
      ReceiveMachine::_save_snapshot() const
      {}

      float
      ReceiveMachine::progress() const
      {
        return this->_transfer_machine->progress();
      }

      std::unique_ptr<infinit::oracles::meta::CloudCredentials>
      ReceiveMachine::_cloud_credentials(bool regenerate)
      {
        return std::unique_ptr<infinit::oracles::meta::CloudCredentials>();
      }

      void
      ReceiveMachine::_cloud_synchronize()
      {}

      void
      ReceiveMachine::_cloud_operation()
      {}

      void
      ReceiveMachine::accept(boost::optional<std::string> relative_output_dir)
      {
        if (!this->_accepted.opened())
        {
          bool onboarding = true;
          if (this->state().metrics_reporter())
            this->state().metrics_reporter()->transaction_accepted(
              this->transaction_id(),
              onboarding);
        }
        this->data()->recipient_device_id = this->state().device().id;
        surface::gap::ReceiveMachine::accept(relative_output_dir);
      }

      void
      ReceiveMachine::reject()
      {
        if (!this->rejected().opened())
        {
          bool onboarding = true;
          if (this->state().metrics_reporter())
            this->state().metrics_reporter()->transaction_ended(
              this->transaction_id(),
              infinit::oracles::Transaction::Status::rejected,
              "",
              onboarding);
        }
        surface::gap::ReceiveMachine::reject();
      }

      infinit::oracles::meta::UpdatePeerTransactionResponse
      ReceiveMachine::_accept()
      {
        return infinit::oracles::meta::UpdatePeerTransactionResponse();
      }

      void
      ReceiveMachine::_transfer_operation(frete::RPCFrete& frete)
      {}

      std::unique_ptr<frete::RPCFrete>
      ReceiveMachine::rpcs(infinit::protocol::ChanneledStream& socket)
      {
        return nullptr;
      }

      void
      ReceiveMachine::cleanup()
      {}

      void
      ReceiveMachine::_update_meta_status(infinit::oracles::Transaction::Status)
      {
        // nothing
      }

      bool
      ReceiveMachine::completed() const
      {
        if (this->_transfer_machine)
          return this->_transfer_machine->finished();
        return false;
      }

      /*--------.
      | Metrics |
      `--------*/

      void
      ReceiveMachine::_metrics_ended(
        infinit::oracles::Transaction::Status status,
        std::string reason)
      {
        bool onboarding = true;
        if (this->state().metrics_reporter())
          this->state().metrics_reporter()->transaction_ended(
            this->transaction_id(),
            status,
            reason,
            onboarding,
            this->transaction().canceled_by_user());
      }
    }
  }
}
