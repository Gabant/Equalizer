/* Copyright (c) 2008-2009, Cedric Stalder <cedric.stalder@gmail.com>
                 2009, Stefan Eilemann <eile@equalizergraphics.com>
   All rights reserved. */

#ifndef EQ_FRAMEBUFFEROBJECT_H 
#define EQ_FRAMEBUFFEROBJECT_H 

#include <eq/client/windowSystem.h> // for GL types
#include <eq/client/texture.h>      // member

namespace eq
{
    /** A C++ class to abstract GL frame buffer objects */
    class FrameBufferObject 
    {
    public: 
        /** Constructs a new Frame Buffer Object */
		FrameBufferObject( GLEWContext* const glewContext );
        
        /** Destruct the Frame Buffer Object */
		~FrameBufferObject();
        
        /** Initialize the Frame Buffer Object */
        bool init( const int width, const int height, 
                   const int depthSize, const int stencilSize );

        /** De-initialize the Frame Buffer Object. */
        void exit();
        
        /** Bind to the Frame Buffer Object */
        void bind();
        
        /** ask if FBO built construction is ok */
        bool checkFBOStatus() const;
       
        /* Resize FBO, if needed. */
        bool resize( const int width, const int height );
          
        /** 
         * Get the GLEW context for this window.
         * 
         * The glew context is initialized during window initialization, and
         * provides access to OpenGL extensions. This function does not follow
         * the Equalizer naming conventions, since GLEW uses a function of this
         * name to automatically resolve OpenGL function entry
         * points. Therefore, any supported GL function can be called directly
         * from an initialized Window.
         * 
         * @return the extended OpenGL function table for the window's OpenGL
         *         context.
         */
        GLEWContext* glewGetContext() { return _glewContext; }
        const GLEWContext* glewGetContext() const { return _glewContext; }

    private:
        GLuint _fboID;
         
        int _width;
        int _height;
         
        enum TextureType
        {
            COLOR_TEXTURE,
            DEPTH_TEXTURE,
            STENCIL_TEXTURE,
            ALL_TEXTURE
        };
        
        Texture _textures[ALL_TEXTURE];
        
        GLEWContext* const _glewContext;

        CHECK_THREAD_DECLARE( _thread );
    };
}


#endif //EQ_FRAMEBUFFEROBJECT_H 
