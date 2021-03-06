#include <fstream>
#include <signal.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>

#include <elle/archive/archive.hh>
#include <elle/filesystem/TemporaryDirectory.hh>
#include <elle/json/json.hh>
#include <elle/finally.hh>
#include <elle/format/base64.hh>
#include <elle/format/gzip.hh>
#include <elle/log.hh>
#include <elle/os/environ.hh>
#include <elle/os/path.hh>
#include <elle/system/home_directory.hh>
#include <elle/system/platform.hh>
#ifndef INFINIT_WINDOWS
# include <elle/system/Process.hh>
#endif

#include <reactor/http/Request.hh>
#include <reactor/scheduler.hh>
#include <reactor/exception.hh>

#include <CrashReporter.hh>
#include <version.hh>

ELLE_LOG_COMPONENT("elle.CrashReporter");

namespace elle
{
#ifndef INFINIT_IOS
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
# ifndef INFINIT_WINDOWS
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
# endif // INFINIT_WINDOWS
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
      std::unordered_map<int, Handler> _previous_handler;
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
          ELLE_DEBUG("handling %s synchronously (%s)", strsignal(sig), this->_handler);
          // To avoid handler to be override.
          this->_previous_handler[sig] = ::signal(sig, this->_handler);
          if (this->_previous_handler[sig] == SIG_ERR)
          {
            ELLE_WARN("%s: impossible set handler for %s", *this, strsignal(sig));
          }
          else if (this->_previous_handler[sig] != SIG_DFL)
          {
            ELLE_WARN("%s: handler for %s replaced (%s)",
                      *this, strsignal(sig), this->_previous_handler[sig]);
          }
        }
      }

      virtual
      void
      release() override
      {
        for (int sig: this->_sigs)
        {
          elle::SafeFinally pop{[&] { this->_previous_handler.erase(sig); }};
          ELLE_DEBUG("releasing %s (sync).", strsignal(sig));
          // Restore handler, according to the hypothesis that nobody erased
          if (this->_previous_handler[sig] == SIG_ERR)
          {
            ELLE_WARN("%s: handler for %s has been ignored", *this, strsignal(sig));
            continue;
          }
          auto old_handler = ::signal(sig, this->_previous_handler[sig]);
          if (old_handler != this->_handler)
          {
            ELLE_WARN("%s: our handler for %s was replaced by %s", *this, strsignal(sig), old_handler);
            ::signal(sig, old_handler);
          }
        }
      }
    };

    ScopedGuard::ScopedGuard(reactor::Scheduler& sched,
                             std::vector<int> const& sigs,
                             Handler const& handler)
      : _impl{new AsyncImpl{sched, handler}}
    {
      this->_impl->launch(sigs);
    }

    ScopedGuard::ScopedGuard(std::vector<int> const& sigs,
                             sighandler_t const& handler)
      : _impl{new SyncImpl{handler}}
    {
      this->_impl->launch(sigs);
    }

    ScopedGuard::~ScopedGuard()
    {
      this->_impl->release();
    }
  } // End of signal.
#endif // INFINIT_IOS

  namespace crash
  {
    void
    static
    _send_report(std::string const& url,
                 std::string const& user_name,
                 std::string const& message,
                 std::string file,
                 std::map<std::string, std::string> const& extra_fields
                   = std::map<std::string, std::string>() //old gcc dislikes {}
                 )
    {
      ELLE_TRACE_SCOPE("send report %s to %s", message, url);
      std::vector<boost::any> env_arr;

      ELLE_DEBUG("store environment")
        for (auto const& pair: elle::os::environ())
        {
          if (boost::starts_with(pair.first, "ELLE_") ||
              boost::starts_with(pair.first, "INFINIT_"))
          {
            std::string line =
              elle::sprintf("%s = %s", pair.first, pair.second);
            env_arr.push_back(line);
          }
        }
      // Create JSON.
      elle::json::Object json_dict;
      for (auto const& f: extra_fields)
        json_dict[f.first] = f.second;
      {
        json_dict["user_name"] = user_name;
        json_dict["client_os"] = elle::system::platform::os_description();
        if (!message.empty())
          json_dict["message"] = message;
        json_dict["env"] = env_arr;
        json_dict["version"] = std::string(INFINIT_VERSION);
        std::string crash_dest = elle::os::getenv("INFINIT_CRASH_DEST", "");
        if (!crash_dest.empty())
          json_dict["email"] = crash_dest;
        json_dict["file"] = file;
# ifdef INFINIT_PRODUCTION_BUILD
        json_dict["send"] = true;
# else
        json_dict["send"] = !crash_dest.empty();
        if (crash_dest.empty())
        {
          ELLE_WARN("CrashReporter not sending report: INFINIT_PRODUCTION_BUILD "
                    "not defined and INFINIT_CRASH_DEST environment variable "
                    "empty");
        }
# endif
      }
      reactor::http::Request::Configuration conf{
        reactor::DurationOpt(300_sec),
        {},
        reactor::http::Version(reactor::http::Version::v10)};
      conf.ssl_verify_host(false);

      ELLE_ASSERT(reactor::Scheduler::scheduler() != nullptr);
      try
      {
        reactor::http::Request request(
          url,
          reactor::http::Method::POST,
          "application/json",
          conf);
        elle::json::write(request, json_dict);
        request.wait();
        if (request.status() != reactor::http::StatusCode::OK)
        {
          ELLE_ERR("error while posting report to %s: (%s) %s",
                   url, request.status(), request.response().string());
        }
        ELLE_TRACE("Report succesfully sent.");
      }
      catch (reactor::Terminate const&)
      {
        ELLE_WARN("Crashreporter interrupted while sending!");
        throw;
      }
      catch (...)
      {
        ELLE_ERR("unable to post report to %s", url);
      }
    }

    static
    std::string
    _to_base64(boost::filesystem::path const& source)
    {
      ELLE_TRACE_SCOPE("turn %s to base 64", source.string());
      std::stringstream base64;
      // Scope to flush Stream(s).
      {
        elle::format::base64::Stream encoder(base64);
        boost::filesystem::ifstream source_stream(source, std::ios::binary);
        std::copy(std::istreambuf_iterator<char>(source_stream),
                  std::istreambuf_iterator<char>(),
                  std::ostreambuf_iterator<char>(encoder));
      }
      return base64.str();
    }

    static
    bool
    temp_file_excluder(boost::filesystem::path const& p)
    {
      return std::find(p.begin(), p.end(), "mirror_files") != p.end()
        || std::find(p.begin(), p.end(), "archive") != p.end();
    }

    void
    existing_report(std::string const& meta_protocol,
                    std::string const& meta_host,
                    uint16_t meta_port,
                    std::vector<std::string> const& files,
                    std::string const& user_name,
                    std::string const& info)
    {
      ELLE_TRACE("report last crash");
      std::string url = elle::sprintf("%s://%s:%d/debug/report/backtrace",
                                      meta_protocol,
                                      meta_host,
                                      meta_port);
      elle::filesystem::TemporaryDirectory tmp;
      boost::filesystem::path destination(tmp.path() / "report.tar.bz2");
      std::vector<boost::filesystem::path> archived;
      for (auto path_str: files)
      {
        boost::filesystem::path path(path_str);
        if (boost::filesystem::exists(path))
        {
          ELLE_DEBUG("archiving %s", path);
          archived.push_back(path);
        }
        else
          ELLE_DEBUG("ignoring %s", path);
      }
      elle::archive::archive(elle::archive::Format::tar_gzip,
                             archived,
                             destination,
                             elle::archive::Renamer(),
                             temp_file_excluder,
                             true);
      _send_report(url, user_name, "", _to_base64(destination));
    }

    void
    transfer_failed_report(common::infinit::Configuration const& config,
                           std::string const& user_name,
                           std::string const& transaction_id,
                           std::string const& reason)
    {
      ELLE_TRACE_SCOPE("sending transaction failed report");
      std::string url = elle::sprintf("%s://%s:%s/debug/report/transaction",
                                      config.meta_protocol(),
                                      config.meta_host(),
                                      config.meta_port());
      elle::filesystem::TemporaryDirectory tmp;
      boost::filesystem::path destination(tmp.path() / "report.tar.bz2");
      std::vector<boost::filesystem::path> files;
      files.push_back(config.non_persistent_config_dir());
#ifdef __ANDROID__
      files.push_back(elle::os::getenv("INFINIT_LOG_FILE"));
#endif
      elle::archive::archive(elle::archive::Format::tar_gzip,
                             files,
                             destination,
                             elle::archive::Renamer(),
                             temp_file_excluder,
                             true);
      _send_report(url, user_name, reason, _to_base64(destination),
                  {{"transaction_id", transaction_id}});
    }

    void
    user_report(common::infinit::Configuration const& config,
                std::vector<std::string> const& attachments_,
                std::string const& user_name,
                std::string const& message)
    {
      ELLE_TRACE_SCOPE("sending user report");
      std::string url = elle::sprintf("%s://%s:%s/debug/report/user",
                                      config.meta_protocol(),
                                      config.meta_host(),
                                      config.meta_port());
      elle::filesystem::TemporaryDirectory tmp;
      boost::filesystem::path destination(tmp.path() / "report.tar.bz2");
      std::vector<boost::filesystem::path> attachments;
#ifdef INFINIT_WINDOWS
      // On windows, libarchive behaves differently regarding of sharing a fd.
      // We need to create a copy of the log before archiving it.
      boost::system::error_code erc;
      boost::filesystem::path log(elle::os::getenv("INFINIT_LOG_FILE"));
      boost::filesystem::path copied_log = log;
      copied_log.replace_extension("copied.txt");
      boost::filesystem::copy(log, copied_log , erc);
      if (erc)
      {
        ELLE_WARN("error while copying log(%s): %s", log, copied_log);
      }
      elle::SafeFinally cleanup{
        [&] {
          boost::system::error_code erc;
          boost::filesystem::remove(copied_log, erc);
          if (erc)
            ELLE_WARN("removing copied file %s failed: %s", copied_log, erc);
        }};
      attachments.push_back(copied_log);
#endif
      boost::filesystem::path logs_dir(config.non_persistent_config_dir());
      attachments.push_back(logs_dir);
      for (auto const& attachment: attachments_)
      {
        boost::filesystem::path path(attachment);
        attachments.push_back(path);
      }
      elle::archive::archive(elle::archive::Format::tar_gzip,
                             attachments,
                             destination,
                             elle::archive::Renamer(),
                             temp_file_excluder,
                             true);
      _send_report(url, user_name, message, _to_base64(destination));
    }
  }
}
