#ifndef HORIZON_HORIZON_HH
# define HORIZON_HORIZON_HH

# include <elle/types.hh>

# include <reactor/scheduler.hh>

# include <lune/Dictionary.hh>

# include <sys/types.h>
# include <pwd.h>

# include <hole/fwd.hh>

# include <etoile/fwd.hh>

///
/// this namespace contains several implementations related to the
/// operating system as the system call interface changes.
///
namespace horizon
{

  /*-----------------------------.
  | Global Hole instance (FIXME) |
  `-----------------------------*/
  hole::Hole&
  hole();
  void
  hole(hole::Hole* hole);
  etoile::Etoile&
  etoile();
  void
  etoile(etoile::Etoile* etoile);

  ///
  /// this class contains general-purpose methods for initializing and
  /// cleaning the horizon component.
  ///
  class Horizon
  {
  public:
    //
    // static methods
    //
    static void
    Initialize(reactor::Scheduler& sched);
    static elle::Status         Clean();

    //
    // static attributes
    //
    struct                    Somebody
    {
      static uid_t            UID;
      static gid_t            GID;
    };

    static lune::Dictionary   Dictionary;
  };

}

#endif
