#include <fstream>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <openssl/sha.h>

#include <elle/AtomicFile.hh>
#include <elle/format/gzip.hh>
#include <elle/log.hh>
#include <elle/log/TextLogger.hh>
#include <elle/os/environ.hh>
#include <elle/os/path.hh>
#include <elle/serialization/json/SerializerIn.hh>
#include <elle/serialization/json/SerializerOut.hh>
#include <elle/serialize/HexadecimalArchive.hh>

#include <reactor/duration.hh>
#include <reactor/http/exceptions.hh>

#include <common/common.hh>

#include <papier/Identity.hh>
#include <papier/Passport.hh>

#include <infinit/metrics/Reporter.hh>
#include <infinit/oracles/trophonius/Client.hh>

#include <surface/gap/Error.hh>
#include <surface/gap/ReceiveMachine.hh>
#include <surface/gap/SendMachine.hh>
#include <surface/gap/State.hh>
#include <surface/gap/TransactionMachine.hh>
#include <version.hh>

ELLE_LOG_COMPONENT("infinit.surface.gap.State");

namespace surface
{
  namespace gap
  {
    namespace fs = boost::filesystem;

    LoggerInitializer::LoggerInitializer()
    {
      ELLE_TRACE_METHOD("");

      std::string log_file = elle::os::getenv("INFINIT_LOG_FILE", "");
      if (!log_file.empty())
      {
        if (elle::os::inenv("INFINIT_LOG_FILE_PID"))
        {
          log_file += ".";
          log_file += std::to_string(::getpid());
        }

        this->_output.reset(
          new std::ofstream{
            log_file,
              std::fstream::trunc | std::fstream::out});

        std::string log_level =
          "elle.CrashReporter:DEBUG,"
          "*FIST*:TRACE,"
          "*FIST.State*:DEBUG,"
          "frete.Frete:TRACE,"
          "infinit.surface.gap.Rounds:DEBUG,"
          "*meta*:TRACE,"
          "OSX*:DUMP,"
          "reactor.fsm.*:TRACE,"
          "reactor.network.upnp:DEBUG,"
          "station.Station:DEBUG,"
          "surface.gap.*:TRACE,"
          "surface.gap.TransferMachine:DEBUG,"
          "*trophonius*:DEBUG";
        bool display_type = true;
        bool enable_pid = false;
        bool enable_tid = true;
        bool enable_time = true;
        bool universal_time = false;

        auto logger_ptr = std::unique_ptr<elle::log::Logger>(
          new elle::log::TextLogger(*this->_output,
                                    log_level,
                                    display_type,
                                    enable_pid,
                                    enable_tid,
                                    enable_time,
                                    universal_time));
        elle::log::logger(std::move(logger_ptr));
      }
      ELLE_LOG("Infinit Version: %s", INFINIT_VERSION);
    }

    LoggerInitializer::~LoggerInitializer()
    {
      std::string log_file = elle::os::getenv("INFINIT_LOG_FILE", "");
      if (!log_file.empty())
      {
        elle::log::logger(
          std::unique_ptr<elle::log::Logger>{
            new elle::log::TextLogger(std::cerr)});

        this->_output.reset();
      }
    }

    /*--------------.
    | Notifications |
    `--------------*/
    State::ConnectionStatus::ConnectionStatus(bool status):
      status(status)
    {}
    Notification::Type State::ConnectionStatus::type =
      NotificationType_ConnectionStatus;

    Notification::Type State::KickedOut::type = NotificationType_KickedOut;

    Notification::Type State::TrophoniusUnavailable::type =
      NotificationType_TrophoniusUnavailable;

    /*-------------------------.
    | Construction/Destruction |
    `-------------------------*/
    State::State(std::string const& meta_protocol,
                 std::string const& meta_host,
                 uint16_t meta_port,
                 boost::uuids::uuid device,
                 std::vector<unsigned char> trophonius_fingerprint,
                 std::unique_ptr<infinit::metrics::Reporter> metrics)
      : _logger_intializer()
      , _meta(meta_protocol, meta_host, meta_port)
      , _meta_message("")
      , _trophonius_fingerprint(trophonius_fingerprint)
      , _trophonius(nullptr)
      , _metrics_reporter(std::move(metrics))
      , _me()
      , _output_dir(common::system::download_directory())
      , _device_uuid(std::move(device))
      , _device()
    {
      ELLE_TRACE_SCOPE("%s: create state", *this);
      if (!this->_metrics_reporter)
        // This is a no-op reporter.
        this->_metrics_reporter.reset(new infinit::metrics::CompositeReporter);
      this->_metrics_reporter->start();
      infinit::metrics::Reporter::metric_device_id(
        boost::lexical_cast<std::string>(this->device_uuid()));
      ELLE_LOG("%s: device uuid: %s", *this, this->device_uuid());
      // Fill configuration.
      auto& config = this->_configuration;
      config.s3.multipart_upload.parallelism = 1;
      config.s3.multipart_upload.chunk_size = 0;
      config.max_mirror_size = 0;
      config.max_compress_size = 0;
      config.disable_upnp = false;
      std::ifstream fconfig(common::infinit::configuration_path());
      if (fconfig.good())
      {
        try
        {
            elle::json::Object obj = boost::any_cast<elle::json::Object>(
              elle::json::read(fconfig));
            _apply_configuration(obj);
        }
        catch(std::exception const& e)
        {
          ELLE_ERR("%s: while reading configuration: %s", *this, e.what());
          std::stringstream str;
          {
            elle::serialization::json::SerializerOut output(str);
            this->_configuration.serialize(output);
          }
          ELLE_TRACE("%s: current config: %s", *this, str.str());
        }
      }
      this->_check_first_launch();
    }

    State::~State()
    {
      ELLE_TRACE_SCOPE("%s: destroying state", *this);

      try
      {
        this->logout();
        this->_metrics_reporter->stop();
      }
      catch (std::runtime_error const&)
      {
        ELLE_WARN("Couldn't logout: %s", elle::exception_string());
      }

      ELLE_TRACE("%s: destroying members", *this);
    }

    void
    State::set_proxy(reactor::network::Proxy const& proxy)
    {
      ELLE_TRACE("%s: set proxy: %s", *this, proxy);
      using ProxyType = reactor::network::ProxyType;
      switch (proxy.type())
      {
        case ProxyType::None:
          break;
        case ProxyType::HTTP:
          this->_set_http_proxy(proxy);
          break;
        case ProxyType::HTTPS:
          this->_set_https_proxy(proxy);
          break;
        case ProxyType::SOCKS:
          this->_set_socks_proxy(proxy);
          break;
      }
    }

    void
    State::unset_proxy(reactor::network::ProxyType const& proxy_type)
    {
      ELLE_TRACE("%s: unset proxy: %s", *this, proxy_type);
      using ProxyType = reactor::network::ProxyType;
      reactor::network::Proxy proxy;
      switch (proxy_type)
      {
        case ProxyType::None:
          break;
        case ProxyType::HTTP:
          this->_set_http_proxy(proxy);
          break;
        case ProxyType::HTTPS:
          this->_set_https_proxy(proxy);
          break;
        case ProxyType::SOCKS:
          this->_set_socks_proxy(proxy);
          break;
      }
    }

    void
    State::_set_http_proxy(reactor::network::Proxy const& proxy)
    {
      // The == operator of Proxy has been overriden to check only the type
      // and host of given proxies.
      bool same = this->_http_proxy == proxy;
      this->_http_proxy = proxy;
      if (this->_meta.protocol() == "http")
        this->_meta.default_configuration().proxy(proxy);
      this->_metrics_reporter->proxy(proxy);
      if (!same && !proxy.host().empty())
        this->_metrics_reporter->user_proxy(proxy.type());
    }

    void
    State::_set_https_proxy(reactor::network::Proxy const& proxy)
    {
      // The == operator of Proxy has been overriden to check only the type
      // and host of given proxies.
      bool same = this->_https_proxy == proxy;
      this->_https_proxy = proxy;
      if (this->_meta.protocol() == "https")
        this->_meta.default_configuration().proxy(proxy);
      if (!same && !proxy.host().empty())
        this->_metrics_reporter->user_proxy(proxy.type());
    }

    void
    State::_set_socks_proxy(reactor::network::Proxy const& proxy)
    {
      // The == operator of Proxy has been overriden to check only the type
      // and host of given proxies.
      bool same = this->_socks_proxy == proxy;
      this->_socks_proxy = proxy;
      if (!same && !proxy.host().empty())
        this->_metrics_reporter->user_proxy(proxy.type());
    }

    infinit::oracles::meta::Client const&
    State::meta(bool authentication_required) const
    {
      if (authentication_required && !this->_meta.logged_in())
        throw Exception{gap_not_logged_in, "you must be logged in"};

      return this->_meta;
    }

    void
    State::_check_first_launch()
    {
      if (boost::filesystem::exists(common::infinit::first_launch_path()))
        return;

      if (!boost::filesystem::exists(common::infinit::home()))
      {
        boost::filesystem::create_directories(
          boost::filesystem::path(common::infinit::home()));
      }
      elle::AtomicFile f(common::infinit::first_launch_path());
      f.write() << [] (elle::AtomicFile::Write& write)
        {
          write.stream() << "0\n";
        };
      this->_metrics_reporter->user_first_launch();
    }

    /*---------------------------.
    | Server Connection Checking |
    `---------------------------*/
    bool
    State::_meta_server_check(reactor::Duration timeout)
    {
      ELLE_TRACE_SCOPE("%s: fetching Meta status", *this);
      bool result = false;
      return elle::With<reactor::Scope>() << [&] (reactor::Scope& scope)
      {
        scope.run_background("meta status check", [&]
        {
          try
          {
            auto meta_response = this->_meta.server_status();
            if (meta_response.status)
            {
              ELLE_TRACE("%s: Meta is reachable", *this);
              result = true;
            }
            else
            {
              this->_meta_message = meta_response.message;
              ELLE_WARN("%s: Meta down with message: %s",
                        *this,
                        this->_meta_message);
              result = false;
            }
          }
          catch (reactor::http::RequestError const& e)
          {
            ELLE_WARN("%s: unable to contact Meta: %s",
                     *this,
                     e.what());
            result = false;
          }
          catch (elle::http::Exception const& e)
          {
            ELLE_WARN("%s: unable to contact Meta: %s",
                     *this,
                     e.what());
            result = false;
          }
          catch (reactor::network::Exception const& e)
          {
            ELLE_WARN("%s: unable to contact Meta: %s",
                     *this,
                     e.what());
            result = false;
          }
          catch (elle::Exception const& e)
          {
            ELLE_WARN("%s: error while checking meta connectivity: %s",
                     *this,
                     e.what());
            result = false;
            // XXX: We shouldn't be catching all exceptions but the old JSON
            // parser throws elle::Exceptions.
          }
        });
        scope.wait(timeout);
        return result;
      };
    }

    bool
    State::_meta_server_check()
    {
      return this->_meta_server_check(10_sec);
    }

    /*----------------------.
    | Login/Logout/Register |
    `----------------------*/
    void
    State::login(
      std::string const& email,
      std::string const& password)
    {
      return this->login(
        email, password,
        std::unique_ptr<infinit::oracles::trophonius::Client>());
    }

    void
    State::login(
      std::string const& email,
      std::string const& password,
      std::unique_ptr<infinit::oracles::trophonius::Client> trophonius)
    {
      ELLE_TRACE_SCOPE("%s: login to meta as %s", *this, email);

      ELLE_ASSERT(reactor::Scheduler::scheduler() != nullptr);

      reactor::Scheduler& scheduler = *reactor::Scheduler::scheduler();

      reactor::Lock l(this->_login_mutex);

      // Ensure we don't have an old Meta message
      this->_meta_message.clear();

      if (this->logged_in())
        throw Exception(gap_already_logged_in, "already logged in");

      this->_cleanup();

      if (!this->_meta_server_check())
      {
        if (this->_meta_message.empty())
        {
          throw Exception(gap_meta_unreachable,
                          "Unable to contact Meta");
        }
        else
        {
          throw Exception(gap_meta_down_with_message,
                          elle::sprintf("Meta down with message: %s",
                                        this->_meta_message));
        }
      }

      std::string lower_email = email;

      std::transform(lower_email.begin(),
                     lower_email.end(),
                     lower_email.begin(),
                     ::tolower);

      elle::SafeFinally login_failed(
        [this, lower_email]
        {
          infinit::metrics::Reporter::metric_sender_id(lower_email);
          this->_metrics_reporter->user_login(false, "");
        });
      auto login_response =
        this->_meta.login(lower_email, password, _device_uuid);
      login_failed.abort();

      ELLE_LOG("%s: logged in as %s", *this, email);
      elle::With<elle::Finally>([&]
        {
          ELLE_WARN("%s: error during login, logout: %s",
                    *this, elle::exception_string());
          this->_meta.logout();
        })
        << [&] (elle::Finally& finally_logout)
      {
        if (trophonius)
        {
          this->_trophonius.swap(trophonius);
        }
        else
        {
          this->_trophonius.reset(new infinit::oracles::trophonius::Client(
          [this] (bool status)
          {
            this->on_connection_changed(status);
          },
          std::bind(&State::on_reconnection_failed, this),
          this->_trophonius_fingerprint));
        }
        std::string trophonius_host =
          elle::os::getenv("INFINIT_TROPHONIUS_HOST",
                           login_response.trophonius.host);
        std::string env_port =
          elle::os::getenv("INFINIT_TROPHONIUS_PORT", "");
        int trophonius_port = login_response.trophonius.port_ssl;
        if (!env_port.empty())
          trophonius_port = boost::lexical_cast<int>(env_port);
        this->_trophonius->server(trophonius_host, trophonius_port);
        infinit::metrics::Reporter::metric_sender_id(login_response.id);
        this->_metrics_reporter->user_login(true, "");
        this->_metrics_heartbeat_thread.reset(
          new reactor::Thread{
            *reactor::Scheduler::scheduler(),
              "metrics heartbeat",
              [&]
              {
                while (true)
                {
                  reactor::sleep(360_min);
                  this->_metrics_reporter->user_heartbeat();
                }
              }});

        std::string identity_clear;

        ELLE_TRACE("%s: decrypt identity", *this)
        {
          this->_identity.reset(new papier::Identity());
          if (this->_identity->Restore(login_response.identity) == elle::Status::Error)
            throw Exception(gap_internal_error, "unable to restore the identity");
          if (this->_identity->Decrypt(password) == elle::Status::Error)
            throw Exception(gap_internal_error, "unable to decrypt the identity");
          if (this->_identity->Clear() == elle::Status::Error)
            throw Exception(gap_internal_error, "unable to clear the identity");
          if (this->_identity->Save(identity_clear) == elle::Status::Error)
            throw Exception(gap_internal_error, "unable to save the identity");
        }

        ELLE_TRACE("%s: store identity", *this)
        {
          if (this->_identity->Restore(identity_clear) == elle::Status::Error)
            throw Exception(gap_internal_error,
                            "Cannot save the identity file.");
          auto user_id = this->_identity->id();
          elle::io::Path path(elle::os::path::join(
                                common::infinit::user_directory(user_id),
                                user_id + ".idy"));
          this->_identity->store(path);
        }

        std::ofstream identity_infos{common::infinit::identity_path(login_response.id)};

        if (identity_infos.good())
        {
          identity_infos << login_response.identity << "\n"
                         << login_response.email << "\n"
                         << login_response.id << "\n"
          ;
          identity_infos.close();
        }

        auto device = this->meta().device(_device_uuid);
        this->_device.reset(new Device{device.id, device.name});
        std::string passport_path =
          common::infinit::passport_path(this->me().id);
        this->_passport.reset(new papier::Passport());
        if (this->_passport->Restore(device.passport) == elle::Status::Error)
          throw Exception(gap_wrong_passport, "Cannot load the passport");
        this->_passport->store(elle::io::Path(passport_path));

        ELLE_TRACE("%s: connecting to trophonius on %s:%s",
                   *this,
                   login_response.trophonius.host,
                   login_response.trophonius.port_ssl)
        {
          // XXX: Throw an exception instead.
          try
          {
            if (!this->_trophonius->connect(
                  this->me().id, this->device().id, this->_meta.session_id()))
              throw Exception(
                gap_trophonius_unreachable, "unable to connect to trophonius");
          }
          catch (infinit::oracles::trophonius::Unreachable const&)
          {
            throw Exception(gap_trophonius_unreachable,
                            "unable to reach Trophonius");
          }
        }

        this->_avatar_fetcher_thread.reset(
          new reactor::Thread{
            scheduler,
            "avatar fetched",
            [&]
            {
              while (true)
              {
                this->_avatar_fetching_barrier.wait();

                ELLE_ASSERT(!this->_avatar_to_fetch.empty());
                auto user_id = *this->_avatar_to_fetch.begin();
                auto id = this->_user_indexes.at(this->user(user_id).id);
                try
                {
                  this->_avatars.insert(
                    std::make_pair(id,
                                   this->_meta.icon(user_id)));
                  this->_avatar_to_fetch.erase(user_id);
                  this->enqueue(AvatarAvailableNotification(id));
                }
                catch (elle::Exception const& e)
                {
                  ELLE_ERR("%s: unable to fetch %s avatar: %s", *this, user_id,
                           e.what());
                  // The UI will ask for the avatar again if it needs it, so
                  // remove the request from the queue if there's a problem.
                  this->_avatar_to_fetch.erase(user_id);
                }

                if (this->_avatar_to_fetch.empty())
                  this->_avatar_fetching_barrier.close();
              }
            }});
        ELLE_TRACE("%s: fetch self", *this)
          this->user(this->me().id);
        ELLE_TRACE("%s: fetch users", *this)
          this->_users_init();
        ELLE_TRACE("%s: fetch transactions", *this)
          this->_transactions_init();
        this->on_connection_changed(true, true);
        this->_polling_thread.reset(
          new reactor::Thread{
            scheduler,
              "poll",
              [&]
              {
                while (true)
                {
                  try
                  {
                    this->handle_notification(this->_trophonius->poll());
                  }
                  catch (elle::Exception const&)
                  {
                    ELLE_ERR("%s: an error occured in trophonius, login is " \
                             "required: %s", *this, elle::exception_string());
                    // Loging out flush the message queue, which means that
                    // KickedOut will be the next event polled.
                    this->logout();
                    this->enqueue(KickedOut());
                    this->enqueue(ConnectionStatus(false));
                    return;
                  }

                  ELLE_TRACE("%s: notification pulled", *this);
                }
              }});

        finally_logout.abort();
      };
    }

    void
    State::logout()
    {
      reactor::Lock l(this->_login_mutex);
      ELLE_TRACE_SCOPE("%s: logout", *this);

      this->_cleanup();
      // After cleanup, all containers are empty.
      // Do not use lazy accessor during the logout phase.

      ELLE_DEBUG("%s: cleaned up", *this);

      if (!this->logged_in())
      {
        ELLE_DEBUG("%s: state was not logged in", *this);
        return;
      }
      try
      {
        this->_meta.logout();
        this->_metrics_reporter->user_logout(true, "");
      }
      catch (elle::Exception const&)
      {
        ELLE_WARN("logout failed, ignore exception: %s",
                  elle::exception_string());
        this->_meta.logged_in(false);
      }
      ELLE_TRACE("%s: logged out", *this);
    }

    void
    State::_cleanup()
    {
      ELLE_TRACE_SCOPE("%s: cleaning up the state", *this);
      if (this->_metrics_heartbeat_thread != nullptr)
      {
        this->_metrics_heartbeat_thread->terminate_now();
        this->_metrics_heartbeat_thread.reset();
      }
      elle::SafeFinally clean_containers(
        [&]
        {
          ELLE_DEBUG("disconnect trophonius")
            if (this->_trophonius)
              this->_trophonius->disconnect();

          ELLE_DEBUG("remove pending callbacks")
            while (!this->_runners.empty())
              this->_runners.pop();

          ELLE_DEBUG("clear transactions")
            this->_transactions_clear();

          ELLE_DEBUG("clear users")
          {
            this->_user_indexes.clear();
            this->_swagger_indexes.clear();
            this->_users.clear();

            this->_avatar_fetching_barrier.close();
            if (this->_avatar_fetcher_thread != nullptr)
            {
              ELLE_DEBUG("stop avatar_fetching");
              this->_avatar_fetcher_thread->terminate_now();
              this->_avatar_fetcher_thread.reset();
            }
            this->_avatar_to_fetch.clear();
            this->_avatars.clear();
          }

          ELLE_DEBUG("clear device (%s)", this->_device.get())
            this->_device.reset();

          ELLE_DEBUG("clear me (%s)", this->_me.get())
            this->_me.reset();

          ELLE_DEBUG("clear passport (%s)", this->_passport.get())
            this->_passport.reset();

          ELLE_DEBUG("clear identity (%s)", this->_identity.get())
            this->_identity.reset();

          ELLE_TRACE("everything has been cleaned");
        });

      /// First step must be to disconnect from trophonius.
      /// If not, you can pull notification that
      if (this->_polling_thread != nullptr &&
          reactor::Scheduler::scheduler()->current() != this->_polling_thread.get())
      {
        ELLE_DEBUG("stop polling");
        this->_polling_thread->terminate_now();
        this->_polling_thread.reset();
      }
    }

    std::string
    State::hash_password(std::string const& email,
                         std::string const& password)
    {
      // !WARNING! Do not log the password.
      ELLE_TRACE_FUNCTION(email);

      std::string lower_email = email;

      std::transform(lower_email.begin(),
                     lower_email.end(),
                     lower_email.begin(),
                     ::tolower);

      unsigned char hash[SHA256_DIGEST_LENGTH];
      SHA256_CTX context;
      std::string to_hash = lower_email + "MEGABIET" + password + lower_email + "MEGABIET";

      if (SHA256_Init(&context) == 0 ||
          SHA256_Update(&context, to_hash.c_str(), to_hash.size()) == 0 ||
          SHA256_Final(hash, &context) == 0)
        throw Exception(gap_internal_error, "Cannot hash login/password");

      std::ostringstream out;
      elle::serialize::OutputHexadecimalArchive ar(out);

      ar.SaveBinary(hash, SHA256_DIGEST_LENGTH);

      return out.str();
    }

    void
    State::register_(std::string const& fullname,
                     std::string const& email,
                     std::string const& password)
    {
      // !WARNING! Do not log the password.
      ELLE_TRACE_SCOPE("%s: register as %s: email %s",
                       *this, fullname, email);

      std::string lower_email = email;

      std::transform(lower_email.begin(),
                     lower_email.end(),
                     lower_email.begin(),
                     ::tolower);

      // Logout first, and ignore errors.
      try { this->logout(); } catch (std::exception const&) {}

      std::string error_details = "";

      elle::SafeFinally register_failed{[this, lower_email, error_details] {
        infinit::metrics::Reporter::metric_sender_id(lower_email);
        this->_metrics_reporter->user_register(false, error_details);
      }};

      std::string user_id =
        this->meta(false).register_(lower_email, fullname, password);

      infinit::metrics::Reporter::metric_sender_id(user_id);

      register_failed.abort();

      ELLE_DEBUG("registered new user %s <%s>", fullname, lower_email);

      elle::SafeFinally registered_metric{[this, user_id] {
        infinit::metrics::Reporter::metric_sender_id(user_id);
        this->_metrics_reporter->user_register(true, "");
      }};
      this->login(lower_email, password);
    }

    Self const&
    State::me() const
    {
      static reactor::Mutex me_mutex;

      reactor::Lock m(me_mutex);
      if (this->_me == nullptr)
      {
        ELLE_TRACE("loading self info")
          this->_me.reset(new Self{this->meta().self()});
      }
      ELLE_ASSERT_NEQ(this->_me, nullptr);
      return *this->_me;
    }

    void
    State::set_avatar(boost::filesystem::path const& image_path)
    {
      if (!boost::filesystem::exists(image_path))
        throw Exception(gap_error,
                        elle::sprintf("file not found at %s", image_path));

      std::ifstream stream{image_path.string()};
      std::istreambuf_iterator<char> eos;
      std::istreambuf_iterator<char> iit(stream);   // stdin iterator

      elle::Buffer output;
      std::copy(iit, eos, output.begin());

      this->set_avatar(output);
    }

    void
    State::set_avatar(elle::Buffer const& avatar)
    {
      this->meta().icon(this->me().id, avatar);
    }

    std::string
    State::user_directory()
    {
      ELLE_TRACE_METHOD("");

      return common::infinit::user_directory(this->me().id);
    }

    void
    State::output_dir(std::string const& dir)
    {
      if (!fs::exists(dir))
        throw Exception{gap_error,
                        "directory doesn't exist."};

      if (!fs::is_directory(dir))
        throw Exception{gap_error,
                        "not a directroy."};

      this->_output_dir = dir;
    }

    void
    State::on_connection_changed(bool connection_status, bool first_connection)
    {
      ELLE_TRACE_SCOPE(
        "%s: %sconnection %s",
        *this,
        first_connection? "first " : "re",
        connection_status ? "established" : "lost");

      if (connection_status)
      {
        bool resynched{false};
        do
        {
          if (!this->logged_in())
            return;

          try
          {
            // Link with tropho might have changed.
            if (!first_connection)
            {
              ELLE_TRACE("reset transactions")
                for (auto& t: this->_transactions)
                {
                  if (!t.second->final())
                    t.second->reset();
                  else
                    ELLE_DEBUG("ignore finalized transaction %s", t.second);
                }
            }
            this->_user_resync();
            this->_peer_transaction_resync();
            this->_link_transaction_resync();
            resynched = true;
          }
          catch (reactor::Terminate const&)
          {
            throw;
          }
          catch (elle::Exception const&)
          {
            ELLE_WARN("%s: failed at resynching (%s). Retrying...",
                      *this, elle::exception_string());
            reactor::sleep(1_sec);
          }
        }
        while (!resynched);
      }

      this->enqueue<ConnectionStatus>(ConnectionStatus(connection_status));
    }

    void
    State::on_reconnection_failed()
    {
      try
      {
        auto tropho = this->_meta.trophonius();
        this->_trophonius->server(tropho.host, tropho.port_ssl);
      }
      catch (infinit::state::CredentialError const&)
      {
        this->_on_invalid_trophonius_credentials();
      }
      if (this->_meta_server_check())
      {
        ELLE_ERR("%s: able to connect to Meta but not Trophonius", *this);
        this->enqueue(TrophoniusUnavailable());
      }
    }

    void
    State::_on_invalid_trophonius_credentials()
    {
      ELLE_WARN("%s: invalid trophonius credentials", *this);
      // Loging out flush the message queue, which means that
      // KickedOut will be the next event polled.
      this->logout();
      this->enqueue(KickedOut());
      this->enqueue(ConnectionStatus(false));
      return;
    }

    void
    State::handle_notification(
      std::unique_ptr<infinit::oracles::trophonius::Notification>&& notif)
    {
      ELLE_TRACE_SCOPE("%s: new notification %s", *this, *notif);
      // FIXME: ever heard of virtual methods ?
      switch (notif->notification_type)
      {
        case infinit::oracles::trophonius::NotificationType::configuration:
        {
          auto configuration =
            dynamic_cast<infinit::oracles::trophonius::ConfigurationNotification const*>(notif.get());
          ELLE_ASSERT(configuration);
          this->_apply_configuration(std::move(configuration->configuration));
          break;
        }
        case infinit::oracles::trophonius::NotificationType::user_status:
          ELLE_ASSERT(
            dynamic_cast<infinit::oracles::trophonius::UserStatusNotification const*>(
              notif.get()) != nullptr);
          this->_on_swagger_status_update(
            *static_cast<infinit::oracles::trophonius::UserStatusNotification const*>(
              notif.release()));
          break;
        case infinit::oracles::trophonius::NotificationType::link_transaction:
          ELLE_ASSERT(
            dynamic_cast<infinit::oracles::trophonius::LinkTransactionNotification const*>(notif.get()) != nullptr);
          this->_on_transaction_update(
            std::make_shared<infinit::oracles::LinkTransaction>(static_cast<infinit::oracles::trophonius::LinkTransactionNotification&>(*notif)));
          break;
        case infinit::oracles::trophonius::NotificationType::peer_transaction:
          ELLE_ASSERT(
            dynamic_cast<infinit::oracles::trophonius::PeerTransactionNotification const*>(notif.get()) != nullptr);
          this->_on_transaction_update(
            std::make_shared<infinit::oracles::PeerTransaction>(static_cast<infinit::oracles::trophonius::PeerTransactionNotification&>(*notif)));
          break;
        case infinit::oracles::trophonius::NotificationType::new_swagger:
          ELLE_ASSERT(
            dynamic_cast<infinit::oracles::trophonius::NewSwaggerNotification const*>(
              notif.get()) != nullptr);
          this->_on_new_swagger(
            *static_cast<infinit::oracles::trophonius::NewSwaggerNotification const*>(
              notif.release()));
          break;
        case infinit::oracles::trophonius::NotificationType::deleted_swagger:
          ELLE_ASSERT(
            dynamic_cast<infinit::oracles::trophonius::DeletedSwaggerNotification const*>(
              notif.get()) != nullptr);
          this->_on_deleted_swagger(
            *static_cast<infinit::oracles::trophonius::DeletedSwaggerNotification const*>(
              notif.release()));
        break;
        case infinit::oracles::trophonius::NotificationType::deleted_favorite:
          ELLE_ASSERT(
            dynamic_cast<infinit::oracles::trophonius::DeletedFavoriteNotification const*>(
              notif.get()) != nullptr);
          this->_on_deleted_favorite(
            *static_cast<infinit::oracles::trophonius::DeletedFavoriteNotification const*>(
              notif.release()));
        break;
        case infinit::oracles::trophonius::NotificationType::peer_connection_update:
          ELLE_ASSERT(
            dynamic_cast<infinit::oracles::trophonius::PeerReachabilityNotification const*>(
              notif.get()) != nullptr);
          this->_on_peer_reachability_updated(
            *static_cast<infinit::oracles::trophonius::PeerReachabilityNotification const*>(
              notif.release()));
          break;
        case infinit::oracles::trophonius::NotificationType::invalid_credentials:
          this->_on_invalid_trophonius_credentials();
          break;
        case infinit::oracles::trophonius::NotificationType::none:
        case infinit::oracles::trophonius::NotificationType::network_update:
        case infinit::oracles::trophonius::NotificationType::message:
        case infinit::oracles::trophonius::NotificationType::ping:
        case infinit::oracles::trophonius::NotificationType::connection_enabled:
        case infinit::oracles::trophonius::NotificationType::suicide:
          ELLE_ERR("%s: unhandled notification of type %s received",
                   *this, notif->notification_type);
          elle::unreachable();
      }
    };

    /*-------------------.
    | External Callbacks |
    `-------------------*/

    void
    State::poll() const
    {
      ELLE_DUMP("poll");

      if (this->_runners.empty())
        return;

      ELLE_DEBUG("poll %s notification(s)", this->_runners.size());

      // I'm the only consumer.
      while (!this->_runners.empty())
      {
        ELLE_ASSERT(!this->_runners.empty());
        std::unique_ptr<_Runner> runner = nullptr;

        {
          std::lock_guard<std::mutex> lock{this->_poll_lock};
          std::swap(runner, this->_runners.front());
          this->_runners.pop();
        }

        (*runner)();
      }
    }

    /*--------------.
    | Configuration |
    `--------------*/

    void
    State::Configuration::serialize(elle::serialization::Serializer& s)
    {
      s.serialize("s3", this->s3);
      s.serialize("max_mirror_size", this->max_mirror_size);
      s.serialize("max_compress_size", this->max_compress_size);
      s.serialize("disable_upnp", this->disable_upnp);
      s.serialize("features", this->features);
    }

    void
    State::Configuration::S3::serialize(elle::serialization::Serializer& s)
    {
      s.serialize("multipart_upload", this->multipart_upload);
    }

    void
    State::Configuration::S3::MultipartUpload::serialize(
      elle::serialization::Serializer& s)
    {
      s.serialize("chunk_size", this->chunk_size);
      s.serialize("parallelism", this->parallelism);
    }

    void
    State::_apply_configuration(elle::json::Object json)
    {
      ELLE_TRACE_SCOPE("%s: apply configuration: %s", *this, json);
      elle::serialization::json::SerializerIn input(json);
      input.partial(true);
      this->_configuration.serialize(input);
      // Build and pass feature string to metrics
      std::string feature_string;
      for (auto const& f: this->_configuration.features)
        feature_string += f.first + '=' + f.second + ';';
      feature_string = feature_string.substr(0, feature_string.length()-1);
      metrics::Reporter::metric_feature_string(feature_string);
      std::ofstream fconfig(common::infinit::configuration_path());
      elle::json::write(fconfig, json);
    }


    /*----------.
    | Printable |
    `----------*/

    void
    State::print(std::ostream& stream) const
    {
      stream << "state(" << this->meta(false).host()
             << ":" << this->meta(false).port();
      if (!this->meta(false).email().empty())
        stream << " as " << this->meta(false).email();
      stream << ")";
    }

    std::ostream&
    operator <<(std::ostream& out,
                NotificationType const& t)
    {
      switch (t)
      {
        case NotificationType_NewTransaction:
          return out << "NewTransaction";
        case NotificationType_TransactionUpdate:
          return out << "TransactionUpdate";
        case NotificationType_UserStatusUpdate:
          return out << "UserStatusUpdate";
        case NotificationType_NewSwagger:
          return out << "NewSwagger";
        case NotificationType_DeletedSwagger:
          return out << "DeletedSwagger";
        case NotificationType_DeletedFavorite:
          return out << "DeletedFavorite";
        case NotificationType_ConnectionStatus:
          return out << "ConnectionStatus";
        case NotificationType_KickedOut:
          return out << "Kicked Out";
        case NotificationType_AvatarAvailable:
          return out << "Avatar Available";
        case NotificationType_TrophoniusUnavailable:
          return out << "Trophonius Unavailable";
        case NotificationType_LinkUpdate:
          return out << "Link Update";
      }

      return out;
    }
  }
}
