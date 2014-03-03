#include <sys/types.h>
#include <unistd.h>

#include <stdexcept>
#include <unordered_map>

#include <boost/preprocessor/cat.hpp>

#include <elle/assert.hh>
#include <elle/os/environ.hh>
#include <elle/os/path.hh>
#include <elle/print.hh>
#include <elle/system/home_directory.hh>
#include <elle/system/platform.hh>

#include <common/common.hh>
#include <version.hh>

#define COMMON_DEFAULT_API                          \
  "development.infinit.io"
#define COMMON_DEFAULT_INFINIT_HOME ".infinit"
#define COMMON_DEFAULT_META_PROTOCOL "https"
#define COMMON_DEFAULT_META_HOST COMMON_DEFAULT_API
#define COMMON_DEFAULT_META_PORT 443
#define COMMON_DEFAULT_TROPHONIUS_PROTOCOL "https"
#define COMMON_DEFAULT_TROPHONIUS_HOST COMMON_DEFAULT_API
#define COMMON_DEFAULT_TROPHONIUS_PORT 444
#define COMMON_DEFAULT_RESOURCES_ROOT_URL "http://download.development.infinit.io"

#define COMMON_PRODUCTION_API                       \
  BOOST_PP_STRINGIZE(INFINIT_VERSION_MINOR) "."     \
  BOOST_PP_STRINGIZE(INFINIT_VERSION_MAJOR)         \
  ".api.production.infinit.io"
#define COMMON_PRODUCTION_INFINIT_HOME ".infinit"
#define COMMON_PRODUCTION_META_PROTOCOL "https"
#define COMMON_PRODUCTION_META_HOST "meta." COMMON_PRODUCTION_API
#define COMMON_PRODUCTION_META_PORT 443
#define COMMON_PRODUCTION_TROPHONIUS_PROTOCOL "https"
#define COMMON_PRODUCTION_TROPHONIUS_HOST "trophonius." COMMON_PRODUCTION_API
#define COMMON_PRODUCTION_TROPHONIUS_PORT 443
#define COMMON_PRODUCTION_RESOURCES_ROOT_URL "http://download.production.infinit.io"

#ifdef INFINIT_PRODUCTION_BUILD
# define VAR_PREFIX COMMON_PRODUCTION
#else
# define VAR_PREFIX COMMON_DEFAULT
#endif

# define COMMON_INFINIT_HOME \
  BOOST_PP_CAT(VAR_PREFIX, _INFINIT_HOME) \
/**/
# define COMMON_META_PROTOCOL \
  BOOST_PP_CAT(VAR_PREFIX, _META_PROTOCOL) \
/**/
# define COMMON_META_HOST \
  BOOST_PP_CAT(VAR_PREFIX, _META_HOST) \
/**/
# define COMMON_META_PORT \
  BOOST_PP_CAT(VAR_PREFIX, _META_PORT) \
/**/
# define COMMON_TROPHONIUS_PROTOCOL \
  BOOST_PP_CAT(VAR_PREFIX, _TROPHONIUS_PROTOCOL) \
/**/
# define COMMON_TROPHONIUS_HOST \
  BOOST_PP_CAT(VAR_PREFIX, _TROPHONIUS_HOST) \
/**/
# define COMMON_TROPHONIUS_PORT \
  BOOST_PP_CAT(VAR_PREFIX, _TROPHONIUS_PORT) \
/**/
# define COMMON_RESOURCES_ROOT_URL \
  BOOST_PP_CAT(VAR_PREFIX, _RESOURCES_ROOT_URL) \
/**/

namespace path = elle::os::path;

namespace
{

  std::string
  _home_directory()
  {
    return elle::system::home_directory().string();
  }

  std::string
  _download_directory()
  {
    std::string download_dir = elle::os::getenv("INFINIT_DOWNLOAD_DIR", "");
    if (download_dir.length() > 0 && path::exists(download_dir) && path::is_directory(download_dir))
      return download_dir;

    std::string home_dir = _home_directory();
    std::string probable_download_dir = path::join(home_dir, "/Downloads");

    if (path::exists(probable_download_dir) && path::is_directory(probable_download_dir))
      return probable_download_dir;

    return home_dir;
  }

  std::string
  _infinit_home()
  {
    return elle::os::getenv(
        "INFINIT_HOME",
        common::system::home_directory() + "/" + COMMON_INFINIT_HOME
    );
  }

  std::string
  _built_binary_relative_path(std::string const& name)
  {
    static std::unordered_map<std::string, std::string> paths{
      {"8access",     "bin/8access"},
      {"8group",      "bin/8group"},
      {"8infinit",    "bin/8infinit"},
      {"8watchdog",   "bin/8watchdog"},
      {"8transfer",   "bin/8transfer"},
      {"8progress",   "bin/8progress"},
      {"gdbmacro.py", "bin/gdbmacro.py"},
      {"heartbeat-server", "bin/heartbeat-server"},
    };
    auto it = paths.find(name);
    if (it == paths.end())
      throw std::runtime_error("Built binary '" + name + "' not registered");
    return it->second;
  }

  uint16_t
  _meta_port()
  {
    return std::stoi(
      elle::os::getenv(
        "INFINIT_META_PORT",
        std::to_string(COMMON_META_PORT)
      )
    );
  }

  uint16_t
  _trophonius_port()
  {
    std::string port_string = elle::os::getenv(
        "INFINIT_TROPHONIUS_PORT",
        elle::sprint(COMMON_TROPHONIUS_PORT)
    );
    std::stringstream ss(port_string);
    uint16_t port;
    ss >> port;
    if (ss.fail())
      throw std::runtime_error{
          "Couldn't retreive the port from '" + port_string + "'"
      };
    return port;
  }
}

namespace common
{

  namespace infinit
  {

    std::string const&
    home()
    {
      static std::string infinit_dir = _infinit_home();
      return infinit_dir;
    }

    std::string const&
    binary_path(std::string const& name, bool ensure)
    {
      static std::unordered_map<std::string, std::string> binaries;

      auto it = binaries.find(name);
      if (it != binaries.end())
        return it->second;

      static std::string build_dir = elle::os::getenv("INFINIT_BUILD_DIR", "");
      static std::string bin_dir = elle::os::getenv("INFINIT_BINARY_DIR", "");
      std::string path;
      if (bin_dir.size())
          path = elle::os::path::join(bin_dir, name);
      else if (build_dir.size())
          path = elle::os::path::join(build_dir,
                                      _built_binary_relative_path(name));
      else
        throw std::runtime_error{
          "Neither INFINIT_BUILD_DIR nor INFINIT_BINARY_DIR has been set"
        };

      if (ensure && !elle::os::path::exists(path))
        throw std::runtime_error("Cannot find any binary at '" + path + "'");

      return (binaries[name] = path);
    }

    std::string
    networks_directory(std::string const& user_id)
    {
      return path::join(user_directory(user_id), "networks");
    }

    std::string
    descriptor_path(std::string const& user_id,
                    std::string const& network_id)
    {
      return path::join(
        network_directory(user_id, network_id),
        network_id + ".dsc");
    }

    std::string
    network_directory(std::string const& user_id,
                      std::string const& network_id)
    {
      return path::join(
        networks_directory(user_id),
        network_id
      );
    }

    std::string
    network_shelter(std::string const& user_id,
                    std::string const& network_id)
    {
      return path::join(
        network_directory(user_id, network_id), "shelter");
    }

    std::string
    passport_path(std::string const& user)
    {
      return path::join(infinit::user_directory(user), user + ".ppt");
    }

    std::string
    log_path(std::string const& user_id,
             std::string const& network_id)
    {
      return path::join(
        networks_directory(user_id), network_id + ".log");
    }

    /// Returns the path of the file containing the computer device uuid.
    std::string
    device_id_path()
    {
      return path::join(home(), "device.uuid");
    }

    std::string
    portal_path(std::string const& user_id,
                std::string const& network_id)
    {
      return path::join(network_directory(user_id, network_id), "portal.phr");
    }

    std::string
    user_directory(std::string const& user_id)
    {
      return path::join(home(), "users", user_id);
    }

    std::string
    transactions_directory(std::string const& user_id)
    {
      return path::join(user_directory(user_id), "transaction");
    }

    std::string
    transaction_snapshots_directory(std::string const& user_id)
    {
      return path::join(transactions_directory(user_id), ".snapshot");
    }

    std::string
    frete_snapshot_path(std::string const& user_id,
                        std::string const& transaction_id)
    {
      return path::join(transactions_directory(user_id), transaction_id + ".frete");
    }

    std::string
    identity_path(std::string const& user_id)
    {
      return path::join(
        infinit::user_directory(user_id),
        "identity"
      );
    }

  } // !infinit


  namespace system
  {

    std::string const&
    home_directory()
    {
      static std::string home_dir = _home_directory();
      return home_dir;
    }

    std::string const&
    platform()
    {
      static std::string const platform_ =
#ifdef INFINIT_LINUX
        "linux";
#elif INFINIT_MACOSX
        "macosx";
#elif INFINIT_WINDOWS
        "windows";
#else
# error "this platform is not supported"
#endif
        return platform_;
    }

    unsigned int
    architecture()
    {
      return sizeof(void*) * 8;
    }

    std::string const&
    download_directory()
    {
      static std::string download_dir = _download_directory();
      return download_dir;
    }

  } //!system

  namespace meta
  {

    std::string const&
    protocol()
    {
      static std::string const protocol = elle::os::getenv(
          "INFINIT_META_PROTOCOL",
          COMMON_META_PROTOCOL
      );
      return protocol;
    }

    uint16_t
    port()
    {
      static uint16_t const port = _meta_port();
      return port;
    }

    std::string const&
    host()
    {
      static std::string const host = elle::os::getenv(
          "INFINIT_META_HOST",
          COMMON_META_HOST
      );
      return host;
    }

    std::string const&
    url()
    {
      static std::string const url = elle::os::getenv(
          "INFINIT_META_URL",
          protocol() + "://" + host()
            + ":" + elle::sprint(port())
      );
      return url;
    }

  } // !meta

  namespace trophonius
  {

    std::string const&
    protocol()
    {
      static std::string const protocol = elle::os::getenv(
          "INFINIT_TROPHONIUS_PROTOCOL",
          COMMON_TROPHONIUS_PROTOCOL
      );
      return protocol;
    }

    uint16_t
    port()
    {
      static uint16_t const port = _trophonius_port();
      return port;
    }

    std::string const&
    host()
    {
      static std::string const host = elle::os::getenv(
          "INFINIT_TROPHONIUS_HOST",
          COMMON_TROPHONIUS_HOST
      );
      return host;
    }

    std::string const&
    url()
    {
      static std::string const url = elle::os::getenv(
          "INFINIT_TROPHONIUS_URL",
          protocol() + "://" + host()
            + ":" + elle::sprint(port())
      );
      return url;
    }
  } // !trophonius

  namespace resources
  {

    std::string
    base_url(char const* platform_,
             unsigned int architecture_)
    {
      static std::string const base_url = elle::os::getenv(
          "INFINIT_RESOURCES_ROOT_URL",
          COMMON_RESOURCES_ROOT_URL
      );
      std::string platform = (
          platform_ != nullptr ?
          std::string(platform_) :
          ::common::system::platform()
      );
      std::string architecture = elle::sprint(
          architecture_ != 0 ?
          architecture_ :
          ::common::system::architecture()
      );
      return base_url + "/" + platform + architecture;
    }
  }
}
