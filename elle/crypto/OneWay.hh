//
// ---------- header ----------------------------------------------------------
//
// project       elle
//
// license       infinit (c)
//
// file          /home/mycure/infinit/elle/crypto/OneWay.hh
//
// created       julien quintard   [mon oct 29 13:19:49 2007]
// updated       julien quintard   [sun aug 23 17:28:21 2009]
//

#ifndef ELLE_CRYPTO_ONEWAY_HH
#define ELLE_CRYPTO_ONEWAY_HH

//
// ---------- includes --------------------------------------------------------
//

#include <elle/core/Core.hh>

#include <elle/misc/Misc.hh>

#include <elle/crypto/Plain.hh>
#include <elle/crypto/Digest.hh>

#include <openssl/evp.h>
#include <openssl/err.h>

namespace elle
{
  using namespace core;
  using namespace misc;

  namespace crypto
  {

//
// ---------- class -----------------------------------------------------------
//

    ///
    /// this class provides one-way functions.
    ///
    class OneWay
    {
    public:
      //
      // constants
      //
      static const ::EVP_MD*		Algorithm;

      //
      // static methods
      //
      static Status	Hash(const Plain&,
			     Digest&);

      //
      // forward methods
      //

      ///
      /// this methods are required because the compiler, given an Archive
      /// object will call a template-based method instead of the Plain one.
      ///
      /// we do not want this especially because the template-based methods
      /// build archives and we are already receiving an archive.
      ///

      static Status	Hash(const Archive&		archive,
			     Digest&			digest)
      {
	return (OneWay::Hash((Plain&)archive, digest));
      }

      //
      // variadic templates
      //
      template <typename T1>
      static Status	Hash(const T1&,
			     Digest&);
      template <typename T1,
		typename T2>
      static Status	Hash(const T1&,
			     const T2&,
			     Digest&);
      template <typename T1,
		typename T2,
		typename T3>
      static Status	Hash(const T1&,
			     const T2&,
			     const T3&,
			     Digest&);
      template <typename T1,
		typename T2,
		typename T3,
		typename T4>
      static Status	Hash(const T1&,
			     const T2&,
			     const T3&,
			     const T4&,
			     Digest&);
      template <typename T1,
		typename T2,
		typename T3,
		typename T4,
		typename T5>
      static Status	Hash(const T1&,
			     const T2&,
			     const T3&,
			     const T4&,
			     const T5&,
			     Digest&);
      template <typename T1,
		typename T2,
		typename T3,
		typename T4,
		typename T5,
		typename T6>
      static Status	Hash(const T1&,
			     const T2&,
			     const T3&,
			     const T4&,
			     const T5&,
			     const T6&,
			     Digest&);
      template <typename T1,
		typename T2,
		typename T3,
		typename T4,
		typename T5,
		typename T6,
		typename T7>
      static Status	Hash(const T1&,
			     const T2&,
			     const T3&,
			     const T4&,
			     const T5&,
			     const T6&,
			     const T7&,
			     Digest&);
      template <typename T1,
		typename T2,
		typename T3,
		typename T4,
		typename T5,
		typename T6,
		typename T7,
		typename T8>
      static Status	Hash(const T1&,
			     const T2&,
			     const T3&,
			     const T4&,
			     const T5&,
			     const T6&,
			     const T7&,
			     const T8&,
			     Digest&);
      template <typename T1,
		typename T2,
		typename T3,
		typename T4,
		typename T5,
		typename T6,
		typename T7,
		typename T8,
		typename T9>
      static Status	Hash(const T1&,
			     const T2&,
			     const T3&,
			     const T4&,
			     const T5&,
			     const T6&,
			     const T7&,
			     const T8&,
			     const T9&,
			     Digest&);
    };

  }
}

//
// ---------- templates -------------------------------------------------------
//

#include <elle/crypto/OneWay.hxx>

#endif
