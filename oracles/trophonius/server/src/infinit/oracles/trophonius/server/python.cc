#include <boost/python.hpp>

#include <elle/python/bindings.cc>

#include <infinit/oracles/trophonius/server/Trophonius.hh>

// Pacify -Wmissing-declarations
extern "C"
{
  PyObject* PyInit_server();
}

static void wait_wrap(infinit::oracles::trophonius::server::Trophonius* t)
{
  t->wait();
}

BOOST_PYTHON_MODULE(server)
{
  using infinit::oracles::trophonius::server::Trophonius;
  boost::python::class_<Trophonius,
                        boost::noncopyable>
    ("Trophonius",
     boost::python::init<int,
                         int,
                         std::string const&,
                         std::string const&,
                         int,
                         int,
                         boost::posix_time::time_duration const&,
                         boost::posix_time::time_duration const&,
                         boost::posix_time::time_duration const&>())
    .def("stop", &Trophonius::stop)
    .def("terminate", &Trophonius::terminate)
    .def("wait", &wait_wrap) // XXX: use Waitable::wait
    .def("port_tcp", &Trophonius::port_tcp)
    .def("port_ssl", &Trophonius::port_ssl)
    ;
}
