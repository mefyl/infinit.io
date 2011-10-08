//
// ---------- header ----------------------------------------------------------
//
// project       elle
//
// license       infinit
//
// author        julien quintard   [thu feb  4 15:20:31 2010]
//

//
// ---------- includes --------------------------------------------------------
//

#include <elle/network/Lane.hh>

#include <elle/standalone/Maid.hh>
#include <elle/standalone/Report.hh>

namespace elle
{
  namespace network
  {

//
// ---------- definitions -----------------------------------------------------
//

    ///
    /// definition of the container.
    ///
    Lane::Container		Lane::Porters;

//
// ---------- constructors & destructors --------------------------------------
//

    ///
    /// the default constructor.
    ///
    LanePorter::LanePorter(const Callback< Status,
					   Parameters<Door*> >&	callback):
      server(NULL),
      callback(callback)
    {
    }

    ///
    /// the destructor.
    ///
    LanePorter::~LanePorter()
    {
      // if there is a server, release it.
      if (this->server != NULL)
	this->server->deleteLater();
    }

//
// ---------- methods ---------------------------------------------------------
//

    //
    // porter
    //

    ///
    /// this method starts listening on the given name.
    ///
    Status		LanePorter::Create(const String&		name)
    {
      enter();

      // set the name.
      this->name = name;

      // allocate a new server.
      this->server = new ::QLocalServer;

      // start listening.
      if (this->server->listen(this->name.c_str()) == false)
	escape(this->server->errorString().toStdString().c_str());

      // connect the signals.
      if (this->connect(this->server, SIGNAL(newConnection()),
			this, SLOT(_accept())) == false)
	escape("unable to connect the signal");

      leave();
    }

    //
    // lane
    //

    ///
    /// this method initializes the lane.
    ///
    Status		Lane::Initialize()
    {
      enter();

      // nothing to do.

      leave();
    }

    ///
    /// this method cleans the lane.
    ///
    Status		Lane::Clean()
    {
      Lane::Scoutor	scoutor;

      enter();

      // go through the porters.
      for (scoutor = Lane::Porters.begin();
	   scoutor != Lane::Porters.end();
	   scoutor++)
	{
	  LanePorter*	porter = scoutor->second;

	  // delete the porter.
	  delete porter;
	}

      leave();
    }

    ///
    /// this method starts a server and waits for new connection. for
    /// every new connection, the Accept signal is generated which, in turn,
    /// creates a new door.
    ///
    /// note that callbacks are used because only a specific handler must
    /// be called. by relying on QT signals/slots (though it is not possible
    /// since the Lane class is static), all the slots registered on the
    /// signal would be triggered which is not want we want.
    ///
    Status		Lane::Listen(const String&		name,
				     const
				       Callback<
					 Status,
					 Parameters<Door*> >&	callback)
    {
      std::pair<Lane::Iterator, Boolean>	result;
      LanePorter*				porter;

      enterx(instance(porter));

      // check if this name is not already listened on.
      if (Lane::Locate(name) == StatusTrue)
	escape("this name seems to have already been registered");

      // allocate a new porter.
      porter = new LanePorter(callback);

      // create the porter.
      if (porter->Create(name) == StatusError)
	escape("unable to create the porter");

      // insert the porter in the container.
      result = Lane::Porters.insert(
	         std::pair<const String, LanePorter*>(name, porter));

      // check if the insertion was successful.
      if (result.second == false)
	escape("unable to insert the porter in the container");

      // stop tracking porter.
      waive(porter);

      leave();
    }

    ///
    /// this method blocks the given name by deleting the associated
    /// porter.
    ///
    Status		Lane::Block(const String&		name)
    {
      Lane::Iterator	iterator;
      LanePorter*	porter;

      enter();

      // locate the porter.
      if (Lane::Locate(name, &iterator) == StatusFalse)
	escape("unable to locate the given porter");

      // retrieve the porter.
      porter = iterator->second;
      
      // delete the porter.
      delete porter;

      // remove the entry from the container.
      Lane::Porters.erase(iterator);

      leave();
    }

    ///
    /// this method returns the porter associated with the given name.
    ///
    Status		Lane::Retrieve(const String&		name,
				       LanePorter*&		porter)
    {
      Lane::Iterator	iterator;

      enter();

      // locate the porter.
      if (Lane::Locate(name, &iterator) == StatusFalse)
	escape("unable to locate the given porter");

      // retrieve the porter.
      porter = iterator->second;

      leave();
    }

    ///
    /// this method tries to locate the porter associated with the given
    /// name and returns true if found.
    ///
    Status		Lane::Locate(const String&		name,
				     Iterator*			iterator)
    {
      Lane::Iterator	i;

      enter();

      // try to locate the porter.
      if ((i = Lane::Porters.find(name)) != Lane::Porters.end())
	{
	  if (iterator != NULL)
	    *iterator = i;

	  true();
	}

      false();
    }

//
// ---------- dumpable --------------------------------------------------------
//

    //
    // porter
    //

    ///
    /// this method dumps the internals of a porter.
    ///
    Status		LanePorter::Dump(const Natural32	margin) const
    {
      String		alignment(margin, ' ');

      enter();

      std::cout << alignment << "[Porter]" << std::endl;

      // dump the server address.
      std::cout << alignment << Dumpable::Shift << "[Server] "
		<< std::hex << this->server << std::endl;

      // dump the platform-specific path.
      std::cout << alignment << Dumpable::Shift << "[Path] "
		<< this->server->fullServerName().toStdString() << std::endl;

      // dump the callback.
      if (this->callback.Dump(margin + 2) == StatusError)
	escape("unable to dump the callback");

      leave();
    }

    //
    // lane
    //

    ///
    /// this method dumps the table of porters.
    ///
    Status		Lane::Show(const Natural32		margin)
    {
      String		alignment(margin, ' ');
      Lane::Scoutor	scoutor;

      enter();

      std::cout << alignment << "[Lane]" << std::endl;

      // dump the porters table.
      for (scoutor = Lane::Porters.begin();
	   scoutor != Lane::Porters.end();
	   scoutor++)
	{
	  LanePorter*	porter = scoutor->second;

	  // dump the porter.
	  if (porter->Dump(margin + 2) == StatusError)
	    escape("unable to dump the porter");
	}

      leave();
    }

//
// ---------- callbacks -------------------------------------------------------
//

    ///
    /// this callback is triggered whenever a new conncetion is made.
    ///
    Status		LanePorter::Accept()
    {
      ::QLocalSocket*	socket;
      Door*		door;

      enterx(instance(door));

      // retrieve the socket from the server.
      if ((socket = this->server->nextPendingConnection()) == NULL)
	escape(this->server->errorString().toStdString().c_str());

      // allocate a new door to this lane.
      door = new Door;

      // create a door with the specific socket.
      if (door->Create(socket) == StatusError)
	escape("unable to create the door");

      // call the callback.
      if (this->callback.Call(door) == StatusError)
	escape("an error occured in the callback");

      // stop tracking door as it has been handed to the callback.
      waive(door);

      leave();
    }

//
// ---------- slots -----------------------------------------------------------
//

    ///
    /// this slot is triggered whenever a connection is being made on the
    /// porter's locus.
    ///
    void		LanePorter::_accept()
    {
      Closure< Status,
	       Parameters<> >	closure(Callback<>::Infer(
					  &LanePorter::Accept, this));

      enter();

      // spawn a fiber.
      if (Fiber::Spawn(closure) == StatusError)
	yield(_(), "unable to spawn a fiber");

      release();
    }

 }
}
