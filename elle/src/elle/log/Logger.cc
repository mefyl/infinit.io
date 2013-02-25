#include <fnmatch.h>

#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <elle/log/Logger.hh>
#include <elle/printf.hh>

#include <reactor/scheduler.hh>

namespace elle
{
  namespace log
  {
    /*------------.
    | Indentation |
    `------------*/

    class PlainIndentation: public Indentation
    {
    public:
      PlainIndentation()
        : _indentation(0)
      {}

      virtual
      unsigned int
      indentation()
      {
        return this->_indentation;
      }

      virtual
      void
      indent()
      {
        this->_indentation += 1;
      }

      virtual
      void
      unindent()
      {
        assert(_indentation >= 1);
        this->_indentation -= 1;
      }

    private:
      unsigned int _indentation;
    };
    std::function<std::unique_ptr<Indentation> ()>&
    Logger::_factory()
    {
      static std::function<std::unique_ptr<Indentation> ()> res =
        [] () { return std::unique_ptr<Indentation>(new PlainIndentation()); };
      return res;
    }

    unsigned int
    Logger::indentation()
    {
      boost::lock_guard<boost::mutex> lock(_indentation_mutex);
      return this->_indentation->indentation();
    }

    void
    Logger::indent()
    {
      boost::lock_guard<boost::mutex> lock(_indentation_mutex);
      this->_indentation->indent();
    }

    void
    Logger::unindent()
    {
      boost::lock_guard<boost::mutex> lock(_indentation_mutex);
      this->_indentation->unindent();
    }

    /*-------------.
    | Construction |
    `-------------*/

    Logger::Logger()
      : _indentation(this->_factory()())
      , _component_patterns()
      , _component_enabled()
      , _component_max_size(0)
    {
      static char const* components_str = ::getenv("ELLE_LOG_COMPONENTS");
      if (components_str == nullptr)
        this->_component_patterns.push_back("*");
      else
        {
          boost::algorithm::split(this->_component_patterns, components_str,
                                  boost::algorithm::is_any_of(","),
                                  boost::algorithm::token_compress_on);
          for (auto& pattern: this->_component_patterns)
            boost::algorithm::trim(pattern);
        }
    }

    Logger::~Logger()
    {}

    /*----------.
    | Messaging |
    `----------*/

    void
    Logger::message(Level level,
                    elle::log::Logger::Type type,
                    std::string const& component,
                    std::string const& msg)
    {
      int indent = this->indentation();
      assert(indent >= 1);

      // Component
      std::string comp;
      {
        unsigned int size = component.size();
        assert(size <= this->component_max_size());
        unsigned int pad = this->component_max_size() - size;
        comp = std::string(pad / 2, ' ')
          + component + std::string(pad / 2 + pad % 2, ' ');
      }

      // Time
      boost::posix_time::ptime ptime;
      {
        static const bool universal =
          ::getenv("ELLE_LOG_TIME_UNIVERSAL") != nullptr;
        if (universal)
          ptime = boost::posix_time::second_clock::universal_time();
        else
          ptime = boost::posix_time::second_clock::local_time();
      }

      // Thread
      std::string thread = " ";
      {
        if (reactor::Scheduler* sched = reactor::Scheduler::scheduler())
          if (reactor::Thread* t = sched->current())
            thread = t->name();
      }

      // PID
      std::string pid;
      {
        pid = boost::lexical_cast<std::string>(getpid());
      }

      // Message
      std::string message;
      {
        std::string align = std::string((indent - 1) * 2, ' ');
        message = align + msg;
      }

      // Format
      static const bool enable_time = ::getenv("ELLE_LOG_TIME") != nullptr;
      static const bool enable_pid = ::getenv("ELLE_LOG_PID") != nullptr;
      static boost::format model(std::string(enable_time ? "%s: " : "") +
                                 std::string(enable_pid ? "[%s] " : "") +
                                 "[%s] [%s] %s");
      boost::format fmt(model);
      if (enable_time)
        fmt % ptime;
      if (enable_pid)
        fmt % pid;
      fmt % comp % thread % message;
      this->_message(level, type, str(fmt));
    }

    /*--------.
    | Enabled |
    `--------*/

    bool
    Logger::component_enabled(std::string const& name)
    {
      auto elt = this->_component_enabled.find(name);
      if (elt == this->_component_enabled.end())
        {
          bool res = false;
          for (auto const& pattern: this->_component_patterns)
            {
              if (fnmatch(pattern.c_str(), name.c_str(), 0) == 0)
                {
                  this->_component_max_size =
                    std::max(this->_component_max_size,
                             static_cast<unsigned int>(name.size()));
                  res = true;
                  break;
                }
            }
          this->_component_enabled[name] = res;
          return res;
        }
      else
        return elt->second;
    }
  }
}
