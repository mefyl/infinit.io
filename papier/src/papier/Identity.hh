#ifndef PAPIER_IDENTITY_HH
# define PAPIER_IDENTITY_HH

# include <elle/attribute.hh>
# include <elle/operator.hh>
# include <elle/concept/Fileable.hh>
# include <elle/concept/Uniquable.hh>
# include <elle/fwd.hh>

# include <cryptography/fwd.hh>

# include <papier/Authority.hh>

namespace papier
{

  ///
  /// this class represents an identity issued by the Infinit authority
  /// which represents a user.
  ///
  /// note that the name attribute is supposed to be unique as it plays the
  /// role of identifier.
  ///
  class Identity:
    public elle::concept::MakeFileable<Identity>,
    public elle::concept::MakeUniquable<Identity>
  {
  public:
    // XXX
    static elle::Natural32 const keypair_length = 2048;

    enum Mode
      {
        ModeEncrypted,
        ModeUnencrypted
      };

  private:
    ELLE_ATTRIBUTE_R(elle::String, id);
  public: // XXX
    elle::String                name;
  private: // XXX
    ELLE_ATTRIBUTE(cryptography::KeyPair*, pair);
    ELLE_ATTRIBUTE(cryptography::Signature*, signature);
  public: // XXX
    cryptography::Code*       code;

    ELLE_OPERATOR_NO_ASSIGNMENT(Identity);

  public:
    // XXX
    cryptography::KeyPair const&
    pair() const
    {
      ELLE_ASSERT(this->_pair != nullptr);

      return (*this->_pair);
    }

  public:
    Identity();
    Identity(elle::io::Path const& path);
    Identity(Identity const& other);
    ~Identity();

  public:
    elle::Status        Create(elle::String const&,
                               const elle::String&,
                               cryptography::KeyPair const&);

    elle::Status        Encrypt(const elle::String&);
    elle::Status        Decrypt(const elle::String&);

    elle::Status        Clear();

    elle::Status        Seal(papier::Authority const&);
    elle::Status        Validate(papier::Authority const&) const;

  private:

    //
    // interfaces
    //
  public:
    // dumpable
    elle::Status        Dump(const elle::Natural32 = 0) const;

    ELLE_SERIALIZE_FRIEND_FOR(Identity);
  };

}

#include <papier/Identity.hxx>

#endif