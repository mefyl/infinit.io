#define BOOST_TEST_DYN_LINK
#include <boost/test/unit_test.hpp>

#include <reactor/scheduler.hh>
#include <elle/serialize/BinaryArchive.hh>

#include <reactor/network/exception.hh>
#include <reactor/network/tcp-server.hh>
#include <reactor/scheduler.hh>
#include <reactor/semaphore.hh>
#include <reactor/thread.hh>

#include <protocol/ChanneledStream.hh>
#include <protocol/Serializer.hh>
#include <protocol/RPC.hh>

reactor::Semaphore lock;

int answer()
{
  return 42;
}

int answer2()
{
  return 42;
}

int square(int x)
{
  return x * x;
}

std::string concat(std::string const& left, std::string const& right)
{
  return left + right;
}

void except()
{
  throw std::runtime_error("blablabla");
}

struct DummyRPC: public infinit::protocol::RPC<elle::serialize::InputBinaryArchive,
                                               elle::serialize::OutputBinaryArchive>
{
  DummyRPC(infinit::protocol::ChanneledStream& channels)
    : infinit::protocol::RPC<elle::serialize::InputBinaryArchive,
                             elle::serialize::OutputBinaryArchive>(channels)
    , answer("answer", *this)
    , square("square", *this)
    , concat("concat", *this)
    , raise ("raise", *this)
  {}

  RemoteProcedure<int> answer;
  RemoteProcedure<int, int> square;
  RemoteProcedure<std::string, std::string const&, std::string const&> concat;
  RemoteProcedure<void> raise;
};

void caller()
{
  auto& sched = *reactor::Scheduler::scheduler();
  sched.current()->wait(lock);
  reactor::network::TCPSocket socket(sched, "127.0.0.1", 12345);
  infinit::protocol::Serializer s(sched, socket);
  infinit::protocol::ChanneledStream channels(sched, s);

  DummyRPC rpc(channels);
  BOOST_CHECK_EQUAL(rpc.answer(), 42);
  BOOST_CHECK_EQUAL(rpc.square(8), 64);
  BOOST_CHECK_EQUAL(rpc.concat("foo", "bar"), "foobar");
  BOOST_CHECK_THROW(rpc.raise(), std::runtime_error);
}

void runner(bool sync)
{
  auto& sched = *reactor::Scheduler::scheduler();
  reactor::network::TCPServer server(sched);
  server.listen(12345);
  lock.release();
  reactor::network::TCPSocket* socket = server.accept();
  infinit::protocol::Serializer s(sched, *socket);
  infinit::protocol::ChanneledStream channels(sched, s);

  DummyRPC rpc(channels);
  rpc.answer = &answer;
  rpc.square = &square;
  rpc.concat = &concat;
  rpc.raise  = &except;
  try
    {
      if (sync)
        rpc.run();
      else
        rpc.parallel_run();
    }
  catch (reactor::network::ConnectionClosed&)
    {}
}

int test(bool sync)
{
  reactor::Scheduler sched{};

  reactor::Thread r(sched, "Runner", std::bind(runner, sync));
  reactor::Thread c(sched, "Caller", std::bind(caller));

  sched.run();
  return 0;
}

bool test_suite()
{
  boost::unit_test::test_suite* rpc = BOOST_TEST_SUITE("RPC");
  boost::unit_test::framework::master_test_suite().add(rpc);
  rpc->add(BOOST_TEST_CASE(std::bind(test, true)));
  rpc->add(BOOST_TEST_CASE(std::bind(test, false)));
  return true;
}

int
main(int argc, char** argv)
{
  return ::boost::unit_test::unit_test_main(test_suite, argc, argv);
}
