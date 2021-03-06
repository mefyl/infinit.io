#ifndef INFINIT_METRICS_COMPOSITE_REPORTER_HH
# define INFINIT_METRICS_COMPOSITE_REPORTER_HH

# include <vector>

# include <infinit/metrics/Reporter.hh>

namespace infinit
{
  namespace metrics
  {
    class CompositeReporter:
      public Reporter
    {
    public:
      CompositeReporter();

      virtual
      ~CompositeReporter() = default;

      virtual
      void
      start();

      virtual
      void
      stop();

    /// Proxy management.
    public:
      virtual
      void
      proxy(reactor::network::Proxy const& proxy) override;

    /// Reporter management.
    public:
      void
      add_reporter(std::unique_ptr<Reporter>&& reporter);

    /// Transaction metrics.
    private:
      void
      _transaction_accepted(std::string const& transaction_id,
                            bool onboarding) override;

      void
      _transaction_connected(std::string const& transaction_id,
                             std::string const& connection_method,
                             int attempt) override;

      void
      _link_transaction_created(std::string const& transaction_id,
                                std::string const& sender_id,
                                int64_t file_count,
                                int64_t total_size,
                                uint32_t message_length,
                                std::vector<std::string> extensions,
                                bool onboarding) override;

      void
      _peer_transaction_created(std::string const& transaction_id,
                                std::string const& sender_id,
                                std::string const& recipient_id,
                                int64_t file_count,
                                int64_t total_size,
                                uint32_t message_length,
                                bool ghost,
                                bool onboarding,
                                std::vector<std::string> extensions
                                ) override;

      void
      _transaction_ended(std::string const& transaction_id,
                         infinit::oracles::Transaction::Status status,
                         std::string const& info,
                         bool onboarding,
                         bool caused_by_user) override;

      void
      _transaction_deleted(std::string const& transaction_id) override;

      void
      _transaction_transfer_begin(std::string const& transaction_id,
                                 TransferMethod method,
                                 float initialization_time,
                                 int attempt) override;

      void
      _transaction_transfer_end(std::string const& transaction_id,
                               TransferMethod method,
                               float duration,
                               uint64_t bytes_transfered,
                               TransferExitReason reason,
                               std::string const& message,
                               int attempt) override;
      void
      _aws_error(std::string const& transaction_id,
                 std::string const& operation,
                 std::string const& url,
                 unsigned int attempt,
                 int http_status,
                 std::string const& aws_error_code,
                 std::string const& message) override;

      virtual
      void
      _quota_exceeded(uint64_t size, uint64_t current, uint64_t total) override;
    /// User metrics.
    private:
      void
      _user_favorite(std::string const& user_id) override;

      void
      _user_login(bool success, std::string const& info) override;

      void
      _facebook_connect(bool success, std::string const& info) override;

      void
      _user_logout(bool success, std::string const& info) override;

      void
      _user_register(bool success, std::string const& info,
                     std::string const& with,
                     boost::optional<std::string> const& ghost_code) override;

      void
      _user_unfavorite(std::string const& user_id) override;

      void
      _user_heartbeat() override;

      void
      _user_first_launch() override;

      void
      _user_proxy(reactor::network::ProxyType proxy_type) override;

      void
      _user_crashed() override;

      void
      _user_changed_download_dir(bool fallback) override;

      void
      _user_used_ghost_code(bool success,
                            std::string const& code,
                            bool link,
                            std::string const& fail_reason) override;

      void
      _user_sent_sms_ghost_code(bool success,
                                std::string const& code,
                                std::string const& fail_reason) override;

      /// UI metrics:
      void
      _ui(std::string const& event,
          std::string const& from,
          Additional const&) override;

    /// Dispatch metrics.
    private:
      void
      _dispatch(std::function<void(Reporter*)> fn);

    /// Private attributes.
    private:
      ELLE_ATTRIBUTE(std::vector<std::unique_ptr<Reporter>>,
                     reporters);
    };
  }
}

#endif // INFINIT_METRICS_COMPOSITE_REPORTER_HH
