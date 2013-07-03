#ifndef HOLEFACTORY_HH
# define HOLEFACTORY_HH

# include <elle/fwd.hh>

# include <hole/Hole.hh>
# include <lune/fwd.hh>

namespace infinit
{
  std::unique_ptr<hole::Hole>
  hole_factory(lune::Descriptor const& descriptor,
               hole::storage::Storage& storage,
               elle::Passport const& passport,
               elle::Authority const& authority,
               std::vector<elle::network::Locus> const& members,
               std::string const& token = "");
}

#endif
