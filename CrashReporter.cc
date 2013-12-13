#include <fstream>
#include <map>
#include <signal.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/signals.hpp>
#include <boost/system/error_code.hpp>

#include <elle/format/json.hh>
#include <elle/log.hh>
#include <elle/os/environ.hh>
#include <elle/os/getenv.hh>
#include <elle/os/path.hh>
#include <elle/system/platform.hh>

#include <reactor/http/Request.hh>
#include <reactor/scheduler.hh>

#include <CrashReporter.hh>
#include <version.hh>

ELLE_LOG_COMPONENT("elle.CrashReporter");

namespace elle
{
  namespace signal
  {
    namespace
    {
      // It seems that strsignal from signal.h isn't portable on every os.
      // This map contains the main posix signals and make can easy be multi-
      // platform by using defines for specific os.
      std::string const&
      strsignal(int signal)
      {
        static const std::unordered_map<int, std::string> bind{
          {
            {SIGINT,  "SIGINT"},  // Interrupt from keyboard
            {SIGILL,  "SIGILL"},  // Illegal Instruction
            {SIGABRT, "SIGABRT"}, // Abort signal from abort(3)
            {SIGFPE,  "SIGFPE"},  // Floating point exception
            {SIGSEGV, "SIGSEGV"}, // Invalid memory reference
            {SIGTERM, "SIGTERM"}, // Termination signal
#ifndef INFINIT_WINDOWS
            {SIGKILL, "SIGKILL"}, // Kill signal
            {SIGHUP,  "SIGHUP"},  // Hangup detected on controlling terminal or death of controlling process
            {SIGQUIT, "SIGQUIT"}, // Quit from keyboard
            {SIGILL,  "SIGILL"},  // Illegal Instruction
            {SIGPIPE, "SIGPIPE"}, // Broken pipe: write to pipe with no readers
            {SIGALRM, "SIGALRM"}, // Timer signal from alarm(2)
            {SIGCHLD, "SIGCHLD"}, // Child stopped or terminated
            {SIGCONT, "SIGCONT"}, // Continue if stopped
            {SIGSTOP, "SIGSTOP"}, // Stop process
            {SIGTSTP, "SIGTSTP"}, // Stop typed at tty
            {SIGTTIN, "SIGTTIN"}, // tty input for background process
            {SIGTTOU, "SIGTTOU"}, // tty output for background process
#endif
          }
        };

        return bind.at(signal);
      }
    } // End of anonymous namespace.

    class ScopedGuard::Impl
    {
      /*---------------.
      | Initialization |
      `---------------*/
    public:
      virtual ~Impl()
      {}

      virtual
      void
      launch(std::vector<int> const& sigs) = 0;

      /*------------.
      | Destruction |
      `------------*/
      virtual
      void
      release() = 0;
    };

    //- Async impl -------------------------------------------------------------
    static
    void
    _wrap(boost::system::error_code const& error,
          int sig,
          ScopedGuard::Handler const& handler)
    {
      if (!error)
      {
        ELLE_WARN("signal caught: %s.", elle::signal::strsignal(sig));
        handler(sig);
      }
      else if (error != boost::system::errc::operation_canceled)
      {
        ELLE_WARN("Error: %d - Sig: %d", error, sig);
      }
    }

    // XXX: Should be templated on Handler maybe.
    class AsyncImpl:
      public ScopedGuard::Impl
    {
      typedef ScopedGuard::Handler Handler;

      Handler _handler;
      boost::asio::signal_set _signals;

    public:
      AsyncImpl(reactor::Scheduler& sched,
                Handler const& handler):
        _handler{handler},
        _signals{sched.io_service()}
      {}

    public:
      virtual
      void
      launch(std::vector<int> const& sigs) override
      {
        ELLE_TRACE("launching guard");
        for (int sig: sigs)
        {
          ELLE_DEBUG("handling %s (%s) asynchronously.", strsignal(sig), sig);
          this->_signals.add(sig);
        }

        ELLE_DEBUG("now waiting for signals...");
        this->_signals.async_wait(
          [&](boost::system::error_code const& error,
              int sig)
          {
            if (error != boost::system::errc::operation_canceled)
            {
              this->release();
              _wrap(error, sig, this->_handler);
            }
          });
      }

      virtual
      void
      release() override
      {
        ELLE_TRACE("releasing guard");
        //XXX We should check errors.
        this->_signals.cancel();
      }
    };

    class SyncImpl:
      public ScopedGuard::Impl
    {
      typedef sighandler_t Handler;

      Handler _handler;
      std::vector<int> _sigs;
    public:
      SyncImpl(Handler const& handler):
        _handler{handler}
      {}

    public:
      virtual
      void
      launch(std::vector<int> const& sigs) override
      {
        this->_sigs = sigs;

        for (int sig: this->_sigs)
        {
          ELLE_DEBUG("handling %s synchronously", strsignal(sig));
          // To avoid handler to be override.
          ELLE_ASSERT(::signal(sig, this->_handler) == SIG_DFL);
        }
      }

      virtual
      void
      release() override
      {
        for (int sig: this->_sigs)
        {
          ELLE_DEBUG("releasing %s (sync).", strsignal(sig));
          // Restore handler, according to the hypothesis that nobody erased it.
          ELLE_ASSERT(::signal(sig, SIG_DFL) == this->_handler);
        }
      }
    };

    ScopedGuard::ScopedGuard(reactor::Scheduler& sched,
                             std::vector<int> const& sigs,
                             Handler const& handler):
      _impl{new AsyncImpl{sched, handler}}
    {
      this->_impl->launch(sigs);
    }

    ScopedGuard::ScopedGuard(std::vector<int> const& sigs,
                             sighandler_t const& handler):
      _impl{new SyncImpl{handler}}
    {
      this->_impl->launch(sigs);
    }

    ScopedGuard::~ScopedGuard()
    {
      this->_impl->release();
    }
  } // End of signal.

  namespace crash
  {

    Handler::Handler(std::string const& host,
                     int port,
                     std::string const& name,
                     bool quit):
                     _host(host),
                     _port(port),
                     _name{name},
                     _quit{quit}
    {}

    Handler::Handler(std::string const& host,
                     int port,
                     std::string const& name,
                     bool quit,
                     int argc,
                     char** argv):
      Handler(host, port, name, quit)
    {
      for (int i = 1; i < argc; ++i)
      {
        if (argv[i] == NULL)
          break;

        this->_name += " ";
        this->_name += argv[i];
      }
    }

    Handler::~Handler()
    {}

    void
    Handler::operator() (int sig)
    {
      if (this->_quit)
        ::exit(sig);
    }

    void
    user_report(std::string const& host,
                uint16_t port,
                std::string const& user_name,
                std::string const& os_description,
                std::string const& message,
                std::string const& file)
    {
      ELLE_TRACE("user report");

      reactor::Scheduler sched;
      reactor::Thread thread(sched, "user report upload", [&]
        {

          elle::format::json::Array env_arr{};

          for (auto const& pair: elle::os::environ())
          if (boost::starts_with(pair.first, "ELLE_") ||
              boost::starts_with(pair.first, "INFINIT_"))
          {
            env_arr.push_back(pair.first + " = " + pair.second);
          }

          elle::format::json::Dictionary json_dict;

          json_dict["user_name"] = user_name;
          json_dict["client_os"] = os_description;
          json_dict["message"] = message;
          json_dict["env"] = env_arr;
          json_dict["version"] = INFINIT_VERSION;
          json_dict["email"] = elle::os::getenv("INFINIT_CRASH_DEST", "");
          json_dict["file"] = file;
#ifdef INFINIT_PRODUCTION_BUILD
          json_dict["send"] = true;
#else
          json_dict["send"] = !json_dict["email"].as<std::string>().empty();
#endif

          std::string url = elle::sprintf("http://%s:%s/debug/report/user",
                                          host,
                                          port);
          reactor::http::Request::Configuration conf{
            reactor::DurationOpt(300_sec),
            reactor::http::Version(reactor::http::Version::v10)};

          try
          {
            reactor::http::Request request(
              url,
              reactor::http::Method::POST,
              "application/json",
              conf);
            json_dict.repr(request);
            request.wait();
            if (request.status() != reactor::http::StatusCode::OK)
            {
              ELLE_ERR("error while posting report to %s: (%s) %s",
                       url, request.status(), request.response().string());
            }
          }
          catch (...)
          {
            ELLE_ERR("unable to post report to %s", url);
          }
        });

    sched.run();
    }

    void
    existing_report(std::string const& host,
                    uint16_t port,
                    std::string const& user_name,
                    std::string const& os_description,
                    std::string const& info,
                    std::string const& file)
    {
      ELLE_TRACE("report last crash");
      reactor::Scheduler sched;
      reactor::Thread thread(sched, "user report upload", [&]
        {

          elle::format::json::Array env_arr{};

          for (auto const& pair: elle::os::environ())
            if (boost::starts_with(pair.first, "ELLE_") ||
              boost::starts_with(pair.first, "INFINIT_"))
            {
              env_arr.push_back(pair.first + " = " + pair.second);
            }

          elle::format::json::Dictionary json_dict;

          json_dict["user_name"] = user_name;
          json_dict["client_os"] = os_description;
          json_dict["more"] = info;
          json_dict["env"] = env_arr;
          json_dict["version"] = INFINIT_VERSION;
          json_dict["email"] = elle::os::getenv("INFINIT_CRASH_DEST", "");
          json_dict["file"] = file;
          #ifdef INFINIT_PRODUCTION_BUILD
          json_dict["send"] = true;
          #else
          json_dict["send"] = !json_dict["email"].as<std::string>().empty();
          #endif
          std::string url = elle::sprintf("http://%s:%d/debug/report/backtrace",
                                          host,
                                          port);
          reactor::http::Request::Configuration conf{
            reactor::DurationOpt(300_sec),
            reactor::http::Version(reactor::http::Version::v10)};
          try
          {
            reactor::http::Request request(
            url,
            reactor::http::Method::POST,
            "application/json",
            conf);
            json_dict.repr(request);
            request.wait();
            if (request.status() != reactor::http::StatusCode::OK)
            {
              ELLE_ERR("error while posting report to %s: (%s) %s",
              url, request.status(), request.response().string());
            }
          }
          catch (...)
          {
            ELLE_ERR("unable to post report to %s", url);
          }
        });
      sched.run();
    }
  } // End of crash.
} // End of elle.
