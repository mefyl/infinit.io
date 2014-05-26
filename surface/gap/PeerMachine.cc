#include <elle/log.hh>

#include <infinit/oracles/meta/Client.hh>
#include <surface/gap/Exception.hh>
#include <surface/gap/PeerMachine.hh>
#include <surface/gap/PeerTransferMachine.hh>
#include <surface/gap/State.hh>

ELLE_LOG_COMPONENT("surface.gap.PeerMachine");

namespace surface
{
  namespace gap
  {
    PeerMachine::PeerMachine(Transaction& transaction,
                             uint32_t id,
                             std::shared_ptr<Data> data)
      : Super(transaction, id, data)
      , _data(data)
      , _transfer_machine(new PeerTransferMachine(*this))
      , _transfer_core_state(
        this->_machine.state_make(
          "transfer core", std::bind(&PeerMachine::_transfer_core, this)))
    {
      this->_machine.transition_add(
        this->_transfer_core_state,
        this->_finish_state,
        reactor::Waitables{&this->finished()},
        true);
      this->_machine.transition_add(
        this->_transfer_core_state,
        this->_cancel_state,
        reactor::Waitables{&this->canceled()}, true);
      this->_machine.transition_add_catch(
        this->_transfer_core_state,
        this->_fail_state)
        .action_exception(
          [this] (std::exception_ptr e)
          {
            ELLE_WARN("%s: error while transfering: %s",
                      *this, elle::exception_string(e));
          });
      this->_machine.transition_add(
        this->_transfer_core_state,
        this->_fail_state,
        reactor::Waitables{&this->failed()}, true);
      this->_machine.transition_add(
        this->_transfer_core_state,
        this->_transfer_core_state,
        reactor::Waitables{&this->reset_transfer_signal()},
        true);
    }

    void
    PeerMachine::_transfer_core()
    {
      ELLE_TRACE_SCOPE("%s: start transfer core machine", *this);
      ELLE_ASSERT(reactor::Scheduler::scheduler() != nullptr);
      try
      {
        this->_transfer_machine->run();
        ELLE_TRACE("%s: core machine finished properly", *this);
      }
      catch (reactor::Terminate const&)
      {
        ELLE_TRACE("%s: terminated", *this);
        throw;
      }
      catch (std::exception const&)
      {
        ELLE_ERR("%s: something went wrong while transfering: %s",
                 *this, elle::exception_string());
        throw;
      }
      if (this->failed().opened())
        throw Exception(gap_error, "an error occured");
    }

    void
    PeerMachine::peer_available(
      std::vector<std::pair<std::string, int>> const& endpoints)
    {
      ELLE_TRACE_SCOPE("%s: peer is available for peer to peer connection",
                       *this);
      this->_transfer_machine->peer_available(endpoints);
    }

    void
    PeerMachine::peer_unavailable()
    {
      ELLE_TRACE_SCOPE("%s: peer is unavailable for peer to peer connection",
                       *this);
      this->_transfer_machine->peer_unavailable();
    }

    void
    PeerMachine::_peer_connection_changed(bool user_status)
    {
      ELLE_TRACE_SCOPE("%s: update with new peer connection status %s",
                       *this, user_status);
      ELLE_ASSERT(reactor::Scheduler::scheduler() != nullptr);
      if (user_status)
        ELLE_DEBUG("%s: peer is now online", *this)
        {
          this->_transfer_machine->peer_offline().close();
          this->_transfer_machine->peer_online().open();
        }
      else
        ELLE_DEBUG("%s: peer is now offline", *this)
        {
          this->_transfer_machine->peer_online().close();
          this->_transfer_machine->peer_offline().open();
        }
    }

    float
    PeerMachine::progress() const
    {
      return this->_transfer_machine->progress();
    }

    aws::Credentials
    PeerMachine::_aws_credentials(bool first_time)
    {
      auto& meta = this->state().meta();
      int delay = 1;
      infinit::oracles::meta::CloudBufferTokenResponse token;
      while (true)
      {
        try
        {
          token =
            meta.get_cloud_buffer_token(this->transaction_id(), !first_time);
          break;
        }
        catch(elle::http::Exception const&)
        { // Permanent error
          ELLE_LOG("%s: get_cloud_buffer_token failed with %s, aborting...",
                   *this, elle::exception_string());
          throw;
        }
        catch(infinit::oracles::meta::Exception const&)
        {
          ELLE_LOG("%s: get_cloud_buffer_token failed with %s, retrying...",
                   *this, elle::exception_string());
          // if meta looses connectivity to provider let's not flood it
          reactor::sleep(boost::posix_time::seconds(delay));
          delay = std::min(delay * 2, 60 * 10);
        }
      }
      auto credentials = aws::Credentials(token.access_key_id,
                                          token.secret_access_key,
                                          token.session_token,
                                          token.region,
                                          token.bucket,
                                          token.folder,
                                          token.expiration);
      return credentials;
    }

    void
    PeerMachine::_finalize(infinit::oracles::Transaction::Status status)
    {
      ELLE_TRACE_SCOPE("%s: finalize machine: %s", *this, status);
      if (!this->data()->id.empty())
      {
        try
        {
          this->state().meta().update_transaction(
            this->transaction_id(), status);
        }
        catch (infinit::oracles::meta::Exception const& e)
        {
          using infinit::oracles::meta::Error;
          if (e.err == Error::transaction_already_finalized)
            ELLE_TRACE("%s: transaction already finalized", *this);
          else if (e.err == Error::transaction_already_has_this_status)
            ELLE_TRACE("%s: transaction already in this state", *this);
          else
            ELLE_ERR("%s: unable to finalize the transaction %s: %s",
                     *this, this->transaction_id(), elle::exception_string());
        }
        catch (std::exception const&)
        {
          ELLE_ERR("%s: unable to finalize the transaction %s: %s",
                   *this, this->transaction_id(), elle::exception_string());
        }
      }
      else
      {
        ELLE_ERR("%s: can't finalize transaction: id is still empty", *this);
      }
      this->cleanup();
    }
  }
}
