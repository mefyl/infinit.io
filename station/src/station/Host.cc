#include <station/Host.hh>
#include <station/Station.hh>

namespace station
{
  Host::Host(Station& owner,
             papier::Passport const& passport,
             std::unique_ptr<reactor::network::Socket>&& socket):
    _owner(owner),
    _passport(passport),
    _socket(std::move(socket))
  {}

  Host::~Host()
  {
    ELLE_ASSERT_CONTAINS(this->_owner._hosts, this->passport());
    this->_owner._host_remove(*this);
  }

  reactor::network::Socket&
  Host::socket()
  {
    if (this->_socket)
      return *this->_socket;
    throw elle::Exception("socket already released");
  }

  std::unique_ptr<reactor::network::Socket>
  Host::release()
  {
    return std::move(this->_socket);
  }

  void
  Host::print(std::ostream& stream) const
  {
    stream << "Host(" << this->passport() << ")";
  }
}
