#ifndef TRANSFERMACHINE_HH
# define TRANSFERMACHINE_HH

# include <surface/gap/usings.hh>

# include <plasma/meta/Client.hh>

# include <papier/fwd.hh>

# include <etoile/fwd.hh>

# include <hole/storage/Directory.hh>
# include <hole/implementations/slug/Slug.hh>

# include <nucleus/proton/Network.hh>

# include <metrics/fwd.hh>

# include <reactor/fsm.hh>
# include <reactor/network/Protocol.hh>
# include <reactor/scheduler.hh>
# include <reactor/thread.hh>
# include <reactor/waitable.hh>
# include <reactor/Barrier.hh>

# include <elle/Printable.hh>

# include <thread>

namespace surface
{
  namespace gap
  {
    class State;

    class TransferMachine:
      public elle::Printable
    {
    public:
      TransferMachine(surface::gap::State const& state);

      virtual
      ~TransferMachine();

    public:
      void
      run(reactor::fsm::State& initial_state);

      virtual
      void
      on_transaction_update(plasma::Transaction const& transaction) = 0;

      virtual
      void
      on_peer_connection_update(PeerConnectionUpdateNotification const& notif) = 0;

      void
      cancel();

    public:
      bool
      concerns_network(std::string const& network_id);

      bool
      concerns_transaction(std::string const& transaction_id);

      bool
      concerns_user(std::string const& user_id);

      bool
      has_id(uint32_t id);

    public:
      void
      join();

    protected:
      void
      _stop();

      /*-----------------------.
      | Machine implementation |
      `-----------------------*/
    private:
      ELLE_ATTRIBUTE_P(reactor::Scheduler, scheduler, mutable);
    public:
      reactor::Scheduler&
      scheduler() const;
      ELLE_ATTRIBUTE(std::unique_ptr<std::thread>, scheduler_thread);
      ELLE_ATTRIBUTE_R(uint32_t, id);

    protected:
      reactor::fsm::Machine _machine;
      ELLE_ATTRIBUTE(std::unique_ptr<reactor::Thread>, machine_thread);

    protected:
      void
      _transfer_core();

      void
      _finish();

      void
      _reject();

      void
      _fail();

      void
      _cancel();

      void
      _finiliaze(plasma::TransactionStatus);

    private:
      void
      _clean();

    protected:
      virtual
      void
      _transfer_operation() = 0;

    protected:
      // This state has to be protected to allow the children to start the
      // machine in this state.
      reactor::fsm::State& _transfer_core_state;
      reactor::fsm::State& _finish_state;
      reactor::fsm::State& _reject_state;
      reactor::fsm::State& _cancel_state;
      reactor::fsm::State& _fail_state;
      reactor::fsm::State& _clean_state;

    protected:
      reactor::Barrier _finished;
      reactor::Barrier _rejected;
      reactor::Barrier _canceled;
      reactor::Barrier _failed;

      /*-------------.
      | Core Machine |
      `-------------*/
    private:
      reactor::fsm::Machine _core_machine;

      void
      _publish_interfaces();

      void
      _connection();

      void
      _wait_for_peer();

      void
      _transfer();

      void
      _core_stoped();

      // Common on both sender and recipient process.
      ELLE_ATTRIBUTE(reactor::fsm::State&, publish_interfaces_state);
      ELLE_ATTRIBUTE(reactor::fsm::State&, connection_state);
      // Broken: I you disconnect, which is quasi transparent, you must publish your interfaces.
      ELLE_ATTRIBUTE(reactor::fsm::State&, wait_for_peer_state);
      ELLE_ATTRIBUTE(reactor::fsm::State&, transfer_state);
      ELLE_ATTRIBUTE(reactor::fsm::State&, core_stoped_state);

    protected:
      // User status signal.
      reactor::Signal _peer_online;
      reactor::Signal _peer_offline;

      // Slug?
      reactor::Signal _peer_connected;
      reactor::Signal _peer_disconnected;

      ELLE_ATTRIBUTE_R(surface::gap::State const&, state);

      /*------------.
      | Transaction |
      `------------*/
      ELLE_ATTRIBUTE(std::string, transaction_id);

    public:
      std::string const&
      transaction_id() const;

    protected:
      void
      transaction_id(std::string const& id);

    protected:
      ELLE_ATTRIBUTE(std::string, peer_id);

    public:
      std::string const&
      peer_id() const;

    protected:
      void
      peer_id(std::string const& id);

    public:
      bool
      is_sender();

      /*--------.
      | Network |
      `--------*/
      ELLE_ATTRIBUTE(std::string, network_id);
    protected:
      std::string const&
      network_id() const;

      void
      network_id(std::string const& id);

      ELLE_ATTRIBUTE(std::unique_ptr<nucleus::proton::Network>, network);
    protected:
      nucleus::proton::Network&
      network();

      ELLE_ATTRIBUTE(std::unique_ptr<papier::Descriptor>, descriptor);
    protected:
      papier::Descriptor const&
      descriptor();

      /*-------.
      | Etoile |
      `-------*/
      ELLE_ATTRIBUTE(std::unique_ptr<hole::storage::Directory>, storage);
    protected:
      hole::storage::Directory&
      storage();

      ELLE_ATTRIBUTE(std::unique_ptr<hole::implementations::slug::Slug>, hole);
    protected:
      hole::implementations::slug::Slug&
      hole();

      ELLE_ATTRIBUTE(std::unique_ptr<etoile::Etoile>, etoile);
    protected:
      etoile::Etoile&
      etoile();

    public:

      virtual
      std::string
      type() const;

      /*--------.
      | Metrics |
      `--------*/
      metrics::Metric
      network_metric();

      metrics::Metric
      transaction_metric();

      /*----------.
      | Printable |
      `----------*/
      void
      print(std::ostream& stream) const override;
    };
  }
}

#endif
