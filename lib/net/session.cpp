/* Copyright (c) 2005-2008, Stefan Eilemann <eile@equalizergraphics.com> 
   All rights reserved. */

#include "session.h"

#include "barrier.h"
#include "command.h"
#include "connection.h"
#include "connectionDescription.h"
#include "log.h"
#include "packets.h"
#include "session.h"

#ifndef WIN32
#  include <alloca.h>
#endif

using namespace eqBase;
using namespace std;

namespace eqNet
{
#define MIN_ID_RANGE 1024

Session::Session( const bool threadSafe )
        : _requestHandler( threadSafe )
        , _id(EQ_ID_INVALID)
        , _isMaster(false)
        , _masterPool( IDPool::MAX_CAPACITY )
        , _localPool( 0 )
        , _instanceIDs( 0 ) 
{
    EQINFO << "New Session @" << (void*)this << endl;
}

Session::~Session()
{
    EQINFO << "Delete Session @" << (void*)this << endl;

    _localNode = 0;
    _server    = 0;
        
    if( !_objects.empty( ))
    {
        if( eqBase::Log::level >= eqBase::LOG_WARN ) // OPT
        {
            EQWARN << _objects.size()
                   << " attached objects in destructor" << endl;

            for( IDHash< vector<Object*> >::const_iterator i = _objects.begin();
                 i != _objects.end(); ++i )
            {
                const vector<Object*>& objects = i->second;
                EQWARN << "  " << objects.size() << " objects with id " 
                       << i->first << endl;

                for( vector<Object*>::const_iterator j = objects.begin();
                     j != objects.end(); ++j )
                {
                    const Object* object = *j;
                    EQINFO << "    object type " << typeid(*object).name() 
                           << endl;
                }
            }
        }
    }
    _objects.clear();
}


void Session::setLocalNode( RefPtr< Node > node )
{
    _localNode = node;
    if( !_localNode.isValid( ))
        return; // TODO deregister command functions?

    CommandQueue& queue = node->getCommandThreadQueue();

    registerCommand( CMD_SESSION_GEN_IDS, 
                     CommandFunc<Session>( this, &Session::_cmdGenIDs ),
                     queue );
    registerCommand( CMD_SESSION_GEN_IDS_REPLY,
                     CommandFunc<Session>( this, &Session::_cmdGenIDsReply ),
                     queue );
    registerCommand( CMD_SESSION_SET_ID_MASTER,
                     CommandFunc<Session>( this, &Session::_cmdSetIDMaster ),
                     queue );
    registerCommand( CMD_SESSION_GET_ID_MASTER, 
                     CommandFunc<Session>( this, &Session::_cmdGetIDMaster ),
                     queue );
    registerCommand( CMD_SESSION_GET_ID_MASTER_REPLY,
                  CommandFunc<Session>( this, &Session::_cmdGetIDMasterReply ),
                     queue );
    registerCommand( CMD_SESSION_ATTACH_OBJECT,
                     CommandFunc<Session>( this, &Session::_cmdAttachObject ),
                     queue );
    registerCommand( CMD_SESSION_DETACH_OBJECT,
                     CommandFunc<Session>( this, &Session::_cmdDetachObject ),
                     queue );
    registerCommand( CMD_SESSION_MAP_OBJECT,
                     CommandFunc<Session>( this, &Session::_cmdMapObject ),
                     queue );
    registerCommand( CMD_SESSION_MAP_OBJECT,
                     CommandFunc<Session>( this, &Session::_cmdMapObject ),
                     queue );
    registerCommand( CMD_SESSION_SUBSCRIBE_OBJECT,
                   CommandFunc<Session>( this, &Session::_cmdSubscribeObject ),
                     queue );
    registerCommand( CMD_SESSION_SUBSCRIBE_OBJECT_SUCCESS,
            CommandFunc<Session>( this, &Session::_cmdSubscribeObjectSuccess ),
                     queue );
    registerCommand( CMD_SESSION_SUBSCRIBE_OBJECT_REPLY,
              CommandFunc<Session>( this, &Session::_cmdSubscribeObjectReply ),
                     queue );
    registerCommand( CMD_SESSION_UNSUBSCRIBE_OBJECT,
                 CommandFunc<Session>( this, &Session::_cmdUnsubscribeObject ),
                     queue );
}

//---------------------------------------------------------------------------
// identifier generation
//---------------------------------------------------------------------------
uint32_t Session::genIDs( const uint32_t range )
{
    if( _isMaster && _server->inCommandThread( ))
        return _masterPool.genIDs( range );

    uint32_t id = _localPool.genIDs( range );
    if( id != EQ_ID_INVALID )
        return id;

    SessionGenIDsPacket packet;
    packet.requestID = _requestHandler.registerRequest();
    packet.range     = EQ_MAX(range, MIN_ID_RANGE);

    send( packet );
    _requestHandler.waitRequest( packet.requestID, id );

    if( id == EQ_ID_INVALID || range >= MIN_ID_RANGE )
        return id;

    // We allocated more IDs than requested - let the pool handle the details
    _localPool.freeIDs( id, MIN_ID_RANGE );
    return _localPool.genIDs( range );
}

void Session::freeIDs( const uint32_t start, const uint32_t range )
{
    _localPool.freeIDs( start, range );
    // TODO: could return IDs to master sometimes ?
}

//---------------------------------------------------------------------------
// identifier master node mapping
//---------------------------------------------------------------------------
void Session::setIDMaster( const uint32_t start, const uint32_t range, 
                           const NodeID& master )
{
    IDMasterInfo info = { start, start+range, master };
    _idMasterInfos.push_back( info );

    if( _isMaster )
        return;

    SessionSetIDMasterPacket packet;
    packet.start    = start;
    packet.range    = range;
    packet.masterID = master;

    send( packet );
}

const NodeID& Session::_pollIDMaster( const uint32_t id ) const 
{
    for( vector<IDMasterInfo>::const_iterator i = _idMasterInfos.begin();
         i != _idMasterInfos.end(); ++i )
    {
        const IDMasterInfo& info = *i;
        if( id >= info.start && id < info.end )
            return info.master;
    }
    return NodeID::ZERO;
}

RefPtr<Node> Session::_pollIDMasterNode( const uint32_t id ) const
{
    const NodeID& nodeID = _pollIDMaster( id );
    return _localNode->getNode( nodeID );
}

const NodeID& Session::getIDMaster( const uint32_t id )
{
    const NodeID& master = _pollIDMaster( id );
        
    if( master != NodeID::ZERO || _isMaster )
        return master;

    // ask session master instance
    SessionGetIDMasterPacket packet;
    packet.requestID = _requestHandler.registerRequest();
    packet.id        = id;

    send( packet );
    _requestHandler.waitRequest( packet.requestID );
    EQLOG( LOG_OBJECTS ) << "Master node for id " << id << ": " 
        << _pollIDMaster( id ) << endl;
    return _pollIDMaster( id );
}

//---------------------------------------------------------------------------
// object mapping
//---------------------------------------------------------------------------
struct MapObjectData
{
    Object*      object;
    uint32_t     objectID;
    RefPtr<Node> master;
};

void Session::attachObject( Object* object, const uint32_t id )
{
    EQASSERT( object );

    if( _localNode->inCommandThread( ))
    {
        CHECK_THREAD( _receiverThread );

        _instanceIDs = ( _instanceIDs + 1 ) % IDPool::MAX_CAPACITY;
        object->attachToSession( id, _instanceIDs, this );

        _objectsMutex.set();
        vector<Object*>& objects = _objects[ id ];
        objects.push_back( object );
        _objectsMutex.unset();

        getLocalNode()->flushCommands(); // redispatch pending commands
        EQLOG( LOG_OBJECTS ) << "Attached " << typeid( *object ).name()
                             << " to id " << id << endl;
        return;
    }
    // else

    SessionAttachObjectPacket packet;
    packet.objectID  = id;
    packet.requestID = _requestHandler.registerRequest( object );
    _sendLocal( packet );
    _requestHandler.waitRequest( packet.requestID );
}

void Session::detachObject( Object* object )
{
    EQASSERT( object );
    if( _localNode->inCommandThread( ))
    {
        CHECK_THREAD( _receiverThread );

        const uint32_t id = object->getID();
        EQASSERT( id != EQ_ID_INVALID );
        EQASSERT( _objects.find( id ) != _objects.end( ));

        EQLOG( LOG_OBJECTS ) << "Detach " << typeid( *object ).name() 
                             << " from id " << id << endl;

        vector<Object*>&          objects = _objects[ id ];
        vector<Object*>::iterator iter    = find( objects.begin(),objects.end(),
                                                  object );
        EQASSERT( iter != objects.end( ));
        objects.erase( iter );

        if( objects.empty( ))
        {
            _objectsMutex.set();
            _objects.erase( id );
            _objectsMutex.unset();
        }

        EQASSERT( object->_instanceID != EQ_ID_INVALID );

        object->_id         = EQ_ID_INVALID;
        object->_instanceID = EQ_ID_INVALID;
        object->_session    = 0;
        // Slave objects keep their cm to be able to sync queued versions
        if( object->isMaster( )) 
            object->_setChangeManager( ObjectCM::ZERO );
        return;
    }
    // else

    SessionDetachObjectPacket packet;
    packet.requestID = _requestHandler.registerRequest();
    packet.objectID  = object->getID();
    packet.objectInstanceID  = object->getInstanceID();

    _sendLocal( packet );
    _requestHandler.waitRequest( packet.requestID );
}

bool Session::mapObject( Object* object, const uint32_t id )
{
    EQASSERT( object );

    EQLOG( LOG_OBJECTS ) << "Mapping " << typeid( *object ).name() << " to id "
                         << id << endl;

    EQASSERT( object->_id == EQ_ID_INVALID );
    EQASSERT( id != EQ_ID_INVALID );
    EQASSERT( !_localNode->inCommandThread( ));
        
    RefPtr<Node> master;
    if( !object->isMaster( ))
    {
        EQASSERTINFO( object->_cm == ObjectCM::ZERO, typeid( *object ).name( ));

        const NodeID& masterID = getIDMaster( id );
        if( masterID == NodeID::ZERO )
        {
            EQWARN << "Can't find master node for object id " << id << endl;
            return false;
        }

        master = _localNode->connect( masterID, getServer( ));
        if( !master || master->getState() == Node::STATE_STOPPED )
        {
            EQWARN << "Can't connect master node with id " << masterID
                   << " for object id " << id << endl;
            return false;
        }
    }

    MapObjectData data = { object, id, master };
    SessionMapObjectPacket packet;
    packet.requestID = _requestHandler.registerRequest( &data );

    _sendLocal( packet );
    bool result = false;
    _requestHandler.waitRequest( packet.requestID, result );

    // apply first instance data on slave instances
    if( result && !object->isMaster( ))
        object->_cm->applyMapData();

    EQLOG( LOG_OBJECTS ) << "Mapped " << typeid( *object ).name() << " to id " 
                         << id << endl;
    EQASSERT( !result || object->getID() != EQ_ID_INVALID );
    return result;
}

void Session::unmapObject( Object* object )
{
    const uint32_t id = object->getID();
    if( id == EQ_ID_INVALID ) // not registered
		return;

    EQASSERT( !_localNode->inCommandThread( ));
    EQLOG( LOG_OBJECTS ) << "Unmap " << typeid( *object ).name() << " from id "
        << object->getID() << endl;

    // Slave: send unsubscribe to master. Master will send detach packet.
    if( !object->isMaster( ))
    {
        const uint32_t masterInstanceID = object->getMasterInstanceID();
        if( masterInstanceID != EQ_ID_INVALID )
        {
            RefPtr<Node> master = _pollIDMasterNode( id );
            if( master.isValid( ))
            {
                SessionUnsubscribeObjectPacket packet;
                packet.requestID = _requestHandler.registerRequest();
                packet.objectID  = id;
                packet.masterInstanceID = masterInstanceID;
                packet.slaveInstanceID  = object->getInstanceID();
                send( master, packet );

                _requestHandler.waitRequest( packet.requestID );
                return;
            }
            EQERROR << "Master node for object id " << id << " not connected"
                    << endl;
        }
    }

    // Master (or no unsubscribe sent): Detach directly
    detachObject( object );
}

void Session::registerObject( Object* object )
{
    EQASSERT( object->_id == EQ_ID_INVALID );

    const uint32_t id = genIDs( 1 );
    EQASSERT( id != EQ_ID_INVALID );

    setIDMaster( id, 1, _localNode->getNodeID( ));
    object->setupChangeManager( object->getChangeType(), true );

    EQCHECK( mapObject( object, id ));
    EQLOG( LOG_OBJECTS ) << "Registered " << typeid( *object ).name()
                         << " to id " << id << endl;
}

void Session::deregisterObject( Object* object )
{
    const uint32_t id = object->getID();

    EQLOG( LOG_OBJECTS ) << "Deregister " << typeid( *object ).name() 
                         << " from id " << id << endl;

    // TODO unsetIDMaster ?
    unmapObject( object );
    freeIDs( id, 1 );
}

//===========================================================================
// Packet handling
//===========================================================================
bool Session::dispatchCommand( Command& command )
{
    EQVERB << "dispatch " << command << endl;
    EQASSERT( command.isValid( ));

    switch( command->datatype )
    {
        case DATATYPE_EQNET_SESSION:
            return Base::dispatchCommand( command );

        case DATATYPE_EQNET_OBJECT:
        {
            EQASSERT( command.isValid( ));
            const ObjectPacket* objPacket = command.getPacket<ObjectPacket>();
            const uint32_t      id        = objPacket->objectID;

            if( _objects.find( id ) == _objects.end( ))
            {
                EQVERB << "no objects to dispatch command, redispatching " 
                       << objPacket << endl;
                return false;
            }

            _objectsMutex.set();

            // create copy of first object pointer for thread-safety
            EQASSERTINFO( !_objects[id].empty(), id );
            Object* object = _objects[id][0];
            EQASSERT( object );

            _objectsMutex.unset();

            return object->dispatchCommand( command );
        }

        default:
            EQASSERTINFO( 0, "Unknown datatype " << command->datatype << " for "
                          << command );
            return true;
    }
}

CommandResult Session::invokeCommand( Command& command )
{
    EQVERB << "dispatch " << command << endl;
    EQASSERT( command.isValid( ));

    switch( command->datatype )
    {
        case DATATYPE_EQNET_SESSION:
            return Base::invokeCommand( command );

        case DATATYPE_EQNET_OBJECT:
            return _invokeObjectCommand( command );

        default:
            EQWARN << "Unhandled command " << command << endl;
            return COMMAND_ERROR;
    }
}

CommandResult Session::_invokeObjectCommand( Command& command )
{
    EQASSERT( command.isValid( ));
    const ObjectPacket* objPacket = command.getPacket<ObjectPacket>();
    const uint32_t      id        = objPacket->objectID;

    EQASSERTINFO( _objects.find( id ) != _objects.end(), 
                  "No objects to handle command " << objPacket );

    _objectsMutex.set();

    // create copy of objects vector for thread-safety
    vector<Object*>& objectsTmp = _objects[id];
    EQASSERT( !objectsTmp.empty( ));

    const size_t nObjects = objectsTmp.size();
    Object** objects = static_cast<Object**>( 
                           alloca( nObjects * sizeof( Object* )));
    memcpy( objects, &objectsTmp[0], nObjects * sizeof( Object* ));

    _objectsMutex.unset();


    for( size_t i=0; i<nObjects; ++i )
    {
        Object* object = objects[i];

        if( objPacket->instanceID == EQ_ID_ANY ||
            objPacket->instanceID == object->getInstanceID( ))
        {
            if( !command.isValid( ))
            {
                // NOTE: command got invalidated (last object was a pushed
                // command to another thread) . Object should push copy of
                // command, or we should make it clear what invalidating a
                // command means here (same as discard?)
                EQERROR << "Object of type " << typeid(*object).name()
                        << " invalidated command send all instances" << endl;
                return COMMAND_ERROR;
            }

            const CommandResult result = object->invokeCommand( command );
            switch( result )
            {
                case COMMAND_DISCARD:
                    return COMMAND_DISCARD;

                case COMMAND_ERROR:
                    EQERROR << "Error handling command " << objPacket
                            << " for object of type " << typeid(*object).name()
                            << endl;
                    return COMMAND_ERROR;

                case COMMAND_HANDLED:
                    if( objPacket->instanceID == object->getInstanceID( ))
                        return result;
                    break;

                default:
                    EQUNREACHABLE;
            }
        }
    }
    if( objPacket->instanceID == EQ_ID_ANY )
        return COMMAND_HANDLED;

    EQWARN << "instance not found for " << objPacket << endl;
    return COMMAND_ERROR;
}

CommandResult Session::_cmdGenIDs( Command& command )
{
    CHECK_THREAD( _receiverThread );
    const SessionGenIDsPacket* packet =command.getPacket<SessionGenIDsPacket>();
    EQINFO << "Cmd gen IDs: " << packet << endl;

    SessionGenIDsReplyPacket reply( packet );

    reply.id = _masterPool.genIDs( packet->range );
    send( command.getNode(), reply );
    return COMMAND_HANDLED;
}

CommandResult Session::_cmdGenIDsReply( Command& command )
{
    CHECK_THREAD( _receiverThread );
    const SessionGenIDsReplyPacket* packet =
        command.getPacket<SessionGenIDsReplyPacket>();
    EQINFO << "Cmd gen IDs reply: " << packet << endl;
    _requestHandler.serveRequest( packet->requestID, packet->id );
    return COMMAND_HANDLED;
}

CommandResult Session::_cmdSetIDMaster( Command& command )
{
    CHECK_THREAD( _receiverThread );
    const SessionSetIDMasterPacket* packet = 
        command.getPacket<SessionSetIDMasterPacket>();
    EQLOG( LOG_OBJECTS ) << "Cmd set ID master: " << packet << endl;

    // TODO thread-safety: _idMasterInfos is also read & written by app
    IDMasterInfo info = { packet->start, packet->start + packet->range, 
                          packet->masterID };
    _idMasterInfos.push_back( info );

    return COMMAND_HANDLED;
}

CommandResult Session::_cmdGetIDMaster( Command& command )
{
    CHECK_THREAD( _receiverThread );
    const SessionGetIDMasterPacket* packet =
        command.getPacket<SessionGetIDMasterPacket>();
    EQLOG( LOG_OBJECTS ) << "handle get idMaster " << packet << endl;

    SessionGetIDMasterReplyPacket reply( packet );
    reply.start = 0;

    // TODO thread-safety: _idMasterInfos is also read & written by app
    const uint32_t id     = packet->id;
    const uint32_t nInfos = _idMasterInfos.size();
    RefPtr<Node>   node   = command.getNode();
    for( uint32_t i=0; i<nInfos; ++i )
    {
        IDMasterInfo& info = _idMasterInfos[i];
        if( id >= info.start && id < info.end )
        {
            reply.start    = info.start;
            reply.end      = info.end;
            reply.masterID = info.master;
            break;
        }
    }

    send( node, reply );
    return COMMAND_HANDLED;
}

CommandResult Session::_cmdGetIDMasterReply( Command& command )
{
    CHECK_THREAD( _receiverThread );
    const SessionGetIDMasterReplyPacket* packet = 
        command.getPacket<SessionGetIDMasterReplyPacket>();
    EQLOG( LOG_OBJECTS ) << "handle get idMaster reply " << packet << endl;

    if( packet->start != 0 )
    {
        // TODO thread-safety: _idMasterInfos is also read & written by app
        IDMasterInfo info = { packet->start, packet->end, packet->masterID };
        _idMasterInfos.push_back( info );
    }
    // else not found

    _requestHandler.serveRequest( packet->requestID );
    return COMMAND_HANDLED;
}

CommandResult Session::_cmdAttachObject( Command& command )
{
    CHECK_THREAD( _receiverThread );
    const SessionAttachObjectPacket* packet = 
        command.getPacket<SessionAttachObjectPacket>();
    EQLOG( LOG_OBJECTS ) << "Cmd attach object " << packet << endl;

    Object* object =  static_cast<Object*>( _requestHandler.getRequestData( 
                                                packet->requestID ));
    attachObject( object, packet->objectID );
    _requestHandler.serveRequest( packet->requestID );
    return COMMAND_HANDLED;
}

CommandResult Session::_cmdDetachObject( Command& command )
{
    CHECK_THREAD( _receiverThread );
    const SessionDetachObjectPacket* packet = 
        command.getPacket<SessionDetachObjectPacket>();
    EQLOG( LOG_OBJECTS ) << "Cmd detach object " << packet << endl;

    const uint32_t id   = packet->objectID;
    if( _objects.find( id ) != _objects.end( ))
    {
        vector<Object*>& objects = _objects[id];

        for( vector<Object*>::const_iterator i = objects.begin();
            i != objects.end(); ++i )
        {
            Object* object = *i;
            if( object->getInstanceID() == packet->objectInstanceID )
            {
                detachObject( object );
                break;
            }
        }
    }

    if( packet->requestID != EQ_ID_INVALID )
        _requestHandler.serveRequest( packet->requestID );

    return COMMAND_HANDLED;
}

CommandResult Session::_cmdMapObject( Command& command )
{
    CHECK_THREAD( _receiverThread );
    const SessionMapObjectPacket* packet = 
        command.getPacket<SessionMapObjectPacket>();
    EQLOG( LOG_OBJECTS ) << "Cmd map object " << packet << endl;

    MapObjectData* data   = static_cast<MapObjectData*>( 
        _requestHandler.getRequestData( packet->requestID ));    
    Object*             object = data->object;
    EQASSERT( object );

    if( !object->isMaster( ))
    { 
        _instanceIDs = ( _instanceIDs + 1 ) % IDPool::MAX_CAPACITY;

        // slave instantiation - subscribe first
        SessionSubscribeObjectPacket subscribePacket;
        subscribePacket.requestID  = packet->requestID;
        subscribePacket.objectID   = data->objectID;
        subscribePacket.instanceID = _instanceIDs;

        send( data->master, subscribePacket );
        return COMMAND_HANDLED;
    }

    attachObject( object, data->objectID );

    EQLOG( LOG_OBJECTS ) << "mapped object id " << object->getID() << " @" 
                         << (void*)object << " is " << typeid(*object).name()
                         << endl;

    _requestHandler.serveRequest( packet->requestID, true );
    return COMMAND_HANDLED;
}

CommandResult Session::_cmdSubscribeObject( Command& command )
{
    CHECK_THREAD( _receiverThread );
    SessionSubscribeObjectPacket* packet =
        command.getPacket<SessionSubscribeObjectPacket>();
    EQLOG( LOG_OBJECTS ) << "Cmd subscribe object " << packet << endl;

    RefPtr<Node>   node = command.getNode();
    const uint32_t id   = packet->objectID;

    if( _objects.find( id ) != _objects.end( ))
    {
        vector<Object*>& objects = _objects[id];

        for( vector<Object*>::const_iterator i = objects.begin();
             i != objects.end(); ++i )
        {
            Object* object = *i;
            if( object->isMaster( ))
            {
                SessionSubscribeObjectSuccessPacket successPacket( packet );
                successPacket.changeType       = object->getChangeType();
                successPacket.masterInstanceID = object->getInstanceID();
                send( node, successPacket );

                object->addSlave( node, packet->instanceID );

                SessionSubscribeObjectReplyPacket reply( packet );
                reply.result = true;
                send( node, reply );
                return COMMAND_HANDLED;
            }
        }   
    }

    EQWARN << "Can't find master object for subscribe" << endl;

    SessionSubscribeObjectReplyPacket reply( packet );
    reply.result = false;
    send( node, reply );
    return COMMAND_HANDLED;
}

CommandResult Session::_cmdSubscribeObjectSuccess( Command& command )
{
    CHECK_THREAD( _receiverThread );
    const SessionSubscribeObjectSuccessPacket* packet = 
        command.getPacket<SessionSubscribeObjectSuccessPacket>();
    EQLOG( LOG_OBJECTS ) << "Cmd object subscribe success " << packet << endl;

    MapObjectData* data   = static_cast<MapObjectData*>( 
        _requestHandler.getRequestData( packet->requestID ));
    Object*        object = data->object;

    EQASSERT( object );
    EQASSERT( !object->isMaster( ));

    object->setupChangeManager( 
        static_cast< Object::ChangeType >( packet->changeType ), false, 
        packet->masterInstanceID );

    // Don't use 'attachObject' - we already have an instance id.
    object->attachToSession( data->objectID, packet->instanceID, this );
    
    _objectsMutex.set();
    vector<Object*>& objects = _objects[ data->objectID ];
    objects.push_back( object );
    _objectsMutex.unset();
    
    getLocalNode()->flushCommands(); // redispatch pending commands

    EQLOG( LOG_OBJECTS ) << "subscribed object id " << object->getID() << '.'
                         << object->getInstanceID() << " cm " 
                         << typeid( *(object->_cm)).name() << " @" 
                         << static_cast< void* >( object ) << " is "
                         << typeid(*object).name() << endl;
    return COMMAND_HANDLED;
}

CommandResult Session::_cmdSubscribeObjectReply( Command& command )
{
    CHECK_THREAD( _receiverThread );
    const SessionSubscribeObjectReplyPacket* packet = 
        command.getPacket<SessionSubscribeObjectReplyPacket>();
    EQLOG( LOG_OBJECTS ) << "Cmd object subscribe reply " << packet << endl;

    _requestHandler.serveRequest( packet->requestID, packet->result );
    return COMMAND_HANDLED;
}

CommandResult Session::_cmdUnsubscribeObject( Command& command )
{
    CHECK_THREAD( _receiverThread );
    SessionUnsubscribeObjectPacket* packet =
        command.getPacket<SessionUnsubscribeObjectPacket>();
    EQLOG( LOG_OBJECTS ) << "Cmd unsubscribe object  " << packet << endl;

    RefPtr<Node>   node = command.getNode();
    const uint32_t id   = packet->objectID;

    if( _objects.find( id ) != _objects.end( ))
    {
        vector<Object*>& objects = _objects[id];

        for( vector<Object*>::const_iterator i = objects.begin();
             i != objects.end(); ++ i )
        {
            Object* object = *i;
            if( object->isMaster( ) && 
                object->getInstanceID() == packet->masterInstanceID )
            {
                object->removeSlave( node );
                break;
            }
        }   
    }

    SessionDetachObjectPacket detachPacket( packet );
    send( node, detachPacket );
    return COMMAND_HANDLED;
}

std::ostream& operator << ( std::ostream& os, Session* session )
{
    if( !session )
    {
        os << "NULL session";
        return os;
    }
    
    os << "session " << session->getID() << "(" << (void*)session
       << ")";

    return os;
}
}
