
#include <elle/print.hh>
#include <infinit/oracles/meta/Client.hh>
#include <infinit/oracles/meta/Admin.hh>

#include <boost/uuid/string_generator.hpp>
#include <boost/python.hpp>
#include <string>

namespace py = boost::python;

template <class T>
struct container_wrap
{
  typedef typename T::value_type value_type;
  typedef typename T::iterator iter_type;

  static
  void add(T & x,
           value_type const& v)
  {
    x.push_back(v);
  }

  static
  bool in(T const& x, value_type const& v)
  {
    return std::find(x.begin(), x.end(), v) != x.end();
  }

  static int index(T const& x, value_type const& v)
  {
    int i = 0;
    for (auto const& val: x)
      if( val == v ) return i;

    PyErr_SetString(PyExc_ValueError, "Value not in the list");
    throw boost::python::error_already_set();
  }

  static void del(T& x, int i)
  {
    if( i<0 )
      i += x.size();

    iter_type it = x.begin();
    for (int pos = 0; pos < i; ++pos)
      ++it;

    if( i >= 0 && i < (int)x.size() ) {
      x.erase(it);
    } else {
      PyErr_SetString(PyExc_IndexError, "Index out of range");
      boost::python::throw_error_already_set();
    }
  }

  static value_type& get(T& x, int i)
  {
    if( i < 0 )
      i += x.size();

    if( i >= 0 && i < (int)x.size() ) {
      iter_type it = x.begin();
      for(int pos = 0; pos < i; ++pos)
        ++it;
      return *it;
    } else {
      PyErr_SetString(PyExc_IndexError, "Index out of range");
      throw boost::python::error_already_set();
    }
  }

  static void set(T& x, int i, value_type const& v)
  {
    if( i < 0 )
      i += x.size();

    if( i >= 0 && i < (int)x.size() ) {
      iter_type it = x.begin();
      for(int pos = 0; pos < i; ++pos)
        ++it;
      *it = v;
    } else {
      PyErr_SetString(PyExc_IndexError, "Index out of range");
      boost::python::throw_error_already_set();
    }
  }
};

template <typename container>
void export_container(const char* typeName)
{
  using namespace boost::python;

  class_<container>(typeName)
    .def("__len__", &container::size)
    .def("clear", &container::clear)
    .def("append", &container_wrap<container>::add, with_custodian_and_ward<1,2>()) // to let container keep value
    .def("__getitem__", &container_wrap<container>::get, return_value_policy<copy_non_const_reference>())
    .def("__setitem__", &container_wrap<container>::set, with_custodian_and_ward<1,2>()) // to let container keep value
    .def("__delitem__", &container_wrap<container>::del)
    .def("__contains__", &container_wrap<container>::in)
    .def("__iter__", iterator<container>())
    .def("index", &container_wrap<container>::index);
}

using infinit::oracles::meta::Client;
using infinit::oracles::meta::Admin;
using infinit::oracles::Transaction;
using infinit::oracles::meta::Error;
using infinit::oracles::meta::Response;
using infinit::oracles::meta::Exception;
using infinit::oracles::meta::DebugResponse;
using infinit::oracles::meta::LoginResponse;
using infinit::oracles::meta::LogoutResponse;
using infinit::oracles::meta::RegisterResponse;
using infinit::oracles::meta::MessageResponse;
using infinit::oracles::meta::User;
using infinit::oracles::meta::UserResponse;
using infinit::oracles::meta::SelfResponse;
using infinit::oracles::meta::InviteUserResponse;
using infinit::oracles::meta::UsersResponse;
using infinit::oracles::meta::SwaggersResponse;
using infinit::oracles::meta::TransactionsResponse;
using infinit::oracles::meta::TransactionResponse;
using infinit::oracles::meta::CreateTransactionResponse;
using infinit::oracles::meta::UpdateTransactionResponse;
using infinit::oracles::meta::EndpointNodeResponse;
using infinit::oracles::meta::Device;
using infinit::oracles::meta::CreateDeviceResponse;
using infinit::oracles::meta::UpdateDeviceResponse;
using infinit::oracles::meta::AddSwaggerResponse;

class MetaClient:
  public Client
{
public:
  MetaClient(std::string const& host, int port)
    : Client(host, port)
  {
  }

  LoginResponse
  login(std::string const& email,
        std::string const& password,
        std::string const& device_uuid)
  {
    static auto string_generator = boost::uuids::string_generator();

    return Client::login(email, password, string_generator(device_uuid));
  }
};

class AdminClient:
  public Admin
{
public:
  AdminClient(std::string const& host, int port)
    : Admin(host, port)
  {}
};

struct std_list_to_pylist
{
  static PyObject* convert(std::vector<std::string> const& l)
  {
    boost::python::list pyl;
    for (auto const& elem: l)
    {
      pyl.append(elem);
    }
    return boost::python::incref(pyl.ptr());
  }
};

void export_meta_client();
void export_meta_client()
{
  typedef py::return_value_policy<py::return_by_value> by_value;

  py::enum_<Error>("Error")
# define ERR_CODE(_name, _value, comment)                                      \
      .value(#_name, Error::_name)
# include <infinit/oracles/meta/error_code.hh.inc>
# undef ERR_CODE
  ;
  export_container<std::vector<std::string>>("vector_string");
  export_container<std::list<std::string>>("list_string");

  py::class_<Transaction>("Transaction");
  py::class_<Response>("Response")
    .add_property("success", &Response::success)
    .def_readonly("error_details", &Response::error_details)
    .def_readonly("error_code", &Response::error_code)
  ;
  py::class_<User>("User")
    .def_readonly("id", &User::id)
    .def_readonly("fullname", &User::fullname)
    .def_readonly("handle", &User::handle)
    .def_readonly("public_key", &User::public_key)
    .def_readonly("status", &User::status)
    .def_readonly("connected_devices", &User::connected_devices)
  ;
  py::class_<Device>("Device")
    .def_readonly("id", &Device::id)
    .def_readonly("name", &Device::name)
    .def_readonly("passport", &Device::passport)
  ;
  py::class_<DebugResponse, py::bases<Response>>("DebugResponse");
  py::class_<LoginResponse, py::bases<Response>>("LoginResponse")
    .def_readonly("token", &LoginResponse::token)
    .def_readonly("fullname", &LoginResponse::fullname)
    .def_readonly("handle", &LoginResponse::handle)
    .def_readonly("email", &LoginResponse::email)
    .def_readonly("identity", &LoginResponse::identity)
    .def_readonly("id", &LoginResponse::id)
  ;
  py::class_<LogoutResponse, py::bases<Response>>("LogoutResponse");
  py::class_<RegisterResponse, py::bases<Response>>("RegisterResponse");
  py::class_<MessageResponse, py::bases<Response>>("MessageResponse");
  py::class_<UserResponse, py::bases<User, Response>>("UserResponse");
  py::class_<SelfResponse, py::bases<UserResponse>>("SelfResponse")
    .def_readonly("identity", &SelfResponse::identity)
    .def_readonly("email", &SelfResponse::email)
    .def_readonly("remaining_invitations", &SelfResponse::remaining_invitations)
    .def_readonly("token_generation_key", &SelfResponse::token_generation_key)
  ;

  py::class_<InviteUserResponse, py::bases<Response>>("InviteUserResponse")
    .def_readonly("_id", &InviteUserResponse::_id)
  ;
  py::class_<UsersResponse, py::bases<Response>>("UsersResponse")
    .def_readonly("users", &UsersResponse::users)
  ;
  py::class_<SwaggersResponse, py::bases<Response>>("SwaggersResponse")
    .def_readonly("swaggers", &SwaggersResponse::swaggers)
  ;

  // py::class_<TransactionsResponse, py::bases<Response>>("TransactionsResponse")
  //   .def_readonly("transactions", &TransactionsResponse::transactions)
  // ;
  py::class_<TransactionResponse, py::bases<Response, Transaction>>("TransactionResponse");
  py::class_<CreateTransactionResponse, py::bases<Response>>("CreateTransactionResponse")
    .def_readonly("created_transaction_id", &CreateTransactionResponse::created_transaction_id)
    .def_readonly("remaining_invitations", &CreateTransactionResponse::remaining_invitations)
  ;

  py::class_<UpdateTransactionResponse, py::bases<Response>>("UpdateTransactionResponse")
    .def_readonly("updated_transaction_id", &UpdateTransactionResponse::updated_transaction_id)
  ;

  py::class_<EndpointNodeResponse, py::bases<Response>>("EndpointNodeResponse")
    .def_readonly("externals", &EndpointNodeResponse::externals)
    .def_readonly("locals", &EndpointNodeResponse::locals)
    .def_readonly("fallback", &EndpointNodeResponse::fallback)
  ;

  py::class_<AddSwaggerResponse>("AddSwaggerResponse")
  ;

  py::class_<CreateDeviceResponse, py::bases<Response, Device>>("CreateDeviceResponse")
  ;
  py::class_<UpdateDeviceResponse, py::bases<Response, Device>>("UpdateDeviceResponse")
  ;

  void (MetaClient::*set_email)(std::string const &) = &MetaClient::email;
  std::string const& (MetaClient::*get_email)() const = &MetaClient::email;

  py::class_<MetaClient, boost::noncopyable>(
    "Meta", py::init<std::string const&, int>())
    .def("debug", &MetaClient::debug)
    .def("login", &MetaClient::login)
    .def("logout", &MetaClient::logout)
    .def("user", &MetaClient::user)
    .def("register", &MetaClient::register_)
    //.def("user_icon", &MetaClient::user_icon)
    .def("self", &MetaClient::self)
    .def("user_from_public_key", &MetaClient::user_from_public_key)
    .def("search_users", &MetaClient::search_users)
    .def("get_swaggers", &MetaClient::get_swaggers)
    .def("create_device", &MetaClient::create_device)
    .def("update_device", &MetaClient::update_device)
    .def("invite_user", &MetaClient::invite_user)
    .def("transaction", &MetaClient::transaction)
    // .def("transactions", &MetaClient::transactions)
    .def("create_transaction", &MetaClient::create_transaction)
    .def("update_transaction", &MetaClient::update_transaction)
    .def("accept_transaction", &MetaClient::accept_transaction)
    .def("send_message", &MetaClient::send_message)
    .add_property("email",
                  make_function(get_email, by_value()),
                  set_email)
  ;

  py::class_<AdminClient, boost::noncopyable>(
    "Admin", py::init<std::string const&, int>())
    .def("add_swaggers", &AdminClient::add_swaggers)
    .def("genocide", &AdminClient::genocide)
    .def("ghostify", &AdminClient::ghostify)
    .def("connect", &AdminClient::connect)
    .def("disconnect", &AdminClient::disconnect)
    .def("register_trophonius", &AdminClient::register_trophonius)
    .def("unregister_trophonius", &AdminClient::unregister_trophonius)
  ;
}

// Pacify -Wmissing-declaration
extern "C"
{
  PyObject* PyInit_MetaClient();
}

BOOST_PYTHON_MODULE(MetaClient)
{
  export_meta_client();
}
