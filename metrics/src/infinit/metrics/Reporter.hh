#ifndef INFINIT_METRICS_REPORTER_HH
# define INFINIT_METRICS_REPORTER_HH

# include <queue>
# include <string>

# include <reactor/Barrier.hh>
# include <reactor/signal.hh>
# include <reactor/thread.hh>

# include <infinit/oracles/Transaction.hh>

namespace infinit
{
  namespace metrics
  {
     enum TransferMethod
      {
        TransferMethodP2P,
        TransferMethodCloud,
        TransferMethodGhostCloud,
      };

      enum TransferExitReason
      {
        TransferExitReasonFinished,
        TransferExitReasonExhausted, // no more data from that source
        TransferExitReasonError, // specific error
        TransferExitReasonTerminated, // terminated by fsm state change
        TransferExitReasonUnknown, // not properly caught reason should not happen
      };

      enum TransactionType
      {
        LinkTransaction,
        PeerTransaction,
      };

    class CompositeReporter;
    /// Abstract metrics reporter.
    class Reporter:
      public elle::Printable
    {
    friend CompositeReporter;
    public:
      Reporter(std::string const& name);

      virtual
      ~Reporter();

      void
      start();

      virtual
      void
      stop();

    /// Public attributes.
    public:
      ELLE_ATTRIBUTE_R(std::string, name);

    /// Setter for metric sender id.
    public:
      static
      void
      metric_sender_id(std::string const& metric_sender_id);

    /// Transaction metrics.
    public:
      void
      transaction_accepted(std::string const& transaction_id);

      void
      transaction_connected(std::string const& transaction_id,
                            std::string const& connection_method);

      void
      link_transaction_created(std::string const& transaction_id,
                               std::string const& sender_id,
                               int64_t file_count,
                               int64_t total_size,
                               uint32_t message_length);

      void
      peer_transaction_created(std::string const& transaction_id,
                               std::string const& sender_id,
                               std::string const& recipient_id,
                               int64_t file_count,
                               int64_t total_size,
                               uint32_t message_length,
                               bool ghost);

      void
      transaction_ended(std::string const& transaction_id,
                        infinit::oracles::Transaction::Status status,
                        std::string const& info);

      /** entering a state that will effectively transfer data
      * @param initialization_time: time in seconds between entering state
      *        and effectively sending/receiving the first bytes
      */
      void
      transaction_transfer_begin(std::string const& transaction_id,
                                 TransferMethod method,
                                 float initialization_time);

      void
      transaction_transfer_end(std::string const& transaction_id,
                               TransferMethod method,
                               float duration,
                               uint64_t bytes_transfered,
                               TransferExitReason reason,
                               std::string const& message);

    /// Transaction metrics implementation.
    protected:
      virtual
      void
      _transaction_accepted(std::string const& transaction_id);

      virtual
      void
      _transaction_connected(std::string const& transaction_id,
                             std::string const& connection_method);

      virtual
      void
      _link_transaction_created(std::string const& transaction_id,
                                std::string const& sender_id,
                                int64_t file_count,
                                int64_t total_size,
                                uint32_t message_length);

      virtual
      void
      _peer_transaction_created(std::string const& transaction_id,
                                std::string const& sender_id,
                                std::string const& recipient_id,
                                int64_t file_count,
                                int64_t total_size,
                                uint32_t message_length,
                                bool ghost);

      virtual
      void
      _transaction_ended(std::string const& transaction_id,
                         infinit::oracles::Transaction::Status status,
                         std::string const& info);

      virtual
      void
      _transaction_transfer_begin(std::string const& transaction_id,
                                 TransferMethod method,
                                 float initialization_time);

      virtual
      void
      _transaction_transfer_end(std::string const& transaction_id,
                               TransferMethod method,
                               float duration,
                               uint64_t bytes_transfered,
                               TransferExitReason reason,
                               std::string const& message);
    /// User metrics.
    public:
      void
      user_favorite(std::string const& user_id);

      void
      user_login(bool success,
                 std::string const& info);

      void
      user_logout(bool success,
                  std::string const& info);

      void
      user_register(bool success,
                    std::string const& info);

      void
      user_unfavorite(std::string const& user_id);

      void
      user_heartbeat();

    /// User metrics implementation.
    protected:
      virtual
      void
      _user_favorite(std::string const& user_id);

      virtual
      void
      _user_login(bool success,
                  std::string const& info);

      virtual
      void
      _user_logout(bool success,
                   std::string const& info);

      virtual
      void
      _user_register(bool success,
                     std::string const& info);

      virtual
      void
      _user_unfavorite(std::string const& user_id);

      virtual
      void
      _user_heartbeat();

    /// Queue handling.
    private:
      void
      _poll();

    /// Protected variable setters.
    protected:
      void
      name(std::string name);

    /// Static class attribute accessors.
    protected:
      static
      std::string
      client_version();

      static
      std::string
      metric_sender_id();

      static
      std::string
      user_agent();

    /// Static class attributes.
    private:
      static
      std::string _client_version;

      static
      std::string _metric_sender_id;

      static
      std::string _user_agent;

    /// Private attributes.
    private:
      ELLE_ATTRIBUTE(reactor::Barrier, metric_available);
      ELLE_ATTRIBUTE(std::queue<std::function<void()>>, metric_queue);
      ELLE_ATTRIBUTE(reactor::Signal, metric_queue_empty);
      ELLE_ATTRIBUTE(bool, no_metrics);
      ELLE_ATTRIBUTE(std::unique_ptr<reactor::Thread>, poll_thread);

    /*----------.
    | Printable |
    `----------*/
    public:
      virtual
      void
      print(std::ostream& stream) const;
    };
  }
}

#endif // METRICS_REPORTER_HH
