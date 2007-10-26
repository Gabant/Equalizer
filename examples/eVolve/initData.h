
/* Copyright (c) 2006-2007, Stefan Eilemann <eile@equalizergraphics.com> 
   All rights reserved. */

#ifndef EVOLVE_INITDATA_H
#define EVOLVE_INITDATA_H

#include "eVolve.h"
#include "frameData.h"

#include <eq/eq.h>

namespace eVolve
{
    class InitData : public eqNet::Object
    {
    public:
        InitData();
        virtual ~InitData();

        void setFrameDataID( const uint32_t id )   { _frameDataID = id; }

        uint32_t           getFrameDataID() const  { return _frameDataID; }
        eq::WindowSystem   getWindowSystem() const { return _windowSystem;}
        uint32_t           getPrecision() const    { return _precision; }
        float              getBrightness() const   { return _brightness; }
        float              getAlpha() const        { return _alpha; }
        bool               useGLSL() const         { return _useGLSL; }
        const std::string& getFilename()    const  { return _filename; }

    protected:
        virtual void getInstanceData( eqNet::DataOStream& os );
        virtual void applyInstanceData( eqNet::DataIStream& is );

        void setWindowSystem( const eq::WindowSystem windowSystem )
            { _windowSystem = windowSystem; }
        void setPrecision( const uint32_t precision ){ _precision = precision; }
        void setBrightness( const float brightness ) {_brightness = brightness;}
        void setAlpha( const float alpha )           { _alpha = alpha;}
        void enableGLSL()                            { _useGLSL = true; }
        void setFilename( const std::string& filename ) { _filename = filename;}

    private:
        uint32_t         _frameDataID;
        eq::WindowSystem _windowSystem;
        uint32_t         _precision;
        float            _brightness;
        float            _alpha;
        bool             _useGLSL;
        std::string      _filename;
    };
}


#endif // EVOLVE_INITDATA_H

