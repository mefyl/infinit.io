#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid.hpp>

#include <elle/test.hh>
#include <elle/log.hh>
#include <elle/Exception.hh>
#include <elle/json/exceptions.hh>
#include <elle/serialization/json.hh>
#include <elle/serialization/json/MissingKey.hh>

#include <reactor/scheduler.hh>
#include <reactor/http/exceptions.hh>

#include <infinit/oracles/meta/Admin.hh>
#include <infinit/oracles/meta/Client.hh>
#include <surface/gap/Exception.hh>
#include <surface/gap/Error.hh>
#include <version.hh>

#include <http_server.hh>

ELLE_LOG_COMPONENT("infinit.plasma.meta.Client.tests");

class HTTPServer
  : public reactor::http::tests::Server
{
public:
  template <typename ... Args>
  HTTPServer(Args&& ... args)
    : reactor::http::tests::Server(std::forward<Args>(args)...)
  {
    this->headers()["X-Fist-Meta-Version"] = INFINIT_VERSION;
  }
};

class Client
  : public infinit::oracles::meta::Client
{
public:
  template <typename ... Args>
  Client(Args&& ... args)
    : infinit::oracles::meta::Client(std::forward<Args>(args)...)
  {}

protected:
  virtual
  void
  _pacify_retry(int& retry_count) const override
  {
    reactor::sleep(10_ms);
  }
};

static
std::string
login_success_response(elle::UUID id = elle::UUID("00000000-0000-0000-0000-000000000001"))
{
  return elle::sprintf("{"
                       "  \"device\": {\"id\": \"%s\", \"name\": \"johny\", \"passport\": \"passport\"},"
                       "  \"trophonius\": {\"host\": \"192.168.1.1\", \"port\": 4923, \"port_ssl\": 4233},"
                       "  \"features\": [],"
                       "  \"self\": {"
                       "    \"_id\": \"0\","
                       "    \"id\": \"0\","
                       "    \"fullname\": \"jean\","
                       "    \"email\": \"jean@infinit.io\","
                       "    \"handle\": \"jean\","
                       "    \"register_status\": \"ok\","
                       "    \"identity\": \"identity\","
                       "    \"passport\": \"passport\","
                       "    \"devices\": [\"%s\"],"
                       "    \"networks\": [],"
                       "    \"public_key\": \"public_key\","
                       "    \"name\": \"FUUUUUUUUUUCK\","
                       "    \"accounts\": [],"
                       "    \"remaining_invitations\": 0,"
                       "    \"token_generation_key\": \"token_generation_key\","
                       "    \"favorites\": [],"
                       "    \"connected_devices\": [\"%s\"],"
                       "    \"status\": 1,"
                       "    \"creation_time\": 1420565249,"
                       "    \"last_connection\": 1420565249"
                       "    }"
                       " }",
                       id, id, id);
}

ELLE_TEST_SCHEDULED(connection_refused)
{
  infinit::oracles::meta::Client c("http", "127.0.0.1", 21232);
}

ELLE_TEST_SCHEDULED(login_success)
{
  HTTPServer s;
  s.register_route(
    "/login",
    reactor::http::Method::POST,
    [] (HTTPServer::Headers const&,
        HTTPServer::Cookies const&,
        HTTPServer::Parameters const&,
        elle::Buffer const& body) -> std::string
    {
      return login_success_response();
    });
  s.register_route("/logout", reactor::http::Method::POST,
                   [] (HTTPServer::Headers const&,
                       HTTPServer::Cookies const&,
                       HTTPServer::Parameters const&,
                       elle::Buffer const& body) -> std::string
                   {
                     return "{}";
                   });
  infinit::oracles::meta::Client c("http", "127.0.0.1", s.port());
  c.login("jean@infinit.io", "password", boost::uuids::nil_uuid());
  c.logout();
}

ELLE_TEST_SCHEDULED(forbidden)
{
  HTTPServer s;
  s.register_route("/login", reactor::http::Method::POST,
                   [] (HTTPServer::Headers const&,
                       HTTPServer::Cookies const&,
                       HTTPServer::Parameters const&,
                       elle::Buffer const& body) -> std::string
                   {
                     throw HTTPServer::Exception(
                       "", reactor::http::StatusCode::Forbidden);
                   });
  infinit::oracles::meta::Client c("http", "127.0.0.1", s.port());
  // Find good error.
  BOOST_CHECK_THROW(c.login("jean@infinit.io",
                            "password",
                            boost::uuids::nil_uuid()),
                    elle::Exception);
}

ELLE_TEST_SCHEDULED(ill_formed_json)
{
  HTTPServer s;
  s.register_route("/login", reactor::http::Method::POST,
                   [] (HTTPServer::Headers const&,
                       HTTPServer::Cookies const&,
                       HTTPServer::Parameters const&,
                       elle::Buffer const& body) -> std::string
                   {
                     return "{\"a\a:}";
                   });
  infinit::oracles::meta::Client c("http", "127.0.0.1", s.port());
  BOOST_CHECK_THROW(c.login("jean@infinit.io",
                            "password",
                            boost::uuids::nil_uuid()),
                    std::runtime_error);
}

ELLE_TEST_SCHEDULED(missing_key)
{
  HTTPServer s;
  s.register_route("/login", reactor::http::Method::POST,
                   [] (HTTPServer::Headers const&,
                       HTTPServer::Cookies const&,
                       HTTPServer::Parameters const&,
                       elle::Buffer const& body) -> std::string
                   {
                     return "{}";
                   });
  infinit::oracles::meta::Client c("http", "127.0.0.1", s.port());
  BOOST_CHECK_THROW(c.login("jean@infinit.io",
                            "password",
                            boost::uuids::nil_uuid()),
                    elle::serialization::MissingKey);
}

ELLE_TEST_SCHEDULED(login_password_dont_match)
{
  HTTPServer s;
  s.register_route("/login", reactor::http::Method::POST,
                   [] (HTTPServer::Headers const&,
                       HTTPServer::Cookies const&,
                       HTTPServer::Parameters const&,
                       elle::Buffer const& body) -> std::string
                   {
                     ELLE_LOG("body: %s", body);
                     throw HTTPServer::Exception("",
                                                 reactor::http::StatusCode::Forbidden,
                                                 "{"
                                                 " \"code\": -10101,"
                                                 " \"message\": \"email password dont match\""
                                                 "}");
                   });
  infinit::oracles::meta::Client c("http", "127.0.0.1", s.port());
  BOOST_CHECK_THROW(c.login("jean@infinit.io",
                            "password",
                            boost::uuids::nil_uuid()),
                      infinit::state::CredentialError);
}

ELLE_TEST_SCHEDULED(unconfirmed_email)
{
  HTTPServer s;
  s.register_route("/login", reactor::http::Method::POST,
                   [] (HTTPServer::Headers const&,
                       HTTPServer::Cookies const&,
                       HTTPServer::Parameters const&,
                       elle::Buffer const& body) -> std::string
                   {
                     ELLE_LOG("body: %s", body);
                     throw HTTPServer::Exception("",
                                                 reactor::http::StatusCode::Forbidden,
                                                 "{"
                                                 " \"code\": -105,"
                                                 " \"message\": \"email not confirmed\""
                                                 "}");
                   });
  infinit::oracles::meta::Client c("http", "127.0.0.1", s.port());
  BOOST_CHECK_THROW(c.login("jean@infinit.io",
                            "password",
                            boost::uuids::nil_uuid()),
                      infinit::state::UnconfirmedEmailError);
}

ELLE_TEST_SCHEDULED(already_logged_in)
{
  HTTPServer s;
  s.register_route("/login", reactor::http::Method::POST,
                   [] (HTTPServer::Headers const&,
                       HTTPServer::Cookies const&,
                       HTTPServer::Parameters const&,
                       elle::Buffer const& body) -> std::string
                   {
                     ELLE_LOG("body: %s", body);
                     throw HTTPServer::Exception("",
                                                 reactor::http::StatusCode::Forbidden,
                                                 "{"
                                                 " \"code\": -102,"
                                                 " \"message\": \"already logged in\""
                                                 "}");
                   });
  infinit::oracles::meta::Client c("http", "127.0.0.1", s.port());
  BOOST_CHECK_THROW(c.login("jean@infinit.io",
                            "password",
                            boost::uuids::nil_uuid()),
                      infinit::state::AlreadyLoggedIn);
}

ELLE_TEST_SCHEDULED(cloud_buffer_gone)
{
  HTTPServer s;
  s.register_route("/transaction/tid/cloud_buffer",
                   reactor::http::Method::GET,
                   [] (HTTPServer::Headers const&,
                       HTTPServer::Cookies const&,
                       HTTPServer::Parameters const&,
                       elle::Buffer const& body) -> std::string
                   {
                     throw HTTPServer::Exception("",
                                                 reactor::http::StatusCode::Gone,
                                                 "{}");
                   });
  infinit::oracles::meta::Client c("http", "127.0.0.1", s.port());
  BOOST_CHECK_THROW(c.get_cloud_buffer_token("tid", false),
                    infinit::state::TransactionFinalized);
}

ELLE_TEST_SCHEDULED(status_found)
{
  HTTPServer s;
  s.headers().erase("X-Fist-Meta-Version");
  int i = 0;
  s.register_route(
    "/trophonius",
    reactor::http::Method::GET,
    [&i, &s] (HTTPServer::Headers const&,
              HTTPServer::Cookies const&,
              HTTPServer::Parameters const&,
              elle::Buffer const& body)
    {
      if (i++ < 3)
      {
        throw HTTPServer::Exception("",
                                    reactor::http::StatusCode::Found,
                                    "{}");
      }
      else
      {
        s.headers()["X-Fist-Meta-Version"] = INFINIT_VERSION;
        return
          "{"
          "  \"host\": \"hostname\","
          "  \"port\": 80,"
          "  \"port_ssl\": 443"
          "}";
      }
    });
  Client c("http", "127.0.0.1", s.port());
  c.trophonius();
}

ELLE_TEST_SCHEDULED(json_error_not_meta)
{
  HTTPServer s;
  s.headers().erase("X-Fist-Meta-Version");
  int i = 0;
  s.register_route(
    "/trophonius",
    reactor::http::Method::GET,
    [&i, &s] (HTTPServer::Headers const&,
              HTTPServer::Cookies const&,
              HTTPServer::Parameters const&,
              elle::Buffer const& body)
    {
      if (i++ < 3)
      {
        return "}";
      }
      else
      {
        s.headers()["X-Fist-Meta-Version"] = INFINIT_VERSION;
        return
          "{"
          "  \"host\": \"hostname\","
          "  \"port\": 80,"
          "  \"port_ssl\": 443"
          "}";
      }
    });
  Client c("http", "127.0.0.1", s.port());
  c.trophonius();
}

ELLE_TEST_SCHEDULED(transactions)
{
  HTTPServer s;
  int i = 0;
  s.register_route(
    "/transactions",
    reactor::http::Method::GET,
    [&i, &s] (HTTPServer::Headers const&,
              HTTPServer::Cookies const&,
              HTTPServer::Parameters const&,
              elle::Buffer const& body)
    {
      return "{"
        "  \"transactions\": []"
        "}";
    });
  Client c("http", "127.0.0.1", s.port());
  BOOST_CHECK(c.transactions().empty());
}

ELLE_TEST_SCHEDULED(transaction_create)
{
  HTTPServer s;
  s.register_route(
    "/transaction/create_empty",
    reactor::http::Method::POST,
    [] (HTTPServer::Headers const&,
        HTTPServer::Cookies const&,
        HTTPServer::Parameters const&,
        elle::Buffer const& body)
    {
      return R"JSON(
        {
          "created_transaction_id": "42"
        }
        )JSON";
    });
  Client c("http", "127.0.0.1", s.port());
  auto t_id = c.create_transaction();
  BOOST_CHECK(t_id == "42");
}

ELLE_TEST_SCHEDULED(trophonius)
{
  HTTPServer s;
  int i = 0;
  boost::uuids::uuid id = boost::uuids::nil_generator()();
  s.register_route(
    elle::sprintf("/trophonius/%s", id),
    reactor::http::Method::PUT,
    [&i, &s] (HTTPServer::Headers const&,
              HTTPServer::Cookies const&,
              HTTPServer::Parameters const&,
              elle::Buffer const& body)
    {
      return "{}";
    });
  s.register_route(
    elle::sprintf("/trophonius/%s", id),
    reactor::http::Method::DELETE,
    [&i, &s] (HTTPServer::Headers const&,
              HTTPServer::Cookies const&,
              HTTPServer::Parameters const&,
              elle::Buffer const& body)
    {
      return "{}";
    });
  infinit::oracles::meta::Admin c("http", "127.0.0.1", s.port());
  ELLE_LOG("register trophonius")
    c.register_trophonius(id, 1, 2, 3, "localhost", 0);
  ELLE_LOG("unregister trophonius")
    c.unregister_trophonius(id);
}

ELLE_TEST_SCHEDULED(upload_avatar)
{
  HTTPServer s;
  int i = 0;
  s.register_route(
    "/user/avatar",
    reactor::http::Method::POST,
    [&i, &s] (HTTPServer::Headers const& headers,
              HTTPServer::Cookies const&,
              HTTPServer::Parameters const&,
              elle::Buffer const& body)
    {
      BOOST_CHECK_EQUAL(headers.at("Content-Type"), "application/octet-stream");
      return "";
    });
  infinit::oracles::meta::Admin c("http", "127.0.0.1", s.port());
  elle::Buffer image("4242424242424242424242", 2);

  ELLE_LOG("upload avatar")
    c.icon(image);
}

ELLE_TEST_SCHEDULED(link_credentials)
{
  using namespace boost::posix_time;
  hours plus_one_hour(1);
  ptime now = second_clock::universal_time();
  ptime expiration = now + plus_one_hour;
  static const std::string response =
    elle::sprintf(
      "{"
      "  \"protocol\": \"aws\","
      "  \"access_key_id\": \"access key id\","
      "  \"secret_access_key\": \"secret access key\","
      "  \"session_token\": \"session token\","
      "  \"region\": \"region\","
      "  \"bucket\": \"bucket\","
      "  \"folder\": \"folder\","
      "  \"expiration\": \"%s\","
      "  \"current_time\": \"%s\""
      "}",
      to_iso_extended_string(expiration),
      to_iso_extended_string(now));
  static const std::string id{"id"};

  HTTPServer s;
  s.register_route(
    elle::sprintf("/link/%s/credentials", id),
    reactor::http::Method::POST,
    [&s] (HTTPServer::Headers const& headers,
          HTTPServer::Cookies const&,
          HTTPServer::Parameters const&,
          elle::Buffer const& body)
    {
      BOOST_CHECK_EQUAL(headers.at("Content-Type"), "application/json");
      return response;
    });

  s.register_route(
    elle::sprintf("/link/%s/credentials", id),
    reactor::http::Method::GET,
    [&s] (HTTPServer::Headers const& headers,
          HTTPServer::Cookies const&,
          HTTPServer::Parameters const&,
          elle::Buffer const& body)
    {
      BOOST_CHECK(headers.find("Content-Type") == headers.end());
      return response;
    });
  infinit::oracles::meta::Client c("http", "127.0.0.1", s.port());
  c.link_credentials(id, false);
  c.link_credentials(id, true);
}

ELLE_TEST_SCHEDULED(change_email)
{
  HTTPServer s;
  std::string stored_password;
  std::string stored_email;
  s.register_route("/user/change_email_request", reactor::http::Method::POST,
                   [&] (HTTPServer::Headers const&,
                       HTTPServer::Cookies const&,
                       HTTPServer::Parameters const&,
                       elle::Buffer const& body) -> std::string
                   {
                     elle::IOStream stream(body.istreambuf());
                     elle::serialization::json::SerializerIn input(stream, false);
                     std::string password_hash, email;
                     input.serialize("password", password_hash);
                     input.serialize("new_email", email);
                     if (password_hash != stored_password)
                     {
                       throw HTTPServer::Exception("",
                         reactor::http::StatusCode::Forbidden,
                         "{"
                         " \"code\": -10101,"
                         " \"message\": \"email password dont match\""
                         "}");
                     }
                     stored_email = email;
                     return "{}";
                   });
  s.register_route("/logout", reactor::http::Method::POST,
                   [&] (HTTPServer::Headers const&,
                        HTTPServer::Cookies const&,
                        HTTPServer::Parameters const&,
                        elle::Buffer const& body) -> std::string
                   {
                     return "{}";
                   });
  s.register_route(
    "/login",
    reactor::http::Method::POST,
    [&] (HTTPServer::Headers const&,
         HTTPServer::Cookies const&,
         HTTPServer::Parameters const&,
         elle::Buffer const& body) -> std::string
    {
      elle::IOStream stream(body.istreambuf());
      elle::serialization::json::SerializerIn input(stream, false);
      std::string password_hash, email;
      input.serialize("password_hash", password_hash);
      input.serialize("email", email);
      if (stored_password.empty())
      {
        stored_password = password_hash;
        stored_email = email;
      }
      else if (stored_password != password_hash
               || stored_email != email)
      {
        throw HTTPServer::Exception("",
                                    reactor::http::StatusCode::Forbidden,
                                    "{"
                                    " \"code\": -10101,"
                                    " \"message\": \"email password dont match\""
                                    "}");
      }
      return login_success_response();
    });
  infinit::oracles::meta::Client c("http", "127.0.0.1", s.port());

  c.login("bob@bob.com", "pass", boost::uuids::nil_uuid());
  c.change_email("bob2@bob.com", "pass");
  c.logout();
  BOOST_CHECK_THROW(c.login("bob@bob.com",
                            "pass",
                            boost::uuids::nil_uuid()),
                    infinit::state::CredentialError);
  c.login("bob2@bob.com", "pass", boost::uuids::nil_uuid());
}

ELLE_TEST_SCHEDULED(merge_ghost)
{
  HTTPServer s;
  s.register_route("/ghost/foo/merge", reactor::http::Method::POST,
                   [] (HTTPServer::Headers const&,
                       HTTPServer::Cookies const&,
                       HTTPServer::Parameters const&,
                       elle::Buffer const& body) -> std::string
                   {
                     return "{}";
                   });
  infinit::oracles::meta::Client c("http", "127.0.0.1", s.port());
  c.use_ghost_code("foo");
}

ELLE_TEST_SCHEDULED(url_encoded_merge_ghost)
{
  HTTPServer s;
  s.register_route("/ghost/foo%20/merge", reactor::http::Method::POST,
                   [] (HTTPServer::Headers const&,
                       HTTPServer::Cookies const&,
                       HTTPServer::Parameters const&,
                       elle::Buffer const& body) -> std::string
                   {
                     return "{}";
                   });
  infinit::oracles::meta::Client c("http", "127.0.0.1", s.port());
  c.use_ghost_code("foo ");
}

ELLE_TEST_SCHEDULED(merge_ghost_failure)
{
  HTTPServer s;
  s.register_route("/ghost/bar/merge", reactor::http::Method::POST,
                   [] (HTTPServer::Headers const&,
                       HTTPServer::Cookies const&,
                       HTTPServer::Parameters const&,
                       elle::Buffer const& body) -> std::string
                   {
                     throw HTTPServer::Exception("",
                                                 reactor::http::StatusCode::Not_Found,
                                                 "{"
                                                 "}");

                   });
  infinit::oracles::meta::Client c("http", "127.0.0.1", s.port());
  // This should check for reactor::http::RequestError but it doesn't work.
  BOOST_CHECK_THROW(c.use_ghost_code("foo"), elle::Exception);
}

ELLE_TEST_SCHEDULED(ghost_user_email)
{
  HTTPServer s;
  s.register_route("/users/id", reactor::http::Method::GET,
                   [] (HTTPServer::Headers const&,
                       HTTPServer::Cookies const&,
                       HTTPServer::Parameters const&,
                       elle::Buffer const& body) -> std::string
                   {
                     return "{"
                       "  \"id\": \"0\","
                       "  \"fullname\": \"jean\","
                       "  \"handle\": \"jean\","
                       "  \"register_status\": \"ghost\","
                       "  \"devices\": [],"
                       "  \"public_key\": \"public_key\","
                       "  \"connected_devices\": []"
                       "}";
                   });
  infinit::oracles::meta::Client c("http", "127.0.0.1", s.port());
  auto user = c.user("id");
  ELLE_ASSERT(!user.ghost_code);
  ELLE_ASSERT_EQ(user.register_status, "ghost");
  ELLE_ASSERT(!user.online());
  ELLE_ASSERT(user.ghost());
  ELLE_ASSERT(!user.deleted());
}

ELLE_TEST_SCHEDULED(ghost_user_phone)
{
  HTTPServer s;
  s.register_route("/users/id", reactor::http::Method::GET,
                   [] (HTTPServer::Headers const&,
                       HTTPServer::Cookies const&,
                       HTTPServer::Parameters const&,
                       elle::Buffer const& body) -> std::string
                   {
                     return "{"
                       "  \"id\": \"0\","
                       "  \"fullname\": \"jean\","
                       "  \"handle\": \"jean\","
                       "  \"register_status\": \"ghost\","
                       "  \"devices\": [],"
                       "  \"public_key\": \"public_key\","
                       "  \"connected_devices\": [],"
                       "  \"phone_number\": \"+33666000666\","
                       "  \"ghost_code\": \"l32dz\","
                       "  \"ghost_profile\": \"http://www.bit.ly/cul\""
                       "}";
                   });
  infinit::oracles::meta::Client c("http", "127.0.0.1", s.port());
  auto user = c.user("id");
  ELLE_ASSERT_EQ(user.ghost_code.get(), "l32dz");
  ELLE_ASSERT_EQ(user.phone_number.get(), "+33666000666");
  ELLE_ASSERT_EQ(user.ghost_profile_url.get(), "http://www.bit.ly/cul");
  ELLE_ASSERT_EQ(user.register_status, "ghost");
  ELLE_ASSERT(!user.online());
  ELLE_ASSERT(user.ghost());
  ELLE_ASSERT(!user.deleted());
}

ELLE_TEST_SCHEDULED(normal_user)
{
  HTTPServer s;
  auto id = elle::UUID("00000000-0000-0000-0000-000000000001");
  s.register_route("/users/id", reactor::http::Method::GET,
                   [&] (HTTPServer::Headers const&,
                       HTTPServer::Cookies const&,
                       HTTPServer::Parameters const&,
                       elle::Buffer const& body) -> std::string
                   {
                     return elle::sprintf("{"
                       "  \"id\": \"1\","
                       "  \"fullname\": \"jean\","
                       "  \"handle\": \"jean\","
                       "  \"register_status\": \"ok\","
                       "  \"devices\": [\"%s\"],"
                       "  \"public_key\": \"public_key\","
                       "  \"connected_devices\": [\"%s\"]"
                       "}", id, id);

                   });
  infinit::oracles::meta::Client c("http", "127.0.0.1", s.port());
  auto user = c.user("id");
  ELLE_ASSERT(user.online());
  ELLE_ASSERT(!user.ghost());
  ELLE_ASSERT(!user.deleted());
  ELLE_ASSERT(!user.online_excluding_device(id));
  ELLE_ASSERT(user.online());
}

namespace facebook
{
  ELLE_TEST_SCHEDULED(connect_success)
  {
    HTTPServer s;
    s.register_route("/login", reactor::http::Method::POST,
                     [] (HTTPServer::Headers const&,
                         HTTPServer::Cookies const&,
                         HTTPServer::Parameters const&,
                         elle::Buffer const& body) -> std::string
                     {
                       return login_success_response();
                     });
    infinit::oracles::meta::Client c("http", "127.0.0.1", s.port());
    c.facebook_connect("foobar", boost::uuids::nil_uuid());
  }

  ELLE_TEST_SCHEDULED(already_registered)
  {
    HTTPServer s;
    s.register_route("/users/yes", reactor::http::Method::GET,
                     [] (HTTPServer::Headers const&,
                         HTTPServer::Cookies const&,
                         HTTPServer::Parameters const& parameters,
                         elle::Buffer const&) -> std::string
                     {
                       ELLE_ASSERT(contains(parameters, "account_type"));
                       return "{}";
                     });
    s.register_route("/users/no", reactor::http::Method::GET,
                     [] (HTTPServer::Headers const&,
                         HTTPServer::Cookies const&,
                         HTTPServer::Parameters const& parameters,
                         elle::Buffer const&) -> std::string
                     {
                       ELLE_ASSERT(contains(parameters, "account_type"));
                       throw HTTPServer::Exception(
                         "/users/facebook/no",
                         reactor::http::StatusCode::Not_Found,
                         "{"
                         "}");
                     });
    infinit::oracles::meta::Client c("http", "127.0.0.1", s.port());
    ELLE_ASSERT(c.facebook_id_already_registered("yes"));
    ELLE_ASSERT(!c.facebook_id_already_registered("no"));
  }

  ELLE_TEST_SCHEDULED(connect_failure)
  {
    HTTPServer s;
    s.register_route("/login", reactor::http::Method::POST,
                     [] (HTTPServer::Headers const&,
                         HTTPServer::Cookies const&,
                         HTTPServer::Parameters const&,
                         elle::Buffer const& body) -> std::string
                     {
                       throw HTTPServer::Exception("",
                                                   reactor::http::StatusCode::Forbidden,
                                                   "{"
                                                   " \"code\": -10101,"
                                                   " \"message\": \"email password dont match\""
                                                   "}");
                     });
    infinit::oracles::meta::Client c("http", "127.0.0.1", s.port());
    BOOST_CHECK_THROW(c.facebook_connect("foobar", boost::uuids::nil_uuid()),
                      infinit::state::CredentialError);
  }
}

namespace devices
{
  ELLE_TEST_SCHEDULED(devices)
  {
    auto uuid1 = elle::UUID("00000000-0000-0000-0000-000000000001");
    auto uuid2 = elle::UUID("00000000-0000-0000-0000-000000000002");
    using namespace boost::posix_time;
    HTTPServer s;
    s.register_route("/user/devices",
                     reactor::http::Method::GET,
                     [uuid1, uuid2] (
                       HTTPServer::Headers const&,
                       HTTPServer::Cookies const&,
                       HTTPServer::Parameters const&,
                       elle::Buffer const& body
                     ) -> std::string
                     {
                       return elle::sprintf(R"JSON({
                         "devices": [
                           {
                             "id": "%s",
                             "name": "device-name-1",
                             "passport": "device-passport",
                             "os": "gnu-linux",
                             "last_sync": "2015-03-06T01:02:03Z"
                           },
                           {
                             "id": "%s",
                             "name": "device-name-2"
                           }
                         ]
                       })JSON", uuid1, uuid2);
                     });
    infinit::oracles::meta::Client c("http", "127.0.0.1", s.port());
    auto devices = c.devices();
    BOOST_CHECK_EQUAL(devices.size(), 2);
    {
      auto device = devices[0];
      BOOST_CHECK_EQUAL(device.id, uuid1);
      BOOST_CHECK_EQUAL((std::string) device.name, "device-name-1");
      BOOST_CHECK_EQUAL(device.passport.get(), "device-passport");
      BOOST_CHECK_EQUAL(device.os.get(), "gnu-linux");
      BOOST_CHECK_EQUAL(device.last_sync.get(),
                        time_from_string("2015-03-06 01:02:03.000"));
    }
    {
      auto device = devices[1];
      BOOST_CHECK_EQUAL(device.id, uuid2);
      BOOST_CHECK_EQUAL((std::string) device.name, "device-name-2");
      BOOST_CHECK(!device.passport);
      BOOST_CHECK(!device.os);
      BOOST_CHECK(!device.last_sync);
    }
  }
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(connection_refused));
  suite.add(BOOST_TEST_CASE(forbidden));
  suite.add(BOOST_TEST_CASE(login_success));
  suite.add(BOOST_TEST_CASE(ill_formed_json));
  suite.add(BOOST_TEST_CASE(missing_key));
  suite.add(BOOST_TEST_CASE(login_password_dont_match));
  suite.add(BOOST_TEST_CASE(unconfirmed_email));
  suite.add(BOOST_TEST_CASE(already_logged_in));
  suite.add(BOOST_TEST_CASE(cloud_buffer_gone));
  suite.add(BOOST_TEST_CASE(status_found));
  suite.add(BOOST_TEST_CASE(json_error_not_meta));
  suite.add(BOOST_TEST_CASE(transactions));
  suite.add(BOOST_TEST_CASE(trophonius));
  suite.add(BOOST_TEST_CASE(upload_avatar));
  suite.add(BOOST_TEST_CASE(link_credentials));
  suite.add(BOOST_TEST_CASE(transaction_create));
  suite.add(BOOST_TEST_CASE(change_email));
  suite.add(BOOST_TEST_CASE(merge_ghost));
  suite.add(BOOST_TEST_CASE(url_encoded_merge_ghost));
  suite.add(BOOST_TEST_CASE(merge_ghost_failure));
  suite.add(BOOST_TEST_CASE(normal_user));
  suite.add(BOOST_TEST_CASE(ghost_user_email));
  suite.add(BOOST_TEST_CASE(ghost_user_phone));
  {
    boost::unit_test::test_suite* s = BOOST_TEST_SUITE("facebook");
    suite.add(s);
    s->add(BOOST_TEST_CASE(facebook::connect_success));
    s->add(BOOST_TEST_CASE(facebook::already_registered));
    s->add(BOOST_TEST_CASE(facebook::connect_failure));
  }
  {
    boost::unit_test::test_suite* s = BOOST_TEST_SUITE("devices");
    suite.add(s);
    auto devices = &devices::devices;
    s->add(BOOST_TEST_CASE(devices));
  }
}
