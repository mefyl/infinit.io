#include <elle/filesystem/TemporaryFile.hh>
#include <elle/log.hh>
#include <elle/test.hh>

#include <surface/gap/Exception.hh>
#include <surface/gap/state.hh>

#include "server.hh"

ELLE_LOG_COMPONENT("surface.gap.state->test");

// Cloud buffered peer transaction.
ELLE_TEST_SCHEDULED(cloud_buffer)
{
  tests::Server server;
  auto const email = "sender@infinit.io";
  auto const password = "secret";
  server.register_user(email, password);

  std::string const recipient_email = "recipient@infinit.io";
  server.register_user("recipient@infinit.io", password);
  tests::State state(server, random_uuid());
  elle::filesystem::TemporaryFile transfered("cloud-buffered");
  {
    boost::filesystem::ofstream f(transfered.path());
    BOOST_CHECK(f.good());
    for (int i = 0; i < 2048; ++i)
    {
      char c = i % 256;
      f.write(&c, 1);
    }
  }
  state->login(email, password);
  auto& state_transaction = state->transaction_peer_create(
    recipient_email,
    std::vector<std::string>{transfered.path().string().c_str()},
    "message");
  reactor::Barrier transferring, cloud_buffered;
  state_transaction.status_changed().connect(
    [&] (gap_TransactionStatus status)
    {
      ELLE_LOG("new local transaction status: %s", status);
      auto& server_transaction =
        server.transaction(state_transaction.data()->id);
      switch (status)
      {
        case gap_transaction_transferring:
        {
          BOOST_CHECK_EQUAL(
            server_transaction.status,
            infinit::oracles::Transaction::Status::initialized);
          transferring.open();
          break;
        }
        case gap_transaction_cloud_buffered:
        {
          BOOST_CHECK(transferring);
          BOOST_CHECK_EQUAL(
            server_transaction.status,
            infinit::oracles::Transaction::Status::cloud_buffered);
          BOOST_CHECK(server.cloud_buffered());
          cloud_buffered.open();
          break;
        }
        default:
        {
          BOOST_FAIL(
            elle::sprintf("unexpected transaction status: %s", status));
          break;
        }
      }
    });
  reactor::wait(cloud_buffered);
}

ELLE_TEST_SUITE()
{
  auto timeout = valgrind(15);
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(cloud_buffer), 0, timeout);
}
