#include <horizon/Horizon.hh>

#include <elle/system/platform.hh>

#include <etoile/Etoile.hh>

#include <Infinit.hh>

#if defined(INFINIT_LINUX)
# include <horizon/linux/Linux.hh>
#elif defined(INFINIT_MACOSX)
# include <horizon/macosx/MacOSX.hh>
#elif defined(INFINIT_WINDOWS)
# include <horizon/windows/Windows.hh>
#else
# error "unsupported platform"
#endif

namespace horizon
{
    /*-----------------------------.
    | Global Hole instance (FIXME) |
    `-----------------------------*/

    static
    hole::Hole*&
    _hole()
    {
      static hole::Hole* hole(nullptr);
      return hole;
    }

    hole::Hole&
    hole()
    {
      assert(_hole());
      return *_hole();
    }

    void
    hole(hole::Hole* hole)
    {
      assert(!_hole() || !hole);
      _hole() = hole;
    }

    static
    etoile::Etoile*&
    _etoile()
    {
      static etoile::Etoile* etoile(nullptr);
      return etoile;
    }

    etoile::Etoile&
    etoile()
    {
      assert(_etoile());
      return *_etoile();
    }

    void
    etoile(etoile::Etoile* etoile)
    {
      assert(!_etoile() || !etoile);
      _etoile() = etoile;
    }

//
// ---------- definitions -----------------------------------------------------
//

    ///
    /// this variable contains the UID of the 'somebody' user, user which
    /// is used whenever the system cannot map the Infinit user on a local
    /// user.
    ///
    uid_t                               Horizon::Somebody::UID;

    ///
    /// this variable contains the GID of the 'somebody' group.
    ///
    gid_t                               Horizon::Somebody::GID;

    ///
    /// this varaible contains the mappings between local user/group
    /// identities and Infinit identities.
    ///
    lune::Dictionary                    Horizon::Dictionary;

//
// ---------- static methods --------------------------------------------------
//

  void
  Horizon::Initialize(reactor::Scheduler& sched)
  {
    //
    // initialize the 'somebody' entity.
    //
    {
      struct ::passwd*        passwd;

      // retrieve the passwd structure related to the user 'somebody'.
      // if nullptr, try to fallback to 'nobody'.
      if (((passwd = ::getpwnam("somebody")) == nullptr) &&
          ((passwd = ::getpwnam("nobody")) == nullptr))
        throw elle::Exception("it seems that the user 'somebody' does not exist");

      // set the uid and gid.
      Horizon::Somebody::UID = passwd->pw_uid;
      Horizon::Somebody::GID = passwd->pw_gid;
    }

    //
    // load the user/group maps which will be used to translate Infinit
    // user/group identifiers into local identifiers.
    //
    {
      // if the dictionary exist.
      if (lune::Dictionary::exists(Infinit::User) == true)
        Horizon::Dictionary.load(Infinit::User);
    }

#if defined(INFINIT_LINUX)
    linux::Linux::Initialize(sched);
#elif defined(INFINIT_MACOSX)
    {
      // initialize the MacOS X implementation.
      if (macosx::MacOSX::Initialize() == elle::Status::Error)
        throw elle::Exception("unable to initialize the MacOS X implementation");
    }
#elif defined(INFINIT_WINDOWS)
    {
      // initialize the Windows implementation.
      if (windows::Windows::Initialize() == elle::Status::Error)
        throw elle::Exception("unable to initialize the Windows implementation");
    }
#else
# error "unsupported platform"
#endif
  }

  ///
  /// this method cleans the horizon.
  ///
  elle::Status          Horizon::Clean()
  {
#if defined(INFINIT_LINUX)
    {
      // clean the Linux implementation.
      if (linux::Linux::Clean() == elle::Status::Error)
        throw elle::Exception("unable to clean the Linux implementation");
    }
#elif defined(INFINIT_MACOSX)
    {
      // clean the MacOS X implementation.
      if (macosx::MacOSX::Clean() == elle::Status::Error)
        throw elle::Exception("unable to clean the MacOS X implementation");
    }
#elif defined(INFINIT_WINDOWS)
    {
      // clean the Windows implementation.
      if (windows::Windows::Clean() == elle::Status::Error)
        throw elle::Exception("unable to clean the Windows implementation");
    }
#else
# error "unsupported platform"
#endif

    return elle::Status::Ok;
  }

}
