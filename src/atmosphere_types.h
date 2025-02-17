#ifndef CLOUDSCAPER_ATMOSPHERE_TYPES_H_
#define CLOUDSCAPER_ATMOSPHERE_TYPES_H_

#include "ninmath/ninmath.h"

struct AtmosphereContext {
    float Rb; // Ground radius
    float Rt; // Atmosphere radius
};

struct SkyBuffer {
    ninmath::Vector3f cameraPos;
    float pad0;
    ninmath::Vector3f lightDir;
    float pad1;
    ninmath::Vector3f viewDir;
    float pad2;
    ninmath::Vector3f sunIlluminance;
    float pad3;
    ninmath::Vector3f groundAlbedo;
    float pad4;
};

#endif // CLOUDSCAPER_ATMOSPHERE_TYPES_H_
