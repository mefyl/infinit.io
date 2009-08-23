//
// ---------- header ----------------------------------------------------------
//
// project       infinit
//
// license       infinit (c)
//
// file          /home/mycure/infinit/etoile/core/Object.hh
//
// created       julien quintard   [thu mar  5 16:04:08 2009]
// updated       julien quintard   [sat aug 22 01:26:15 2009]
//

#ifndef ETOILE_CORE_OBJECT_HH
#define ETOILE_CORE_OBJECT_HH

//
// ---------- includes --------------------------------------------------------
//

#include <elle/Elle.hh>

#include <etoile/core/PublicKeyBlock.hh>
#include <etoile/core/Genre.hh>
#include <etoile/core/Access.hh>
#include <etoile/core/Token.hh>
#include <etoile/core/Permissions.hh>
#include <etoile/core/Version.hh>
#include <etoile/core/Catalog.hh>
#include <etoile/core/Data.hh>
#include <etoile/core/Reference.hh>
#include <etoile/core/Contents.hh>
#include <etoile/core/Author.hh>
#include <etoile/core/State.hh>
#include <etoile/core/Time.hh>

#include <etoile/hole/Hole.hh>

namespace etoile
{
  namespace core
  {

//
// ---------- classes ---------------------------------------------------------
//

    ///
    /// XXX
    ///
    /// note: update and administrate just modify the object and mark what
    ///   section will need to be re-signed 'cause it has been modified. then
    ///   seal regenerate these signatures. that way, the cryptographic
    ///   operations are done only once and that speeds up the process.
    ///
    class Object:
      public PublicKeyBlock
    {
    public:
      //
      // constructors & destructors
      //
      Object();

      //
      // methods
      //
      Status		Create(const Genre,
			       const PublicKey&);

      Status		Update(const Author&,
			       const hole::Address&,
			       const Natural64&,
			       const Digest&);
      Status		Administrate(const hole::Address&,
				     const Permissions&);

      Status		Seal(const PrivateKey&);

      Status		Validate(const hole::Address&) const;

      //
      // interfaces
      //

      // dumpable
      Status		Dump(const Natural32 = 0) const;

      // archivable
      Status		Serialize(Archive&) const;
      Status		Extract(Archive&);

      //
      // attributes
      //

      struct
      {
	PublicKey	K;

	Signature	signature;
      }			owner;

      Author		author;

      struct
      {
	struct
	{
	  Permissions	permissions;
	  Token		token;
	}		owner;

	struct
	{
	  Genre		genre;
	  Time		stamp;
	}		status;

	hole::Address	access;

	Version		version;
	Signature	signature;

	State		state;
      }			meta;

      struct
      {
	hole::Address	contents;

	Natural64	size;
	Time		stamp;

	Digest		fingerprint;

	Version		version;
	Signature	signature;

	State		state;
      }			data;
    };

  }
}

#endif
