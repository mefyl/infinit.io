#include <surface/gap/TransactionMachine.hh>

#include <functional>
#include <sstream>

#include <elle/AtomicFile.hh>
#include <elle/Backtrace.hh>
#include <elle/Error.hh>
#include <elle/container/list.hh>
#include <elle/container/set.hh>
#include <elle/network/Interface.hh>
#include <elle/os/environ.hh>
#include <elle/printf.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json.hh>

#include <reactor/fsm/Machine.hh>
#include <reactor/exception.hh>
#include <reactor/Scope.hh>

#include <CrashReporter.hh>
#include <common/common.hh>
#include <frete/Frete.hh>
#include <infinit/metrics/CompositeReporter.hh>
#include <papier/Authority.hh>
#include <surface/gap/PeerTransferMachine.hh>
#include <surface/gap/State.hh>

ELLE_LOG_COMPONENT("surface.gap.TransactionMachine");

namespace surface
{
  namespace gap
  {
    /*---------.
    | Snapshot |
    `---------*/

    TransactionMachine::Snapshot::Snapshot(TransactionMachine const& machine)
      : _current_state(machine._machine.current_state()->name())
    {}

    TransactionMachine::Snapshot::Snapshot(
      elle::serialization::SerializerIn& source)
      : _current_state()
    {
      this->serialize(source);
    }

    void
    TransactionMachine::Snapshot::serialize(elle::serialization::Serializer& s)
    {
      s.serialize("current_state", this->_current_state);
    }

    boost::filesystem::path
    TransactionMachine::Snapshot::path(TransactionMachine const& machine)
    {
      return machine.transaction().snapshots_directory() / "fsm.snapshot";
    }

    void
    TransactionMachine::Snapshot::print(std::ostream& stream) const
    {
      elle::fprintf(stream, "TransactionMachin::Snapshot(\"%s\")",
                    this->_current_state);
    }

    TransactionMachine::Snapshot
    TransactionMachine::snapshot() const
    {
      boost::filesystem::path path = Snapshot::path(*this);
      if (!exists(path))
        throw elle::Error(elle::sprintf("missing snapshot: %s", path));
      elle::AtomicFile source(path);
      return source.read() << [&] (elle::AtomicFile::Read& read)
      {
        elle::serialization::json::SerializerIn input(read.stream());
        return Snapshot(input);
      };
    }

    /*-------------------.
    | TransactionMachine |
    `-------------------*/

    TransactionMachine::TransactionMachine(
      Transaction& transaction,
      uint32_t id,
      std::shared_ptr<TransactionMachine::Data> data)
      : _id(id)
      , _machine(elle::sprintf("transaction (%s) fsm", id))
      , _machine_thread()
      , _finish_state(
        this->_machine.state_make(
          "finish", std::bind(&TransactionMachine::_finish, this)))
      , _reject_state(
        this->_machine.state_make(
          "reject", std::bind(&TransactionMachine::_reject, this)))
      , _cancel_state(
        this->_machine.state_make(
          "cancel", std::bind(&TransactionMachine::_cancel, this)))
      , _fail_state(
        this->_machine.state_make(
          "fail", std::bind(&TransactionMachine::_fail, this)))
      , _end_state(
        this->_machine.state_make(
          "end", std::bind(&TransactionMachine::_end, this)))
      , _finished("finished")
      , _rejected("rejected")
      , _canceled("canceled")
      , _failed("failed")
      , _transaction(transaction)
      , _state(transaction.state())
      , _data(std::move(data))
      , _gap_status(gap_transaction_new)
    {
      ELLE_TRACE_SCOPE("%s: create transaction machine", *this);
      this->_machine.transition_add(this->_finish_state, this->_end_state);
      this->_machine.transition_add(this->_cancel_state, this->_end_state);
      this->_machine.transition_add(this->_fail_state, this->_end_state);
      // Reject.
      this->_machine.transition_add(this->_reject_state, this->_end_state);
      // The catch transitions just open the barrier to logging purpose.
      // The snapshot will be kept.
      this->_machine.transition_add_catch(this->_fail_state, this->_end_state)
        .action([this] { ELLE_ERR("%s: failure failed", *this); });
      this->_machine.transition_add_catch(this->_cancel_state, this->_end_state)
        .action_exception(
          [this] (std::exception_ptr e)
          {
            ELLE_ERR("%s: cancellation failed: %s",
                     *this, elle::exception_string(e));
            this->_failed.open();
          });
      this->_machine.transition_add_catch(this->_finish_state, this->_end_state)
        .action_exception(
          [this] (std::exception_ptr e)
          {
            ELLE_ERR("%s: termination failed: %s",
                     *this, elle::exception_string(e));
            this->_failed.open();
          });

      this->_machine.transition_triggered().connect(
        [this] (reactor::fsm::Transition& transition)
        {
          ELLE_LOG_COMPONENT("surface.gap.TransactionMachine.Transition");
          ELLE_TRACE("%s: %s triggered", *this, transition);
        });

      this->_machine.state_changed().connect(
        [this] (reactor::fsm::State const& state)
        {
          this->_save_snapshot();
          ELLE_LOG_COMPONENT("surface.gap.TransactionMachine.State");
          ELLE_TRACE("%s: entering %s", *this, state);
        });
    }

    TransactionMachine::~TransactionMachine()
    {
      ELLE_TRACE_SCOPE("%s: destroying transaction machine", *this);
    }

    void
    TransactionMachine::_save_snapshot() const
    {
      boost::filesystem::path path = Snapshot::path(*this);
      ELLE_TRACE_SCOPE("%s: save snapshot to %s", *this, path);
      elle::AtomicFile destination(path);
      destination.write() << [&] (elle::AtomicFile::Write& write)
      {
        elle::serialization::json::SerializerOut output(write.stream());
        Snapshot(*this).serialize(output);
      };
    }

    void
    TransactionMachine::gap_status(gap_TransactionStatus v)
    {
      if (v != this->_gap_status)
      {
        ELLE_TRACE("%s: change GAP status to %s", *this, v);
        this->_gap_status = v;
        this->state().enqueue(Transaction::Notification(this->id(), v));
      }
    }

    void
    TransactionMachine::_end()
    {
      ELLE_TRACE_SCOPE("%s: end", *this);
      this->_transaction._over = true;
      boost::system::error_code error;
      auto path = this->transaction().snapshots_directory();
      boost::filesystem::remove_all(path, error);
      if (error)
        ELLE_WARN("%s: unable to remove snapshot directory %s: %s",
                  *this, path, error.message());
    }

    void
    TransactionMachine::_finish()
    {
      ELLE_TRACE_SCOPE("%s: finish", *this);
      this->gap_status(gap_transaction_finished);
      this->_finalize(infinit::oracles::Transaction::Status::finished);
    }

    void
    TransactionMachine::_reject()
    {
      ELLE_TRACE_SCOPE("%s: reject", *this);
      this->gap_status(gap_transaction_rejected);
      this->_finalize(infinit::oracles::Transaction::Status::rejected);
    }

    void
    TransactionMachine::_cancel()
    {
      ELLE_TRACE_SCOPE("%s: cancel", *this);
      this->gap_status(gap_transaction_canceled);
      this->_finalize(infinit::oracles::Transaction::Status::canceled);
    }

    void
    TransactionMachine::_fail()
    {
      ELLE_TRACE_SCOPE("%s: fail", *this);
      std::string transaction_id;
      if (!this->data()->id.empty())
        transaction_id = this->transaction_id();
      else
        transaction_id = "unknown";
      // Send report for failed transfer
      elle::crash::transfer_failed_report(this->state().meta().protocol(),
                                          this->state().meta().host(),
                                          this->state().meta().port(),
                                          this->state().me().email,
                                          transaction_id,
                                          this->transaction().failure_reason());
      this->gap_status(gap_transaction_failed);
      this->_finalize(infinit::oracles::Transaction::Status::failed);
    }

    void
    TransactionMachine::_run(reactor::fsm::State& initial_state)
    {
      ELLE_TRACE_SCOPE("%s: start transaction machine at %s",
                       *this, initial_state);
      ELLE_ASSERT(reactor::Scheduler::scheduler() != nullptr);
      this->_machine_thread.reset(
        new reactor::Thread{
          "TransactionMachine::run",
          [&]
          {
            try
            {
              this->_machine.run(initial_state);
              ELLE_TRACE("%s: machine finished properly", *this);
            }
            catch (reactor::Terminate const& e)
            {
              throw;
            }
            catch (...)
            {
              ELLE_WARN("%s: Exception escaped fsm run: %s",
                        *this, elle::exception_string());
              // Pretend this did not happen if state is final, or cancel.
              if (!_transaction.final())
              {
                try
                {
                  _transaction.cancel();
                }
                catch(...)
                {
                  // transaction can be in a non-cancelleable state (not initialized)
                }
              }
            }
          }});
    }

    void
    TransactionMachine::peer_available(
      std::vector<std::pair<std::string, int>> const& endpoints)
    {}

    void
    TransactionMachine::peer_unavailable()
    {}

    void
    TransactionMachine::notify_user_connection_status(std::string const&,
                                                      std::string const&,
                                                      bool)
    {}

    void
    TransactionMachine::cancel()
    {
      ELLE_TRACE_SCOPE("%s: cancel transaction %s", *this, this->data()->id);
      this->_canceled.open();
    }

    bool
    TransactionMachine::pause()
    {
      ELLE_TRACE_SCOPE("%s: pause transaction %s", *this, this->data()->id);
      throw elle::Exception(
        elle::sprintf("%s: pause not implemented yet", *this));
    }

    void
    TransactionMachine::interrupt()
    {
      ELLE_TRACE_SCOPE("%s: interrupt transaction %s", *this, this->data()->id);
      throw elle::Exception(
        elle::sprintf("%s: interruption not implemented yet", *this));
    }

    bool
    TransactionMachine::has_id(uint32_t id)
    {
      return (id == this->id());
    }

    void
    TransactionMachine::join()
    {
      ELLE_TRACE_SCOPE("%s: join machine", *this);
      if (this->_machine_thread == nullptr)
      {
        ELLE_WARN("%s: thread already destroyed", *this);
        return;
      }
      ELLE_ASSERT(reactor::Scheduler::scheduler() != nullptr);
      reactor::Thread* current = reactor::Scheduler::scheduler()->current();
      ELLE_ASSERT(current != nullptr);
      ELLE_DEBUG("%s: start joining", *this);
      if (this->_machine_thread.get() != nullptr)
      {
        current->wait(*this->_machine_thread.get());
      }
      ELLE_DEBUG("%s: successfully joined", *this);
    }

    void
    TransactionMachine::_stop()
    {
      ELLE_TRACE_SCOPE("%s: stop machine for transaction", *this);
      ELLE_ASSERT(reactor::Scheduler::scheduler() != nullptr);
      if (this->_machine_thread != nullptr)
      {
        ELLE_DEBUG("%s: terminate machine thread", *this)
          this->_machine_thread->terminate_now();
        this->_machine_thread.reset();
      }
    }

    /*-----------.
    | Attributes |
    `-----------*/

    std::string const&
    TransactionMachine::transaction_id() const
    {
      if (this->_data->id.empty())
        throw elle::Exception(
          elle::sprintf("%s: Transaction machine is not ready", *this));
      return this->_data->id;
    }

    void
    TransactionMachine::transaction_id(std::string const& id)
    {
      if (!this->_data->id.empty())
      {
        ELLE_ASSERT_EQ(this->_data->id, id);
        return;
      }
      this->_data->id = id;
    }

    /*----------.
    | Printable |
    `----------*/

    void
    TransactionMachine::print(std::ostream& stream) const
    {
      auto const& data = *this->_data;
      auto const& me = this->state().me();
      stream << elle::demangle(typeid(*this).name())
             << "(id=" << this->id() << ", "
             << "(u=" << me.id;
      if (!data.id.empty())
        stream << ", t=" << data.id;
      stream << ")";
    }

    void
    TransactionMachine::reset_transfer()
    {
      this->reset_transfer_signal().signal();
    }

    void
    TransactionMachine::_report_s3_error(aws::AWSException const& exception, bool will_retry)
    {
      if (auto& mr = state().metrics_reporter())
      {
        int http_status = 0;
        std::string aws_error_code;
        std::string message = exception.what();
        if (exception.inner_exception())
        {
          if (auto awserror = dynamic_cast<aws::RequestError*>(
            exception.inner_exception().get()))
          {
            if (auto& ec = awserror->error_code())
              aws_error_code = *ec;
            if (auto& hs = awserror->http_status())
              http_status = static_cast<int>(*hs);
            message = awserror->what();
          }
          else
            message = exception.inner_exception()->what();
        }
        mr->aws_error(this->transaction_id(),
                      exception.operation(),
                      exception.url(),
                      exception.attempt(),
                      http_status,
                      aws_error_code,
                      (will_retry? "TRANSIENT:":"FATAL:") + message);
      }
    }
  }
}