
/* Copyright (c) 2005-2010, Stefan Eilemann <eile@equalizergraphics.com>
                          , Maxim Makhinya
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *  
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef EQ_AGL_WINDOW_H
#define EQ_AGL_WINDOW_H

#include <eq/client/glWindow.h>       // base class

namespace eq
{
    class AGLEventHandler;
    class AGLWindowEvent;

    /** The interface defining the minimum functionality for an AGL window. */
    class AGLWindowIF : public GLWindow
    {
    public:
        /** Create a new AGL window for the given eq::Window. @version 1.0 */
        AGLWindowIF( Window* parent ) : GLWindow( parent ) {}

        /** Destruct the AGL window. @version 1.0 */
        virtual ~AGLWindowIF() {}

        /** @return the AGL rendering context. @version 1.0 */
        EQ_CLIENT_DECL virtual AGLContext getAGLContext() const = 0;

        /** @return the carbon window reference. @version 1.0 */
        EQ_CLIENT_DECL virtual WindowRef getCarbonWindow() const = 0;

        /** @return the AGL PBuffer object. @version 1.0 */
        EQ_CLIENT_DECL virtual AGLPbuffer getAGLPBuffer() const = 0;

        /** Process an event. @version 1.0 */
        EQ_CLIENT_DECL virtual bool processEvent( const AGLWindowEvent& event );

    private:
    };

    /** Equalizer default implementation of an AGL window interface. */
    class AGLWindow : public AGLWindowIF
    {
    public:
        /** Create a new AGL window for the given eq::Window. @version 1.0 */
        EQ_CLIENT_DECL AGLWindow( Window* parent );

        /** Destruct the AGL window. @version 1.0 */
        EQ_CLIENT_DECL virtual ~AGLWindow();

        /** @version 1.0 */
        EQ_CLIENT_DECL virtual void configExit( );

        /** @version 1.0 */
        EQ_CLIENT_DECL virtual void makeCurrent() const;

        /** @version 1.0 */
        EQ_CLIENT_DECL virtual void swapBuffers();

        /** Not implemented for AGL. @version 1.0 */
        EQ_CLIENT_DECL virtual void joinNVSwapBarrier( const uint32_t group,
                                                  const uint32_t barrier );

        /** @return the AGL rendering context. @version 1.0 */
        virtual AGLContext getAGLContext() const { return _aglContext; }

        /** @return the carbon window reference. @version 1.0 */
        virtual WindowRef getCarbonWindow() const { return _carbonWindow; }

        /** @return the AGL PBuffer object. @version 1.0 */
        virtual AGLPbuffer getAGLPBuffer() const { return _aglPBuffer; }

        /** @name Data Access */
        //@{
        /** 
         * Set the AGL rendering context for this window.
         * 
         * This function should only be called from configInit() or
         * configExit(). The context has to be set to 0 before it is destroyed.
         *
         * @param context the AGL rendering context.
         * @version 1.0
         */
        EQ_CLIENT_DECL virtual void setAGLContext( AGLContext context );

        /** 
         * Set the carbon window to be used with the current AGL context.
         * 
         * @param window the window reference.
         * @version 1.0
         */
        EQ_CLIENT_DECL virtual void setCarbonWindow( WindowRef window );
        
        /** 
         * Set the AGL PBuffer object to be used with the current AGL context.
         * 
         * @param pbuffer the PBuffer.
         * @version 1.0
         */
        EQ_CLIENT_DECL virtual void setAGLPBuffer( AGLPbuffer pbuffer );
        //@}

        /** @name AGL/Carbon initialization */
        //@{
        /** 
         * Initialize this window for the AGL window system.
         *
         * This method first call chooseAGLPixelFormat(), then
         * createAGLContext() with the chosen pixel format, destroys the pixel
         * format using destroyAGLPixelFormat() and finally creates a drawable
         * using configInitAGLDrawable().
         * 
         * @return true if the initialization was successful, false otherwise.
         * @version 1.0
         */
        EQ_CLIENT_DECL virtual bool configInit();

        /** 
         * Choose a pixel format based on the window's attributes.
         * 
         * The returned pixel format has to be destroyed using
         * destroyAGLPixelFormat() to avoid memory leaks.
         *
         * This method uses Global::enterCarbon() and Global::leaveCarbon() to
         * protect the calls to AGL/Carbon.
         *  
         * @return a pixel format, or 0 if no pixel format was found.
         * @version 1.0
         */
        EQ_CLIENT_DECL virtual AGLPixelFormat chooseAGLPixelFormat();

        /** 
         * Destroy a pixel format obtained with chooseAGLPixelFormat().
         * 
         * This method uses Global::enterCarbon() and Global::leaveCarbon() to
         * protect the calls to AGL/Carbon.
         *
         * @param format a pixel format.
         * @version 1.0
         */
        EQ_CLIENT_DECL virtual void destroyAGLPixelFormat( AGLPixelFormat format );

        /** 
         * Create an AGL context.
         * 
         * This method does not set the window's AGL context.
         *
         * This method uses Global::enterCarbon() and Global::leaveCarbon() to
         * protect the calls to AGL/Carbon.
         *
         * @param format the pixel format for the context.
         * @return the context, or 0 if context creation failed.
         * @version 1.0
         */
        EQ_CLIENT_DECL virtual AGLContext createAGLContext( AGLPixelFormat format );

        /** 
         * Initialize the window's drawable (fullscreen, pbuffer or window) and
         * bind the AGL context.
         *
         * Sets the window's carbon window on success. Calls
         * configInitAGLFullscreen() or configInitAGLWindow().
         * 
         * @return true if the drawable was created, false otherwise.
         * @version 1.0
         */
        EQ_CLIENT_DECL virtual bool configInitAGLDrawable();

        /** 
         * Initialize the window with a fullscreen Carbon window.
         *
         * Sets the window's carbon window on success.
         *
         * This method uses Global::enterCarbon() and Global::leaveCarbon() to
         * protect the calls to AGL/Carbon.
         *
         * @return true if the window was created, false otherwise.
         * @version 1.0
         */
        EQ_CLIENT_DECL virtual bool configInitAGLFullscreen();

        /** 
         * Initialize the window with a normal Carbon window.
         *
         * Sets the window's carbon window on success.
         *
         * This method uses Global::enterCarbon() and Global::leaveCarbon() to
         * protect the calls to AGL/Carbon.
         *
         * @return true if the window was created, false otherwise.
         * @version 1.0
         */
        EQ_CLIENT_DECL virtual bool configInitAGLWindow();

        /** 
         * Initialize the window with an offscreen AGL PBuffer.
         *
         * Sets the window's AGL PBuffer on success.
         *
         * @return true if the PBuffer was created, false otherwise.
         * @version 1.0
         */
        EQ_CLIENT_DECL virtual bool configInitAGLPBuffer(); 
        
        /**
         * Set up an AGLEventHandler, called by setCarbonWindow().
         * @version 1.0
         */
        EQ_CLIENT_DECL virtual void initEventHandler();

        /**
         * Destroy the AGLEventHandler, called by setCarbonWindow().
         * @version 1.0
         */
        EQ_CLIENT_DECL virtual void exitEventHandler();
        //@}

        /** @version 1.0 */
        EQ_CLIENT_DECL virtual bool processEvent( const AGLWindowEvent& event );

    private:
        /** The AGL context. */
        AGLContext _aglContext;
        /** The carbon window reference. */
        WindowRef _carbonWindow;
        /** The AGL PBuffer object. */
        AGLPbuffer _aglPBuffer;
        /** The AGL event handler. */
        AGLEventHandler* _eventHandler;

        union // placeholder for binary-compatible changes
        {
            char dummy[64];
        };
    };
}

#endif // EQ_AGL_WINDOW_H

