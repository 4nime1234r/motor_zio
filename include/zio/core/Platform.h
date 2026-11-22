#pragma once

#include <cassert>

#if defined(_WIN32)
#define ZIO_PLATFORM_WINDOWS 1
#elif defined(__linux__)
#define ZIO_PLATFORM_LINUX 1
#endif

#if defined(ZIO_SHARED_LIB)
#if defined(ZIO_PLATFORM_WINDOWS)
#if defined(ZIO_EXPORTS)
#define ZIO_API __declspec(dllexport)
#else
#define ZIO_API __declspec(dllimport)
#endif
#else
#define ZIO_API __attribute__((visibility("default")))
#endif
#else
#define ZIO_API
#endif

#define ZIO_ASSERT(cond, msg) assert((cond) && (msg))
