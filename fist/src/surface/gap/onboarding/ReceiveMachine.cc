#include <elle/log.hh>

#include <frete/RPCFrete.hh>

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
                                     reactor::Duration duration)
        : surface::gap::TransactionMachine(transaction, id, data)
        , surface::gap::ReceiveMachine(transaction, id, data)
        , surface::gap::PeerMachine(transaction, id, std::move(data))
        , _file_path(file_path)
      {
        ELLE_TRACE_SCOPE("%s: construction", *this);
        this->_transfer_machine.reset(
          new TransferMachine(
            *this, this->_file_path, this->state().output_dir(), duration));

        // Normal way.
        this->_machine.transition_add(this->_accept_state,
                                      this->_transfer_core_state);
        this->_machine.transition_add(this->_transfer_core_state,
                                      this->_finish_state);

        this->_machine.transition_add(
          _transfer_core_state,
          _cancel_state,
          reactor::Waitables{&this->canceled()}, true);

        // Exception.
        this->_machine.transition_add_catch(_transfer_core_state, _fail_state);

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
            onboarding);
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

      aws::Credentials
      ReceiveMachine::_aws_credentials(bool regenerate)
      {
        return aws::Credentials();
      }

      void
      ReceiveMachine::_cloud_synchronize()
      {}

      void
      ReceiveMachine::_cloud_operation()
      {}

      void
      ReceiveMachine::accept()
      {
        if (!this->_accepted.opened())
        {
          bool onboarding = true;
          if (this->state().metrics_reporter())
            this->state().metrics_reporter()->transaction_accepted(
              this->transaction_id(),
              onboarding);
        }
        surface::gap::ReceiveMachine::accept();
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

      bool
      ReceiveMachine::pause()
      {
        auto* machine = static_cast<TransferMachine*>(
          this->_transfer_machine.get());
        ELLE_TRACE_SCOPE("%s: machine %spaused",
                         *this, (machine->running().opened() ? "" : "un"));
        if (machine->running().opened())
          machine->running().close();
        else
          machine->running().open();
        return machine->running().opened();
      }

      void
      ReceiveMachine::interrupt()
      {
        auto* machine = static_cast<TransferMachine*>(
          this->_transfer_machine.get());
        machine->interrupt();
      }

      void
      ReceiveMachine::_accept()
      {}

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
      ReceiveMachine::_finalize(infinit::oracles::Transaction::Status s)
      {
        if (this->state().metrics_reporter())
        {
          bool onboarding = true;
          if (this->state().metrics_reporter())
            this->state().metrics_reporter()->transaction_ended(
            this->transaction_id(),
            s,
            "",
            onboarding);
        }
      }
    }
  }
}