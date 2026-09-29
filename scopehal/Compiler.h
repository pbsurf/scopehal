/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
* All rights reserved.                                                                                                 *
*                                                                                                                      *
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the     *
* following conditions are met:                                                                                        *
*                                                                                                                      *
*    * Redistributions of source code must retain the above copyright notice, this list of conditions, and the         *
*      following disclaimer.                                                                                           *
*                                                                                                                      *
*    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the       *
*      following disclaimer in the documentation and/or other materials provided with the distribution.                *
*                                                                                                                      *
*    * Neither the name of the author nor the names of any contributors may be used to endorse or promote products     *
*      derived from this software without specific prior written permission.                                           *
*                                                                                                                      *
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED   *
* TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL *
* THE AUTHORS BE HELD LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES        *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/

/**
	@file
	@brief Portability macros for compiler-specific attributes and builtins (GCC/Clang vs MSVC),
	and shims for POSIX functions the MSVC runtime lacks
 */

#ifndef Compiler_h
#define Compiler_h

#include <stddef.h>
#include <stdint.h>

#ifdef _MSC_VER

#include <intrin.h>
#include <stdlib.h>

//MSVC has no ssize_t. libiio's iio.h also typedefs it unless _SSIZE_T_DEFINED is set, so set that too
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef ptrdiff_t ssize_t;
#endif

#define ATTR_NOINLINE				__declspec(noinline)

//MSVC does not need a target attribute to use AVX2/AVX-512 intrinsics
#define ATTR_TARGET(isa)

#define ASSUME_ALIGNED(ptr, align)	(ptr)
#define BSWAP32(x)					_byteswap_ulong(x)

//POSIX functions that MinGW provides but the MSVC CRT does not
#include <direct.h>		//mkdir(), getcwd(), chdir()
#include <stdio.h>
#include <time.h>

#ifndef PATH_MAX
#define PATH_MAX 260		//MAX_PATH
#endif

#define ftello _ftelli64
#define fseeko _fseeki64

inline struct tm* localtime_r(const time_t* timep, struct tm* result)
{
	return (localtime_s(result, timep) == 0) ? result : nullptr;
}

#else

#define ATTR_NOINLINE				__attribute__((noinline))
#define ATTR_TARGET(isa)			__attribute__((target(isa)))
#define ASSUME_ALIGNED(ptr, align)	__builtin_assume_aligned((ptr), (align))
#define BSWAP32(x)					__builtin_bswap32(x)

#endif

#endif
