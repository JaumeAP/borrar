/*
 * Stream position and offset types: the standard std::streampos and std::streamoff. The original code spelled them
 * std::ios_base::streampos and std::ios_base::streamoff, which libstdc++ never had and which C++17 removed (libc++ keeps
 * them only up to C++14), so they did not build on Linux, nor on macOS in C++17 (the unit tests). The types are the same
 * where both spellings exist.
 */

#ifndef COMMON_STREAM_STREAM_STREAMPOS_H_
#define COMMON_STREAM_STREAM_STREAMPOS_H_

#include <ios>

typedef std::streampos IABStreamPos;
typedef std::streamoff IABStreamOff;

#endif /* COMMON_STREAM_STREAM_STREAMPOS_H_ */
