//
// ---------- header ----------------------------------------------------------
//
// project       infinit
//
// license       infinit (c)
//
// file          /home/mycure/infinit/etoile/core/Proof.cc
//
// created       julien quintard   [mon feb 16 21:42:37 2009]
// updated       julien quintard   [fri aug 21 22:31:28 2009]
//

//
// ---------- includes --------------------------------------------------------
//

#include <etoile/core/Proof.hh>

namespace etoile
{
  namespace core
  {

//
// ---------- constructors & destructors --------------------------------------
//

    ///
    /// this method initializes the object.
    ///
    Proof::Proof():
      index(0),
      voucher(NULL)
    {
    }

    ///
    /// this method reinitializes the object.
    ///
    Proof::~Proof()
    {
      // release the voucher if present.
      if (this->voucher != NULL)
	delete this->voucher;
    }

//
// ---------- entity ----------------------------------------------------------
//

    ///
    /// assign the address.
    ///
    Proof&		Proof::operator=(const Proof&	element)
    {
      // self-check.
      if (this == &element)
	return (*this);

      // recycle the address.
      if (this->Recycle<Proof>(&element) == StatusError)
	yield("unable to recycle the address", *this);

      return (*this);
    }

    ///
    /// this operator compares two objects.
    ///
    Boolean		Proof::operator==(const Proof&	element) const
    {
      // compare the addresses since one of them is null.
      if ((this->voucher == NULL) || (element.voucher == NULL))
	return ((this->index == element.index) &&
		(this->voucher == element.voucher));

      return ((this->index == element.index) &&
	      (*this->voucher == *element.voucher));
    }

    ///
    /// this operator compares two objects.
    ///
    Boolean		Proof::operator!=(const Proof&	element) const
    {
      return (!(*this == element));
    }

//
// ---------- dumpable --------------------------------------------------------
//

    ///
    /// this function dumps an proof object.
    ///
    Status		Proof::Dump(Natural32			margin) const
    {
      String		alignment(margin, ' ');
      String		shift(2, ' ');

      std::cout << alignment << "[Proof]" << std::endl;

      std::cout << alignment << shift << "[Index] "
		<< this->index << std::endl;

      if (this->voucher != NULL)
	{
	  if (this->voucher->Dump(margin + 2) == StatusError)
	    escape("unable to dump the voucher");
	}
      else
	{
	  std::cout << alignment << shift << "[Voucher] " << none << std::endl;
	}

      leave();
    }

//
// ---------- archivable ------------------------------------------------------
//

    ///
    /// this method serializes the proof object.
    ///
    Status		Proof::Serialize(Archive&		archive) const
    {
      // serialize the attributes.
      if (archive.Serialize(this->index) == StatusError)
	escape("unable to serialize the index");

      // serialize the voucher.
      if (this->voucher != NULL)
	{
	  if (archive.Serialize(*this->voucher) == StatusError)
	    escape("unable to serialize the voucher");
	}
      else
	{
	  // serialize 'none'.
	  if (archive.Serialize(none) == StatusError)
	    escape("unable to serialize 'none'");
	}

      leave();
    }

    ///
    /// this method extracts the proof object.
    ///
    Status		Proof::Extract(Archive&			archive)
    {
      Archive::Type	type;

      // extract the attributes.
      if (archive.Extract(this->index) == StatusError)
	escape("unable to extract the index");

      // fetch the next element's type.
      if (archive.Fetch(type) == StatusError)
	escape("unable to fetch the next element's type");

      if (type == Archive::TypeNull)
	{
	  // nothing to do, keep the digest to NULL.
	  if (archive.Extract(none) == StatusError)
	    escape("unable to extract null");
	}
      else
	{
	  // allocate a digest.
	  this->voucher = new Voucher;

	  // extract the voucher.
	  if (archive.Extract(*this->voucher) == StatusError)
	    escape("unable to extract the voucher");
	}

      leave();
    }

  }
}
