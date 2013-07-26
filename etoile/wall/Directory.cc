#include <elle/log.hh>

#include <reactor/scheduler.hh>

#include <etoile/Etoile.hh>
#include <etoile/wall/Directory.hh>
#include <etoile/gear/Identifier.hh>
#include <etoile/gear/Scope.hh>
#include <etoile/gear/Directory.hh>
#include <etoile/gear/Operation.hh>
#include <etoile/gear/Guard.hh>
#include <etoile/automaton/Directory.hh>
#include <etoile/automaton/Rights.hh>
#include <etoile/journal/Journal.hh>
#include <etoile/path/Path.hh>
#include <etoile/shrub/Shrub.hh>
#include <etoile/Exception.hh>

#include <nucleus/proton/Address.hh>
#include <nucleus/neutron/Index.hh>
#include <nucleus/neutron/Size.hh>
#include <nucleus/neutron/Range.hh>
#include <nucleus/neutron/Entry.hh>

ELLE_LOG_COMPONENT("infinit.etoile.wall.Directory");

namespace etoile
{
  namespace wall
  {

//
// ---------- methods ---------------------------------------------------------
//

    gear::Identifier
    Directory::create(etoile::Etoile& etoile)
    {
      ELLE_TRACE_FUNCTION("");

      std::shared_ptr<gear::Scope> scope = etoile.scope_supply();
      gear::Guard guard(scope);
      gear::Directory* context;
      gear::Identifier identifier;

      // allocate an actor.
      guard.actor(new gear::Actor(etoile, scope));

      // Declare a critical section.
      {
        reactor::Lock lock(scope->mutex.write());

        // return the identifier.
        identifier = guard.actor()->identifier;

        // retrieve the context.
        if (scope->Use(etoile, context) == elle::Status::Error)
          throw Exception("unable to retrieve the context");

        // apply the create automaton on the context.
        if (automaton::Directory::Create(*context) == elle::Status::Error)
          throw Exception("unable to create the directory");

        // set the actor's state.
        guard.actor()->state = gear::Actor::StateUpdated;

        ELLE_DEBUG("returning identifier %s on %s", identifier, *scope);

        // waive the scope.
        if (guard.Release() == elle::Status::Error)
          throw Exception("unable to release the guard");
      }

      return (identifier);
    }

    gear::Identifier
    Directory::load(etoile::Etoile& etoile,
                    path::Chemin const& chemin)
    {
      ELLE_TRACE_FUNCTION(chemin);

      std::shared_ptr<gear::Scope> scope =
        etoile.scope_acquire(chemin);
      gear::Guard guard(scope);
      gear::Directory* context;
      gear::Identifier identifier;

      // allocate an actor.
      guard.actor(new gear::Actor(etoile, scope));

      // Declare a critical section.
      {
        reactor::Lock lock(scope->mutex.write());

        // return the identifier.
        identifier = guard.actor()->identifier;

        // retrieve the context.
        if (scope->Use(etoile, context) == elle::Status::Error)
          throw Exception("unable to retrieve the context");

        // locate the object based on the chemin.
        context->location = chemin.locate();

        ELLE_DEBUG("about to load the directory from the location '%s'",
                   context->location);

        try
          {
            // apply the load automaton on the context.
            if (automaton::Directory::Load(*context) == elle::Status::Error)
              throw Exception("unable to load the directory");
          }
        catch (elle::Exception const&)
          {
            Object::reload<gear::Directory>(etoile, *scope);
          }

        ELLE_DEBUG("returning identifier %s on %s", identifier, *scope);

        // waive the scope.
        if (guard.Release() == elle::Status::Error)
          throw Exception("unable to release the guard");
      }

      return (identifier);
    }

    void
    Directory::add(etoile::Etoile& etoile,
                   gear::Identifier const& parent,
                   std::string const& name,
                   gear::Identifier const& child)
    {
      ELLE_TRACE_FUNCTION(parent, name, child);

      gear::Actor* child_actor = etoile.actor_get(child);
      std::shared_ptr<gear::Scope> child_scope = child_actor->scope;

      gear::Actor* parent_actor = etoile.actor_get(parent);
      std::shared_ptr<gear::Scope> parent_scope = parent_actor->scope;

      // Declare a critical section.
      {
        reactor::Lock lock(parent_scope->mutex.write());

        // retrieve the context.
        gear::Directory* directory;
        gear::Object* object;

        if (child_scope->Use(etoile, object) == elle::Status::Error)
          throw Exception("unable to retrieve the context");

        if (parent_scope->Use(etoile, directory) == elle::Status::Error)
          throw Exception("unable to retrieve the context");

        nucleus::proton::Address address = object->location.address();

        // apply the add automaton on the context.
        if (automaton::Directory::Add(*directory,
                                      name,
                                      address) == elle::Status::Error)
          throw Exception("unable to add the directory entry");

        // set the actor's state.
        parent_actor->state = gear::Actor::StateUpdated;
      }
    }

    nucleus::neutron::Entry const*
    Directory::lookup(Etoile& etoile,
                      const gear::Identifier& identifier,
                      const std::string& name)
    {
      ELLE_TRACE_FUNCTION(identifier, name);

      gear::Actor* actor = etoile.actor_get(identifier);
      std::shared_ptr<gear::Scope> scope = actor->scope;
      gear::Directory* context;

      {
        reactor::Lock lock(scope->mutex);
        if (scope->Use(etoile, context) == elle::Status::Error)
          throw Exception("unable to retrieve the context");
        nucleus::neutron::Entry const* entry;
        if (automaton::Directory::Lookup(*context,
                                         name,
                                         entry) == elle::Status::Error)
          throw Exception("unable to lookup the directory entry");
        return entry;
      }
    }

    nucleus::neutron::Range<nucleus::neutron::Entry>
    Directory::consult(etoile::Etoile& etoile,
                       gear::Identifier const& identifier,
                       nucleus::neutron::Index const& index,
                       nucleus::neutron::Index const& size)
    {
      ELLE_TRACE_FUNCTION(identifier, index, size);

      gear::Actor* actor = etoile.actor_get(identifier);
      std::shared_ptr<gear::Scope> scope = actor->scope;
      gear::Directory* context;

      nucleus::neutron::Range<nucleus::neutron::Entry> range;

      // Declare a critical section.
      {
        reactor::Lock lock(scope->mutex);

        // retrieve the context.
        if (scope->Use(etoile, context) == elle::Status::Error)
          throw Exception("unable to retrieve the context");

        // apply the consult automaton on the context.
        if (automaton::Directory::Consult(*context,
                                          index,
                                          size,
                                          range) == elle::Status::Error)
          throw Exception("unable to consult the directory entries");
      }

      return (range);
    }

    ///
    /// this method renames a directory entry.
    ///
    elle::Status
    Directory::Rename(etoile::Etoile& etoile,
                      const gear::Identifier& identifier,
                      const std::string& from,
                      const std::string& to)
    {
      ELLE_TRACE_FUNCTION(identifier, from, to);

      gear::Actor* actor = etoile.actor_get(identifier);
      std::shared_ptr<gear::Scope> scope = actor->scope;
      gear::Directory*  context;

      ELLE_TRACE("lock the actor's scope for writing")
      {
        reactor::Lock lock(scope->mutex.write());

        path::Venue venue(scope->chemin.venue());
        ELLE_TRACE("old venue: %s", venue);

        ELLE_TRACE("retrieve the context")
          if (scope->Use(etoile, context) == elle::Status::Error)
            throw Exception("unable to retrieve the context");

        ELLE_TRACE("apply the rename automaton on the context")
          if (automaton::Directory::Rename(*context,
                                           from,
                                           to) == elle::Status::Error)
            throw Exception("unable to rename the directory entry");

        actor->state = gear::Actor::StateUpdated;

        path::Route route_from(scope->chemin.route(), from);
        path::Route route_to(scope->chemin.route(), to);

        // Update the scopes should some reference the renamed entry.
        //
        // Indeed, let us imagine the following scenario. a file
        // /tmp/F1 is created. this file is opened by two actors A and
        // B. then, actor A renames the file into /tmp/F2.
        //
        // Later one, a actor, say C, re-creates and releases /tmp/F1.
        // then C loads /tmp/F1. since the original scope for /tmp/F1
        // has not been updated and since actors remain, i.e A and B,
        // the original scope is retrieved instead of the new one.
        //
        // For this reason, the scopes must be updated.
        {
          path::Chemin chemin_from(route_from, venue);
          path::Chemin chemin_to(route_to, venue);

          // Update the scope so as to update all the scopes whose
          // chemins are now inconsistent---i.e referencing the old
          // chemin _chemin.from_.
          ELLE_TRACE("update the scope")
            etoile.scope_update(chemin_from, chemin_to);
        }

        //
        // invalidate the _from_ and _to_ routes from the shrub.
        //
        {
          // evict the route from the shrub.
          etoile.shrub().evict(route_from);
          etoile.shrub().evict(route_to);
        }
      }

      return elle::Status::Ok;
    }

    ///
    /// this method removes a directory entry.
    ///
    elle::Status
    Directory::Remove(etoile::Etoile& etoile,
                      const gear::Identifier& identifier,
                      const std::string& name)
    {
      ELLE_TRACE_FUNCTION(identifier, name);

      gear::Actor* actor = etoile.actor_get(identifier);
      std::shared_ptr<gear::Scope> scope = actor->scope;
      gear::Directory*  context;

      // Declare a critical section.
      {
        reactor::Lock lock(scope->mutex.write());

        // retrieve the context.
        if (scope->Use(etoile, context) == elle::Status::Error)
          throw Exception("unable to retrieve the context");

        // apply the remove automaton on the context.
        if (automaton::Directory::Remove(*context,
                                         name) == elle::Status::Error)
          throw Exception("unable to remove the directory entry");

        // set the actor's state.
        actor->state = gear::Actor::StateUpdated;

        // Invalidate the route in the shrub.
        {
          path::Route route(scope->chemin.route(), name);
          etoile.shrub().evict(route);
        }
      }

      return elle::Status::Ok;
    }

    void
    Directory::discard(etoile::Etoile& etoile,
                       gear::Identifier const& identifier)
    {
      ELLE_TRACE_FUNCTION(identifier);

      gear::Actor* actor = etoile.actor_get(identifier);
      std::shared_ptr<gear::Scope> scope = actor->scope;
      gear::Directory* context;

      gear::Guard guard(actor);

      // Declare a critical section.
      {
        reactor::Lock lock(scope->mutex.write());

        // retrieve the context.
        if (scope->Use(etoile, context) == elle::Status::Error)
          throw Exception("unable to retrieve the context");

        // check the permissions before performing the operation in
        // order not to alter the scope should the operation not be
        // allowed.
        if (automaton::Rights::Operate(
              *context,
              gear::OperationDiscard) == elle::Status::Error)
          throw Exception("the user does not seem to have the necessary permission for "
                 "discarding this directory");

        // specify the closing operation performed by the actor.
        if (actor->Operate(gear::OperationDiscard) == elle::Status::Error)
          throw Exception("this operation cannot be performed by this actor");

        // delete the actor.
        guard.actor(nullptr);

        // specify the closing operation performed on the scope.
        if (scope->Operate(gear::OperationDiscard) == elle::Status::Error)
          throw Exception("unable to specify the operation being performed "
                 "on the scope");

        // trigger the shutdown.
        try
          {
            if (scope->Shutdown() == elle::Status::Error)
              throw Exception("unable to trigger the shutdown");
          }
        catch (elle::Exception const& e)
          {
            ELLE_ERR("unable to shutdown the scope: '%s'", e.what());
            return;
          }
      }

      // depending on the context's state.
      switch (context->state)
        {
        case gear::Context::StateDiscarded:
        case gear::Context::StateStored:
        case gear::Context::StateDestroyed:
          {
            //
            // if the directory has been sealed, i.e there is no more actor
            // operating on it, record it in the journal.
            //

            // record the scope in the journal.
            if (journal::Journal::Record(std::move(scope)) == elle::Status::Error)
              throw Exception("unable to record the scope in the journal");

            break;
          }
        default:
          {
            //
            // otherwise, some actors are probably still working on it.
            //

            break;
          }
        }
    }

    void
    Directory::store(etoile::Etoile& etoile,
                     gear::Identifier const& identifier)
    {
      ELLE_TRACE_FUNCTION(identifier);

      gear::Actor* actor = etoile.actor_get(identifier);
      std::shared_ptr<gear::Scope> scope = actor->scope;
      gear::Directory* context;

      gear::Guard guard(actor);

      // Declare a critical section.
      {
        reactor::Lock lock(scope->mutex.write());

        // retrieve the context.
        if (scope->Use(etoile, context) == elle::Status::Error)
          throw Exception("unable to retrieve the context");

        // check the permissions before performing the operation in
        // order not to alter the scope should the operation not be
        // allowed.
        if (automaton::Rights::Operate(
              *context,
              gear::OperationStore) == elle::Status::Error)
          throw Exception("the user does not seem to have the necessary permission for "
                 "storing this directory");

        // specify the closing operation performed by the actor.
        if (actor->Operate(gear::OperationStore) == elle::Status::Error)
          throw Exception("this operation cannot be performed by this actor");

        // delete the actor.
        guard.actor(nullptr);

        // specify the closing operation performed on the scope.
        if (scope->Operate(gear::OperationStore) == elle::Status::Error)
          throw Exception("unable to specify the operation being performed "
                 "on the scope");

        // trigger the shutdown.
        try
          {
            if (scope->Shutdown() == elle::Status::Error)
              throw Exception("unable to trigger the shutdown");
          }
        catch (elle::Exception const& e)
          {
            ELLE_ERR("unable to shutdown the scope: '%s'", e.what());
            return;
          }
      }

      // depending on the context's state.
      switch (context->state)
        {
        case gear::Context::StateDiscarded:
        case gear::Context::StateStored:
        case gear::Context::StateDestroyed:
          {
            //
            // if the directory has been sealed, i.e there is no more actor
            // operating on it, record it in the journal.
            //

            // record the scope in the journal.
            if (journal::Journal::Record(std::move(scope)) == elle::Status::Error)
              throw Exception("unable to record the scope in the journal");

            break;
          }
        default:
          {
            //
            // otherwise, some actors are probably still working on it.
            //

            break;
          }
        }
    }

    ///
    /// this method destroys a directory.
    ///
    elle::Status
    Directory::Destroy(etoile::Etoile& etoile,
                       const gear::Identifier& identifier)
    {
      ELLE_TRACE_FUNCTION(identifier);

      gear::Actor* actor = etoile.actor_get(identifier);
      std::shared_ptr<gear::Scope> scope = actor->scope;
      gear::Directory*          context;

      gear::Guard               guard(actor);

      // Declare a critical section.
      {
        reactor::Lock lock(scope->mutex.write());

        // retrieve the context.
        if (scope->Use(etoile, context) == elle::Status::Error)
          throw Exception("unable to retrieve the context");

        // check the permissions before performing the operation in
        // order not to alter the scope should the operation not be
        // allowed.
        if (automaton::Rights::Operate(
              *context,
              gear::OperationDestroy) == elle::Status::Error)
          throw Exception("the user does not seem to have the necessary permission for "
                 "destroying this directory");

        // specify the closing operation performed by the actor.
        if (actor->Operate(gear::OperationDestroy) == elle::Status::Error)
          throw Exception("this operation cannot be performed by this actor");

        // delete the actor.
        guard.actor(nullptr);

        // specify the closing operation performed on the scope.
        if (scope->Operate(gear::OperationDestroy) == elle::Status::Error)
          throw Exception("unable to specify the operation being performed "
                 "on the scope");

        // trigger the shutdown.
        try
          {
            if (scope->Shutdown() == elle::Status::Error)
              throw Exception("unable to trigger the shutdown");
          }
        catch (elle::Exception const& e)
          {
            ELLE_ERR("unable to shutdown the scope: '%s'", e.what());
            return elle::Status::Ok;
          }
      }

      // depending on the context's state.
      switch (context->state)
        {
        case gear::Context::StateDiscarded:
        case gear::Context::StateStored:
        case gear::Context::StateDestroyed:
          {
            //
            // if the directory has been sealed, i.e there is no more actor
            // operating on it, record it in the journal.
            //

            // record the scope in the journal.
            if (journal::Journal::Record(std::move(scope)) == elle::Status::Error)
              throw Exception("unable to record the scope in the journal");

            break;
          }
        default:
          {
            //
            // otherwise, some actors are probably still working on it.
            //

            break;
          }
        }

      return elle::Status::Ok;
    }
  }
}
