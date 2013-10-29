#ifndef INFINIT_ORACLES_TROPHONIUS_SERVER_TROPHONIUS_HH
# define INFINIT_ORACLES_TROPHONIUS_SERVER_TROPHONIUS_HH

# include <unordered_set>

# include <boost/date_time/posix_time/posix_time.hpp>

# include <elle/attribute.hh>

# include <reactor/network/tcp-server.hh>
# include <reactor/thread.hh>
# include <reactor/waitable.hh>

# include <infinit/oracles/trophonius/server/fwd.hh>
# include <infinit/oracles/meta/Admin.hh>

# include <boost/uuid/uuid.hpp>

namespace infinit
{
  namespace oracles
  {
    namespace trophonius
    {
      namespace server
      {
        class UnknownClient:
          public elle::Exception
        {
        public:
          UnknownClient(boost::uuids::uuid const& device_id);
        };

        class Trophonius:
          public reactor::Waitable
        {
        public:
          Trophonius(int port,
                     std::string const& meta_host,
                     int meta_port,
                     boost::posix_time::time_duration const& ping_period);
          ~Trophonius();
          void
          stop();

        /*-------.
        | Server |
        `-------*/
        private:
          void
          _serve();

          void
          _serve_notifier();

          ELLE_ATTRIBUTE(reactor::network::TCPServer, server);
          ELLE_ATTRIBUTE_r(int, port);
          ELLE_ATTRIBUTE(reactor::network::TCPServer, notifications);
          int
          notification_port() const;
          ELLE_ATTRIBUTE(reactor::Thread, accepter);
          ELLE_ATTRIBUTE(reactor::Thread, meta_accepter);
          ELLE_ATTRIBUTE_R(boost::uuids::uuid, uuid);
          ELLE_ATTRIBUTE_R(meta::Admin, meta);

        /*--------.
        | Clients |
        `--------*/
        public:
          void
          client_remove(Client& c);
          ELLE_ATTRIBUTE(std::unordered_set<Client*>, clients);
          ELLE_ATTRIBUTE_R(boost::posix_time::time_duration, ping_period);

          User&
          user(boost::uuids::uuid const& device);

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
  }
}

#endif
