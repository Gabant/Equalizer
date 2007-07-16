
/* Copyright (c) 2006-2007, Stefan Eilemann <eile@equalizergraphics.com> 
   All rights reserved. */

#include "frame.h"

#include "compound.h"
#include "frameData.h"

#include <eq/net/session.h>

using namespace eqs;
using namespace eqBase;
using namespace std;

Frame::Frame()
        : _compound( 0 ),
          _masterFrameData( 0 )
{
    _data.buffers = eq::Frame::BUFFER_UNDEFINED;
    for( unsigned i = 0; i<eq::EYE_ALL; ++i )
        _frameData[i] = 0;

    setInstanceData( &_inherit, sizeof( eq::Frame::Data ));
}

Frame::Frame( const Frame& from )
        : _compound( 0 )
{
    _data = from._data;
    _name = from._name;
    _vp   = from._vp;

    for( unsigned i = 0; i<eq::EYE_ALL; ++i )
        _frameData[i] = 0;

    setInstanceData( &_inherit, sizeof( eq::Frame::Data ));
}

Frame::~Frame()
{
    EQASSERT( _datas.empty());
}

void Frame::flush()
{
    unsetData();

    eqNet::Session* session = getSession();
    EQASSERT( session );
    while( !_datas.empty( ))
    {
        FrameData* data = _datas.front();
        session->deregisterObject( data );
        _datas.pop_front();
    }

}

void Frame::unsetData()
{
    for( unsigned i = 0; i<eq::EYE_ALL; ++i )
    {
        _frameData[i] = 0;
        _inputFrames[i].clear();
    }
}

void Frame::commitData()
{
    if( !_masterFrameData ) // not used
        return;

    for( unsigned i = 0; i<eq::EYE_ALL; ++i )
    {
        if( _frameData[i] )
        {
            if( _frameData[i] != _masterFrameData )
                _frameData[i]->_data = _masterFrameData->_data;

            _frameData[i]->commit();
        }
    }
}

void Frame::updateInheritData( const Compound* compound )
{
    _inherit = _data;
    if( _inherit.buffers == eq::Frame::BUFFER_UNDEFINED )
        _inherit.buffers = compound->getInheritBuffers();

    for( unsigned i = 0; i<eq::EYE_ALL; ++i )
    {
        if( _frameData[i] )
        {
            _inherit.frameData[i].objectID = _frameData[i]->getID();
            _inherit.frameData[i].version  = _frameData[i]->getVersion();
        }
        else
            _inherit.frameData[i].objectID = EQ_ID_INVALID;
    }
}

void Frame::cycleData( const uint32_t frameNumber, const uint32_t eyes )
{
    _masterFrameData = 0;
    for( unsigned i = 0; i<eq::EYE_ALL; ++i )
    {
        _inputFrames[i].clear();

        if( !(eyes & (1<<i))) // eye pass not used
        {
            _frameData[i] = 0;
            continue;
        }

        // find unused frame data
        FrameData*     data    = _datas.empty() ? 0 : _datas.back();
        const uint32_t latency = getAutoObsoleteCount();
        const uint32_t dataAge = data ? data->getFrameNumber() : 0;

        if( data && dataAge < frameNumber-latency && frameNumber > latency )
            // not used anymore
            _datas.pop_back();
        else // still used - allocate new data
        {
            data = new FrameData;
        
            eqNet::Session* session = getSession();
            EQASSERT( session );

            session->registerObject( data );
            data->setAutoObsolete( 1 ); // current + in use by render nodes
        }

        data->setFrameNumber( frameNumber );
    
        _datas.push_front( data );
        _frameData[i] = data;
        if( !_masterFrameData )
            _masterFrameData = data;
    }
}

void Frame::addInputFrame( Frame* frame, const uint32_t eyes )
{
    for( unsigned i = 0; i<eq::EYE_ALL; ++i )
    {
        if( !(eyes & (1<<i)) ||  // eye pass not used
            !_frameData[i] )     // no output frame for eye pass
        {
            frame->_frameData[i] = 0;
        }
        else
        {
            frame->_frameData[i] = _frameData[i];
            _inputFrames[i].push_back( frame );
        }
    }
}

std::ostream& eqs::operator << ( std::ostream& os, const Frame* frame )
{
    if( !frame )
        return os;
    
    os << disableFlush << "Frame" << endl;
    os << "{" << endl << indent;
      
    const std::string& name = frame->getName();
    os << "name     \"" << name << "\"" << endl;

    const uint32_t buffers = frame->getBuffers();
    if( buffers != eq::Frame::BUFFER_UNDEFINED )
    {
        os << "buffers  [";
        if( buffers & eq::Frame::BUFFER_COLOR )  os << " COLOR";
        if( buffers & eq::Frame::BUFFER_DEPTH )  os << " DEPTH";
        os << " ]" << endl;
    }

    const eq::Viewport& vp = frame->getViewport();
    if( !vp.isFullScreen( ))
        os << "viewport " << vp << endl;

    os << exdent << "}" << endl << enableFlush;
    return os;
}
