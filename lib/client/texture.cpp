
/* Copyright (c) 2009, Stefan Eilemann <eile@equalizergraphics.com>
   All rights reserved. */

#include "texture.h"

#include "image.h"
#include "window.h"

namespace eq
{
Texture::Texture() 
        : _id( 0 )
        , _format( 0 )
        , _width(0)
        , _height(0)
        , _defined( false ) 
{
}

Texture::~Texture()
{
    if( _id != 0 )
        EQWARN << "OpenGL texture was not freed" << std::endl;
    _id      = 0;
    _defined = false;
}

bool Texture::isValid() const
{ 
    return ( _id != 0 && _defined );
}

void Texture::flush()
{
    if( _id == 0 )
        return;

    CHECK_THREAD( _thread );
    glDeleteTextures( 1, &_id );
    _id = 0;
    _defined = false;
}

void Texture::setFormat( const GLuint format )
{
    if( _format == format )
        return;

    _defined = false;
    _format  = format;
}

void Texture::_generate()
{
    CHECK_THREAD( _thread );
    if( _id != 0 )
        return;

    _defined = false;
    glGenTextures( 1, &_id );
}

void Texture::_resize( const int32_t width, const int32_t height )
{
    if( _width < width ) 
    {
        _width   = width;
        _defined = false;
    }
    
    if( _height < height )
    {
        _height  = height;
        _defined = false;
    }
}

void Texture::copyFromFrameBuffer( const PixelViewport& pvp )
{
    CHECK_THREAD( _thread );
    EQASSERT( _format != 0 );

    _generate();
    _resize( pvp.w, pvp.h );  
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, _id );

    if( _defined )
    {
        EQ_GL_CALL( glCopyTexSubImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
                                         pvp.x, pvp.y, pvp.w, pvp.h ));
    }
    else
    {
        EQ_GL_CALL( glCopyTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, _format, 
                                      pvp.x, pvp.y, pvp.w, pvp.h, 0 ));
        _defined = true;
    }
}

void Texture::upload( const Image* image, const Frame::Buffer which )
{
    EQASSERT( _format != 0 );

    const eq::PixelViewport& pvp = image->getPixelViewport();

    _generate();
    _resize( pvp.w, pvp.h );  
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, _id );

    if( _defined )
    {
        EQ_GL_CALL( glTexSubImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
                                     pvp.w, pvp.h,
                                     image->getFormat( which ), 
                                     image->getType( which ),
                                     image->getPixelPointer( which )));
    }
    else
    {
        EQ_GL_CALL( glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, 
                                  _format, pvp.w, pvp.h, 0,
                                  image->getFormat( which ), 
                                  image->getType( which ),
                                  image->getPixelPointer( which )));
        _defined = true;
    }
}

void Texture::bindToFBO( const GLenum target, const int width, 
                         const int height )
{
    EQASSERT( _format );

    _generate();
    _width = width;
    _height = height;

    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, _id );
    glTexImage2D ( GL_TEXTURE_RECTANGLE_ARB, 0, _format, width, height, 0, 
                   _format, GL_INT, 0 );
    glFramebufferTexture2DEXT( GL_FRAMEBUFFER, target, GL_TEXTURE_RECTANGLE_ARB,
                               _id, 0 );
    _defined = true;
}

void Texture::resize( const int width, const int height )
{
    EQASSERT( _id );
    EQASSERT( _format );

    if( _width == width && _height == height )
        return;

    _width = width;
    _height = height;

    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, _id );
    glTexImage2D ( GL_TEXTURE_RECTANGLE_ARB, 0, _format, width, height, 0, 
                   _format, GL_INT, 0 );
    _defined = true;
}

}
