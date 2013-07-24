#include <etoile/gear/Actor.hh>
#include <etoile/gear/Scope.hh>
#include <etoile/Etoile.hh>
#include <etoile/Exception.hh>

namespace etoile
{
  namespace gear
  {
//
// ---------- constructors & destructors --------------------------------------
//

    Actor::Actor(Etoile& etoile,
                 std::shared_ptr<Scope> scope):
      _etoile(etoile),
      scope(scope),
      state(Actor::StateClean)
    {
      // generate an identifier.
      if (this->identifier.Generate() == elle::Status::Error)
        return;

      this->_etoile.actor_add(*this);

      // add the actor to the scope's set.
      if (this->scope->Attach(this) == elle::Status::Error)
        return;
    }

    ///
    /// destructor.
    ///
    Actor::~Actor()
    {
      // remove the actor from the scope's set.
      if (this->scope->Detach(this) == elle::Status::Error)
        return;

      this->_etoile.actor_remove(*this);
    }

//
// ---------- methods ---------------------------------------------------------
//

    ///
    /// this method is called to indicate the operation being performed
    /// by the actor.
    ///
    /// let us recall that multiple actors operate on the same scope.
    ///
    /// therefore, since modifications are directly applied on the context
    /// when requested, an actor cannot perform modifications and finally
    /// decide to discard them.
    ///
    /// this method therefore checks that the operation is consistent
    /// regarding the previous requests i.e the actor's state.
    ///
    elle::Status        Actor::Operate(const Operation          operation)
    {
      // process the operation.
      switch (operation)
        {
        case OperationDiscard:
          {
            //
            // the actor is discarding the context.
            //
            // thus, the actor must never have performed a modification
            // on the context.
            //
            // note that there is a catch: if the actor has modified a
            // freshly created scope, it can actually be discarded. this
            // is made possible because (i) it is a common thing for
            // an application to create an object and finally realize it
            // does not have the permission to add it to a directory for
            // instance (ii) a created object cannot be accessed i.e loaded
            // by another actor since it is not referenced yet by a chemin.

            // check the scope's nature i.e does it have a chemin.
            if (!this->scope->chemin.empty())
              {
                //
                // the normal case: check that no modification has been
                // performed unless it is alone.
                //

                // check the state, especially if there are multiple actors.
                if (this->scope->actors.size() == 1)
                  {
                    // Nothing to do: we assume the actor can exceptionally
                    // modify an object and discard his modifications if
                    // he is alone because this is equivalent to the user
                    // re-modifying the object the other way around, ending
                    // up with the exact same original state.
                  }
                else
                  {
                    if (this->state != Actor::StateClean)
                      throw Exception("unable to discard previously performed "
                             "modifications");
                  }
              }
            else
              {
                //
                // the exceptional case: allow the actor to discard a
                // created scope.
                //

                // do nothing.
              }

            break;
          }
        case OperationStore:
          {
            //
            // the actor is storing the potentially modified context.
            //
            // there is nothing to do here: should the actor have updated
            // the context or not, the store operation can be requested.
            //

            break;
          }
        case OperationDestroy:
          {
            //
            // the actor is destroying the context, even though it
            // has been modified.
            //
            // as for the store operation, this operation is valid
            // no matter the actor's state.
            //

            break;
          }
        case OperationUnknown:
          {
            throw Exception
              (elle::sprintf("unable to process the closing operation '%u'\n",
                             operation));
          }
        }

      return elle::Status::Ok;
    }

//
// ---------- dumpable --------------------------------------------------------
//

    ///
    /// this method dumps an actor.
    ///
    elle::Status        Actor::Dump(const elle::Natural32       margin) const
    {
      elle::String      alignment(margin, ' ');

      std::cout << alignment << "[Actor]" << std::endl;

      // dump the identifier.
      if (this->identifier.Dump(margin + 2) == elle::Status::Error)
        throw Exception("unable to dump the identifier");

      // dump the scope's address.
      std::cout << alignment << elle::io::Dumpable::Shift
                << "[Scope] " << std::hex << this->scope << std::endl;

      // dump the state.
      std::cout << alignment << elle::io::Dumpable::Shift
                << "[State] " << std::dec << this->state << std::endl;

      return elle::Status::Ok;
    }

  }
}
