
/* Copyright (c) 2006-2009, Stefan Eilemann <eile@equalizergraphics.com>
                 2007       Maxim Makhinya
   All rights reserved. */

#include "channel.h"

#include "frameData.h"
#include "initData.h"
#include "node.h"
#include "pipe.h"
#include "window.h"
#include "hlp.h"
#include "framesOrderer.h"
#include <eq/client/zoom.h>

using namespace eq::base;
using namespace std;

namespace eVolve
{
Channel::Channel( eq::Window* parent )
    : eq::Channel( parent )
    , _bgColor   ( 0.0f, 0.0f, 0.0f, 1.0f )
    , _bgColorMode( BG_SOLID_COLORED )
    , _drawRange( eq::Range::ALL )
{
    eq::FrameData* frameData = new eq::FrameData;
    frameData->setBuffers( eq::Frame::BUFFER_COLOR );
    _frame.setData( frameData );
}

static void checkError( const std::string& msg ) 
{
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR)
        EQERROR << msg << " GL Error: " << error << endl;
}


bool Channel::configInit( const uint32_t initID )
{
    if( !eq::Channel::configInit( initID ))
        return false;
    EQINFO << "Init channel initID " << initID << " ptr " << this << endl;

    // chose projection type
    setNearFar( 0.0001f, 10.0f );

    if( getenv( "EQ_TAINT_CHANNELS" ))
    {
        _bgColor = getUniqueColor();
        _bgColor /= 255.f;
    }

    if( _bgColor.r + _bgColor.g + _bgColor.b <= 0.0f )
        _bgColorMode = BG_SOLID_BLACK;
    else
        _bgColorMode = BG_SOLID_COLORED;

    return true;
}

void Channel::frameStart( const uint32_t frameID, const uint32_t frameNumber )
{
    _drawRange = eq::Range::ALL;
    eq::Channel::frameStart( frameID, frameNumber );
}

void Channel::frameClear( const uint32_t frameID )
{
    applyBuffer();
    applyViewport();

    if( getRange() == eq::Range::ALL )
        glClearColor( _bgColor.r, _bgColor.g, _bgColor.b, _bgColor.a );
    else
        glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );

    glClear( GL_COLOR_BUFFER_BIT );
}


static void setLights( vmml::Matrix4f& invRotationM )
{
    GLfloat lightAmbient[]  = {0.05f, 0.05f, 0.05f, 1.0f};
    GLfloat lightDiffuse[]  = {0.9f , 0.9f , 0.9f , 1.0f};
    GLfloat lightSpecular[] = {0.8f , 0.8f , 0.8f , 1.0f};
    GLfloat lightPosition[] = {1.0f , 1.0f , 1.0f , 0.0f};

    glLightfv( GL_LIGHT0, GL_AMBIENT,  lightAmbient  );
    glLightfv( GL_LIGHT0, GL_DIFFUSE,  lightDiffuse  );
    glLightfv( GL_LIGHT0, GL_SPECULAR, lightSpecular );

    // rotate light in the opposite direction of the model rotation to keep
    // light position constant and avoid recalculating normals in the fragment
    // shader
    glPushMatrix();
    glMultMatrixf( invRotationM.ml );
    glLightfv( GL_LIGHT0, GL_POSITION, lightPosition );
    glPopMatrix();
}


void Channel::frameDraw( const uint32_t frameID )
{
    // Setup frustum
    eq::Channel::frameDraw( frameID );

    const FrameData::Data& frameData = _getFrameData();
    vmml::Matrix4f         invRotationM;
    frameData.rotation.getInverse( invRotationM );
    setLights( invRotationM );

    glTranslatef(  frameData.translation.x, frameData.translation.y, 
                   frameData.translation.z );
    glMultMatrixf( frameData.rotation.ml );

    Pipe*     pipe     = static_cast<Pipe*>( getPipe( ));
    Renderer* renderer = pipe->getRenderer();
    EQASSERT( renderer );

    vmml::Matrix4d  modelviewM;     // modelview matrix
    vmml::Matrix3d  modelviewITM;   // modelview inversed transposed matrix
    _calcMVandITMV( modelviewM, modelviewITM );

    const eq::Range& range = getRange();
    renderer->render( range, modelviewM, modelviewITM, invRotationM );

    checkError( "error during rendering " );

    // Draw logo
    const eq::Viewport& vp = getViewport();
    if( range == eq::Range::ALL && vp == eq::Viewport::FULL )
       _drawLogo();

    _drawRange = range;
}

const FrameData::Data& Channel::_getFrameData() const
{
    const Pipe* pipe = static_cast< const Pipe* >( getPipe( ));
    return pipe->getFrameData();
}

void Channel::applyFrustum() const
{
    const FrameData::Data& frameData = _getFrameData();

    if( frameData.ortho )
        eq::Channel::applyOrtho();
    else
        eq::Channel::applyFrustum();
}


void Channel::_calcMVandITMV(
    vmml::Matrix4d& modelviewM,
    vmml::Matrix3d& modelviewITM ) const
{
    const FrameData::Data& frameData = _getFrameData();
    const Pipe*            pipe      = static_cast< const Pipe* >( getPipe( ));
    const Renderer*        renderer  = pipe->getRenderer();

    if( renderer )
    {
        const VolumeScaling& volScaling = renderer->getVolumeScaling();
        
        vmml::Matrix4f scale( 
            volScaling.W, 0, 0, 0,
            0, volScaling.H, 0, 0,
            0, 0, volScaling.D, 0,
            0, 0,            0, 1 );

        modelviewM = scale * frameData.rotation;
    }
    modelviewM.setTranslation( frameData.translation );

    modelviewM = getHeadTransform() * modelviewM;

    //calculate inverse transposed matrix
    vmml::Matrix4d modelviewIM;
    modelviewM.getInverse( modelviewIM );
    modelviewITM = modelviewIM.getTransposed();
}


static void _expandPVP( eq::PixelViewport& pvp, 
                        const vector< eq::Image* >& images,
                        const vmml::Vector2i& offset )
{
    for( vector< eq::Image* >::const_iterator i = images.begin();
         i != images.end(); ++i )
    {
        const eq::PixelViewport imagePVP = (*i)->getPixelViewport() + offset;
        pvp.merge( imagePVP );
    }
}


void Channel::clearViewport( const eq::PixelViewport &pvp )
{
    // clear given area
    glScissor(  pvp.x, pvp.y, pvp.w, pvp.h );
    glClearColor( _bgColor.r, _bgColor.g, _bgColor.b, _bgColor.a );
    glClear( GL_COLOR_BUFFER_BIT );

    // restore assembly state
    const eq::PixelViewport& windowPVP = getWindow()->getPixelViewport();
    glScissor( 0, 0, windowPVP.w, windowPVP.h );
}

void Channel::_orderFrames( eq::FrameVector& frames )
{
          vmml::Matrix4d   modelviewM;   // modelview matrix
          vmml::Matrix3d   modelviewITM; // modelview inversed transposed matrix
    const FrameData::Data& frameData = _getFrameData();
    const vmml::Matrix4f&  rotation  = frameData.rotation;

    if( !frameData.ortho )
        _calcMVandITMV( modelviewM, modelviewITM );

    orderFrames( frames, modelviewM, modelviewITM, rotation, frameData.ortho );
}


void Channel::frameAssemble( const uint32_t frameID )
{
    const bool composeOnly = (_drawRange == eq::Range::ALL);

    _startAssemble();

    const eq::FrameVector& frames = getInputFrames();
    eq::PixelViewport  coveredPVP;
    eq::FrameVector    dbFrames;
    eq::Zoom           zoom( eq::Zoom::NONE );

    // Make sure all frames are ready and gather some information on them
    for( eq::FrameVector::const_iterator i = frames.begin();
         i != frames.end(); ++i )
    {
        eq::Frame* frame = *i;
        frame->waitReady( );

        const eq::Range& curRange = frame->getRange();
        if( curRange == eq::Range::ALL ) // 2D frame, assemble directly
            eq::Compositor::assembleFrame( frame, this );
        else
        {
            dbFrames.push_back( frame );
            zoom = frame->getZoom();
            _expandPVP( coveredPVP, frame->getImages(), frame->getOffset() );
        }
    }
    coveredPVP.intersect( getPixelViewport( ));

    if( dbFrames.empty( ))
    {
        _finishAssemble();
        return;
    }

    //calculate correct frames sequence
    if( !composeOnly && coveredPVP.hasArea( ))
    {
        _frame.clear();
        _frame.setRange( _drawRange );
        dbFrames.push_back( &_frame );
    }

    _orderFrames( dbFrames );

    // check if current frame is in proper position, read back if not
    if( !composeOnly )
    {
        if( _bgColorMode == BG_SOLID_BLACK && dbFrames.front() == &_frame )
            dbFrames.erase( dbFrames.begin( ));
        else if( coveredPVP.hasArea())
        {
            eq::Window*                window    = getWindow();
            eq::Window::ObjectManager* glObjects = window->getObjectManager();

            _frame.setOffset( vmml::Vector2i( 0, 0 ));
            _frame.setZoom( zoom );
            _frame.setPixelViewport( coveredPVP );
            _frame.startReadback( glObjects );
            clearViewport( coveredPVP );
            _frame.syncReadback();

            // offset for assembly
            _frame.setOffset( vmml::Vector2i( coveredPVP.x, coveredPVP.y ));
        }
    }

    // blend DB frames in order
    eq::Compositor::assembleFramesSorted( dbFrames, this, true /*blendAlpha*/ );

    _finishAssemble();

    // Update range
    _drawRange = getRange();
}

void Channel::_startAssemble()
{
    applyBuffer();
    applyViewport();
    setupAssemblyState();
}   

void Channel::_finishAssemble()
{
    resetAssemblyState();
    _drawLogo();
}


void Channel::frameReadback( const uint32_t frameID )
{
    // Drop depth buffer flag from all output frames
    const eq::FrameVector& frames = getOutputFrames();
    for( eq::FrameVector::const_iterator i = frames.begin(); 
         i != frames.end(); ++i )
    {
        (*i)->disableBuffer( eq::Frame::BUFFER_DEPTH );
    }

    eq::Channel::frameReadback( frameID );
}

void Channel::_drawLogo()
{
    const Window*  window      = static_cast<Window*>( getWindow( ));
    GLuint         texture;
    vmml::Vector2i size;

    window->getLogoTexture( texture, size );
    if( !texture )
        return;
    
    const eq::Zoom& zoom = getZoom();

    const float newX = size.x * zoom.x;
    const float newY = size.y * zoom.y;
    const float delta = 5.0f * zoom.x ;
    
    glMatrixMode( GL_PROJECTION );
    glLoadIdentity();
    applyScreenFrustum();
    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity();

    glDisable( GL_DEPTH_TEST );
    glDisable( GL_LIGHTING );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

    glEnable( GL_TEXTURE_RECTANGLE_ARB );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, texture );
    glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
                     GL_LINEAR );
    glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, 
                     GL_LINEAR );

    glColor3f( 1.0f, 1.0f, 1.0f );
    glBegin( GL_TRIANGLE_STRIP ); {
        glTexCoord2f( 0.0f, 0.0f );
        glVertex3f( delta, delta, 0.0f );

        glTexCoord2f( size.x, 0.0f );
        glVertex3f( newX + delta, delta, 0.0f );

        glTexCoord2f( 0.0f, size.y );
        glVertex3f( delta, newY + delta, 0.0f );

        glTexCoord2f( size.x, size.y );
        glVertex3f( newX + delta, newY + delta, 0.0f );
    } glEnd();

    glDisable( GL_TEXTURE_RECTANGLE_ARB );
    glDisable( GL_BLEND );
}

}
