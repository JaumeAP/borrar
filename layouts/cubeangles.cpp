// The angles IABLib gives a place in the IAB unit cube: reads "name x y z" lines (x left to right, y front to back,
// z floor to ceiling, each 0 to 1) on stdin and writes "name azimuth elevation" (radians) with
// IABTransform::TransformIABToSphericalVBAP, the transform the renderer applies to object positions. The layout
// generators use it so that a speaker's angles in a .cfg are those of its place in the room.

#include "renderer/IABTransform/IABTransform.h"

#include <cstdio>
#include <iostream>
#include <string>

using namespace SMPTE::ImmersiveAudioBitstream;

int main()
{
    IABTransform transform;
    std::string name;
    float x, y, z;
    while (std::cin >> name >> x >> y >> z)
    {
        float azimuth, elevation, radius;
        if (transform.TransformIABToSphericalVBAP(x, y, z, azimuth, elevation, radius) != kIABNoError)
        {
            std::fprintf(stderr, "cubeangles: %s is outside the unit cube\n", name.c_str());
            return 1;
        }
        std::printf("%s %.4f %.4f\n", name.c_str(), azimuth, elevation);
    }
    return 0;
}
