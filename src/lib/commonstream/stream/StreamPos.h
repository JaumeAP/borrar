/*
 * Stream position and offset types. Apple's C++ library provides std::ios_base::streampos and streamoff, which the
 * original code uses. Other C++ libraries (libstdc++) do not provide them: they use the standard std::streampos and
 * std::streamoff.
 */

#ifndef COMMON_STREAM_STREAM_STREAMPOS_H_
#define COMMON_STREAM_STREAM_STREAMPOS_H_

#include <ios>

#if defined(__APPLE__)
typedef std::ios_base::streampos IABStreamPos;
typedef std::ios_base::streamoff IABStreamOff;
#else
typedef std::streampos IABStreamPos;
typedef std::streamoff IABStreamOff;
#endif

#endif /* COMMON_STREAM_STREAM_STREAMPOS_H_ */
