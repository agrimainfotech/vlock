/* libFLAC - Free Lossless Audio Codec library
 * Copyright (C) 2000,2001,2002,2003,2004,2005,2006,2007  Josh Coalson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * - Neither the name of the Xiph.org Foundation nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#define VERSION "1.2.0"
#define FLAC__NO_DLL 1

#if 1 //++ ordinals.h  
#ifndef FLAC__ORDINALS_H
#define FLAC__ORDINALS_H

#if !(defined(_MSC_VER) || defined(__BORLANDC__) || defined(__EMX__))
#include <inttypes.h>
#endif

typedef signed char FLAC__int8;
typedef unsigned char FLAC__uint8;

#if defined(_MSC_VER) || defined(__BORLANDC__)
typedef __int16 FLAC__int16;
typedef __int32 FLAC__int32;
typedef __int64 FLAC__int64;
typedef unsigned __int16 FLAC__uint16;
typedef unsigned __int32 FLAC__uint32;
typedef unsigned __int64 FLAC__uint64;
#elif defined(__EMX__)
typedef short FLAC__int16;
typedef long FLAC__int32;
typedef long long FLAC__int64;
typedef unsigned short FLAC__uint16;
typedef unsigned long FLAC__uint32;
typedef unsigned long long FLAC__uint64;
#else
typedef int16_t FLAC__int16;
typedef int32_t FLAC__int32;
typedef int64_t FLAC__int64;
typedef uint16_t FLAC__uint16;
typedef uint32_t FLAC__uint32;
typedef uint64_t FLAC__uint64;
#endif

typedef int FLAC__bool;

typedef FLAC__uint8 FLAC__byte;

#ifdef true
#undef true
#endif
#ifdef false
#undef false
#endif
#ifndef __cplusplus
#define true 1
#define false 0
#endif

#endif
#endif //++-- ordinals.h 

#if 1 //++ export.h 
#ifndef FLAC__EXPORT_H
#define FLAC__EXPORT_H

/** \file include/FLAC/export.h
 *
 *  \brief
 *  This module contains #defines and symbols for exporting function
 *  calls, and providing version information and compiled-in features.
 *
 *  See the \link flac_export export \endlink module.
 */

/** \defgroup flac_export FLAC/export.h: export symbols
 *  \ingroup flac
 *
 *  \brief
 *  This module contains #defines and symbols for exporting function
 *  calls, and providing version information and compiled-in features.
 *
 *  If you are compiling with MSVC and will link to the static library
 *  (libFLAC.lib) you should define FLAC__NO_DLL in your project to
 *  make sure the symbols are exported properly.
 *
 * \{
 */

#if defined(FLAC__NO_DLL) || !defined(_MSC_VER)
#define FLAC_API

#else

#ifdef FLAC_API_EXPORTS
#define	FLAC_API	_declspec(dllexport)
#else
#define FLAC_API	_declspec(dllimport)

#endif
#endif

/** These #defines will mirror the libtool-based library version number, see
 * http://www.gnu.org/software/libtool/manual.html#Libtool-versioning
 */
#define FLAC_API_VERSION_CURRENT 10
#define FLAC_API_VERSION_REVISION 0 /**< see above */
#define FLAC_API_VERSION_AGE 2 /**< see above */

#ifdef __cplusplus
extern "C" {
#endif

/** \c 1 if the library has been compiled with support for Ogg FLAC, else \c 0. */
extern FLAC_API int FLAC_API_SUPPORTS_OGG_FLAC;

#ifdef __cplusplus
}
#endif

/* \} */

#endif
#endif //++-- export.h 

#if 1 //++ alloc.h 
#ifndef FLAC__SHARE__ALLOC_H
#define FLAC__SHARE__ALLOC_H

#if HAVE_CONFIG_H
#  include <config.h>
#endif

/* WATCHOUT: for c++ you may have to #define __STDC_LIMIT_MACROS 1 real early
 * before #including this file,  otherwise SIZE_MAX might not be defined
 */

#include <limits.h> /* for SIZE_MAX */
#if !defined _MSC_VER && !defined __MINGW32__ && !defined __EMX__
#include <stdint.h> /* for SIZE_MAX in case limits.h didn't get it */
#endif
#include <stdlib.h> /* for size_t, malloc(), etc */

#ifndef SIZE_MAX
# ifndef SIZE_T_MAX
#   define SIZE_T_MAX 0xffffffff
# endif
# define SIZE_MAX SIZE_T_MAX
#endif

#ifndef FLaC__INLINE
#define FLaC__INLINE
#endif

/* avoid malloc()ing 0 bytes, see:
 * https://www.securecoding.cert.org/confluence/display/seccode/MEM04-A.+Do+not+make+assumptions+about+the+result+of+allocating+0+bytes?focusedCommentId=5407003
*/
static FLaC__INLINE void *safe_malloc_(size_t size)
{
	/* malloc(0) is undefined; FLAC src convention is to always allocate */
	if(!size)
		size++;
	return malloc(size);
}

static FLaC__INLINE void *safe_calloc_(size_t nmemb, size_t size)
{
	if(!nmemb || !size)
		return malloc(1); /* malloc(0) is undefined; FLAC src convention is to always allocate */
	return calloc(nmemb, size);
}

/*@@@@ there's probably a better way to prevent overflows when allocating untrusted sums but this works for now */

static FLaC__INLINE void *safe_malloc_add_2op_(size_t size1, size_t size2)
{
	size2 += size1;
	if(size2 < size1)
		return 0;
	return safe_malloc_(size2);
}

static FLaC__INLINE void *safe_malloc_add_3op_(size_t size1, size_t size2, size_t size3)
{
	size2 += size1;
	if(size2 < size1)
		return 0;
	size3 += size2;
	if(size3 < size2)
		return 0;
	return safe_malloc_(size3);
}

static FLaC__INLINE void *safe_malloc_add_4op_(size_t size1, size_t size2, size_t size3, size_t size4)
{
	size2 += size1;
	if(size2 < size1)
		return 0;
	size3 += size2;
	if(size3 < size2)
		return 0;
	size4 += size3;
	if(size4 < size3)
		return 0;
	return safe_malloc_(size4);
}

static FLaC__INLINE void *safe_malloc_mul_2op_(size_t size1, size_t size2)
#if 0
needs support for cases where sizeof(size_t) != 4
{
	/* could be faster #ifdef'ing off SIZEOF_SIZE_T */
	if(sizeof(size_t) == 4) {
		if ((double)size1 * (double)size2 < 4294967296.0)
			return malloc(size1*size2);
	}
	return 0;
}
#else
/* better? */
{
	if(!size1 || !size2)
		return malloc(1); /* malloc(0) is undefined; FLAC src convention is to always allocate */
	if(size1 > SIZE_MAX / size2)
		return 0;
	return malloc(size1*size2);
}
#endif

static FLaC__INLINE void *safe_malloc_mul_3op_(size_t size1, size_t size2, size_t size3)
{
	if(!size1 || !size2 || !size3)
		return malloc(1); /* malloc(0) is undefined; FLAC src convention is to always allocate */
	if(size1 > SIZE_MAX / size2)
		return 0;
	size1 *= size2;
	if(size1 > SIZE_MAX / size3)
		return 0;
	return malloc(size1*size3);
}

/* size1*size2 + size3 */
static FLaC__INLINE void *safe_malloc_mul2add_(size_t size1, size_t size2, size_t size3)
{
	if(!size1 || !size2)
		return safe_malloc_(size3);
	if(size1 > SIZE_MAX / size2)
		return 0;
	return safe_malloc_add_2op_(size1*size2, size3);
}

/* size1 * (size2 + size3) */
static FLaC__INLINE void *safe_malloc_muladd2_(size_t size1, size_t size2, size_t size3)
{
	if(!size1 || (!size2 && !size3))
		return malloc(1); /* malloc(0) is undefined; FLAC src convention is to always allocate */
	size2 += size3;
	if(size2 < size3)
		return 0;
	return safe_malloc_mul_2op_(size1, size2);
}

static FLaC__INLINE void *safe_realloc_add_2op_(void *ptr, size_t size1, size_t size2)
{
	size2 += size1;
	if(size2 < size1)
		return 0;
	return realloc(ptr, size2);
}

static FLaC__INLINE void *safe_realloc_add_3op_(void *ptr, size_t size1, size_t size2, size_t size3)
{
	size2 += size1;
	if(size2 < size1)
		return 0;
	size3 += size2;
	if(size3 < size2)
		return 0;
	return realloc(ptr, size3);
}

static FLaC__INLINE void *safe_realloc_add_4op_(void *ptr, size_t size1, size_t size2, size_t size3, size_t size4)
{
	size2 += size1;
	if(size2 < size1)
		return 0;
	size3 += size2;
	if(size3 < size2)
		return 0;
	size4 += size3;
	if(size4 < size3)
		return 0;
	return realloc(ptr, size4);
}

static FLaC__INLINE void *safe_realloc_mul_2op_(void *ptr, size_t size1, size_t size2)
{
	if(!size1 || !size2)
		return realloc(ptr, 0); /* preserve POSIX realloc(ptr, 0) semantics */
	if(size1 > SIZE_MAX / size2)
		return 0;
	return realloc(ptr, size1*size2);
}

/* size1 * (size2 + size3) */
static FLaC__INLINE void *safe_realloc_muladd2_(void *ptr, size_t size1, size_t size2, size_t size3)
{
	if(!size1 || (!size2 && !size3))
		return realloc(ptr, 0); /* preserve POSIX realloc(ptr, 0) semantics */
	size2 += size3;
	if(size2 < size3)
		return 0;
	return safe_realloc_mul_2op_(ptr, size1, size2);
}

#endif
#endif //++ alloc.h 

//++ assert.h 
#ifndef FLAC__ASSERT_H
#define FLAC__ASSERT_H

/* we need this since some compilers (like MSVC) leave assert()s on release code (and we don't want to use their ASSERT) */
#ifdef DEBUG
#include <assert.h>
#define FLAC__ASSERT(x) assert(x)
#define FLAC__ASSERT_DECLARATION(x) x
#else
#define FLAC__ASSERT(x)
#define FLAC__ASSERT_DECLARATION(x)
#endif

#endif

//++-- assert.h 

#if 1 //++ cpu.h 
#ifndef FLAC__PRIVATE__CPU_H
#define FLAC__PRIVATE__CPU_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

typedef enum {
	FLAC__CPUINFO_TYPE_IA32,
	FLAC__CPUINFO_TYPE_PPC,
	FLAC__CPUINFO_TYPE_UNKNOWN
} FLAC__CPUInfo_Type;

typedef struct {
	FLAC__bool cpuid;
	FLAC__bool bswap;
	FLAC__bool cmov;
	FLAC__bool mmx;
	FLAC__bool fxsr;
	FLAC__bool sse;
	FLAC__bool sse2;
	FLAC__bool sse3;
	FLAC__bool ssse3;
	FLAC__bool _3dnow;
	FLAC__bool ext3dnow;
	FLAC__bool extmmx;
} FLAC__CPUInfo_IA32;

typedef struct {
	FLAC__bool altivec;
	FLAC__bool ppc64;
} FLAC__CPUInfo_PPC;

typedef struct {
	FLAC__bool use_asm;
	FLAC__CPUInfo_Type type;
	union {
		FLAC__CPUInfo_IA32 ia32;
		FLAC__CPUInfo_PPC ppc;
	} data;
} FLAC__CPUInfo;

void FLAC__cpu_info(FLAC__CPUInfo *info);

#ifndef FLAC__NO_ASM
#ifdef FLAC__CPU_IA32
#ifdef FLAC__HAS_NASM
FLAC__uint32 FLAC__cpu_have_cpuid_asm_ia32(void);
void         FLAC__cpu_info_asm_ia32(FLAC__uint32 *flags_edx, FLAC__uint32 *flags_ecx);
FLAC__uint32 FLAC__cpu_info_extended_amd_asm_ia32(void);
#endif
#endif
#endif

#endif 
#endif //++-- cpu.h 

#if 1 //++ callback.h 
#ifndef FLAC__CALLBACK_H
#define FLAC__CALLBACK_H

#include <stdlib.h> /* for size_t */

/** \file include/FLAC/callback.h
 *
 *  \brief
 *  This module defines the structures for describing I/O callbacks
 *  to the other FLAC interfaces.
 *
 *  See the detailed documentation for callbacks in the
 *  \link flac_callbacks callbacks \endlink module.
 */

/** \defgroup flac_callbacks FLAC/callback.h: I/O callback structures
 *  \ingroup flac
 *
 *  \brief
 *  This module defines the structures for describing I/O callbacks
 *  to the other FLAC interfaces.
 *
 *  The purpose of the I/O callback functions is to create a common way
 *  for the metadata interfaces to handle I/O.
 *
 *  Originally the metadata interfaces required filenames as the way of
 *  specifying FLAC files to operate on.  This is problematic in some
 *  environments so there is an additional option to specify a set of
 *  callbacks for doing I/O on the FLAC file, instead of the filename.
 *
 *  In addition to the callbacks, a FLAC__IOHandle type is defined as an
 *  opaque structure for a data source.
 *
 *  The callback function prototypes are similar (but not identical) to the
 *  stdio functions fread, fwrite, fseek, ftell, feof, and fclose.  If you use
 *  stdio streams to implement the callbacks, you can pass fread, fwrite, and
 *  fclose anywhere a FLAC__IOCallback_Read, FLAC__IOCallback_Write, or
 *  FLAC__IOCallback_Close is required, and a FILE* anywhere a FLAC__IOHandle
 *  is required.  \warning You generally CANNOT directly use fseek or ftell
 *  for FLAC__IOCallback_Seek or FLAC__IOCallback_Tell since on most systems
 *  these use 32-bit offsets and FLAC requires 64-bit offsets to deal with
 *  large files.  You will have to find an equivalent function (e.g. ftello),
 *  or write a wrapper.  The same is true for feof() since this is usually
 *  implemented as a macro, not as a function whose address can be taken.
 *
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/** This is the opaque handle type used by the callbacks.  Typically
 *  this is a \c FILE* or address of a file descriptor.
 */
typedef void* FLAC__IOHandle;

/** Signature for the read callback.
 *  The signature and semantics match POSIX fread() implementations
 *  and can generally be used interchangeably.
 *
 * \param  ptr      The address of the read buffer.
 * \param  size     The size of the records to be read.
 * \param  nmemb    The number of records to be read.
 * \param  handle   The handle to the data source.
 * \retval size_t
 *    The number of records read.
 */
typedef size_t (*FLAC__IOCallback_Read) (void *ptr, size_t size, size_t nmemb, FLAC__IOHandle handle);

/** Signature for the write callback.
 *  The signature and semantics match POSIX fwrite() implementations
 *  and can generally be used interchangeably.
 *
 * \param  ptr      The address of the write buffer.
 * \param  size     The size of the records to be written.
 * \param  nmemb    The number of records to be written.
 * \param  handle   The handle to the data source.
 * \retval size_t
 *    The number of records written.
 */
typedef size_t (*FLAC__IOCallback_Write) (const void *ptr, size_t size, size_t nmemb, FLAC__IOHandle handle);

/** Signature for the seek callback.
 *  The signature and semantics mostly match POSIX fseek() WITH ONE IMPORTANT
 *  EXCEPTION: the offset is a 64-bit type whereas fseek() is generally 'long'
 *  and 32-bits wide.
 *
 * \param  handle   The handle to the data source.
 * \param  offset   The new position, relative to \a whence
 * \param  whence   \c SEEK_SET, \c SEEK_CUR, or \c SEEK_END
 * \retval int
 *    \c 0 on success, \c -1 on error.
 */
typedef int (*FLAC__IOCallback_Seek) (FLAC__IOHandle handle, FLAC__int64 offset, int whence);

/** Signature for the tell callback.
 *  The signature and semantics mostly match POSIX ftell() WITH ONE IMPORTANT
 *  EXCEPTION: the offset is a 64-bit type whereas ftell() is generally 'long'
 *  and 32-bits wide.
 *
 * \param  handle   The handle to the data source.
 * \retval FLAC__int64
 *    The current position on success, \c -1 on error.
 */
typedef FLAC__int64 (*FLAC__IOCallback_Tell) (FLAC__IOHandle handle);

/** Signature for the EOF callback.
 *  The signature and semantics mostly match POSIX feof() but WATCHOUT:
 *  on many systems, feof() is a macro, so in this case a wrapper function
 *  must be provided instead.
 *
 * \param  handle   The handle to the data source.
 * \retval int
 *    \c 0 if not at end of file, nonzero if at end of file.
 */
typedef int (*FLAC__IOCallback_Eof) (FLAC__IOHandle handle);

/** Signature for the close callback.
 *  The signature and semantics match POSIX fclose() implementations
 *  and can generally be used interchangeably.
 *
 * \param  handle   The handle to the data source.
 * \retval int
 *    \c 0 on success, \c EOF on error.
 */
typedef int (*FLAC__IOCallback_Close) (FLAC__IOHandle handle);

/** A structure for holding a set of callbacks.
 *  Each FLAC interface that requires a FLAC__IOCallbacks structure will
 *  describe which of the callbacks are required.  The ones that are not
 *  required may be set to NULL.
 *
 *  If the seek requirement for an interface is optional, you can signify that
 *  a data sorce is not seekable by setting the \a seek field to \c NULL.
 */
typedef struct {
	FLAC__IOCallback_Read read;
	FLAC__IOCallback_Write write;
	FLAC__IOCallback_Seek seek;
	FLAC__IOCallback_Tell tell;
	FLAC__IOCallback_Eof eof;
	FLAC__IOCallback_Close close;
} FLAC__IOCallbacks;

/* \} */

#ifdef __cplusplus
}
#endif

#endif
#endif //++-- callback.h 

#if 1 //++ format.h 
#ifndef FLAC__FORMAT_H
#define FLAC__FORMAT_H

#ifdef __cplusplus
extern "C" {
#endif

/** \file include/FLAC/format.h
 *
 *  \brief
 *  This module contains structure definitions for the representation
 *  of FLAC format components in memory.  These are the basic
 *  structures used by the rest of the interfaces.
 *
 *  See the detailed documentation in the
 *  \link flac_format format \endlink module.
 */

/** \defgroup flac_format FLAC/format.h: format components
 *  \ingroup flac
 *
 *  \brief
 *  This module contains structure definitions for the representation
 *  of FLAC format components in memory.  These are the basic
 *  structures used by the rest of the interfaces.
 *
 *  First, you should be familiar with the
 *  <A HREF="../format.html">FLAC format</A>.  Many of the values here
 *  follow directly from the specification.  As a user of libFLAC, the
 *  interesting parts really are the structures that describe the frame
 *  header and metadata blocks.
 *
 *  The format structures here are very primitive, designed to store
 *  information in an efficient way.  Reading information from the
 *  structures is easy but creating or modifying them directly is
 *  more complex.  For the most part, as a user of a library, editing
 *  is not necessary; however, for metadata blocks it is, so there are
 *  convenience functions provided in the \link flac_metadata metadata
 *  module \endlink to simplify the manipulation of metadata blocks.
 *
 * \note
 * It's not the best convention, but symbols ending in _LEN are in bits
 * and _LENGTH are in bytes.  _LENGTH symbols are \#defines instead of
 * global variables because they are usually used when declaring byte
 * arrays and some compilers require compile-time knowledge of array
 * sizes when declared on the stack.
 *
 * \{
 */


/*
	Most of the values described in this file are defined by the FLAC
	format specification.  There is nothing to tune here.
*/

/** The largest legal metadata type code. */
#define FLAC__MAX_METADATA_TYPE_CODE (126u)

/** The minimum block size, in samples, permitted by the format. */
#define FLAC__MIN_BLOCK_SIZE (16u)

/** The maximum block size, in samples, permitted by the format. */
#define FLAC__MAX_BLOCK_SIZE (65535u)

/** The maximum block size, in samples, permitted by the FLAC subset for
 *  sample rates up to 48kHz. */
#define FLAC__SUBSET_MAX_BLOCK_SIZE_48000HZ (4608u)

/** The maximum number of channels permitted by the format. */
#define FLAC__MAX_CHANNELS (8u)

/** The minimum sample resolution permitted by the format. */
#define FLAC__MIN_BITS_PER_SAMPLE (4u)

/** The maximum sample resolution permitted by the format. */
#define FLAC__MAX_BITS_PER_SAMPLE (32u)

/** The maximum sample resolution permitted by libFLAC.
 *
 * \warning
 * FLAC__MAX_BITS_PER_SAMPLE is the limit of the FLAC format.  However,
 * the reference encoder/decoder is currently limited to 24 bits because
 * of prevalent 32-bit math, so make sure and use this value when
 * appropriate.
 */
#define FLAC__REFERENCE_CODEC_MAX_BITS_PER_SAMPLE (24u)

/** The maximum sample rate permitted by the format.  The value is
 *  ((2 ^ 16) - 1) * 10; see <A HREF="../format.html">FLAC format</A>
 *  as to why.
 */
#define FLAC__MAX_SAMPLE_RATE (655350u)

/** The maximum LPC order permitted by the format. */
#define FLAC__MAX_LPC_ORDER (32u)

/** The maximum LPC order permitted by the FLAC subset for sample rates
 *  up to 48kHz. */
#define FLAC__SUBSET_MAX_LPC_ORDER_48000HZ (12u)

/** The minimum quantized linear predictor coefficient precision
 *  permitted by the format.
 */
#define FLAC__MIN_QLP_COEFF_PRECISION (5u)

/** The maximum quantized linear predictor coefficient precision
 *  permitted by the format.
 */
#define FLAC__MAX_QLP_COEFF_PRECISION (15u)

/** The maximum order of the fixed predictors permitted by the format. */
#define FLAC__MAX_FIXED_ORDER (4u)

/** The maximum Rice partition order permitted by the format. */
#define FLAC__MAX_RICE_PARTITION_ORDER (15u)

/** The maximum Rice partition order permitted by the FLAC Subset. */
#define FLAC__SUBSET_MAX_RICE_PARTITION_ORDER (8u)

/** The version string of the release, stamped onto the libraries and binaries.
 *
 * \note
 * This does not correspond to the shared library version number, which
 * is used to determine binary compatibility.
 */
extern FLAC_API const char *FLAC__VERSION_STRING;

/** The vendor string inserted by the encoder into the VORBIS_COMMENT block.
 *  This is a NUL-terminated ASCII string; when inserted into the
 *  VORBIS_COMMENT the trailing null is stripped.
 */
extern FLAC_API const char *FLAC__VENDOR_STRING;

/** The byte string representation of the beginning of a FLAC stream. */
extern FLAC_API const FLAC__byte FLAC__STREAM_SYNC_STRING[4]; /* = "fLaC" */

/** The 32-bit integer big-endian representation of the beginning of
 *  a FLAC stream.
 */
extern FLAC_API const unsigned FLAC__STREAM_SYNC; /* = 0x664C6143 */

/** The length of the FLAC signature in bits. */
extern FLAC_API const unsigned FLAC__STREAM_SYNC_LEN; /* = 32 bits */

/** The length of the FLAC signature in bytes. */
#define FLAC__STREAM_SYNC_LENGTH (4u)


/*****************************************************************************
 *
 * Subframe structures
 *
 *****************************************************************************/

/*****************************************************************************/

/** An enumeration of the available entropy coding methods. */
typedef enum {
	FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE = 0,
	/**< Residual is coded by partitioning into contexts, each with it's own
	 * 4-bit Rice parameter. */

	FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2 = 1
	/**< Residual is coded by partitioning into contexts, each with it's own
	 * 5-bit Rice parameter. */
} FLAC__EntropyCodingMethodType;

/** Maps a FLAC__EntropyCodingMethodType to a C string.
 *
 *  Using a FLAC__EntropyCodingMethodType as the index to this array will
 *  give the string equivalent.  The contents should not be modified.
 */
extern FLAC_API const char * const FLAC__EntropyCodingMethodTypeString[];


/** Contents of a Rice partitioned residual
 */
typedef struct {

	unsigned *parameters;
	/**< The Rice parameters for each context. */

	unsigned *raw_bits;
	/**< Widths for escape-coded partitions.  Will be non-zero for escaped
	 * partitions and zero for unescaped partitions.
	 */

	unsigned capacity_by_order;
	/**< The capacity of the \a parameters and \a raw_bits arrays
	 * specified as an order, i.e. the number of array elements
	 * allocated is 2 ^ \a capacity_by_order.
	 */
} FLAC__EntropyCodingMethod_PartitionedRiceContents;

/** Header for a Rice partitioned residual.  (c.f. <A HREF="../format.html#partitioned_rice">format specification</A>)
 */
typedef struct {

	unsigned order;
	/**< The partition order, i.e. # of contexts = 2 ^ \a order. */

	const FLAC__EntropyCodingMethod_PartitionedRiceContents *contents;
	/**< The context's Rice parameters and/or raw bits. */

} FLAC__EntropyCodingMethod_PartitionedRice;

extern FLAC_API const unsigned FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ORDER_LEN; /**< == 4 (bits) */
extern FLAC_API const unsigned FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_PARAMETER_LEN; /**< == 4 (bits) */
extern FLAC_API const unsigned FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2_PARAMETER_LEN; /**< == 5 (bits) */
extern FLAC_API const unsigned FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_RAW_LEN; /**< == 5 (bits) */

extern FLAC_API const unsigned FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER;
/**< == (1<<FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_PARAMETER_LEN)-1 */
extern FLAC_API const unsigned FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2_ESCAPE_PARAMETER;
/**< == (1<<FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2_PARAMETER_LEN)-1 */

/** Header for the entropy coding method.  (c.f. <A HREF="../format.html#residual">format specification</A>)
 */
typedef struct {
	FLAC__EntropyCodingMethodType type;
	union {
		FLAC__EntropyCodingMethod_PartitionedRice partitioned_rice;
	} data;
} FLAC__EntropyCodingMethod;

extern FLAC_API const unsigned FLAC__ENTROPY_CODING_METHOD_TYPE_LEN; /**< == 2 (bits) */

/*****************************************************************************/

/** An enumeration of the available subframe types. */
typedef enum {
	FLAC__SUBFRAME_TYPE_CONSTANT = 0, /**< constant signal */
	FLAC__SUBFRAME_TYPE_VERBATIM = 1, /**< uncompressed signal */
	FLAC__SUBFRAME_TYPE_FIXED = 2, /**< fixed polynomial prediction */
	FLAC__SUBFRAME_TYPE_LPC = 3 /**< linear prediction */
} FLAC__SubframeType;

/** Maps a FLAC__SubframeType to a C string.
 *
 *  Using a FLAC__SubframeType as the index to this array will
 *  give the string equivalent.  The contents should not be modified.
 */
extern FLAC_API const char * const FLAC__SubframeTypeString[];


/** CONSTANT subframe.  (c.f. <A HREF="../format.html#subframe_constant">format specification</A>)
 */
typedef struct {
	FLAC__int32 value; /**< The constant signal value. */
} FLAC__Subframe_Constant;


/** VERBATIM subframe.  (c.f. <A HREF="../format.html#subframe_verbatim">format specification</A>)
 */
typedef struct {
	const FLAC__int32 *data; /**< A pointer to verbatim signal. */
} FLAC__Subframe_Verbatim;


/** FIXED subframe.  (c.f. <A HREF="../format.html#subframe_fixed">format specification</A>)
 */
typedef struct {
	FLAC__EntropyCodingMethod entropy_coding_method;
	/**< The residual coding method. */

	unsigned order;
	/**< The polynomial order. */

	FLAC__int32 warmup[FLAC__MAX_FIXED_ORDER];
	/**< Warmup samples to prime the predictor, length == order. */

	const FLAC__int32 *residual;
	/**< The residual signal, length == (blocksize minus order) samples. */
} FLAC__Subframe_Fixed;


/** LPC subframe.  (c.f. <A HREF="../format.html#subframe_lpc">format specification</A>)
 */
typedef struct {
	FLAC__EntropyCodingMethod entropy_coding_method;
	/**< The residual coding method. */

	unsigned order;
	/**< The FIR order. */

	unsigned qlp_coeff_precision;
	/**< Quantized FIR filter coefficient precision in bits. */

	int quantization_level;
	/**< The qlp coeff shift needed. */

	FLAC__int32 qlp_coeff[FLAC__MAX_LPC_ORDER];
	/**< FIR filter coefficients. */

	FLAC__int32 warmup[FLAC__MAX_LPC_ORDER];
	/**< Warmup samples to prime the predictor, length == order. */

	const FLAC__int32 *residual;
	/**< The residual signal, length == (blocksize minus order) samples. */
} FLAC__Subframe_LPC;

extern FLAC_API const unsigned FLAC__SUBFRAME_LPC_QLP_COEFF_PRECISION_LEN; /**< == 4 (bits) */
extern FLAC_API const unsigned FLAC__SUBFRAME_LPC_QLP_SHIFT_LEN; /**< == 5 (bits) */


/** FLAC subframe structure.  (c.f. <A HREF="../format.html#subframe">format specification</A>)
 */
typedef struct {
	FLAC__SubframeType type;
	union {
		FLAC__Subframe_Constant constant;
		FLAC__Subframe_Fixed fixed;
		FLAC__Subframe_LPC lpc;
		FLAC__Subframe_Verbatim verbatim;
	} data;
	unsigned wasted_bits;
} FLAC__Subframe;

/** == 1 (bit)
 *
 * This used to be a zero-padding bit (hence the name
 * FLAC__SUBFRAME_ZERO_PAD_LEN) but is now a reserved bit.  It still has a
 * mandatory value of \c 0 but in the future may take on the value \c 0 or \c 1
 * to mean something else.
 */
extern FLAC_API const unsigned FLAC__SUBFRAME_ZERO_PAD_LEN;
extern FLAC_API const unsigned FLAC__SUBFRAME_TYPE_LEN; /**< == 6 (bits) */
extern FLAC_API const unsigned FLAC__SUBFRAME_WASTED_BITS_FLAG_LEN; /**< == 1 (bit) */

extern FLAC_API const unsigned FLAC__SUBFRAME_TYPE_CONSTANT_BYTE_ALIGNED_MASK; /**< = 0x00 */
extern FLAC_API const unsigned FLAC__SUBFRAME_TYPE_VERBATIM_BYTE_ALIGNED_MASK; /**< = 0x02 */
extern FLAC_API const unsigned FLAC__SUBFRAME_TYPE_FIXED_BYTE_ALIGNED_MASK; /**< = 0x10 */
extern FLAC_API const unsigned FLAC__SUBFRAME_TYPE_LPC_BYTE_ALIGNED_MASK; /**< = 0x40 */

/*****************************************************************************/


/*****************************************************************************
 *
 * Frame structures
 *
 *****************************************************************************/

/** An enumeration of the available channel assignments. */
typedef enum {
	FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT = 0, /**< independent channels */
	FLAC__CHANNEL_ASSIGNMENT_LEFT_SIDE = 1, /**< left+side stereo */
	FLAC__CHANNEL_ASSIGNMENT_RIGHT_SIDE = 2, /**< right+side stereo */
	FLAC__CHANNEL_ASSIGNMENT_MID_SIDE = 3 /**< mid+side stereo */
} FLAC__ChannelAssignment;

/** Maps a FLAC__ChannelAssignment to a C string.
 *
 *  Using a FLAC__ChannelAssignment as the index to this array will
 *  give the string equivalent.  The contents should not be modified.
 */
extern FLAC_API const char * const FLAC__ChannelAssignmentString[];

/** An enumeration of the possible frame numbering methods. */
typedef enum {
	FLAC__FRAME_NUMBER_TYPE_FRAME_NUMBER, /**< number contains the frame number */
	FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER /**< number contains the sample number of first sample in frame */
} FLAC__FrameNumberType;

/** Maps a FLAC__FrameNumberType to a C string.
 *
 *  Using a FLAC__FrameNumberType as the index to this array will
 *  give the string equivalent.  The contents should not be modified.
 */
extern FLAC_API const char * const FLAC__FrameNumberTypeString[];


/** FLAC frame header structure.  (c.f. <A HREF="../format.html#frame_header">format specification</A>)
 */
typedef struct {
	unsigned blocksize;
	/**< The number of samples per subframe. */

	unsigned sample_rate;
	/**< The sample rate in Hz. */

	unsigned channels;
	/**< The number of channels (== number of subframes). */

	FLAC__ChannelAssignment channel_assignment;
	/**< The channel assignment for the frame. */

	unsigned bits_per_sample;
	/**< The sample resolution. */

	FLAC__FrameNumberType number_type;
	/**< The numbering scheme used for the frame.  As a convenience, the
	 * decoder will always convert a frame number to a sample number because
	 * the rules are complex. */

	union {
		FLAC__uint32 frame_number;
		FLAC__uint64 sample_number;
	} number;
	/**< The frame number or sample number of first sample in frame;
	 * use the \a number_type value to determine which to use. */

	FLAC__uint8 crc;
	/**< CRC-8 (polynomial = x^8 + x^2 + x^1 + x^0, initialized with 0)
	 * of the raw frame header bytes, meaning everything before the CRC byte
	 * including the sync code.
	 */
} FLAC__FrameHeader;

extern FLAC_API const unsigned FLAC__FRAME_HEADER_SYNC; /**< == 0x3ffe; the frame header sync code */
extern FLAC_API const unsigned FLAC__FRAME_HEADER_SYNC_LEN; /**< == 14 (bits) */
extern FLAC_API const unsigned FLAC__FRAME_HEADER_RESERVED_LEN; /**< == 1 (bits) */
extern FLAC_API const unsigned FLAC__FRAME_HEADER_BLOCKING_STRATEGY_LEN; /**< == 1 (bits) */
extern FLAC_API const unsigned FLAC__FRAME_HEADER_BLOCK_SIZE_LEN; /**< == 4 (bits) */
extern FLAC_API const unsigned FLAC__FRAME_HEADER_SAMPLE_RATE_LEN; /**< == 4 (bits) */
extern FLAC_API const unsigned FLAC__FRAME_HEADER_CHANNEL_ASSIGNMENT_LEN; /**< == 4 (bits) */
extern FLAC_API const unsigned FLAC__FRAME_HEADER_BITS_PER_SAMPLE_LEN; /**< == 3 (bits) */
extern FLAC_API const unsigned FLAC__FRAME_HEADER_ZERO_PAD_LEN; /**< == 1 (bit) */
extern FLAC_API const unsigned FLAC__FRAME_HEADER_CRC_LEN; /**< == 8 (bits) */


/** FLAC frame footer structure.  (c.f. <A HREF="../format.html#frame_footer">format specification</A>)
 */
typedef struct {
	FLAC__uint16 crc;
	/**< CRC-16 (polynomial = x^16 + x^15 + x^2 + x^0, initialized with
	 * 0) of the bytes before the crc, back to and including the frame header
	 * sync code.
	 */
} FLAC__FrameFooter;

extern FLAC_API const unsigned FLAC__FRAME_FOOTER_CRC_LEN; /**< == 16 (bits) */


/** FLAC frame structure.  (c.f. <A HREF="../format.html#frame">format specification</A>)
 */
typedef struct {
	FLAC__FrameHeader header;
	FLAC__Subframe subframes[FLAC__MAX_CHANNELS];
	FLAC__FrameFooter footer;
} FLAC__Frame;

/*****************************************************************************/


/*****************************************************************************
 *
 * Meta-data structures
 *
 *****************************************************************************/

/** An enumeration of the available metadata block types. */
typedef enum {

	FLAC__METADATA_TYPE_STREAMINFO = 0,
	/**< <A HREF="../format.html#metadata_block_streaminfo">STREAMINFO</A> block */

	FLAC__METADATA_TYPE_PADDING = 1,
	/**< <A HREF="../format.html#metadata_block_padding">PADDING</A> block */

	FLAC__METADATA_TYPE_APPLICATION = 2,
	/**< <A HREF="../format.html#metadata_block_application">APPLICATION</A> block */

	FLAC__METADATA_TYPE_SEEKTABLE = 3,
	/**< <A HREF="../format.html#metadata_block_seektable">SEEKTABLE</A> block */

	FLAC__METADATA_TYPE_VORBIS_COMMENT = 4,
	/**< <A HREF="../format.html#metadata_block_vorbis_comment">VORBISCOMMENT</A> block (a.k.a. FLAC tags) */

	FLAC__METADATA_TYPE_CUESHEET = 5,
	/**< <A HREF="../format.html#metadata_block_cuesheet">CUESHEET</A> block */

	FLAC__METADATA_TYPE_PICTURE = 6,
	/**< <A HREF="../format.html#metadata_block_picture">PICTURE</A> block */

	FLAC__METADATA_TYPE_UNDEFINED = 7
	/**< marker to denote beginning of undefined type range; this number will increase as new metadata types are added */

} FLAC__MetadataType;

/** Maps a FLAC__MetadataType to a C string.
 *
 *  Using a FLAC__MetadataType as the index to this array will
 *  give the string equivalent.  The contents should not be modified.
 */
extern FLAC_API const char * const FLAC__MetadataTypeString[];


/** FLAC STREAMINFO structure.  (c.f. <A HREF="../format.html#metadata_block_streaminfo">format specification</A>)
 */
typedef struct {
	unsigned min_blocksize, max_blocksize;
	unsigned min_framesize, max_framesize;
	unsigned sample_rate;
	unsigned channels;
	unsigned bits_per_sample;
	FLAC__uint64 total_samples;
	FLAC__byte md5sum[16];
} FLAC__StreamMetadata_StreamInfo;

extern FLAC_API const unsigned FLAC__STREAM_METADATA_STREAMINFO_MIN_BLOCK_SIZE_LEN; /**< == 16 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_STREAMINFO_MAX_BLOCK_SIZE_LEN; /**< == 16 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_STREAMINFO_MIN_FRAME_SIZE_LEN; /**< == 24 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_STREAMINFO_MAX_FRAME_SIZE_LEN; /**< == 24 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_STREAMINFO_SAMPLE_RATE_LEN; /**< == 20 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_STREAMINFO_CHANNELS_LEN; /**< == 3 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_STREAMINFO_BITS_PER_SAMPLE_LEN; /**< == 5 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_STREAMINFO_TOTAL_SAMPLES_LEN; /**< == 36 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_STREAMINFO_MD5SUM_LEN; /**< == 128 (bits) */

/** The total stream length of the STREAMINFO block in bytes. */
#define FLAC__STREAM_METADATA_STREAMINFO_LENGTH (34u)

/** FLAC PADDING structure.  (c.f. <A HREF="../format.html#metadata_block_padding">format specification</A>)
 */
typedef struct {
	int dummy;
	/**< Conceptually this is an empty struct since we don't store the
	 * padding bytes.  Empty structs are not allowed by some C compilers,
	 * hence the dummy.
	 */
} FLAC__StreamMetadata_Padding;


/** FLAC APPLICATION structure.  (c.f. <A HREF="../format.html#metadata_block_application">format specification</A>)
 */
typedef struct {
	FLAC__byte id[4];
	FLAC__byte *data;
} FLAC__StreamMetadata_Application;

extern FLAC_API const unsigned FLAC__STREAM_METADATA_APPLICATION_ID_LEN; /**< == 32 (bits) */

/** SeekPoint structure used in SEEKTABLE blocks.  (c.f. <A HREF="../format.html#seekpoint">format specification</A>)
 */
typedef struct {
	FLAC__uint64 sample_number;
	/**<  The sample number of the target frame. */

	FLAC__uint64 stream_offset;
	/**< The offset, in bytes, of the target frame with respect to
	 * beginning of the first frame. */

	unsigned frame_samples;
	/**< The number of samples in the target frame. */
} FLAC__StreamMetadata_SeekPoint;

extern FLAC_API const unsigned FLAC__STREAM_METADATA_SEEKPOINT_SAMPLE_NUMBER_LEN; /**< == 64 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_SEEKPOINT_STREAM_OFFSET_LEN; /**< == 64 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_SEEKPOINT_FRAME_SAMPLES_LEN; /**< == 16 (bits) */

/** The total stream length of a seek point in bytes. */
#define FLAC__STREAM_METADATA_SEEKPOINT_LENGTH (18u)

/** The value used in the \a sample_number field of
 *  FLAC__StreamMetadataSeekPoint used to indicate a placeholder
 *  point (== 0xffffffffffffffff).
 */
extern FLAC_API const FLAC__uint64 FLAC__STREAM_METADATA_SEEKPOINT_PLACEHOLDER;


/** FLAC SEEKTABLE structure.  (c.f. <A HREF="../format.html#metadata_block_seektable">format specification</A>)
 *
 * \note From the format specification:
 * - The seek points must be sorted by ascending sample number.
 * - Each seek point's sample number must be the first sample of the
 *   target frame.
 * - Each seek point's sample number must be unique within the table.
 * - Existence of a SEEKTABLE block implies a correct setting of
 *   total_samples in the stream_info block.
 * - Behavior is undefined when more than one SEEKTABLE block is
 *   present in a stream.
 */
typedef struct {
	unsigned num_points;
	FLAC__StreamMetadata_SeekPoint *points;
} FLAC__StreamMetadata_SeekTable;


/** Vorbis comment entry structure used in VORBIS_COMMENT blocks.  (c.f. <A HREF="../format.html#metadata_block_vorbis_comment">format specification</A>)
 *
 *  For convenience, the APIs maintain a trailing NUL character at the end of
 *  \a entry which is not counted toward \a length, i.e.
 *  \code strlen(entry) == length \endcode
 */
typedef struct {
	FLAC__uint32 length;
	FLAC__byte *entry;
} FLAC__StreamMetadata_VorbisComment_Entry;

extern FLAC_API const unsigned FLAC__STREAM_METADATA_VORBIS_COMMENT_ENTRY_LENGTH_LEN; /**< == 32 (bits) */


/** FLAC VORBIS_COMMENT structure.  (c.f. <A HREF="../format.html#metadata_block_vorbis_comment">format specification</A>)
 */
typedef struct {
	FLAC__StreamMetadata_VorbisComment_Entry vendor_string;
	FLAC__uint32 num_comments;
	FLAC__StreamMetadata_VorbisComment_Entry *comments;
} FLAC__StreamMetadata_VorbisComment;

extern FLAC_API const unsigned FLAC__STREAM_METADATA_VORBIS_COMMENT_NUM_COMMENTS_LEN; /**< == 32 (bits) */


/** FLAC CUESHEET track index structure.  (See the
 * <A HREF="../format.html#cuesheet_track_index">format specification</A> for
 * the full description of each field.)
 */
typedef struct {
	FLAC__uint64 offset;
	/**< Offset in samples, relative to the track offset, of the index
	 * point.
	 */

	FLAC__byte number;
	/**< The index point number. */
} FLAC__StreamMetadata_CueSheet_Index;

extern FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_INDEX_OFFSET_LEN; /**< == 64 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_INDEX_NUMBER_LEN; /**< == 8 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_INDEX_RESERVED_LEN; /**< == 3*8 (bits) */


/** FLAC CUESHEET track structure.  (See the
 * <A HREF="../format.html#cuesheet_track">format specification</A> for
 * the full description of each field.)
 */
typedef struct {
	FLAC__uint64 offset;
	/**< Track offset in samples, relative to the beginning of the FLAC audio stream. */

	FLAC__byte number;
	/**< The track number. */

	char isrc[13];
	/**< Track ISRC.  This is a 12-digit alphanumeric code plus a trailing \c NUL byte */

	unsigned type:1;
	/**< The track type: 0 for audio, 1 for non-audio. */

	unsigned pre_emphasis:1;
	/**< The pre-emphasis flag: 0 for no pre-emphasis, 1 for pre-emphasis. */

	FLAC__byte num_indices;
	/**< The number of track index points. */

	FLAC__StreamMetadata_CueSheet_Index *indices;
	/**< NULL if num_indices == 0, else pointer to array of index points. */

} FLAC__StreamMetadata_CueSheet_Track;

extern FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_TRACK_OFFSET_LEN; /**< == 64 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_TRACK_NUMBER_LEN; /**< == 8 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_TRACK_ISRC_LEN; /**< == 12*8 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_TRACK_TYPE_LEN; /**< == 1 (bit) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_TRACK_PRE_EMPHASIS_LEN; /**< == 1 (bit) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_TRACK_RESERVED_LEN; /**< == 6+13*8 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_TRACK_NUM_INDICES_LEN; /**< == 8 (bits) */


/** FLAC CUESHEET structure.  (See the
 * <A HREF="../format.html#metadata_block_cuesheet">format specification</A>
 * for the full description of each field.)
 */
typedef struct {
	char media_catalog_number[129];
	/**< Media catalog number, in ASCII printable characters 0x20-0x7e.  In
	 * general, the media catalog number may be 0 to 128 bytes long; any
	 * unused characters should be right-padded with NUL characters.
	 */

	FLAC__uint64 lead_in;
	/**< The number of lead-in samples. */

	FLAC__bool is_cd;
	/**< \c true if CUESHEET corresponds to a Compact Disc, else \c false. */

	unsigned num_tracks;
	/**< The number of tracks. */

	FLAC__StreamMetadata_CueSheet_Track *tracks;
	/**< NULL if num_tracks == 0, else pointer to array of tracks. */

} FLAC__StreamMetadata_CueSheet;

extern FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_MEDIA_CATALOG_NUMBER_LEN; /**< == 128*8 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_LEAD_IN_LEN; /**< == 64 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_IS_CD_LEN; /**< == 1 (bit) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_RESERVED_LEN; /**< == 7+258*8 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_NUM_TRACKS_LEN; /**< == 8 (bits) */


/** An enumeration of the PICTURE types (see FLAC__StreamMetadataPicture and id3 v2.4 APIC tag). */
typedef enum {
	FLAC__STREAM_METADATA_PICTURE_TYPE_OTHER = 0, /**< Other */
	FLAC__STREAM_METADATA_PICTURE_TYPE_FILE_ICON_STANDARD = 1, /**< 32x32 pixels 'file icon' (PNG only) */
	FLAC__STREAM_METADATA_PICTURE_TYPE_FILE_ICON = 2, /**< Other file icon */
	FLAC__STREAM_METADATA_PICTURE_TYPE_FRONT_COVER = 3, /**< Cover (front) */
	FLAC__STREAM_METADATA_PICTURE_TYPE_BACK_COVER = 4, /**< Cover (back) */
	FLAC__STREAM_METADATA_PICTURE_TYPE_LEAFLET_PAGE = 5, /**< Leaflet page */
	FLAC__STREAM_METADATA_PICTURE_TYPE_MEDIA = 6, /**< Media (e.g. label side of CD) */
	FLAC__STREAM_METADATA_PICTURE_TYPE_LEAD_ARTIST = 7, /**< Lead artist/lead performer/soloist */
	FLAC__STREAM_METADATA_PICTURE_TYPE_ARTIST = 8, /**< Artist/performer */
	FLAC__STREAM_METADATA_PICTURE_TYPE_CONDUCTOR = 9, /**< Conductor */
	FLAC__STREAM_METADATA_PICTURE_TYPE_BAND = 10, /**< Band/Orchestra */
	FLAC__STREAM_METADATA_PICTURE_TYPE_COMPOSER = 11, /**< Composer */
	FLAC__STREAM_METADATA_PICTURE_TYPE_LYRICIST = 12, /**< Lyricist/text writer */
	FLAC__STREAM_METADATA_PICTURE_TYPE_RECORDING_LOCATION = 13, /**< Recording Location */
	FLAC__STREAM_METADATA_PICTURE_TYPE_DURING_RECORDING = 14, /**< During recording */
	FLAC__STREAM_METADATA_PICTURE_TYPE_DURING_PERFORMANCE = 15, /**< During performance */
	FLAC__STREAM_METADATA_PICTURE_TYPE_VIDEO_SCREEN_CAPTURE = 16, /**< Movie/video screen capture */
	FLAC__STREAM_METADATA_PICTURE_TYPE_FISH = 17, /**< A bright coloured fish */
	FLAC__STREAM_METADATA_PICTURE_TYPE_ILLUSTRATION = 18, /**< Illustration */
	FLAC__STREAM_METADATA_PICTURE_TYPE_BAND_LOGOTYPE = 19, /**< Band/artist logotype */
	FLAC__STREAM_METADATA_PICTURE_TYPE_PUBLISHER_LOGOTYPE = 20, /**< Publisher/Studio logotype */
	FLAC__STREAM_METADATA_PICTURE_TYPE_UNDEFINED
} FLAC__StreamMetadata_Picture_Type;

/** Maps a FLAC__StreamMetadata_Picture_Type to a C string.
 *
 *  Using a FLAC__StreamMetadata_Picture_Type as the index to this array
 *  will give the string equivalent.  The contents should not be
 *  modified.
 */
extern FLAC_API const char * const FLAC__StreamMetadata_Picture_TypeString[];

/** FLAC PICTURE structure.  (See the
 * <A HREF="../format.html#metadata_block_picture">format specification</A>
 * for the full description of each field.)
 */
typedef struct {
	FLAC__StreamMetadata_Picture_Type type;
	/**< The kind of picture stored. */

	char *mime_type;
	/**< Picture data's MIME type, in ASCII printable characters
	 * 0x20-0x7e, NUL terminated.  For best compatibility with players,
	 * use picture data of MIME type \c image/jpeg or \c image/png.  A
	 * MIME type of '-->' is also allowed, in which case the picture
	 * data should be a complete URL.  In file storage, the MIME type is
	 * stored as a 32-bit length followed by the ASCII string with no NUL
	 * terminator, but is converted to a plain C string in this structure
	 * for convenience.
	 */

	FLAC__byte *description;
	/**< Picture's description in UTF-8, NUL terminated.  In file storage,
	 * the description is stored as a 32-bit length followed by the UTF-8
	 * string with no NUL terminator, but is converted to a plain C string
	 * in this structure for convenience.
	 */

	FLAC__uint32 width;
	/**< Picture's width in pixels. */

	FLAC__uint32 height;
	/**< Picture's height in pixels. */

	FLAC__uint32 depth;
	/**< Picture's color depth in bits-per-pixel. */

	FLAC__uint32 colors;
	/**< For indexed palettes (like GIF), picture's number of colors (the
	 * number of palette entries), or \c 0 for non-indexed (i.e. 2^depth).
	 */

	FLAC__uint32 data_length;
	/**< Length of binary picture data in bytes. */

	FLAC__byte *data;
	/**< Binary picture data. */

} FLAC__StreamMetadata_Picture;

extern FLAC_API const unsigned FLAC__STREAM_METADATA_PICTURE_TYPE_LEN; /**< == 32 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_PICTURE_MIME_TYPE_LENGTH_LEN; /**< == 32 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_PICTURE_DESCRIPTION_LENGTH_LEN; /**< == 32 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_PICTURE_WIDTH_LEN; /**< == 32 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_PICTURE_HEIGHT_LEN; /**< == 32 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_PICTURE_DEPTH_LEN; /**< == 32 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_PICTURE_COLORS_LEN; /**< == 32 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_PICTURE_DATA_LENGTH_LEN; /**< == 32 (bits) */


/** Structure that is used when a metadata block of unknown type is loaded.
 *  The contents are opaque.  The structure is used only internally to
 *  correctly handle unknown metadata.
 */
typedef struct {
	FLAC__byte *data;
} FLAC__StreamMetadata_Unknown;

/** FLAC metadata block structure.  (c.f. <A HREF="../format.html#metadata_block">format specification</A>)
 */
typedef struct {
	FLAC__MetadataType type;
	/**< The type of the metadata block; used determine which member of the
	 * \a data union to dereference.  If type >= FLAC__METADATA_TYPE_UNDEFINED
	 * then \a data.unknown must be used. */

	FLAC__bool is_last;
	/**< \c true if this metadata block is the last, else \a false */

	unsigned length;
	/**< Length, in bytes, of the block data as it appears in the stream. */

	union {
		FLAC__StreamMetadata_StreamInfo stream_info;
		FLAC__StreamMetadata_Padding padding;
		FLAC__StreamMetadata_Application application;
		FLAC__StreamMetadata_SeekTable seek_table;
		FLAC__StreamMetadata_VorbisComment vorbis_comment;
		FLAC__StreamMetadata_CueSheet cue_sheet;
		FLAC__StreamMetadata_Picture picture;
		FLAC__StreamMetadata_Unknown unknown;
	} data;
	/**< Polymorphic block data; use the \a type value to determine which
	 * to use. */
} FLAC__StreamMetadata;

extern FLAC_API const unsigned FLAC__STREAM_METADATA_IS_LAST_LEN; /**< == 1 (bit) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_TYPE_LEN; /**< == 7 (bits) */
extern FLAC_API const unsigned FLAC__STREAM_METADATA_LENGTH_LEN; /**< == 24 (bits) */

/** The total stream length of a metadata block header in bytes. */
#define FLAC__STREAM_METADATA_HEADER_LENGTH (4u)

/*****************************************************************************/


/*****************************************************************************
 *
 * Utility functions
 *
 *****************************************************************************/

/** Tests that a sample rate is valid for FLAC.
 *
 * \param sample_rate  The sample rate to test for compliance.
 * \retval FLAC__bool
 *    \c true if the given sample rate conforms to the specification, else
 *    \c false.
 */
FLAC_API FLAC__bool FLAC__format_sample_rate_is_valid(unsigned sample_rate);

/** Tests that a sample rate is valid for the FLAC subset.  The subset rules
 *  for valid sample rates are slightly more complex since the rate has to
 *  be expressible completely in the frame header.
 *
 * \param sample_rate  The sample rate to test for compliance.
 * \retval FLAC__bool
 *    \c true if the given sample rate conforms to the specification for the
 *    subset, else \c false.
 */
FLAC_API FLAC__bool FLAC__format_sample_rate_is_subset(unsigned sample_rate);

/** Check a Vorbis comment entry name to see if it conforms to the Vorbis
 *  comment specification.
 *
 *  Vorbis comment names must be composed only of characters from
 *  [0x20-0x3C,0x3E-0x7D].
 *
 * \param name       A NUL-terminated string to be checked.
 * \assert
 *    \code name != NULL \endcode
 * \retval FLAC__bool
 *    \c false if entry name is illegal, else \c true.
 */
FLAC_API FLAC__bool FLAC__format_vorbiscomment_entry_name_is_legal(const char *name);

/** Check a Vorbis comment entry value to see if it conforms to the Vorbis
 *  comment specification.
 *
 *  Vorbis comment values must be valid UTF-8 sequences.
 *
 * \param value      A string to be checked.
 * \param length     A the length of \a value in bytes.  May be
 *                   \c (unsigned)(-1) to indicate that \a value is a plain
 *                   UTF-8 NUL-terminated string.
 * \assert
 *    \code value != NULL \endcode
 * \retval FLAC__bool
 *    \c false if entry name is illegal, else \c true.
 */
FLAC_API FLAC__bool FLAC__format_vorbiscomment_entry_value_is_legal(const FLAC__byte *value, unsigned length);

/** Check a Vorbis comment entry to see if it conforms to the Vorbis
 *  comment specification.
 *
 *  Vorbis comment entries must be of the form 'name=value', and 'name' and
 *  'value' must be legal according to
 *  FLAC__format_vorbiscomment_entry_name_is_legal() and
 *  FLAC__format_vorbiscomment_entry_value_is_legal() respectively.
 *
 * \param entry      An entry to be checked.
 * \param length     The length of \a entry in bytes.
 * \assert
 *    \code value != NULL \endcode
 * \retval FLAC__bool
 *    \c false if entry name is illegal, else \c true.
 */
FLAC_API FLAC__bool FLAC__format_vorbiscomment_entry_is_legal(const FLAC__byte *entry, unsigned length);

/** Check a seek table to see if it conforms to the FLAC specification.
 *  See the format specification for limits on the contents of the
 *  seek table.
 *
 * \param seek_table  A pointer to a seek table to be checked.
 * \assert
 *    \code seek_table != NULL \endcode
 * \retval FLAC__bool
 *    \c false if seek table is illegal, else \c true.
 */
FLAC_API FLAC__bool FLAC__format_seektable_is_legal(const FLAC__StreamMetadata_SeekTable *seek_table);

/** Sort a seek table's seek points according to the format specification.
 *  This includes a "unique-ification" step to remove duplicates, i.e.
 *  seek points with identical \a sample_number values.  Duplicate seek
 *  points are converted into placeholder points and sorted to the end of
 *  the table.
 *
 * \param seek_table  A pointer to a seek table to be sorted.
 * \assert
 *    \code seek_table != NULL \endcode
 * \retval unsigned
 *    The number of duplicate seek points converted into placeholders.
 */
FLAC_API unsigned FLAC__format_seektable_sort(FLAC__StreamMetadata_SeekTable *seek_table);

/** Check a cue sheet to see if it conforms to the FLAC specification.
 *  See the format specification for limits on the contents of the
 *  cue sheet.
 *
 * \param cue_sheet  A pointer to an existing cue sheet to be checked.
 * \param check_cd_da_subset  If \c true, check CUESHEET against more
 *                   stringent requirements for a CD-DA (audio) disc.
 * \param violation  Address of a pointer to a string.  If there is a
 *                   violation, a pointer to a string explanation of the
 *                   violation will be returned here. \a violation may be
 *                   \c NULL if you don't need the returned string.  Do not
 *                   free the returned string; it will always point to static
 *                   data.
 * \assert
 *    \code cue_sheet != NULL \endcode
 * \retval FLAC__bool
 *    \c false if cue sheet is illegal, else \c true.
 */
FLAC_API FLAC__bool FLAC__format_cuesheet_is_legal(const FLAC__StreamMetadata_CueSheet *cue_sheet, FLAC__bool check_cd_da_subset, const char **violation);

/** Check picture data to see if it conforms to the FLAC specification.
 *  See the format specification for limits on the contents of the
 *  PICTURE block.
 *
 * \param picture    A pointer to existing picture data to be checked.
 * \param violation  Address of a pointer to a string.  If there is a
 *                   violation, a pointer to a string explanation of the
 *                   violation will be returned here. \a violation may be
 *                   \c NULL if you don't need the returned string.  Do not
 *                   free the returned string; it will always point to static
 *                   data.
 * \assert
 *    \code picture != NULL \endcode
 * \retval FLAC__bool
 *    \c false if picture data is illegal, else \c true.
 */
FLAC_API FLAC__bool FLAC__format_picture_is_legal(const FLAC__StreamMetadata_Picture *picture, const char **violation);

/* \} */

#ifdef __cplusplus
}
#endif

#endif
#endif //++-- format.h  
#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined _MSC_VER || defined __BORLANDC__ || defined __MINGW32__
#if defined __BORLANDC__
#include <utime.h> /* for utime() */
#else
#include <sys/utime.h> /* for utime() */
#endif
#include <io.h> /* for chmod() */
#include <sys/types.h> /* for off_t */
#if _MSC_VER <= 1600 || defined __BORLANDC__ /* @@@ [2G limit] */
#define fseeko fseek
#define ftello ftell
#endif
#else
#include <sys/types.h> /* some flavors of BSD (like OS X) require this to get time_t */
#include <utime.h> /* for utime() */
#include <unistd.h> /* for chown(), unlink() */
#endif
#include <sys/stat.h> /* for stat(), maybe chmod() */


#if 1 //++ metadata.h 
#ifndef FLAC__METADATA_H
#define FLAC__METADATA_H

#include <sys/types.h> /* for off_t */

/* --------------------------------------------------------------------
   (For an example of how all these routines are used, see the source
   code for the unit tests in src/test_libFLAC/metadata_*.c, or
   metaflac in src/metaflac/)
   ------------------------------------------------------------------*/

/** \file include/FLAC/metadata.h
 *
 *  \brief
 *  This module provides functions for creating and manipulating FLAC
 *  metadata blocks in memory, and three progressively more powerful
 *  interfaces for traversing and editing metadata in FLAC files.
 *
 *  See the detailed documentation for each interface in the
 *  \link flac_metadata metadata \endlink module.
 */

/** \defgroup flac_metadata FLAC/metadata.h: metadata interfaces
 *  \ingroup flac
 *
 *  \brief
 *  This module provides functions for creating and manipulating FLAC
 *  metadata blocks in memory, and three progressively more powerful
 *  interfaces for traversing and editing metadata in native FLAC files.
 *  Note that currently only the Chain interface (level 2) supports Ogg
 *  FLAC files, and it is read-only i.e. no writing back changed
 *  metadata to file.
 *
 *  There are three metadata interfaces of increasing complexity:
 *
 *  Level 0:
 *  Read-only access to the STREAMINFO, VORBIS_COMMENT, CUESHEET, and
 *  PICTURE blocks.
 *
 *  Level 1:
 *  Read-write access to all metadata blocks.  This level is write-
 *  efficient in most cases (more on this below), and uses less memory
 *  than level 2.
 *
 *  Level 2:
 *  Read-write access to all metadata blocks.  This level is write-
 *  efficient in all cases, but uses more memory since all metadata for
 *  the whole file is read into memory and manipulated before writing
 *  out again.
 *
 *  What do we mean by efficient?  Since FLAC metadata appears at the
 *  beginning of the file, when writing metadata back to a FLAC file
 *  it is possible to grow or shrink the metadata such that the entire
 *  file must be rewritten.  However, if the size remains the same during
 *  changes or PADDING blocks are utilized, only the metadata needs to be
 *  overwritten, which is much faster.
 *
 *  Efficient means the whole file is rewritten at most one time, and only
 *  when necessary.  Level 1 is not efficient only in the case that you
 *  cause more than one metadata block to grow or shrink beyond what can
 *  be accomodated by padding.  In this case you should probably use level
 *  2, which allows you to edit all the metadata for a file in memory and
 *  write it out all at once.
 *
 *  All levels know how to skip over and not disturb an ID3v2 tag at the
 *  front of the file.
 *
 *  All levels access files via their filenames.  In addition, level 2
 *  has additional alternative read and write functions that take an I/O
 *  handle and callbacks, for situations where access by filename is not
 *  possible.
 *
 *  In addition to the three interfaces, this module defines functions for
 *  creating and manipulating various metadata objects in memory.  As we see
 *  from the Format module, FLAC metadata blocks in memory are very primitive
 *  structures for storing information in an efficient way.  Reading
 *  information from the structures is easy but creating or modifying them
 *  directly is more complex.  The metadata object routines here facilitate
 *  this by taking care of the consistency and memory management drudgery.
 *
 *  Unless you will be using the level 1 or 2 interfaces to modify existing
 *  metadata however, you will not probably not need these.
 *
 *  From a dependency standpoint, none of the encoders or decoders require
 *  the metadata module.  This is so that embedded users can strip out the
 *  metadata module from libFLAC to reduce the size and complexity.
 */

#ifdef __cplusplus
extern "C" {
#endif


/** \defgroup flac_metadata_level0 FLAC/metadata.h: metadata level 0 interface
 *  \ingroup flac_metadata
 *
 *  \brief
 *  The level 0 interface consists of individual routines to read the
 *  STREAMINFO, VORBIS_COMMENT, CUESHEET, and PICTURE blocks, requiring
 *  only a filename.
 *
 *  They try to skip any ID3v2 tag at the head of the file.
 *
 * \{
 */

/** Read the STREAMINFO metadata block of the given FLAC file.  This function
 *  will try to skip any ID3v2 tag at the head of the file.
 *
 * \param filename    The path to the FLAC file to read.
 * \param streaminfo  A pointer to space for the STREAMINFO block.  Since
 *                    FLAC__StreamMetadata is a simple structure with no
 *                    memory allocation involved, you pass the address of
 *                    an existing structure.  It need not be initialized.
 * \assert
 *    \code filename != NULL \endcode
 *    \code streaminfo != NULL \endcode
 * \retval FLAC__bool
 *    \c true if a valid STREAMINFO block was read from \a filename.  Returns
 *    \c false if there was a memory allocation error, a file decoder error,
 *    or the file contained no STREAMINFO block.  (A memory allocation error
 *    is possible because this function must set up a file decoder.)
 */
FLAC_API FLAC__bool FLAC__metadata_get_streaminfo(const char *filename, FLAC__StreamMetadata *streaminfo);

/** Read the VORBIS_COMMENT metadata block of the given FLAC file.  This
 *  function will try to skip any ID3v2 tag at the head of the file.
 *
 * \param filename    The path to the FLAC file to read.
 * \param tags        The address where the returned pointer will be
 *                    stored.  The \a tags object must be deleted by
 *                    the caller using FLAC__metadata_object_delete().
 * \assert
 *    \code filename != NULL \endcode
 *    \code tags != NULL \endcode
 * \retval FLAC__bool
 *    \c true if a valid VORBIS_COMMENT block was read from \a filename,
 *    and \a *tags will be set to the address of the metadata structure.
 *    Returns \c false if there was a memory allocation error, a file
 *    decoder error, or the file contained no VORBIS_COMMENT block, and
 *    \a *tags will be set to \c NULL.
 */
FLAC_API FLAC__bool FLAC__metadata_get_tags(const char *filename, FLAC__StreamMetadata **tags);

/** Read the CUESHEET metadata block of the given FLAC file.  This
 *  function will try to skip any ID3v2 tag at the head of the file.
 *
 * \param filename    The path to the FLAC file to read.
 * \param cuesheet    The address where the returned pointer will be
 *                    stored.  The \a cuesheet object must be deleted by
 *                    the caller using FLAC__metadata_object_delete().
 * \assert
 *    \code filename != NULL \endcode
 *    \code cuesheet != NULL \endcode
 * \retval FLAC__bool
 *    \c true if a valid CUESHEET block was read from \a filename,
 *    and \a *cuesheet will be set to the address of the metadata
 *    structure.  Returns \c false if there was a memory allocation
 *    error, a file decoder error, or the file contained no CUESHEET
 *    block, and \a *cuesheet will be set to \c NULL.
 */
FLAC_API FLAC__bool FLAC__metadata_get_cuesheet(const char *filename, FLAC__StreamMetadata **cuesheet);

/** Read a PICTURE metadata block of the given FLAC file.  This
 *  function will try to skip any ID3v2 tag at the head of the file.
 *  Since there can be more than one PICTURE block in a file, this
 *  function takes a number of parameters that act as constraints to
 *  the search.  The PICTURE block with the largest area matching all
 *  the constraints will be returned, or \a *picture will be set to
 *  \c NULL if there was no such block.
 *
 * \param filename    The path to the FLAC file to read.
 * \param picture     The address where the returned pointer will be
 *                    stored.  The \a picture object must be deleted by
 *                    the caller using FLAC__metadata_object_delete().
 * \param type        The desired picture type.  Use \c -1 to mean
 *                    "any type".
 * \param mime_type   The desired MIME type, e.g. "image/jpeg".  The
 *                    string will be matched exactly.  Use \c NULL to
 *                    mean "any MIME type".
 * \param description The desired description.  The string will be
 *                    matched exactly.  Use \c NULL to mean "any
 *                    description".
 * \param max_width   The maximum width in pixels desired.  Use
 *                    \c (unsigned)(-1) to mean "any width".
 * \param max_height  The maximum height in pixels desired.  Use
 *                    \c (unsigned)(-1) to mean "any height".
 * \param max_depth   The maximum color depth in bits-per-pixel desired.
 *                    Use \c (unsigned)(-1) to mean "any depth".
 * \param max_colors  The maximum number of colors desired.  Use
 *                    \c (unsigned)(-1) to mean "any number of colors".
 * \assert
 *    \code filename != NULL \endcode
 *    \code picture != NULL \endcode
 * \retval FLAC__bool
 *    \c true if a valid PICTURE block was read from \a filename,
 *    and \a *picture will be set to the address of the metadata
 *    structure.  Returns \c false if there was a memory allocation
 *    error, a file decoder error, or the file contained no PICTURE
 *    block, and \a *picture will be set to \c NULL.
 */
FLAC_API FLAC__bool FLAC__metadata_get_picture(const char *filename, FLAC__StreamMetadata **picture, FLAC__StreamMetadata_Picture_Type type, const char *mime_type, const FLAC__byte *description, unsigned max_width, unsigned max_height, unsigned max_depth, unsigned max_colors);

/* \} */


/** \defgroup flac_metadata_level1 FLAC/metadata.h: metadata level 1 interface
 *  \ingroup flac_metadata
 *
 * \brief
 * The level 1 interface provides read-write access to FLAC file metadata and
 * operates directly on the FLAC file.
 *
 * The general usage of this interface is:
 *
 * - Create an iterator using FLAC__metadata_simple_iterator_new()
 * - Attach it to a file using FLAC__metadata_simple_iterator_init() and check
 *   the exit code.  Call FLAC__metadata_simple_iterator_is_writable() to
 *   see if the file is writable, or only read access is allowed.
 * - Use FLAC__metadata_simple_iterator_next() and
 *   FLAC__metadata_simple_iterator_prev() to traverse the blocks.
 *   This is does not read the actual blocks themselves.
 *   FLAC__metadata_simple_iterator_next() is relatively fast.
 *   FLAC__metadata_simple_iterator_prev() is slower since it needs to search
 *   forward from the front of the file.
 * - Use FLAC__metadata_simple_iterator_get_block_type() or
 *   FLAC__metadata_simple_iterator_get_block() to access the actual data at
 *   the current iterator position.  The returned object is yours to modify
 *   and free.
 * - Use FLAC__metadata_simple_iterator_set_block() to write a modified block
 *   back.  You must have write permission to the original file.  Make sure to
 *   read the whole comment to FLAC__metadata_simple_iterator_set_block()
 *   below.
 * - Use FLAC__metadata_simple_iterator_insert_block_after() to add new blocks.
 *   Use the object creation functions from
 *   \link flac_metadata_object here \endlink to generate new objects.
 * - Use FLAC__metadata_simple_iterator_delete_block() to remove the block
 *   currently referred to by the iterator, or replace it with padding.
 * - Destroy the iterator with FLAC__metadata_simple_iterator_delete() when
 *   finished.
 *
 * \note
 * The FLAC file remains open the whole time between
 * FLAC__metadata_simple_iterator_init() and
 * FLAC__metadata_simple_iterator_delete(), so make sure you are not altering
 * the file during this time.
 *
 * \note
 * Do not modify the \a is_last, \a length, or \a type fields of returned
 * FLAC__StreamMetadata objects.  These are managed automatically.
 *
 * \note
 * If any of the modification functions
 * (FLAC__metadata_simple_iterator_set_block(),
 * FLAC__metadata_simple_iterator_delete_block(),
 * FLAC__metadata_simple_iterator_insert_block_after(), etc.) return \c false,
 * you should delete the iterator as it may no longer be valid.
 *
 * \{
 */

struct FLAC__Metadata_SimpleIterator;
/** The opaque structure definition for the level 1 iterator type.
 *  See the
 *  \link flac_metadata_level1 metadata level 1 module \endlink
 *  for a detailed description.
 */
typedef struct FLAC__Metadata_SimpleIterator FLAC__Metadata_SimpleIterator;

/** Status type for FLAC__Metadata_SimpleIterator.
 *
 *  The iterator's current status can be obtained by calling FLAC__metadata_simple_iterator_status().
 */
typedef enum {

	FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK = 0,
	/**< The iterator is in the normal OK state */

	FLAC__METADATA_SIMPLE_ITERATOR_STATUS_ILLEGAL_INPUT,
	/**< The data passed into a function violated the function's usage criteria */

	FLAC__METADATA_SIMPLE_ITERATOR_STATUS_ERROR_OPENING_FILE,
	/**< The iterator could not open the target file */

	FLAC__METADATA_SIMPLE_ITERATOR_STATUS_NOT_A_FLAC_FILE,
	/**< The iterator could not find the FLAC signature at the start of the file */

	FLAC__METADATA_SIMPLE_ITERATOR_STATUS_NOT_WRITABLE,
	/**< The iterator tried to write to a file that was not writable */

	FLAC__METADATA_SIMPLE_ITERATOR_STATUS_BAD_METADATA,
	/**< The iterator encountered input that does not conform to the FLAC metadata specification */

	FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR,
	/**< The iterator encountered an error while reading the FLAC file */

	FLAC__METADATA_SIMPLE_ITERATOR_STATUS_SEEK_ERROR,
	/**< The iterator encountered an error while seeking in the FLAC file */

	FLAC__METADATA_SIMPLE_ITERATOR_STATUS_WRITE_ERROR,
	/**< The iterator encountered an error while writing the FLAC file */

	FLAC__METADATA_SIMPLE_ITERATOR_STATUS_RENAME_ERROR,
	/**< The iterator encountered an error renaming the FLAC file */

	FLAC__METADATA_SIMPLE_ITERATOR_STATUS_UNLINK_ERROR,
	/**< The iterator encountered an error removing the temporary file */

	FLAC__METADATA_SIMPLE_ITERATOR_STATUS_MEMORY_ALLOCATION_ERROR,
	/**< Memory allocation failed */

	FLAC__METADATA_SIMPLE_ITERATOR_STATUS_INTERNAL_ERROR
	/**< The caller violated an assertion or an unexpected error occurred */

} FLAC__Metadata_SimpleIteratorStatus;

/** Maps a FLAC__Metadata_SimpleIteratorStatus to a C string.
 *
 *  Using a FLAC__Metadata_SimpleIteratorStatus as the index to this array
 *  will give the string equivalent.  The contents should not be modified.
 */
extern FLAC_API const char * const FLAC__Metadata_SimpleIteratorStatusString[];


/** Create a new iterator instance.
 *
 * \retval FLAC__Metadata_SimpleIterator*
 *    \c NULL if there was an error allocating memory, else the new instance.
 */
FLAC_API FLAC__Metadata_SimpleIterator *FLAC__metadata_simple_iterator_new(void);

/** Free an iterator instance.  Deletes the object pointed to by \a iterator.
 *
 * \param iterator  A pointer to an existing iterator.
 * \assert
 *    \code iterator != NULL \endcode
 */
FLAC_API void FLAC__metadata_simple_iterator_delete(FLAC__Metadata_SimpleIterator *iterator);

/** Get the current status of the iterator.  Call this after a function
 *  returns \c false to get the reason for the error.  Also resets the status
 *  to FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK.
 *
 * \param iterator  A pointer to an existing iterator.
 * \assert
 *    \code iterator != NULL \endcode
 * \retval FLAC__Metadata_SimpleIteratorStatus
 *    The current status of the iterator.
 */
FLAC_API FLAC__Metadata_SimpleIteratorStatus FLAC__metadata_simple_iterator_status(FLAC__Metadata_SimpleIterator *iterator);

/** Initialize the iterator to point to the first metadata block in the
 *  given FLAC file.
 *
 * \param iterator             A pointer to an existing iterator.
 * \param filename             The path to the FLAC file.
 * \param read_only            If \c true, the FLAC file will be opened
 *                             in read-only mode; if \c false, the FLAC
 *                             file will be opened for edit even if no
 *                             edits are performed.
 * \param preserve_file_stats  If \c true, the owner and modification
 *                             time will be preserved even if the FLAC
 *                             file is written to.
 * \assert
 *    \code iterator != NULL \endcode
 *    \code filename != NULL \endcode
 * \retval FLAC__bool
 *    \c false if a memory allocation error occurs, the file can't be
 *    opened, or another error occurs, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_simple_iterator_init(FLAC__Metadata_SimpleIterator *iterator, const char *filename, FLAC__bool read_only, FLAC__bool preserve_file_stats);

/** Returns \c true if the FLAC file is writable.  If \c false, calls to
 *  FLAC__metadata_simple_iterator_set_block() and
 *  FLAC__metadata_simple_iterator_insert_block_after() will fail.
 *
 * \param iterator             A pointer to an existing iterator.
 * \assert
 *    \code iterator != NULL \endcode
 * \retval FLAC__bool
 *    See above.
 */
FLAC_API FLAC__bool FLAC__metadata_simple_iterator_is_writable(const FLAC__Metadata_SimpleIterator *iterator);

/** Moves the iterator forward one metadata block, returning \c false if
 *  already at the end.
 *
 * \param iterator  A pointer to an existing initialized iterator.
 * \assert
 *    \code iterator != NULL \endcode
 *    \a iterator has been successfully initialized with
 *    FLAC__metadata_simple_iterator_init()
 * \retval FLAC__bool
 *    \c false if already at the last metadata block of the chain, else
 *    \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_simple_iterator_next(FLAC__Metadata_SimpleIterator *iterator);

/** Moves the iterator backward one metadata block, returning \c false if
 *  already at the beginning.
 *
 * \param iterator  A pointer to an existing initialized iterator.
 * \assert
 *    \code iterator != NULL \endcode
 *    \a iterator has been successfully initialized with
 *    FLAC__metadata_simple_iterator_init()
 * \retval FLAC__bool
 *    \c false if already at the first metadata block of the chain, else
 *    \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_simple_iterator_prev(FLAC__Metadata_SimpleIterator *iterator);

/** Returns a flag telling if the current metadata block is the last.
 *
 * \param iterator  A pointer to an existing initialized iterator.
 * \assert
 *    \code iterator != NULL \endcode
 *    \a iterator has been successfully initialized with
 *    FLAC__metadata_simple_iterator_init()
 * \retval FLAC__bool
 *    \c true if the current metadata block is the last in the file,
 *    else \c false.
 */
FLAC_API FLAC__bool FLAC__metadata_simple_iterator_is_last(const FLAC__Metadata_SimpleIterator *iterator);

/** Get the offset of the metadata block at the current position.  This
 *  avoids reading the actual block data which can save time for large
 *  blocks.
 *
 * \param iterator  A pointer to an existing initialized iterator.
 * \assert
 *    \code iterator != NULL \endcode
 *    \a iterator has been successfully initialized with
 *    FLAC__metadata_simple_iterator_init()
 * \retval off_t
 *    The offset of the metadata block at the current iterator position.
 *    This is the byte offset relative to the beginning of the file of
 *    the current metadata block's header.
 */
FLAC_API off_t FLAC__metadata_simple_iterator_get_block_offset(const FLAC__Metadata_SimpleIterator *iterator);

/** Get the type of the metadata block at the current position.  This
 *  avoids reading the actual block data which can save time for large
 *  blocks.
 *
 * \param iterator  A pointer to an existing initialized iterator.
 * \assert
 *    \code iterator != NULL \endcode
 *    \a iterator has been successfully initialized with
 *    FLAC__metadata_simple_iterator_init()
 * \retval FLAC__MetadataType
 *    The type of the metadata block at the current iterator position.
 */
FLAC_API FLAC__MetadataType FLAC__metadata_simple_iterator_get_block_type(const FLAC__Metadata_SimpleIterator *iterator);

/** Get the length of the metadata block at the current position.  This
 *  avoids reading the actual block data which can save time for large
 *  blocks.
 *
 * \param iterator  A pointer to an existing initialized iterator.
 * \assert
 *    \code iterator != NULL \endcode
 *    \a iterator has been successfully initialized with
 *    FLAC__metadata_simple_iterator_init()
 * \retval unsigned
 *    The length of the metadata block at the current iterator position.
 *    The is same length as that in the
 *    <a href="http://flac.sourceforge.net/format.html#metadata_block_header">metadata block header</a>,
 *    i.e. the length of the metadata body that follows the header.
 */
FLAC_API unsigned FLAC__metadata_simple_iterator_get_block_length(const FLAC__Metadata_SimpleIterator *iterator);

/** Get the application ID of the \c APPLICATION block at the current
 *  position.  This avoids reading the actual block data which can save
 *  time for large blocks.
 *
 * \param iterator  A pointer to an existing initialized iterator.
 * \param id        A pointer to a buffer of at least \c 4 bytes where
 *                  the ID will be stored.
 * \assert
 *    \code iterator != NULL \endcode
 *    \code id != NULL \endcode
 *    \a iterator has been successfully initialized with
 *    FLAC__metadata_simple_iterator_init()
 * \retval FLAC__bool
 *    \c true if the ID was successfully read, else \c false, in which
 *    case you should check FLAC__metadata_simple_iterator_status() to
 *    find out why.  If the status is
 *    \c FLAC__METADATA_SIMPLE_ITERATOR_STATUS_ILLEGAL_INPUT, then the
 *    current metadata block is not an \c APPLICATION block.  Otherwise
 *    if the status is
 *    \c FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR or
 *    \c FLAC__METADATA_SIMPLE_ITERATOR_STATUS_SEEK_ERROR, an I/O error
 *    occurred and the iterator can no longer be used.
 */
FLAC_API FLAC__bool FLAC__metadata_simple_iterator_get_application_id(FLAC__Metadata_SimpleIterator *iterator, FLAC__byte *id);

/** Get the metadata block at the current position.  You can modify the
 *  block but must use FLAC__metadata_simple_iterator_set_block() to
 *  write it back to the FLAC file.
 *
 *  You must call FLAC__metadata_object_delete() on the returned object
 *  when you are finished with it.
 *
 * \param iterator  A pointer to an existing initialized iterator.
 * \assert
 *    \code iterator != NULL \endcode
 *    \a iterator has been successfully initialized with
 *    FLAC__metadata_simple_iterator_init()
 * \retval FLAC__StreamMetadata*
 *    The current metadata block, or \c NULL if there was a memory
 *    allocation error.
 */
FLAC_API FLAC__StreamMetadata *FLAC__metadata_simple_iterator_get_block(FLAC__Metadata_SimpleIterator *iterator);

/** Write a block back to the FLAC file.  This function tries to be
 *  as efficient as possible; how the block is actually written is
 *  shown by the following:
 *
 *  Existing block is a STREAMINFO block and the new block is a
 *  STREAMINFO block: the new block is written in place.  Make sure
 *  you know what you're doing when changing the values of a
 *  STREAMINFO block.
 *
 *  Existing block is a STREAMINFO block and the new block is a
 *  not a STREAMINFO block: this is an error since the first block
 *  must be a STREAMINFO block.  Returns \c false without altering the
 *  file.
 *
 *  Existing block is not a STREAMINFO block and the new block is a
 *  STREAMINFO block: this is an error since there may be only one
 *  STREAMINFO block.  Returns \c false without altering the file.
 *
 *  Existing block and new block are the same length: the existing
 *  block will be replaced by the new block, written in place.
 *
 *  Existing block is longer than new block: if use_padding is \c true,
 *  the existing block will be overwritten in place with the new
 *  block followed by a PADDING block, if possible, to make the total
 *  size the same as the existing block.  Remember that a padding
 *  block requires at least four bytes so if the difference in size
 *  between the new block and existing block is less than that, the
 *  entire file will have to be rewritten, using the new block's
 *  exact size.  If use_padding is \c false, the entire file will be
 *  rewritten, replacing the existing block by the new block.
 *
 *  Existing block is shorter than new block: if use_padding is \c true,
 *  the function will try and expand the new block into the following
 *  PADDING block, if it exists and doing so won't shrink the PADDING
 *  block to less than 4 bytes.  If there is no following PADDING
 *  block, or it will shrink to less than 4 bytes, or use_padding is
 *  \c false, the entire file is rewritten, replacing the existing block
 *  with the new block.  Note that in this case any following PADDING
 *  block is preserved as is.
 *
 *  After writing the block, the iterator will remain in the same
 *  place, i.e. pointing to the new block.
 *
 * \param iterator     A pointer to an existing initialized iterator.
 * \param block        The block to set.
 * \param use_padding  See above.
 * \assert
 *    \code iterator != NULL \endcode
 *    \a iterator has been successfully initialized with
 *    FLAC__metadata_simple_iterator_init()
 *    \code block != NULL \endcode
 * \retval FLAC__bool
 *    \c true if successful, else \c false.
 */
FLAC_API FLAC__bool FLAC__metadata_simple_iterator_set_block(FLAC__Metadata_SimpleIterator *iterator, FLAC__StreamMetadata *block, FLAC__bool use_padding);

/** This is similar to FLAC__metadata_simple_iterator_set_block()
 *  except that instead of writing over an existing block, it appends
 *  a block after the existing block.  \a use_padding is again used to
 *  tell the function to try an expand into following padding in an
 *  attempt to avoid rewriting the entire file.
 *
 *  This function will fail and return \c false if given a STREAMINFO
 *  block.
 *
 *  After writing the block, the iterator will be pointing to the
 *  new block.
 *
 * \param iterator     A pointer to an existing initialized iterator.
 * \param block        The block to set.
 * \param use_padding  See above.
 * \assert
 *    \code iterator != NULL \endcode
 *    \a iterator has been successfully initialized with
 *    FLAC__metadata_simple_iterator_init()
 *    \code block != NULL \endcode
 * \retval FLAC__bool
 *    \c true if successful, else \c false.
 */
FLAC_API FLAC__bool FLAC__metadata_simple_iterator_insert_block_after(FLAC__Metadata_SimpleIterator *iterator, FLAC__StreamMetadata *block, FLAC__bool use_padding);

/** Deletes the block at the current position.  This will cause the
 *  entire FLAC file to be rewritten, unless \a use_padding is \c true,
 *  in which case the block will be replaced by an equal-sized PADDING
 *  block.  The iterator will be left pointing to the block before the
 *  one just deleted.
 *
 *  You may not delete the STREAMINFO block.
 *
 * \param iterator     A pointer to an existing initialized iterator.
 * \param use_padding  See above.
 * \assert
 *    \code iterator != NULL \endcode
 *    \a iterator has been successfully initialized with
 *    FLAC__metadata_simple_iterator_init()
 * \retval FLAC__bool
 *    \c true if successful, else \c false.
 */
FLAC_API FLAC__bool FLAC__metadata_simple_iterator_delete_block(FLAC__Metadata_SimpleIterator *iterator, FLAC__bool use_padding);

/* \} */


/** \defgroup flac_metadata_level2 FLAC/metadata.h: metadata level 2 interface
 *  \ingroup flac_metadata
 *
 * \brief
 * The level 2 interface provides read-write access to FLAC file metadata;
 * all metadata is read into memory, operated on in memory, and then written
 * to file, which is more efficient than level 1 when editing multiple blocks.
 *
 * Currently Ogg FLAC is supported for read only, via
 * FLAC__metadata_chain_read_ogg() but a subsequent
 * FLAC__metadata_chain_write() will fail.
 *
 * The general usage of this interface is:
 *
 * - Create a new chain using FLAC__metadata_chain_new().  A chain is a
 *   linked list of FLAC metadata blocks.
 * - Read all metadata into the the chain from a FLAC file using
 *   FLAC__metadata_chain_read() or FLAC__metadata_chain_read_ogg() and
 *   check the status.
 * - Optionally, consolidate the padding using
 *   FLAC__metadata_chain_merge_padding() or
 *   FLAC__metadata_chain_sort_padding().
 * - Create a new iterator using FLAC__metadata_iterator_new()
 * - Initialize the iterator to point to the first element in the chain
 *   using FLAC__metadata_iterator_init()
 * - Traverse the chain using FLAC__metadata_iterator_next and
 *   FLAC__metadata_iterator_prev().
 * - Get a block for reading or modification using
 *   FLAC__metadata_iterator_get_block().  The pointer to the object
 *   inside the chain is returned, so the block is yours to modify.
 *   Changes will be reflected in the FLAC file when you write the
 *   chain.  You can also add and delete blocks (see functions below).
 * - When done, write out the chain using FLAC__metadata_chain_write().
 *   Make sure to read the whole comment to the function below.
 * - Delete the chain using FLAC__metadata_chain_delete().
 *
 * \note
 * Even though the FLAC file is not open while the chain is being
 * manipulated, you must not alter the file externally during
 * this time.  The chain assumes the FLAC file will not change
 * between the time of FLAC__metadata_chain_read()/FLAC__metadata_chain_read_ogg()
 * and FLAC__metadata_chain_write().
 *
 * \note
 * Do not modify the is_last, length, or type fields of returned
 * FLAC__StreamMetadata objects.  These are managed automatically.
 *
 * \note
 * The metadata objects returned by FLAC__metadata_iterator_get_block()
 * are owned by the chain; do not FLAC__metadata_object_delete() them.
 * In the same way, blocks passed to FLAC__metadata_iterator_set_block()
 * become owned by the chain and they will be deleted when the chain is
 * deleted.
 *
 * \{
 */

struct FLAC__Metadata_Chain;
/** The opaque structure definition for the level 2 chain type.
 */
typedef struct FLAC__Metadata_Chain FLAC__Metadata_Chain;

struct FLAC__Metadata_Iterator;
/** The opaque structure definition for the level 2 iterator type.
 */
typedef struct FLAC__Metadata_Iterator FLAC__Metadata_Iterator;

typedef enum {
	FLAC__METADATA_CHAIN_STATUS_OK = 0,
	/**< The chain is in the normal OK state */

	FLAC__METADATA_CHAIN_STATUS_ILLEGAL_INPUT,
	/**< The data passed into a function violated the function's usage criteria */

	FLAC__METADATA_CHAIN_STATUS_ERROR_OPENING_FILE,
	/**< The chain could not open the target file */

	FLAC__METADATA_CHAIN_STATUS_NOT_A_FLAC_FILE,
	/**< The chain could not find the FLAC signature at the start of the file */

	FLAC__METADATA_CHAIN_STATUS_NOT_WRITABLE,
	/**< The chain tried to write to a file that was not writable */

	FLAC__METADATA_CHAIN_STATUS_BAD_METADATA,
	/**< The chain encountered input that does not conform to the FLAC metadata specification */

	FLAC__METADATA_CHAIN_STATUS_READ_ERROR,
	/**< The chain encountered an error while reading the FLAC file */

	FLAC__METADATA_CHAIN_STATUS_SEEK_ERROR,
	/**< The chain encountered an error while seeking in the FLAC file */

	FLAC__METADATA_CHAIN_STATUS_WRITE_ERROR,
	/**< The chain encountered an error while writing the FLAC file */

	FLAC__METADATA_CHAIN_STATUS_RENAME_ERROR,
	/**< The chain encountered an error renaming the FLAC file */

	FLAC__METADATA_CHAIN_STATUS_UNLINK_ERROR,
	/**< The chain encountered an error removing the temporary file */

	FLAC__METADATA_CHAIN_STATUS_MEMORY_ALLOCATION_ERROR,
	/**< Memory allocation failed */

	FLAC__METADATA_CHAIN_STATUS_INTERNAL_ERROR,
	/**< The caller violated an assertion or an unexpected error occurred */

	FLAC__METADATA_CHAIN_STATUS_INVALID_CALLBACKS,
	/**< One or more of the required callbacks was NULL */

	FLAC__METADATA_CHAIN_STATUS_READ_WRITE_MISMATCH,
	/**< FLAC__metadata_chain_write() was called on a chain read by
	 *   FLAC__metadata_chain_read_with_callbacks()/FLAC__metadata_chain_read_ogg_with_callbacks(),
	 *   or 
	 *   FLAC__metadata_chain_write_with_callbacks()/FLAC__metadata_chain_write_with_callbacks_and_tempfile()
	 *   was called on a chain read by
	 *   FLAC__metadata_chain_read()/FLAC__metadata_chain_read_ogg().
	 *   Matching read/write methods must always be used. */

	FLAC__METADATA_CHAIN_STATUS_WRONG_WRITE_CALL
	/**< FLAC__metadata_chain_write_with_callbacks() was called when the
	 *   chain write requires a tempfile; use
	 *   FLAC__metadata_chain_write_with_callbacks_and_tempfile() instead.
	 *   Or, FLAC__metadata_chain_write_with_callbacks_and_tempfile() was
	 *   called when the chain write does not require a tempfile; use
	 *   FLAC__metadata_chain_write_with_callbacks() instead.
	 *   Always check FLAC__metadata_chain_check_if_tempfile_needed()
	 *   before writing via callbacks. */

} FLAC__Metadata_ChainStatus;

/** Maps a FLAC__Metadata_ChainStatus to a C string.
 *
 *  Using a FLAC__Metadata_ChainStatus as the index to this array
 *  will give the string equivalent.  The contents should not be modified.
 */
extern FLAC_API const char * const FLAC__Metadata_ChainStatusString[];

/*********** FLAC__Metadata_Chain ***********/

/** Create a new chain instance.
 *
 * \retval FLAC__Metadata_Chain*
 *    \c NULL if there was an error allocating memory, else the new instance.
 */
FLAC_API FLAC__Metadata_Chain *FLAC__metadata_chain_new(void);

/** Free a chain instance.  Deletes the object pointed to by \a chain.
 *
 * \param chain  A pointer to an existing chain.
 * \assert
 *    \code chain != NULL \endcode
 */
FLAC_API void FLAC__metadata_chain_delete(FLAC__Metadata_Chain *chain);

/** Get the current status of the chain.  Call this after a function
 *  returns \c false to get the reason for the error.  Also resets the
 *  status to FLAC__METADATA_CHAIN_STATUS_OK.
 *
 * \param chain    A pointer to an existing chain.
 * \assert
 *    \code chain != NULL \endcode
 * \retval FLAC__Metadata_ChainStatus
 *    The current status of the chain.
 */
FLAC_API FLAC__Metadata_ChainStatus FLAC__metadata_chain_status(FLAC__Metadata_Chain *chain);

/** Read all metadata from a FLAC file into the chain.
 *
 * \param chain    A pointer to an existing chain.
 * \param filename The path to the FLAC file to read.
 * \assert
 *    \code chain != NULL \endcode
 *    \code filename != NULL \endcode
 * \retval FLAC__bool
 *    \c true if a valid list of metadata blocks was read from
 *    \a filename, else \c false.  On failure, check the status with
 *    FLAC__metadata_chain_status().
 */
FLAC_API FLAC__bool FLAC__metadata_chain_read(FLAC__Metadata_Chain *chain, const char *filename);

/** Read all metadata from an Ogg FLAC file into the chain.
 *
 * \note Ogg FLAC metadata data writing is not supported yet and
 * FLAC__metadata_chain_write() will fail.
 *
 * \param chain    A pointer to an existing chain.
 * \param filename The path to the Ogg FLAC file to read.
 * \assert
 *    \code chain != NULL \endcode
 *    \code filename != NULL \endcode
 * \retval FLAC__bool
 *    \c true if a valid list of metadata blocks was read from
 *    \a filename, else \c false.  On failure, check the status with
 *    FLAC__metadata_chain_status().
 */
FLAC_API FLAC__bool FLAC__metadata_chain_read_ogg(FLAC__Metadata_Chain *chain, const char *filename);

/** Read all metadata from a FLAC stream into the chain via I/O callbacks.
 *
 *  The \a handle need only be open for reading, but must be seekable.
 *  The equivalent minimum stdio fopen() file mode is \c "r" (or \c "rb"
 *  for Windows).
 *
 * \param chain    A pointer to an existing chain.
 * \param handle   The I/O handle of the FLAC stream to read.  The
 *                 handle will NOT be closed after the metadata is read;
 *                 that is the duty of the caller.
 * \param callbacks
 *                 A set of callbacks to use for I/O.  The mandatory
 *                 callbacks are \a read, \a seek, and \a tell.
 * \assert
 *    \code chain != NULL \endcode
 * \retval FLAC__bool
 *    \c true if a valid list of metadata blocks was read from
 *    \a handle, else \c false.  On failure, check the status with
 *    FLAC__metadata_chain_status().
 */
FLAC_API FLAC__bool FLAC__metadata_chain_read_with_callbacks(FLAC__Metadata_Chain *chain, FLAC__IOHandle handle, FLAC__IOCallbacks callbacks);

/** Read all metadata from an Ogg FLAC stream into the chain via I/O callbacks.
 *
 *  The \a handle need only be open for reading, but must be seekable.
 *  The equivalent minimum stdio fopen() file mode is \c "r" (or \c "rb"
 *  for Windows).
 *
 * \note Ogg FLAC metadata data writing is not supported yet and
 * FLAC__metadata_chain_write() will fail.
 *
 * \param chain    A pointer to an existing chain.
 * \param handle   The I/O handle of the Ogg FLAC stream to read.  The
 *                 handle will NOT be closed after the metadata is read;
 *                 that is the duty of the caller.
 * \param callbacks
 *                 A set of callbacks to use for I/O.  The mandatory
 *                 callbacks are \a read, \a seek, and \a tell.
 * \assert
 *    \code chain != NULL \endcode
 * \retval FLAC__bool
 *    \c true if a valid list of metadata blocks was read from
 *    \a handle, else \c false.  On failure, check the status with
 *    FLAC__metadata_chain_status().
 */
FLAC_API FLAC__bool FLAC__metadata_chain_read_ogg_with_callbacks(FLAC__Metadata_Chain *chain, FLAC__IOHandle handle, FLAC__IOCallbacks callbacks);

/** Checks if writing the given chain would require the use of a
 *  temporary file, or if it could be written in place.
 *
 *  Under certain conditions, padding can be utilized so that writing
 *  edited metadata back to the FLAC file does not require rewriting the
 *  entire file.  If rewriting is required, then a temporary workfile is
 *  required.  When writing metadata using callbacks, you must check
 *  this function to know whether to call
 *  FLAC__metadata_chain_write_with_callbacks() or
 *  FLAC__metadata_chain_write_with_callbacks_and_tempfile().  When
 *  writing with FLAC__metadata_chain_write(), the temporary file is
 *  handled internally.
 *
 * \param chain    A pointer to an existing chain.
 * \param use_padding
 *                 Whether or not padding will be allowed to be used
 *                 during the write.  The value of \a use_padding given
 *                 here must match the value later passed to
 *                 FLAC__metadata_chain_write_with_callbacks() or
 *                 FLAC__metadata_chain_write_with_callbacks_with_tempfile().
 * \assert
 *    \code chain != NULL \endcode
 * \retval FLAC__bool
 *    \c true if writing the current chain would require a tempfile, or
 *    \c false if metadata can be written in place.
 */
FLAC_API FLAC__bool FLAC__metadata_chain_check_if_tempfile_needed(FLAC__Metadata_Chain *chain, FLAC__bool use_padding);

/** Write all metadata out to the FLAC file.  This function tries to be as
 *  efficient as possible; how the metadata is actually written is shown by
 *  the following:
 *
 *  If the current chain is the same size as the existing metadata, the new
 *  data is written in place.
 *
 *  If the current chain is longer than the existing metadata, and
 *  \a use_padding is \c true, and the last block is a PADDING block of
 *  sufficient length, the function will truncate the final padding block
 *  so that the overall size of the metadata is the same as the existing
 *  metadata, and then just rewrite the metadata.  Otherwise, if not all of
 *  the above conditions are met, the entire FLAC file must be rewritten.
 *  If you want to use padding this way it is a good idea to call
 *  FLAC__metadata_chain_sort_padding() first so that you have the maximum
 *  amount of padding to work with, unless you need to preserve ordering
 *  of the PADDING blocks for some reason.
 *
 *  If the current chain is shorter than the existing metadata, and
 *  \a use_padding is \c true, and the final block is a PADDING block, the padding
 *  is extended to make the overall size the same as the existing data.  If
 *  \a use_padding is \c true and the last block is not a PADDING block, a new
 *  PADDING block is added to the end of the new data to make it the same
 *  size as the existing data (if possible, see the note to
 *  FLAC__metadata_simple_iterator_set_block() about the four byte limit)
 *  and the new data is written in place.  If none of the above apply or
 *  \a use_padding is \c false, the entire FLAC file is rewritten.
 *
 *  If \a preserve_file_stats is \c true, the owner and modification time will
 *  be preserved even if the FLAC file is written.
 *
 *  For this write function to be used, the chain must have been read with
 *  FLAC__metadata_chain_read()/FLAC__metadata_chain_read_ogg(), not
 *  FLAC__metadata_chain_read_with_callbacks()/FLAC__metadata_chain_read_ogg_with_callbacks().
 *
 * \param chain               A pointer to an existing chain.
 * \param use_padding         See above.
 * \param preserve_file_stats See above.
 * \assert
 *    \code chain != NULL \endcode
 * \retval FLAC__bool
 *    \c true if the write succeeded, else \c false.  On failure,
 *    check the status with FLAC__metadata_chain_status().
 */
FLAC_API FLAC__bool FLAC__metadata_chain_write(FLAC__Metadata_Chain *chain, FLAC__bool use_padding, FLAC__bool preserve_file_stats);

/** Write all metadata out to a FLAC stream via callbacks.
 *
 *  (See FLAC__metadata_chain_write() for the details on how padding is
 *  used to write metadata in place if possible.)
 *
 *  The \a handle must be open for updating and be seekable.  The
 *  equivalent minimum stdio fopen() file mode is \c "r+" (or \c "r+b"
 *  for Windows).
 *
 *  For this write function to be used, the chain must have been read with
 *  FLAC__metadata_chain_read_with_callbacks()/FLAC__metadata_chain_read_ogg_with_callbacks(),
 *  not FLAC__metadata_chain_read()/FLAC__metadata_chain_read_ogg().
 *  Also, FLAC__metadata_chain_check_if_tempfile_needed() must have returned
 *  \c false.
 *
 * \param chain        A pointer to an existing chain.
 * \param use_padding  See FLAC__metadata_chain_write()
 * \param handle       The I/O handle of the FLAC stream to write.  The
 *                     handle will NOT be closed after the metadata is
 *                     written; that is the duty of the caller.
 * \param callbacks    A set of callbacks to use for I/O.  The mandatory
 *                     callbacks are \a write and \a seek.
 * \assert
 *    \code chain != NULL \endcode
 * \retval FLAC__bool
 *    \c true if the write succeeded, else \c false.  On failure,
 *    check the status with FLAC__metadata_chain_status().
 */
FLAC_API FLAC__bool FLAC__metadata_chain_write_with_callbacks(FLAC__Metadata_Chain *chain, FLAC__bool use_padding, FLAC__IOHandle handle, FLAC__IOCallbacks callbacks);

/** Write all metadata out to a FLAC stream via callbacks.
 *
 *  (See FLAC__metadata_chain_write() for the details on how padding is
 *  used to write metadata in place if possible.)
 *
 *  This version of the write-with-callbacks function must be used when
 *  FLAC__metadata_chain_check_if_tempfile_needed() returns true.  In
 *  this function, you must supply an I/O handle corresponding to the
 *  FLAC file to edit, and a temporary handle to which the new FLAC
 *  file will be written.  It is the caller's job to move this temporary
 *  FLAC file on top of the original FLAC file to complete the metadata
 *  edit.
 *
 *  The \a handle must be open for reading and be seekable.  The
 *  equivalent minimum stdio fopen() file mode is \c "r" (or \c "rb"
 *  for Windows).
 *
 *  The \a temp_handle must be open for writing.  The
 *  equivalent minimum stdio fopen() file mode is \c "w" (or \c "wb"
 *  for Windows).  It should be an empty stream, or at least positioned
 *  at the start-of-file (in which case it is the caller's duty to
 *  truncate it on return).
 *
 *  For this write function to be used, the chain must have been read with
 *  FLAC__metadata_chain_read_with_callbacks()/FLAC__metadata_chain_read_ogg_with_callbacks(),
 *  not FLAC__metadata_chain_read()/FLAC__metadata_chain_read_ogg().
 *  Also, FLAC__metadata_chain_check_if_tempfile_needed() must have returned
 *  \c true.
 *
 * \param chain        A pointer to an existing chain.
 * \param use_padding  See FLAC__metadata_chain_write()
 * \param handle       The I/O handle of the original FLAC stream to read.
 *                     The handle will NOT be closed after the metadata is
 *                     written; that is the duty of the caller.
 * \param callbacks    A set of callbacks to use for I/O on \a handle.
 *                     The mandatory callbacks are \a read, \a seek, and
 *                     \a eof.
 * \param temp_handle  The I/O handle of the FLAC stream to write.  The
 *                     handle will NOT be closed after the metadata is
 *                     written; that is the duty of the caller.
 * \param temp_callbacks
 *                     A set of callbacks to use for I/O on temp_handle.
 *                     The only mandatory callback is \a write.
 * \assert
 *    \code chain != NULL \endcode
 * \retval FLAC__bool
 *    \c true if the write succeeded, else \c false.  On failure,
 *    check the status with FLAC__metadata_chain_status().
 */
FLAC_API FLAC__bool FLAC__metadata_chain_write_with_callbacks_and_tempfile(FLAC__Metadata_Chain *chain, FLAC__bool use_padding, FLAC__IOHandle handle, FLAC__IOCallbacks callbacks, FLAC__IOHandle temp_handle, FLAC__IOCallbacks temp_callbacks);

/** Merge adjacent PADDING blocks into a single block.
 *
 * \note This function does not write to the FLAC file, it only
 * modifies the chain.
 *
 * \warning Any iterator on the current chain will become invalid after this
 * call.  You should delete the iterator and get a new one.
 *
 * \param chain               A pointer to an existing chain.
 * \assert
 *    \code chain != NULL \endcode
 */
FLAC_API void FLAC__metadata_chain_merge_padding(FLAC__Metadata_Chain *chain);

/** This function will move all PADDING blocks to the end on the metadata,
 *  then merge them into a single block.
 *
 * \note This function does not write to the FLAC file, it only
 * modifies the chain.
 *
 * \warning Any iterator on the current chain will become invalid after this
 * call.  You should delete the iterator and get a new one.
 *
 * \param chain  A pointer to an existing chain.
 * \assert
 *    \code chain != NULL \endcode
 */
FLAC_API void FLAC__metadata_chain_sort_padding(FLAC__Metadata_Chain *chain);


/*********** FLAC__Metadata_Iterator ***********/

/** Create a new iterator instance.
 *
 * \retval FLAC__Metadata_Iterator*
 *    \c NULL if there was an error allocating memory, else the new instance.
 */
FLAC_API FLAC__Metadata_Iterator *FLAC__metadata_iterator_new(void);

/** Free an iterator instance.  Deletes the object pointed to by \a iterator.
 *
 * \param iterator  A pointer to an existing iterator.
 * \assert
 *    \code iterator != NULL \endcode
 */
FLAC_API void FLAC__metadata_iterator_delete(FLAC__Metadata_Iterator *iterator);

/** Initialize the iterator to point to the first metadata block in the
 *  given chain.
 *
 * \param iterator  A pointer to an existing iterator.
 * \param chain     A pointer to an existing and initialized (read) chain.
 * \assert
 *    \code iterator != NULL \endcode
 *    \code chain != NULL \endcode
 */
FLAC_API void FLAC__metadata_iterator_init(FLAC__Metadata_Iterator *iterator, FLAC__Metadata_Chain *chain);

/** Moves the iterator forward one metadata block, returning \c false if
 *  already at the end.
 *
 * \param iterator  A pointer to an existing initialized iterator.
 * \assert
 *    \code iterator != NULL \endcode
 *    \a iterator has been successfully initialized with
 *    FLAC__metadata_iterator_init()
 * \retval FLAC__bool
 *    \c false if already at the last metadata block of the chain, else
 *    \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_iterator_next(FLAC__Metadata_Iterator *iterator);

/** Moves the iterator backward one metadata block, returning \c false if
 *  already at the beginning.
 *
 * \param iterator  A pointer to an existing initialized iterator.
 * \assert
 *    \code iterator != NULL \endcode
 *    \a iterator has been successfully initialized with
 *    FLAC__metadata_iterator_init()
 * \retval FLAC__bool
 *    \c false if already at the first metadata block of the chain, else
 *    \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_iterator_prev(FLAC__Metadata_Iterator *iterator);

/** Get the type of the metadata block at the current position.
 *
 * \param iterator  A pointer to an existing initialized iterator.
 * \assert
 *    \code iterator != NULL \endcode
 *    \a iterator has been successfully initialized with
 *    FLAC__metadata_iterator_init()
 * \retval FLAC__MetadataType
 *    The type of the metadata block at the current iterator position.
 */
FLAC_API FLAC__MetadataType FLAC__metadata_iterator_get_block_type(const FLAC__Metadata_Iterator *iterator);

/** Get the metadata block at the current position.  You can modify
 *  the block in place but must write the chain before the changes
 *  are reflected to the FLAC file.  You do not need to call
 *  FLAC__metadata_iterator_set_block() to reflect the changes;
 *  the pointer returned by FLAC__metadata_iterator_get_block()
 *  points directly into the chain.
 *
 * \warning
 * Do not call FLAC__metadata_object_delete() on the returned object;
 * to delete a block use FLAC__metadata_iterator_delete_block().
 *
 * \param iterator  A pointer to an existing initialized iterator.
 * \assert
 *    \code iterator != NULL \endcode
 *    \a iterator has been successfully initialized with
 *    FLAC__metadata_iterator_init()
 * \retval FLAC__StreamMetadata*
 *    The current metadata block.
 */
FLAC_API FLAC__StreamMetadata *FLAC__metadata_iterator_get_block(FLAC__Metadata_Iterator *iterator);

/** Set the metadata block at the current position, replacing the existing
 *  block.  The new block passed in becomes owned by the chain and it will be
 *  deleted when the chain is deleted.
 *
 * \param iterator  A pointer to an existing initialized iterator.
 * \param block     A pointer to a metadata block.
 * \assert
 *    \code iterator != NULL \endcode
 *    \a iterator has been successfully initialized with
 *    FLAC__metadata_iterator_init()
 *    \code block != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the conditions in the above description are not met, or
 *    a memory allocation error occurs, otherwise \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_iterator_set_block(FLAC__Metadata_Iterator *iterator, FLAC__StreamMetadata *block);

/** Removes the current block from the chain.  If \a replace_with_padding is
 *  \c true, the block will instead be replaced with a padding block of equal
 *  size.  You can not delete the STREAMINFO block.  The iterator will be
 *  left pointing to the block before the one just "deleted", even if
 *  \a replace_with_padding is \c true.
 *
 * \param iterator              A pointer to an existing initialized iterator.
 * \param replace_with_padding  See above.
 * \assert
 *    \code iterator != NULL \endcode
 *    \a iterator has been successfully initialized with
 *    FLAC__metadata_iterator_init()
 * \retval FLAC__bool
 *    \c false if the conditions in the above description are not met,
 *    otherwise \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_iterator_delete_block(FLAC__Metadata_Iterator *iterator, FLAC__bool replace_with_padding);

/** Insert a new block before the current block.  You cannot insert a block
 *  before the first STREAMINFO block.  You cannot insert a STREAMINFO block
 *  as there can be only one, the one that already exists at the head when you
 *  read in a chain.  The chain takes ownership of the new block and it will be
 *  deleted when the chain is deleted.  The iterator will be left pointing to
 *  the new block.
 *
 * \param iterator  A pointer to an existing initialized iterator.
 * \param block     A pointer to a metadata block to insert.
 * \assert
 *    \code iterator != NULL \endcode
 *    \a iterator has been successfully initialized with
 *    FLAC__metadata_iterator_init()
 * \retval FLAC__bool
 *    \c false if the conditions in the above description are not met, or
 *    a memory allocation error occurs, otherwise \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_iterator_insert_block_before(FLAC__Metadata_Iterator *iterator, FLAC__StreamMetadata *block);

/** Insert a new block after the current block.  You cannot insert a STREAMINFO
 *  block as there can be only one, the one that already exists at the head when
 *  you read in a chain.  The chain takes ownership of the new block and it will
 *  be deleted when the chain is deleted.  The iterator will be left pointing to
 *  the new block.
 *
 * \param iterator  A pointer to an existing initialized iterator.
 * \param block     A pointer to a metadata block to insert.
 * \assert
 *    \code iterator != NULL \endcode
 *    \a iterator has been successfully initialized with
 *    FLAC__metadata_iterator_init()
 * \retval FLAC__bool
 *    \c false if the conditions in the above description are not met, or
 *    a memory allocation error occurs, otherwise \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_iterator_insert_block_after(FLAC__Metadata_Iterator *iterator, FLAC__StreamMetadata *block);

/* \} */


/** \defgroup flac_metadata_object FLAC/metadata.h: metadata object methods
 *  \ingroup flac_metadata
 *
 * \brief
 * This module contains methods for manipulating FLAC metadata objects.
 *
 * Since many are variable length we have to be careful about the memory
 * management.  We decree that all pointers to data in the object are
 * owned by the object and memory-managed by the object.
 *
 * Use the FLAC__metadata_object_new() and FLAC__metadata_object_delete()
 * functions to create all instances.  When using the
 * FLAC__metadata_object_set_*() functions to set pointers to data, set
 * \a copy to \c true to have the function make it's own copy of the data, or
 * to \c false to give the object ownership of your data.  In the latter case
 * your pointer must be freeable by free() and will be free()d when the object
 * is FLAC__metadata_object_delete()d.  It is legal to pass a null pointer as
 * the data pointer to a FLAC__metadata_object_set_*() function as long as
 * the length argument is 0 and the \a copy argument is \c false.
 *
 * The FLAC__metadata_object_new() and FLAC__metadata_object_clone() function
 * will return \c NULL in the case of a memory allocation error, otherwise a new
 * object.  The FLAC__metadata_object_set_*() functions return \c false in the
 * case of a memory allocation error.
 *
 * We don't have the convenience of C++ here, so note that the library relies
 * on you to keep the types straight.  In other words, if you pass, for
 * example, a FLAC__StreamMetadata* that represents a STREAMINFO block to
 * FLAC__metadata_object_application_set_data(), you will get an assertion
 * failure.
 *
 * For convenience the FLAC__metadata_object_vorbiscomment_*() functions
 * maintain a trailing NUL on each Vorbis comment entry.  This is not counted
 * toward the length or stored in the stream, but it can make working with plain
 * comments (those that don't contain embedded-NULs in the value) easier.
 * Entries passed into these functions have trailing NULs added if missing, and
 * returned entries are guaranteed to have a trailing NUL.
 *
 * The FLAC__metadata_object_vorbiscomment_*() functions that take a Vorbis
 * comment entry/name/value will first validate that it complies with the Vorbis
 * comment specification and return false if it does not.
 *
 * There is no need to recalculate the length field on metadata blocks you
 * have modified.  They will be calculated automatically before they  are
 * written back to a file.
 *
 * \{
 */


/** Create a new metadata object instance of the given type.
 *
 *  The object will be "empty"; i.e. values and data pointers will be \c 0,
 *  with the exception of FLAC__METADATA_TYPE_VORBIS_COMMENT, which will have
 *  the vendor string set (but zero comments).
 *
 *  Do not pass in a value greater than or equal to
 *  \a FLAC__METADATA_TYPE_UNDEFINED unless you really know what you're
 *  doing.
 *
 * \param type  Type of object to create
 * \retval FLAC__StreamMetadata*
 *    \c NULL if there was an error allocating memory or the type code is
 *    greater than FLAC__MAX_METADATA_TYPE_CODE, else the new instance.
 */
FLAC_API FLAC__StreamMetadata *FLAC__metadata_object_new(FLAC__MetadataType type);

/** Create a copy of an existing metadata object.
 *
 *  The copy is a "deep" copy, i.e. dynamically allocated data within the
 *  object is also copied.  The caller takes ownership of the new block and
 *  is responsible for freeing it with FLAC__metadata_object_delete().
 *
 * \param object  Pointer to object to copy.
 * \assert
 *    \code object != NULL \endcode
 * \retval FLAC__StreamMetadata*
 *    \c NULL if there was an error allocating memory, else the new instance.
 */
FLAC_API FLAC__StreamMetadata *FLAC__metadata_object_clone(const FLAC__StreamMetadata *object);

/** Free a metadata object.  Deletes the object pointed to by \a object.
 *
 *  The delete is a "deep" delete, i.e. dynamically allocated data within the
 *  object is also deleted.
 *
 * \param object  A pointer to an existing object.
 * \assert
 *    \code object != NULL \endcode
 */
FLAC_API void FLAC__metadata_object_delete(FLAC__StreamMetadata *object);

/** Compares two metadata objects.
 *
 *  The compare is "deep", i.e. dynamically allocated data within the
 *  object is also compared.
 *
 * \param block1  A pointer to an existing object.
 * \param block2  A pointer to an existing object.
 * \assert
 *    \code block1 != NULL \endcode
 *    \code block2 != NULL \endcode
 * \retval FLAC__bool
 *    \c true if objects are identical, else \c false.
 */
FLAC_API FLAC__bool FLAC__metadata_object_is_equal(const FLAC__StreamMetadata *block1, const FLAC__StreamMetadata *block2);

/** Sets the application data of an APPLICATION block.
 *
 *  If \a copy is \c true, a copy of the data is stored; otherwise, the object
 *  takes ownership of the pointer.  The existing data will be freed if this
 *  function is successful, otherwise the original data will remain if \a copy
 *  is \c true and malloc() fails.
 *
 * \note It is safe to pass a const pointer to \a data if \a copy is \c true.
 *
 * \param object  A pointer to an existing APPLICATION object.
 * \param data    A pointer to the data to set.
 * \param length  The length of \a data in bytes.
 * \param copy    See above.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_APPLICATION \endcode
 *    \code (data != NULL && length > 0) ||
 * (data == NULL && length == 0 && copy == false) \endcode
 * \retval FLAC__bool
 *    \c false if \a copy is \c true and malloc() fails, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_application_set_data(FLAC__StreamMetadata *object, FLAC__byte *data, unsigned length, FLAC__bool copy);

/** Resize the seekpoint array.
 *
 *  If the size shrinks, elements will truncated; if it grows, new placeholder
 *  points will be added to the end.
 *
 * \param object          A pointer to an existing SEEKTABLE object.
 * \param new_num_points  The desired length of the array; may be \c 0.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_SEEKTABLE \endcode
 *    \code (object->data.seek_table.points == NULL && object->data.seek_table.num_points == 0) ||
 * (object->data.seek_table.points != NULL && object->data.seek_table.num_points > 0) \endcode
 * \retval FLAC__bool
 *    \c false if memory allocation error, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_seektable_resize_points(FLAC__StreamMetadata *object, unsigned new_num_points);

/** Set a seekpoint in a seektable.
 *
 * \param object     A pointer to an existing SEEKTABLE object.
 * \param point_num  Index into seekpoint array to set.
 * \param point      The point to set.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_SEEKTABLE \endcode
 *    \code object->data.seek_table.num_points > point_num \endcode
 */
FLAC_API void FLAC__metadata_object_seektable_set_point(FLAC__StreamMetadata *object, unsigned point_num, FLAC__StreamMetadata_SeekPoint point);

/** Insert a seekpoint into a seektable.
 *
 * \param object     A pointer to an existing SEEKTABLE object.
 * \param point_num  Index into seekpoint array to set.
 * \param point      The point to set.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_SEEKTABLE \endcode
 *    \code object->data.seek_table.num_points >= point_num \endcode
 * \retval FLAC__bool
 *    \c false if memory allocation error, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_seektable_insert_point(FLAC__StreamMetadata *object, unsigned point_num, FLAC__StreamMetadata_SeekPoint point);

/** Delete a seekpoint from a seektable.
 *
 * \param object     A pointer to an existing SEEKTABLE object.
 * \param point_num  Index into seekpoint array to set.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_SEEKTABLE \endcode
 *    \code object->data.seek_table.num_points > point_num \endcode
 * \retval FLAC__bool
 *    \c false if memory allocation error, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_seektable_delete_point(FLAC__StreamMetadata *object, unsigned point_num);

/** Check a seektable to see if it conforms to the FLAC specification.
 *  See the format specification for limits on the contents of the
 *  seektable.
 *
 * \param object  A pointer to an existing SEEKTABLE object.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_SEEKTABLE \endcode
 * \retval FLAC__bool
 *    \c false if seek table is illegal, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_seektable_is_legal(const FLAC__StreamMetadata *object);

/** Append a number of placeholder points to the end of a seek table.
 *
 * \note
 * As with the other ..._seektable_template_... functions, you should
 * call FLAC__metadata_object_seektable_template_sort() when finished
 * to make the seek table legal.
 *
 * \param object  A pointer to an existing SEEKTABLE object.
 * \param num     The number of placeholder points to append.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_SEEKTABLE \endcode
 * \retval FLAC__bool
 *    \c false if memory allocation fails, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_seektable_template_append_placeholders(FLAC__StreamMetadata *object, unsigned num);

/** Append a specific seek point template to the end of a seek table.
 *
 * \note
 * As with the other ..._seektable_template_... functions, you should
 * call FLAC__metadata_object_seektable_template_sort() when finished
 * to make the seek table legal.
 *
 * \param object  A pointer to an existing SEEKTABLE object.
 * \param sample_number  The sample number of the seek point template.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_SEEKTABLE \endcode
 * \retval FLAC__bool
 *    \c false if memory allocation fails, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_seektable_template_append_point(FLAC__StreamMetadata *object, FLAC__uint64 sample_number);

/** Append specific seek point templates to the end of a seek table.
 *
 * \note
 * As with the other ..._seektable_template_... functions, you should
 * call FLAC__metadata_object_seektable_template_sort() when finished
 * to make the seek table legal.
 *
 * \param object  A pointer to an existing SEEKTABLE object.
 * \param sample_numbers  An array of sample numbers for the seek points.
 * \param num     The number of seek point templates to append.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_SEEKTABLE \endcode
 * \retval FLAC__bool
 *    \c false if memory allocation fails, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_seektable_template_append_points(FLAC__StreamMetadata *object, FLAC__uint64 sample_numbers[], unsigned num);

/** Append a set of evenly-spaced seek point templates to the end of a
 *  seek table.
 *
 * \note
 * As with the other ..._seektable_template_... functions, you should
 * call FLAC__metadata_object_seektable_template_sort() when finished
 * to make the seek table legal.
 *
 * \param object  A pointer to an existing SEEKTABLE object.
 * \param num     The number of placeholder points to append.
 * \param total_samples  The total number of samples to be encoded;
 *                       the seekpoints will be spaced approximately
 *                       \a total_samples / \a num samples apart.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_SEEKTABLE \endcode
 *    \code total_samples > 0 \endcode
 * \retval FLAC__bool
 *    \c false if memory allocation fails, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_seektable_template_append_spaced_points(FLAC__StreamMetadata *object, unsigned num, FLAC__uint64 total_samples);

/** Append a set of evenly-spaced seek point templates to the end of a
 *  seek table.
 *
 * \note
 * As with the other ..._seektable_template_... functions, you should
 * call FLAC__metadata_object_seektable_template_sort() when finished
 * to make the seek table legal.
 *
 * \param object  A pointer to an existing SEEKTABLE object.
 * \param samples The number of samples apart to space the placeholder
 *                points.  The first point will be at sample \c 0, the
 *                second at sample \a samples, then 2*\a samples, and
 *                so on.  As long as \a samples and \a total_samples
 *                are greater than \c 0, there will always be at least
 *                one seekpoint at sample \c 0.
 * \param total_samples  The total number of samples to be encoded;
 *                       the seekpoints will be spaced
 *                       \a samples samples apart.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_SEEKTABLE \endcode
 *    \code samples > 0 \endcode
 *    \code total_samples > 0 \endcode
 * \retval FLAC__bool
 *    \c false if memory allocation fails, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_seektable_template_append_spaced_points_by_samples(FLAC__StreamMetadata *object, unsigned samples, FLAC__uint64 total_samples);

/** Sort a seek table's seek points according to the format specification,
 *  removing duplicates.
 *
 * \param object   A pointer to a seek table to be sorted.
 * \param compact  If \c false, behaves like FLAC__format_seektable_sort().
 *                 If \c true, duplicates are deleted and the seek table is
 *                 shrunk appropriately; the number of placeholder points
 *                 present in the seek table will be the same after the call
 *                 as before.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_SEEKTABLE \endcode
 * \retval FLAC__bool
 *    \c false if realloc() fails, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_seektable_template_sort(FLAC__StreamMetadata *object, FLAC__bool compact);

/** Sets the vendor string in a VORBIS_COMMENT block.
 *
 *  For convenience, a trailing NUL is added to the entry if it doesn't have
 *  one already.
 *
 *  If \a copy is \c true, a copy of the entry is stored; otherwise, the object
 *  takes ownership of the \c entry.entry pointer.
 *
 *  \note If this function returns \c false, the caller still owns the
 *  pointer.
 *
 * \param object  A pointer to an existing VORBIS_COMMENT object.
 * \param entry   The entry to set the vendor string to.
 * \param copy    See above.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_VORBIS_COMMENT \endcode
 *    \code (entry.entry != NULL && entry.length > 0) ||
 * (entry.entry == NULL && entry.length == 0) \endcode
 * \retval FLAC__bool
 *    \c false if memory allocation fails or \a entry does not comply with the
 *    Vorbis comment specification, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_vorbiscomment_set_vendor_string(FLAC__StreamMetadata *object, FLAC__StreamMetadata_VorbisComment_Entry entry, FLAC__bool copy);

/** Resize the comment array.
 *
 *  If the size shrinks, elements will truncated; if it grows, new empty
 *  fields will be added to the end.
 *
 * \param object            A pointer to an existing VORBIS_COMMENT object.
 * \param new_num_comments  The desired length of the array; may be \c 0.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_VORBIS_COMMENT \endcode
 *    \code (object->data.vorbis_comment.comments == NULL && object->data.vorbis_comment.num_comments == 0) ||
 * (object->data.vorbis_comment.comments != NULL && object->data.vorbis_comment.num_comments > 0) \endcode
 * \retval FLAC__bool
 *    \c false if memory allocation fails, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_vorbiscomment_resize_comments(FLAC__StreamMetadata *object, unsigned new_num_comments);

/** Sets a comment in a VORBIS_COMMENT block.
 *
 *  For convenience, a trailing NUL is added to the entry if it doesn't have
 *  one already.
 *
 *  If \a copy is \c true, a copy of the entry is stored; otherwise, the object
 *  takes ownership of the \c entry.entry pointer.
 *
 *  \note If this function returns \c false, the caller still owns the
 *  pointer.
 *
 * \param object       A pointer to an existing VORBIS_COMMENT object.
 * \param comment_num  Index into comment array to set.
 * \param entry        The entry to set the comment to.
 * \param copy         See above.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_VORBIS_COMMENT \endcode
 *    \code comment_num < object->data.vorbis_comment.num_comments \endcode
 *    \code (entry.entry != NULL && entry.length > 0) ||
 * (entry.entry == NULL && entry.length == 0) \endcode
 * \retval FLAC__bool
 *    \c false if memory allocation fails or \a entry does not comply with the
 *    Vorbis comment specification, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_vorbiscomment_set_comment(FLAC__StreamMetadata *object, unsigned comment_num, FLAC__StreamMetadata_VorbisComment_Entry entry, FLAC__bool copy);

/** Insert a comment in a VORBIS_COMMENT block at the given index.
 *
 *  For convenience, a trailing NUL is added to the entry if it doesn't have
 *  one already.
 *
 *  If \a copy is \c true, a copy of the entry is stored; otherwise, the object
 *  takes ownership of the \c entry.entry pointer.
 *
 *  \note If this function returns \c false, the caller still owns the
 *  pointer.
 *
 * \param object       A pointer to an existing VORBIS_COMMENT object.
 * \param comment_num  The index at which to insert the comment.  The comments
 *                     at and after \a comment_num move right one position.
 *                     To append a comment to the end, set \a comment_num to
 *                     \c object->data.vorbis_comment.num_comments .
 * \param entry        The comment to insert.
 * \param copy         See above.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_VORBIS_COMMENT \endcode
 *    \code object->data.vorbis_comment.num_comments >= comment_num \endcode
 *    \code (entry.entry != NULL && entry.length > 0) ||
 * (entry.entry == NULL && entry.length == 0 && copy == false) \endcode
 * \retval FLAC__bool
 *    \c false if memory allocation fails or \a entry does not comply with the
 *    Vorbis comment specification, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_vorbiscomment_insert_comment(FLAC__StreamMetadata *object, unsigned comment_num, FLAC__StreamMetadata_VorbisComment_Entry entry, FLAC__bool copy);

/** Appends a comment to a VORBIS_COMMENT block.
 *
 *  For convenience, a trailing NUL is added to the entry if it doesn't have
 *  one already.
 *
 *  If \a copy is \c true, a copy of the entry is stored; otherwise, the object
 *  takes ownership of the \c entry.entry pointer.
 *
 *  \note If this function returns \c false, the caller still owns the
 *  pointer.
 *
 * \param object       A pointer to an existing VORBIS_COMMENT object.
 * \param entry        The comment to insert.
 * \param copy         See above.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_VORBIS_COMMENT \endcode
 *    \code (entry.entry != NULL && entry.length > 0) ||
 * (entry.entry == NULL && entry.length == 0 && copy == false) \endcode
 * \retval FLAC__bool
 *    \c false if memory allocation fails or \a entry does not comply with the
 *    Vorbis comment specification, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_vorbiscomment_append_comment(FLAC__StreamMetadata *object, FLAC__StreamMetadata_VorbisComment_Entry entry, FLAC__bool copy);

/** Replaces comments in a VORBIS_COMMENT block with a new one.
 *
 *  For convenience, a trailing NUL is added to the entry if it doesn't have
 *  one already.
 *
 *  Depending on the the value of \a all, either all or just the first comment
 *  whose field name(s) match the given entry's name will be replaced by the
 *  given entry.  If no comments match, \a entry will simply be appended.
 *
 *  If \a copy is \c true, a copy of the entry is stored; otherwise, the object
 *  takes ownership of the \c entry.entry pointer.
 *
 *  \note If this function returns \c false, the caller still owns the
 *  pointer.
 *
 * \param object       A pointer to an existing VORBIS_COMMENT object.
 * \param entry        The comment to insert.
 * \param all          If \c true, all comments whose field name matches
 *                     \a entry's field name will be removed, and \a entry will
 *                     be inserted at the position of the first matching
 *                     comment.  If \c false, only the first comment whose
 *                     field name matches \a entry's field name will be
 *                     replaced with \a entry.
 * \param copy         See above.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_VORBIS_COMMENT \endcode
 *    \code (entry.entry != NULL && entry.length > 0) ||
 * (entry.entry == NULL && entry.length == 0 && copy == false) \endcode
 * \retval FLAC__bool
 *    \c false if memory allocation fails or \a entry does not comply with the
 *    Vorbis comment specification, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_vorbiscomment_replace_comment(FLAC__StreamMetadata *object, FLAC__StreamMetadata_VorbisComment_Entry entry, FLAC__bool all, FLAC__bool copy);

/** Delete a comment in a VORBIS_COMMENT block at the given index.
 *
 * \param object       A pointer to an existing VORBIS_COMMENT object.
 * \param comment_num  The index of the comment to delete.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_VORBIS_COMMENT \endcode
 *    \code object->data.vorbis_comment.num_comments > comment_num \endcode
 * \retval FLAC__bool
 *    \c false if realloc() fails, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_vorbiscomment_delete_comment(FLAC__StreamMetadata *object, unsigned comment_num);

/** Creates a Vorbis comment entry from NUL-terminated name and value strings.
 *
 *  On return, the filled-in \a entry->entry pointer will point to malloc()ed
 *  memory and shall be owned by the caller.  For convenience the entry will
 *  have a terminating NUL.
 *
 * \param entry              A pointer to a Vorbis comment entry.  The entry's
 *                           \c entry pointer should not point to allocated
 *                           memory as it will be overwritten.
 * \param field_name         The field name in ASCII, \c NUL terminated.
 * \param field_value        The field value in UTF-8, \c NUL terminated.
 * \assert
 *    \code entry != NULL \endcode
 *    \code field_name != NULL \endcode
 *    \code field_value != NULL \endcode
 * \retval FLAC__bool
 *    \c false if malloc() fails, or if \a field_name or \a field_value does
 *    not comply with the Vorbis comment specification, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(FLAC__StreamMetadata_VorbisComment_Entry *entry, const char *field_name, const char *field_value);

/** Splits a Vorbis comment entry into NUL-terminated name and value strings.
 *
 *  The returned pointers to name and value will be allocated by malloc()
 *  and shall be owned by the caller.
 *
 * \param entry              An existing Vorbis comment entry.
 * \param field_name         The address of where the returned pointer to the
 *                           field name will be stored.
 * \param field_value        The address of where the returned pointer to the
 *                           field value will be stored.
 * \assert
 *    \code (entry.entry != NULL && entry.length > 0) \endcode
 *    \code memchr(entry.entry, '=', entry.length) != NULL \endcode
 *    \code field_name != NULL \endcode
 *    \code field_value != NULL \endcode
 * \retval FLAC__bool
 *    \c false if memory allocation fails or \a entry does not comply with the
 *    Vorbis comment specification, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_vorbiscomment_entry_to_name_value_pair(const FLAC__StreamMetadata_VorbisComment_Entry entry, char **field_name, char **field_value);

/** Check if the given Vorbis comment entry's field name matches the given
 *  field name.
 *
 * \param entry              An existing Vorbis comment entry.
 * \param field_name         The field name to check.
 * \param field_name_length  The length of \a field_name, not including the
 *                           terminating \c NUL.
 * \assert
 *    \code (entry.entry != NULL && entry.length > 0) \endcode
 * \retval FLAC__bool
 *    \c true if the field names match, else \c false
 */
FLAC_API FLAC__bool FLAC__metadata_object_vorbiscomment_entry_matches(const FLAC__StreamMetadata_VorbisComment_Entry entry, const char *field_name, unsigned field_name_length);

/** Find a Vorbis comment with the given field name.
 *
 *  The search begins at entry number \a offset; use an offset of 0 to
 *  search from the beginning of the comment array.
 *
 * \param object      A pointer to an existing VORBIS_COMMENT object.
 * \param offset      The offset into the comment array from where to start
 *                    the search.
 * \param field_name  The field name of the comment to find.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_VORBIS_COMMENT \endcode
 *    \code field_name != NULL \endcode
 * \retval int
 *    The offset in the comment array of the first comment whose field
 *    name matches \a field_name, or \c -1 if no match was found.
 */
FLAC_API int FLAC__metadata_object_vorbiscomment_find_entry_from(const FLAC__StreamMetadata *object, unsigned offset, const char *field_name);

/** Remove first Vorbis comment matching the given field name.
 *
 * \param object      A pointer to an existing VORBIS_COMMENT object.
 * \param field_name  The field name of comment to delete.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_VORBIS_COMMENT \endcode
 * \retval int
 *    \c -1 for memory allocation error, \c 0 for no matching entries,
 *    \c 1 for one matching entry deleted.
 */
FLAC_API int FLAC__metadata_object_vorbiscomment_remove_entry_matching(FLAC__StreamMetadata *object, const char *field_name);

/** Remove all Vorbis comments matching the given field name.
 *
 * \param object      A pointer to an existing VORBIS_COMMENT object.
 * \param field_name  The field name of comments to delete.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_VORBIS_COMMENT \endcode
 * \retval int
 *    \c -1 for memory allocation error, \c 0 for no matching entries,
 *    else the number of matching entries deleted.
 */
FLAC_API int FLAC__metadata_object_vorbiscomment_remove_entries_matching(FLAC__StreamMetadata *object, const char *field_name);

/** Create a new CUESHEET track instance.
 *
 *  The object will be "empty"; i.e. values and data pointers will be \c 0.
 *
 * \retval FLAC__StreamMetadata_CueSheet_Track*
 *    \c NULL if there was an error allocating memory, else the new instance.
 */
FLAC_API FLAC__StreamMetadata_CueSheet_Track *FLAC__metadata_object_cuesheet_track_new(void);

/** Create a copy of an existing CUESHEET track object.
 *
 *  The copy is a "deep" copy, i.e. dynamically allocated data within the
 *  object is also copied.  The caller takes ownership of the new object and
 *  is responsible for freeing it with
 *  FLAC__metadata_object_cuesheet_track_delete().
 *
 * \param object  Pointer to object to copy.
 * \assert
 *    \code object != NULL \endcode
 * \retval FLAC__StreamMetadata_CueSheet_Track*
 *    \c NULL if there was an error allocating memory, else the new instance.
 */
FLAC_API FLAC__StreamMetadata_CueSheet_Track *FLAC__metadata_object_cuesheet_track_clone(const FLAC__StreamMetadata_CueSheet_Track *object);

/** Delete a CUESHEET track object
 *
 * \param object       A pointer to an existing CUESHEET track object.
 * \assert
 *    \code object != NULL \endcode
 */
FLAC_API void FLAC__metadata_object_cuesheet_track_delete(FLAC__StreamMetadata_CueSheet_Track *object);

/** Resize a track's index point array.
 *
 *  If the size shrinks, elements will truncated; if it grows, new blank
 *  indices will be added to the end.
 *
 * \param object           A pointer to an existing CUESHEET object.
 * \param track_num        The index of the track to modify.  NOTE: this is not
 *                         necessarily the same as the track's \a number field.
 * \param new_num_indices  The desired length of the array; may be \c 0.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_CUESHEET \endcode
 *    \code object->data.cue_sheet.num_tracks > track_num \endcode
 *    \code (object->data.cue_sheet.tracks[track_num].indices == NULL && object->data.cue_sheet.tracks[track_num].num_indices == 0) ||
 * (object->data.cue_sheet.tracks[track_num].indices != NULL && object->data.cue_sheet.tracks[track_num].num_indices > 0) \endcode
 * \retval FLAC__bool
 *    \c false if memory allocation error, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_cuesheet_track_resize_indices(FLAC__StreamMetadata *object, unsigned track_num, unsigned new_num_indices);

/** Insert an index point in a CUESHEET track at the given index.
 *
 * \param object       A pointer to an existing CUESHEET object.
 * \param track_num    The index of the track to modify.  NOTE: this is not
 *                     necessarily the same as the track's \a number field.
 * \param index_num    The index into the track's index array at which to
 *                     insert the index point.  NOTE: this is not necessarily
 *                     the same as the index point's \a number field.  The
 *                     indices at and after \a index_num move right one
 *                     position.  To append an index point to the end, set
 *                     \a index_num to
 *                     \c object->data.cue_sheet.tracks[track_num].num_indices .
 * \param index        The index point to insert.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_CUESHEET \endcode
 *    \code object->data.cue_sheet.num_tracks > track_num \endcode
 *    \code object->data.cue_sheet.tracks[track_num].num_indices >= index_num \endcode
 * \retval FLAC__bool
 *    \c false if realloc() fails, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_cuesheet_track_insert_index(FLAC__StreamMetadata *object, unsigned track_num, unsigned index_num, FLAC__StreamMetadata_CueSheet_Index index);

/** Insert a blank index point in a CUESHEET track at the given index.
 *
 *  A blank index point is one in which all field values are zero.
 *
 * \param object       A pointer to an existing CUESHEET object.
 * \param track_num    The index of the track to modify.  NOTE: this is not
 *                     necessarily the same as the track's \a number field.
 * \param index_num    The index into the track's index array at which to
 *                     insert the index point.  NOTE: this is not necessarily
 *                     the same as the index point's \a number field.  The
 *                     indices at and after \a index_num move right one
 *                     position.  To append an index point to the end, set
 *                     \a index_num to
 *                     \c object->data.cue_sheet.tracks[track_num].num_indices .
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_CUESHEET \endcode
 *    \code object->data.cue_sheet.num_tracks > track_num \endcode
 *    \code object->data.cue_sheet.tracks[track_num].num_indices >= index_num \endcode
 * \retval FLAC__bool
 *    \c false if realloc() fails, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_cuesheet_track_insert_blank_index(FLAC__StreamMetadata *object, unsigned track_num, unsigned index_num);

/** Delete an index point in a CUESHEET track at the given index.
 *
 * \param object       A pointer to an existing CUESHEET object.
 * \param track_num    The index into the track array of the track to
 *                     modify.  NOTE: this is not necessarily the same
 *                     as the track's \a number field.
 * \param index_num    The index into the track's index array of the index
 *                     to delete.  NOTE: this is not necessarily the same
 *                     as the index's \a number field.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_CUESHEET \endcode
 *    \code object->data.cue_sheet.num_tracks > track_num \endcode
 *    \code object->data.cue_sheet.tracks[track_num].num_indices > index_num \endcode
 * \retval FLAC__bool
 *    \c false if realloc() fails, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_cuesheet_track_delete_index(FLAC__StreamMetadata *object, unsigned track_num, unsigned index_num);

/** Resize the track array.
 *
 *  If the size shrinks, elements will truncated; if it grows, new blank
 *  tracks will be added to the end.
 *
 * \param object            A pointer to an existing CUESHEET object.
 * \param new_num_tracks    The desired length of the array; may be \c 0.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_CUESHEET \endcode
 *    \code (object->data.cue_sheet.tracks == NULL && object->data.cue_sheet.num_tracks == 0) ||
 * (object->data.cue_sheet.tracks != NULL && object->data.cue_sheet.num_tracks > 0) \endcode
 * \retval FLAC__bool
 *    \c false if memory allocation error, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_cuesheet_resize_tracks(FLAC__StreamMetadata *object, unsigned new_num_tracks);

/** Sets a track in a CUESHEET block.
 *
 *  If \a copy is \c true, a copy of the track is stored; otherwise, the object
 *  takes ownership of the \a track pointer.
 *
 * \param object       A pointer to an existing CUESHEET object.
 * \param track_num    Index into track array to set.  NOTE: this is not
 *                     necessarily the same as the track's \a number field.
 * \param track        The track to set the track to.  You may safely pass in
 *                     a const pointer if \a copy is \c true.
 * \param copy         See above.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_CUESHEET \endcode
 *    \code track_num < object->data.cue_sheet.num_tracks \endcode
 *    \code (track->indices != NULL && track->num_indices > 0) ||
 * (track->indices == NULL && track->num_indices == 0)
 * \retval FLAC__bool
 *    \c false if \a copy is \c true and malloc() fails, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_cuesheet_set_track(FLAC__StreamMetadata *object, unsigned track_num, FLAC__StreamMetadata_CueSheet_Track *track, FLAC__bool copy);

/** Insert a track in a CUESHEET block at the given index.
 *
 *  If \a copy is \c true, a copy of the track is stored; otherwise, the object
 *  takes ownership of the \a track pointer.
 *
 * \param object       A pointer to an existing CUESHEET object.
 * \param track_num    The index at which to insert the track.  NOTE: this
 *                     is not necessarily the same as the track's \a number
 *                     field.  The tracks at and after \a track_num move right
 *                     one position.  To append a track to the end, set
 *                     \a track_num to \c object->data.cue_sheet.num_tracks .
 * \param track        The track to insert.  You may safely pass in a const
 *                     pointer if \a copy is \c true.
 * \param copy         See above.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_CUESHEET \endcode
 *    \code object->data.cue_sheet.num_tracks >= track_num \endcode
 * \retval FLAC__bool
 *    \c false if \a copy is \c true and malloc() fails, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_cuesheet_insert_track(FLAC__StreamMetadata *object, unsigned track_num, FLAC__StreamMetadata_CueSheet_Track *track, FLAC__bool copy);

/** Insert a blank track in a CUESHEET block at the given index.
 *
 *  A blank track is one in which all field values are zero.
 *
 * \param object       A pointer to an existing CUESHEET object.
 * \param track_num    The index at which to insert the track.  NOTE: this
 *                     is not necessarily the same as the track's \a number
 *                     field.  The tracks at and after \a track_num move right
 *                     one position.  To append a track to the end, set
 *                     \a track_num to \c object->data.cue_sheet.num_tracks .
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_CUESHEET \endcode
 *    \code object->data.cue_sheet.num_tracks >= track_num \endcode
 * \retval FLAC__bool
 *    \c false if \a copy is \c true and malloc() fails, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_cuesheet_insert_blank_track(FLAC__StreamMetadata *object, unsigned track_num);

/** Delete a track in a CUESHEET block at the given index.
 *
 * \param object       A pointer to an existing CUESHEET object.
 * \param track_num    The index into the track array of the track to
 *                     delete.  NOTE: this is not necessarily the same
 *                     as the track's \a number field.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_CUESHEET \endcode
 *    \code object->data.cue_sheet.num_tracks > track_num \endcode
 * \retval FLAC__bool
 *    \c false if realloc() fails, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_cuesheet_delete_track(FLAC__StreamMetadata *object, unsigned track_num);

/** Check a cue sheet to see if it conforms to the FLAC specification.
 *  See the format specification for limits on the contents of the
 *  cue sheet.
 *
 * \param object     A pointer to an existing CUESHEET object.
 * \param check_cd_da_subset  If \c true, check CUESHEET against more
 *                   stringent requirements for a CD-DA (audio) disc.
 * \param violation  Address of a pointer to a string.  If there is a
 *                   violation, a pointer to a string explanation of the
 *                   violation will be returned here. \a violation may be
 *                   \c NULL if you don't need the returned string.  Do not
 *                   free the returned string; it will always point to static
 *                   data.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_CUESHEET \endcode
 * \retval FLAC__bool
 *    \c false if cue sheet is illegal, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_cuesheet_is_legal(const FLAC__StreamMetadata *object, FLAC__bool check_cd_da_subset, const char **violation);

/** Calculate and return the CDDB/freedb ID for a cue sheet.  The function
 *  assumes the cue sheet corresponds to a CD; the result is undefined
 *  if the cuesheet's is_cd bit is not set.
 *
 * \param object     A pointer to an existing CUESHEET object.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_CUESHEET \endcode
 * \retval FLAC__uint32
 *    The unsigned integer representation of the CDDB/freedb ID
 */
FLAC_API FLAC__uint32 FLAC__metadata_object_cuesheet_calculate_cddb_id(const FLAC__StreamMetadata *object);

/** Sets the MIME type of a PICTURE block.
 *
 *  If \a copy is \c true, a copy of the string is stored; otherwise, the object
 *  takes ownership of the pointer.  The existing string will be freed if this
 *  function is successful, otherwise the original string will remain if \a copy
 *  is \c true and malloc() fails.
 *
 * \note It is safe to pass a const pointer to \a mime_type if \a copy is \c true.
 *
 * \param object      A pointer to an existing PICTURE object.
 * \param mime_type   A pointer to the MIME type string.  The string must be
 *                    ASCII characters 0x20-0x7e, NUL-terminated.  No validation
 *                    is done.
 * \param copy        See above.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_PICTURE \endcode
 *    \code (mime_type != NULL) \endcode
 * \retval FLAC__bool
 *    \c false if \a copy is \c true and malloc() fails, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_picture_set_mime_type(FLAC__StreamMetadata *object, char *mime_type, FLAC__bool copy);

/** Sets the description of a PICTURE block.
 *
 *  If \a copy is \c true, a copy of the string is stored; otherwise, the object
 *  takes ownership of the pointer.  The existing string will be freed if this
 *  function is successful, otherwise the original string will remain if \a copy
 *  is \c true and malloc() fails.
 *
 * \note It is safe to pass a const pointer to \a description if \a copy is \c true.
 *
 * \param object      A pointer to an existing PICTURE object.
 * \param description A pointer to the description string.  The string must be
 *                    valid UTF-8, NUL-terminated.  No validation is done.
 * \param copy        See above.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_PICTURE \endcode
 *    \code (description != NULL) \endcode
 * \retval FLAC__bool
 *    \c false if \a copy is \c true and malloc() fails, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_picture_set_description(FLAC__StreamMetadata *object, FLAC__byte *description, FLAC__bool copy);

/** Sets the picture data of a PICTURE block.
 *
 *  If \a copy is \c true, a copy of the data is stored; otherwise, the object
 *  takes ownership of the pointer.  Also sets the \a data_length field of the
 *  metadata object to what is passed in as the \a length parameter.  The
 *  existing data will be freed if this function is successful, otherwise the
 *  original data and data_length will remain if \a copy is \c true and
 *  malloc() fails.
 *
 * \note It is safe to pass a const pointer to \a data if \a copy is \c true.
 *
 * \param object  A pointer to an existing PICTURE object.
 * \param data    A pointer to the data to set.
 * \param length  The length of \a data in bytes.
 * \param copy    See above.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_PICTURE \endcode
 *    \code (data != NULL && length > 0) ||
 * (data == NULL && length == 0 && copy == false) \endcode
 * \retval FLAC__bool
 *    \c false if \a copy is \c true and malloc() fails, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_picture_set_data(FLAC__StreamMetadata *object, FLAC__byte *data, FLAC__uint32 length, FLAC__bool copy);

/** Check a PICTURE block to see if it conforms to the FLAC specification.
 *  See the format specification for limits on the contents of the
 *  PICTURE block.
 *
 * \param object     A pointer to existing PICTURE block to be checked.
 * \param violation  Address of a pointer to a string.  If there is a
 *                   violation, a pointer to a string explanation of the
 *                   violation will be returned here. \a violation may be
 *                   \c NULL if you don't need the returned string.  Do not
 *                   free the returned string; it will always point to static
 *                   data.
 * \assert
 *    \code object != NULL \endcode
 *    \code object->type == FLAC__METADATA_TYPE_PICTURE \endcode
 * \retval FLAC__bool
 *    \c false if PICTURE block is illegal, else \c true.
 */
FLAC_API FLAC__bool FLAC__metadata_object_picture_is_legal(const FLAC__StreamMetadata *object, const char **violation);

/* \} */

#ifdef __cplusplus
}
#endif

#endif
#endif //++-- metadata.h  

//++++++++++++ private
#if 1 //++ bitmath.h
#ifndef FLAC__PRIVATE__BITMATH_H
#define FLAC__PRIVATE__BITMATH_H

unsigned FLAC__bitmath_ilog2(FLAC__uint32 v);
unsigned FLAC__bitmath_ilog2_wide(FLAC__uint64 v);
unsigned FLAC__bitmath_silog2(int v);
unsigned FLAC__bitmath_silog2_wide(FLAC__int64 v);

#endif
#endif //++-- bitmath.h

#if 1 //++ float.h 


#ifndef FLAC__PRIVATE__FLOAT_H
#define FLAC__PRIVATE__FLOAT_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * These typedefs make it easier to ensure that integer versions of
 * the library really only contain integer operations.  All the code
 * in libFLAC should use FLAC__float and FLAC__double in place of
 * float and double, and be protected by checks of the macro
 * FLAC__INTEGER_ONLY_LIBRARY.
 *
 * FLAC__real is the basic floating point type used in LPC analysis.
 */
#ifndef FLAC__INTEGER_ONLY_LIBRARY
typedef double FLAC__double;
typedef float FLAC__float;
/*
 * WATCHOUT: changing FLAC__real will change the signatures of many
 * functions that have assembly language equivalents and break them.
 */
typedef float FLAC__real;
#else
/*
 * The convention for FLAC__fixedpoint is to use the upper 16 bits
 * for the integer part and lower 16 bits for the fractional part.
 */
typedef FLAC__int32 FLAC__fixedpoint;
extern const FLAC__fixedpoint FLAC__FP_ZERO;
extern const FLAC__fixedpoint FLAC__FP_ONE_HALF;
extern const FLAC__fixedpoint FLAC__FP_ONE;
extern const FLAC__fixedpoint FLAC__FP_LN2;
extern const FLAC__fixedpoint FLAC__FP_E;

#define FLAC__fixedpoint_trunc(x) ((x)>>16)

#define FLAC__fixedpoint_mul(x, y) ( (FLAC__fixedpoint) ( ((FLAC__int64)(x)*(FLAC__int64)(y)) >> 16 ) )

#define FLAC__fixedpoint_div(x, y) ( (FLAC__fixedpoint) ( ( ((FLAC__int64)(x)<<32) / (FLAC__int64)(y) ) >> 16 ) )

/*
 *	FLAC__fixedpoint_log2()
 *	--------------------------------------------------------------------
 *	Returns the base-2 logarithm of the fixed-point number 'x' using an
 *	algorithm by Knuth for x >= 1.0
 *
 *	'fracbits' is the number of fractional bits of 'x'.  'fracbits' must
 *	be < 32 and evenly divisible by 4 (0 is OK but not very precise).
 *
 *	'precision' roughly limits the number of iterations that are done;
 *	use (unsigned)(-1) for maximum precision.
 *
 *	If 'x' is less than one -- that is, x < (1<<fracbits) -- then this
 *	function will punt and return 0.
 *
 *	The return value will also have 'fracbits' fractional bits.
 */
FLAC__uint32 FLAC__fixedpoint_log2(FLAC__uint32 x, unsigned fracbits, unsigned precision);

#endif

#endif
#endif //++-- float.h 


#if 1 //++ crc.h 

#ifndef FLAC__PRIVATE__CRC_H
#define FLAC__PRIVATE__CRC_H

/* 8 bit CRC generator, MSB shifted first
** polynomial = x^8 + x^2 + x^1 + x^0
** init = 0
*/
extern FLAC__byte const FLAC__crc8_table[256];
#define FLAC__CRC8_UPDATE(data, crc) (crc) = FLAC__crc8_table[(crc) ^ (data)];
void FLAC__crc8_update(const FLAC__byte data, FLAC__uint8 *crc);
void FLAC__crc8_update_block(const FLAC__byte *data, unsigned len, FLAC__uint8 *crc);
FLAC__uint8 FLAC__crc8(const FLAC__byte *data, unsigned len);

/* 16 bit CRC generator, MSB shifted first
** polynomial = x^16 + x^15 + x^2 + x^0
** init = 0
*/
extern unsigned FLAC__crc16_table[256];

#define FLAC__CRC16_UPDATE(data, crc) (((((crc)<<8) & 0xffff) ^ FLAC__crc16_table[((crc)>>8) ^ (data)]))
/* this alternate may be faster on some systems/compilers */
#if 0
#define FLAC__CRC16_UPDATE(data, crc) ((((crc)<<8) ^ FLAC__crc16_table[((crc)>>8) ^ (data)]) & 0xffff)
#endif

unsigned FLAC__crc16(const FLAC__byte *data, unsigned len);

#endif
#endif //++-- crc.h 

#if 1 //++ bitreader.h 

#ifndef FLAC__PRIVATE__BITREADER_H
#define FLAC__PRIVATE__BITREADER_H

#include <stdio.h> /* for FILE */

/*
 * opaque structure definition
 */
struct FLAC__BitReader;
typedef struct FLAC__BitReader FLAC__BitReader;

typedef FLAC__bool (*FLAC__BitReaderReadCallback)(FLAC__byte buffer[], size_t *bytes, void *client_data);

/*
 * construction, deletion, initialization, etc functions
 */
FLAC__BitReader *FLAC__bitreader_new(void);
void FLAC__bitreader_delete(FLAC__BitReader *br);
FLAC__bool FLAC__bitreader_init(FLAC__BitReader *br, FLAC__CPUInfo cpu, FLAC__BitReaderReadCallback rcb, void *cd);
void FLAC__bitreader_free(FLAC__BitReader *br); /* does not 'free(br)' */
FLAC__bool FLAC__bitreader_clear(FLAC__BitReader *br);
void FLAC__bitreader_dump(const FLAC__BitReader *br, FILE *out);

/*
 * CRC functions
 */
void FLAC__bitreader_reset_read_crc16(FLAC__BitReader *br, FLAC__uint16 seed);
FLAC__uint16 FLAC__bitreader_get_read_crc16(FLAC__BitReader *br);

/*
 * info functions
 */
FLAC__bool FLAC__bitreader_is_consumed_byte_aligned(const FLAC__BitReader *br);
unsigned FLAC__bitreader_bits_left_for_byte_alignment(const FLAC__BitReader *br);
unsigned FLAC__bitreader_get_input_bits_unconsumed(const FLAC__BitReader *br);

/*
 * read functions
 */

FLAC__bool FLAC__bitreader_read_raw_uint32(FLAC__BitReader *br, FLAC__uint32 *val, unsigned bits);
FLAC__bool FLAC__bitreader_read_raw_int32(FLAC__BitReader *br, FLAC__int32 *val, unsigned bits);
FLAC__bool FLAC__bitreader_read_raw_uint64(FLAC__BitReader *br, FLAC__uint64 *val, unsigned bits);
FLAC__bool FLAC__bitreader_read_uint32_little_endian(FLAC__BitReader *br, FLAC__uint32 *val); /*only for bits=32*/
FLAC__bool FLAC__bitreader_skip_bits_no_crc(FLAC__BitReader *br, unsigned bits); /* WATCHOUT: does not CRC the skipped data! */ /*@@@@ add to unit tests */
FLAC__bool FLAC__bitreader_skip_byte_block_aligned_no_crc(FLAC__BitReader *br, unsigned nvals); /* WATCHOUT: does not CRC the read data! */
FLAC__bool FLAC__bitreader_read_byte_block_aligned_no_crc(FLAC__BitReader *br, FLAC__byte *val, unsigned nvals); /* WATCHOUT: does not CRC the read data! */
FLAC__bool FLAC__bitreader_read_unary_unsigned(FLAC__BitReader *br, unsigned *val);
FLAC__bool FLAC__bitreader_read_rice_signed(FLAC__BitReader *br, int *val, unsigned parameter);
FLAC__bool FLAC__bitreader_read_rice_signed_block(FLAC__BitReader *br, int vals[], unsigned nvals, unsigned parameter);
#ifndef FLAC__NO_ASM
#  ifdef FLAC__CPU_IA32
#    ifdef FLAC__HAS_NASM
FLAC__bool FLAC__bitreader_read_rice_signed_block_asm_ia32_bswap(FLAC__BitReader *br, int vals[], unsigned nvals, unsigned parameter);
#    endif
#  endif
#endif
#if 0 /* UNUSED */
FLAC__bool FLAC__bitreader_read_golomb_signed(FLAC__BitReader *br, int *val, unsigned parameter);
FLAC__bool FLAC__bitreader_read_golomb_unsigned(FLAC__BitReader *br, unsigned *val, unsigned parameter);
#endif
FLAC__bool FLAC__bitreader_read_utf8_uint32(FLAC__BitReader *br, FLAC__uint32 *val, FLAC__byte *raw, unsigned *rawlen);
FLAC__bool FLAC__bitreader_read_utf8_uint64(FLAC__BitReader *br, FLAC__uint64 *val, FLAC__byte *raw, unsigned *rawlen);

FLAC__bool bitreader_read_from_client_(FLAC__BitReader *br);
#endif
#endif //++-- bitreader.h 

#if 1 //++ bitwriter.h 
#ifndef FLAC__PRIVATE__BITWRITER_H
#define FLAC__PRIVATE__BITWRITER_H

#include <stdio.h> /* for FILE */

/*
 * opaque structure definition
 */
struct FLAC__BitWriter;
typedef struct FLAC__BitWriter FLAC__BitWriter;

/*
 * construction, deletion, initialization, etc functions
 */
FLAC__BitWriter *FLAC__bitwriter_new(void);
void FLAC__bitwriter_delete(FLAC__BitWriter *bw);
FLAC__bool FLAC__bitwriter_init(FLAC__BitWriter *bw);
void FLAC__bitwriter_free(FLAC__BitWriter *bw); /* does not 'free(buffer)' */
void FLAC__bitwriter_clear(FLAC__BitWriter *bw);
void FLAC__bitwriter_dump(const FLAC__BitWriter *bw, FILE *out);

/*
 * CRC functions
 *
 * non-const *bw because they have to cal FLAC__bitwriter_get_buffer()
 */
FLAC__bool FLAC__bitwriter_get_write_crc16(FLAC__BitWriter *bw, FLAC__uint16 *crc);
FLAC__bool FLAC__bitwriter_get_write_crc8(FLAC__BitWriter *bw, FLAC__byte *crc);

/*
 * info functions
 */
FLAC__bool FLAC__bitwriter_is_byte_aligned(const FLAC__BitWriter *bw);
unsigned FLAC__bitwriter_get_input_bits_unconsumed(const FLAC__BitWriter *bw); /* can be called anytime, returns total # of bits unconsumed */

/*
 * direct buffer access
 *
 * there may be no calls on the bitwriter between get and release.
 * the bitwriter continues to own the returned buffer.
 * before get, bitwriter MUST be byte aligned: check with FLAC__bitwriter_is_byte_aligned()
 */
FLAC__bool FLAC__bitwriter_get_buffer(FLAC__BitWriter *bw, const FLAC__byte **buffer, size_t *bytes);
void FLAC__bitwriter_release_buffer(FLAC__BitWriter *bw);

/*
 * write functions
 */
FLAC__bool FLAC__bitwriter_write_zeroes(FLAC__BitWriter *bw, unsigned bits);
FLAC__bool FLAC__bitwriter_write_raw_uint32(FLAC__BitWriter *bw, FLAC__uint32 val, unsigned bits);
FLAC__bool FLAC__bitwriter_write_raw_int32(FLAC__BitWriter *bw, FLAC__int32 val, unsigned bits);
FLAC__bool FLAC__bitwriter_write_raw_uint64(FLAC__BitWriter *bw, FLAC__uint64 val, unsigned bits);
FLAC__bool FLAC__bitwriter_write_raw_uint32_little_endian(FLAC__BitWriter *bw, FLAC__uint32 val); /*only for bits=32*/
FLAC__bool FLAC__bitwriter_write_byte_block(FLAC__BitWriter *bw, const FLAC__byte vals[], unsigned nvals);
FLAC__bool FLAC__bitwriter_write_unary_unsigned(FLAC__BitWriter *bw, unsigned val);
unsigned FLAC__bitwriter_rice_bits(FLAC__int32 val, unsigned parameter);
#if 0 /* UNUSED */
unsigned FLAC__bitwriter_golomb_bits_signed(int val, unsigned parameter);
unsigned FLAC__bitwriter_golomb_bits_unsigned(unsigned val, unsigned parameter);
#endif
FLAC__bool FLAC__bitwriter_write_rice_signed(FLAC__BitWriter *bw, FLAC__int32 val, unsigned parameter);
FLAC__bool FLAC__bitwriter_write_rice_signed_block(FLAC__BitWriter *bw, const FLAC__int32 *vals, unsigned nvals, unsigned parameter);
#if 0 /* UNUSED */
FLAC__bool FLAC__bitwriter_write_golomb_signed(FLAC__BitWriter *bw, int val, unsigned parameter);
FLAC__bool FLAC__bitwriter_write_golomb_unsigned(FLAC__BitWriter *bw, unsigned val, unsigned parameter);
#endif
FLAC__bool FLAC__bitwriter_write_utf8_uint32(FLAC__BitWriter *bw, FLAC__uint32 val);
FLAC__bool FLAC__bitwriter_write_utf8_uint64(FLAC__BitWriter *bw, FLAC__uint64 val);
FLAC__bool FLAC__bitwriter_zero_pad_to_byte_boundary(FLAC__BitWriter *bw);

#endif
#endif //++-- bitwriter.h 

#if 1 //++ format.h 

#ifndef FLAC__PRIVATE__FORMAT_H
#define FLAC__PRIVATE__FORMAT_H

unsigned FLAC__format_get_max_rice_partition_order(unsigned blocksize, unsigned predictor_order);
unsigned FLAC__format_get_max_rice_partition_order_from_blocksize(unsigned blocksize);
unsigned FLAC__format_get_max_rice_partition_order_from_blocksize_limited_max_and_predictor_order(unsigned limit, unsigned blocksize, unsigned predictor_order);
void FLAC__format_entropy_coding_method_partitioned_rice_contents_init(FLAC__EntropyCodingMethod_PartitionedRiceContents *object);
void FLAC__format_entropy_coding_method_partitioned_rice_contents_clear(FLAC__EntropyCodingMethod_PartitionedRiceContents *object);
FLAC__bool FLAC__format_entropy_coding_method_partitioned_rice_contents_ensure_size(FLAC__EntropyCodingMethod_PartitionedRiceContents *object, unsigned max_partition_order);

#endif
#endif //++-- format.h 

#if 1 //++ fixed.h 

#ifndef FLAC__PRIVATE__FIXED_H
#define FLAC__PRIVATE__FIXED_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 *	FLAC__fixed_compute_best_predictor()
 *	--------------------------------------------------------------------
 *	Compute the best fixed predictor and the expected bits-per-sample
 *  of the residual signal for each order.  The _wide() version uses
 *  64-bit integers which is statistically necessary when bits-per-
 *  sample + log2(blocksize) > 30
 *
 *	IN data[0,data_len-1]
 *	IN data_len
 *	OUT residual_bits_per_sample[0,FLAC__MAX_FIXED_ORDER]
 */
#ifndef FLAC__INTEGER_ONLY_LIBRARY
unsigned FLAC__fixed_compute_best_predictor(const FLAC__int32 data[], unsigned data_len, FLAC__float residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1]);
# ifndef FLAC__NO_ASM
#  ifdef FLAC__CPU_IA32
#   ifdef FLAC__HAS_NASM
unsigned FLAC__fixed_compute_best_predictor_asm_ia32_mmx_cmov(const FLAC__int32 data[], unsigned data_len, FLAC__float residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1]);
#   endif
#  endif
# endif
unsigned FLAC__fixed_compute_best_predictor_wide(const FLAC__int32 data[], unsigned data_len, FLAC__float residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1]);
#else
unsigned FLAC__fixed_compute_best_predictor(const FLAC__int32 data[], unsigned data_len, FLAC__fixedpoint residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1]);
unsigned FLAC__fixed_compute_best_predictor_wide(const FLAC__int32 data[], unsigned data_len, FLAC__fixedpoint residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1]);
#endif

/*
 *	FLAC__fixed_compute_residual()
 *	--------------------------------------------------------------------
 *	Compute the residual signal obtained from sutracting the predicted
 *	signal from the original.
 *
 *	IN data[-order,data_len-1]        original signal (NOTE THE INDICES!)
 *	IN data_len                       length of original signal
 *	IN order <= FLAC__MAX_FIXED_ORDER fixed-predictor order
 *	OUT residual[0,data_len-1]        residual signal
 */
void FLAC__fixed_compute_residual(const FLAC__int32 data[], unsigned data_len, unsigned order, FLAC__int32 residual[]);

/*
 *	FLAC__fixed_restore_signal()
 *	--------------------------------------------------------------------
 *	Restore the original signal by summing the residual and the
 *	predictor.
 *
 *	IN residual[0,data_len-1]         residual signal
 *	IN data_len                       length of original signal
 *	IN order <= FLAC__MAX_FIXED_ORDER fixed-predictor order
 *	*** IMPORTANT: the caller must pass in the historical samples:
 *	IN  data[-order,-1]               previously-reconstructed historical samples
 *	OUT data[0,data_len-1]            original signal
 */
void FLAC__fixed_restore_signal(const FLAC__int32 residual[], unsigned data_len, unsigned order, FLAC__int32 data[]);

#endif
#endif //++-- fixed.h 

#if 1 //++ memory.h 


#ifndef FLAC__PRIVATE__MEMORY_H
#define FLAC__PRIVATE__MEMORY_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h> /* for size_t */

/* Returns the unaligned address returned by malloc.
 * Use free() on this address to deallocate.
 */
void *FLAC__memory_alloc_aligned(size_t bytes, void **aligned_address);
FLAC__bool FLAC__memory_alloc_aligned_int32_array(unsigned elements, FLAC__int32 **unaligned_pointer, FLAC__int32 **aligned_pointer);
FLAC__bool FLAC__memory_alloc_aligned_uint32_array(unsigned elements, FLAC__uint32 **unaligned_pointer, FLAC__uint32 **aligned_pointer);
FLAC__bool FLAC__memory_alloc_aligned_uint64_array(unsigned elements, FLAC__uint64 **unaligned_pointer, FLAC__uint64 **aligned_pointer);
FLAC__bool FLAC__memory_alloc_aligned_unsigned_array(unsigned elements, unsigned **unaligned_pointer, unsigned **aligned_pointer);
#ifndef FLAC__INTEGER_ONLY_LIBRARY
FLAC__bool FLAC__memory_alloc_aligned_real_array(unsigned elements, FLAC__real **unaligned_pointer, FLAC__real **aligned_pointer);
#endif

#endif
#endif //++-- memory.h  

#if 1 //++ window.h 

#ifndef FLAC__PRIVATE__WINDOW_H
#define FLAC__PRIVATE__WINDOW_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef FLAC__INTEGER_ONLY_LIBRARY

/*
 *	FLAC__window_*()
 *	--------------------------------------------------------------------
 *	Calculates window coefficients according to different apodization
 *	functions.
 *
 *	OUT window[0,L-1]
 *	IN L (number of points in window)
 */
void FLAC__window_bartlett(FLAC__real *window, const FLAC__int32 L);
void FLAC__window_bartlett_hann(FLAC__real *window, const FLAC__int32 L);
void FLAC__window_blackman(FLAC__real *window, const FLAC__int32 L);
void FLAC__window_blackman_harris_4term_92db_sidelobe(FLAC__real *window, const FLAC__int32 L);
void FLAC__window_connes(FLAC__real *window, const FLAC__int32 L);
void FLAC__window_flattop(FLAC__real *window, const FLAC__int32 L);
void FLAC__window_gauss(FLAC__real *window, const FLAC__int32 L, const FLAC__real stddev); /* 0.0 < stddev <= 0.5 */
void FLAC__window_hamming(FLAC__real *window, const FLAC__int32 L);
void FLAC__window_hann(FLAC__real *window, const FLAC__int32 L);
void FLAC__window_kaiser_bessel(FLAC__real *window, const FLAC__int32 L);
void FLAC__window_nuttall(FLAC__real *window, const FLAC__int32 L);
void FLAC__window_rectangle(FLAC__real *window, const FLAC__int32 L);
void FLAC__window_triangle(FLAC__real *window, const FLAC__int32 L);
void FLAC__window_tukey(FLAC__real *window, const FLAC__int32 L, const FLAC__real p);
void FLAC__window_welch(FLAC__real *window, const FLAC__int32 L);

#endif /* !defined FLAC__INTEGER_ONLY_LIBRARY */

#endif
#endif //++-- window.h 

#if 1 //++ md5.h 
#ifndef FLAC__PRIVATE__MD5_H
#define FLAC__PRIVATE__MD5_H

/*
 * This is the header file for the MD5 message-digest algorithm.
 * The algorithm is due to Ron Rivest.  This code was
 * written by Colin Plumb in 1993, no copyright is claimed.
 * This code is in the public domain; do with it what you wish.
 *
 * Equivalent code is available from RSA Data Security, Inc.
 * This code has been tested against that, and is equivalent,
 * except that you don't need to include two pages of legalese
 * with every copy.
 *
 * To compute the message digest of a chunk of bytes, declare an
 * MD5Context structure, pass it to MD5Init, call MD5Update as
 * needed on buffers full of bytes, and then call MD5Final, which
 * will fill a supplied 16-byte array with the digest.
 *
 * Changed so as no longer to depend on Colin Plumb's `usual.h'
 * header definitions; now uses stuff from dpkg's config.h
 *  - Ian Jackson <ijackson@nyx.cs.du.edu>.
 * Still in the public domain.
 *
 * Josh Coalson: made some changes to integrate with libFLAC.
 * Still in the public domain, with no warranty.
 */

typedef struct {
	FLAC__uint32 in[16];
	FLAC__uint32 buf[4];
	FLAC__uint32 bytes[2];
	FLAC__byte *internal_buf;
	size_t capacity;
} FLAC__MD5Context;

void FLAC__MD5Init(FLAC__MD5Context *context);
void FLAC__MD5Final(FLAC__byte digest[16], FLAC__MD5Context *context);

FLAC__bool FLAC__MD5Accumulate(FLAC__MD5Context *ctx, const FLAC__int32 * const signal[], unsigned channels, unsigned samples, unsigned bytes_per_sample);

#endif
#endif //++-- md5.h 

#if 1 //++ lpc.h 

#ifndef FLAC__PRIVATE__LPC_H
#define FLAC__PRIVATE__LPC_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef FLAC__INTEGER_ONLY_LIBRARY

/*
 *	FLAC__lpc_window_data()
 *	--------------------------------------------------------------------
 *	Applies the given window to the data.
 *  OPT: asm implementation
 *
 *	IN in[0,data_len-1]
 *	IN window[0,data_len-1]
 *	OUT out[0,lag-1]
 *	IN data_len
 */
void FLAC__lpc_window_data(const FLAC__int32 in[], const FLAC__real window[], FLAC__real out[], unsigned data_len);

/*
 *	FLAC__lpc_compute_autocorrelation()
 *	--------------------------------------------------------------------
 *	Compute the autocorrelation for lags between 0 and lag-1.
 *	Assumes data[] outside of [0,data_len-1] == 0.
 *	Asserts that lag > 0.
 *
 *	IN data[0,data_len-1]
 *	IN data_len
 *	IN 0 < lag <= data_len
 *	OUT autoc[0,lag-1]
 */
void FLAC__lpc_compute_autocorrelation(const FLAC__real data[], unsigned data_len, unsigned lag, FLAC__real autoc[]);
#ifndef FLAC__NO_ASM
#  ifdef FLAC__CPU_IA32
#    ifdef FLAC__HAS_NASM
void FLAC__lpc_compute_autocorrelation_asm_ia32(const FLAC__real data[], unsigned data_len, unsigned lag, FLAC__real autoc[]);
void FLAC__lpc_compute_autocorrelation_asm_ia32_sse_lag_4(const FLAC__real data[], unsigned data_len, unsigned lag, FLAC__real autoc[]);
void FLAC__lpc_compute_autocorrelation_asm_ia32_sse_lag_8(const FLAC__real data[], unsigned data_len, unsigned lag, FLAC__real autoc[]);
void FLAC__lpc_compute_autocorrelation_asm_ia32_sse_lag_12(const FLAC__real data[], unsigned data_len, unsigned lag, FLAC__real autoc[]);
void FLAC__lpc_compute_autocorrelation_asm_ia32_3dnow(const FLAC__real data[], unsigned data_len, unsigned lag, FLAC__real autoc[]);
#    endif
#  endif
#endif

/*
 *	FLAC__lpc_compute_lp_coefficients()
 *	--------------------------------------------------------------------
 *	Computes LP coefficients for orders 1..max_order.
 *	Do not call if autoc[0] == 0.0.  This means the signal is zero
 *	and there is no point in calculating a predictor.
 *
 *	IN autoc[0,max_order]                      autocorrelation values
 *	IN 0 < max_order <= FLAC__MAX_LPC_ORDER    max LP order to compute
 *	OUT lp_coeff[0,max_order-1][0,max_order-1] LP coefficients for each order
 *	*** IMPORTANT:
 *	*** lp_coeff[0,max_order-1][max_order,FLAC__MAX_LPC_ORDER-1] are untouched
 *	OUT error[0,max_order-1]                   error for each order (more
 *	                                           specifically, the variance of
 *	                                           the error signal times # of
 *	                                           samples in the signal)
 *
 *	Example: if max_order is 9, the LP coefficients for order 9 will be
 *	         in lp_coeff[8][0,8], the LP coefficients for order 8 will be
 *			 in lp_coeff[7][0,7], etc.
 */
void FLAC__lpc_compute_lp_coefficients(const FLAC__real autoc[], unsigned *max_order, FLAC__real lp_coeff[][FLAC__MAX_LPC_ORDER], FLAC__double error[]);

/*
 *	FLAC__lpc_quantize_coefficients()
 *	--------------------------------------------------------------------
 *	Quantizes the LP coefficients.  NOTE: precision + bits_per_sample
 *	must be less than 32 (sizeof(FLAC__int32)*8).
 *
 *	IN lp_coeff[0,order-1]    LP coefficients
 *	IN order                  LP order
 *	IN FLAC__MIN_QLP_COEFF_PRECISION < precision
 *	                          desired precision (in bits, including sign
 *	                          bit) of largest coefficient
 *	OUT qlp_coeff[0,order-1]  quantized coefficients
 *	OUT shift                 # of bits to shift right to get approximated
 *	                          LP coefficients.  NOTE: could be negative.
 *	RETURN 0 => quantization OK
 *	       1 => coefficients require too much shifting for *shift to
 *              fit in the LPC subframe header.  'shift' is unset.
 *         2 => coefficients are all zero, which is bad.  'shift' is
 *              unset.
 */
int FLAC__lpc_quantize_coefficients(const FLAC__real lp_coeff[], unsigned order, unsigned precision, FLAC__int32 qlp_coeff[], int *shift);

/*
 *	FLAC__lpc_compute_residual_from_qlp_coefficients()
 *	--------------------------------------------------------------------
 *	Compute the residual signal obtained from sutracting the predicted
 *	signal from the original.
 *
 *	IN data[-order,data_len-1] original signal (NOTE THE INDICES!)
 *	IN data_len                length of original signal
 *	IN qlp_coeff[0,order-1]    quantized LP coefficients
 *	IN order > 0               LP order
 *	IN lp_quantization         quantization of LP coefficients in bits
 *	OUT residual[0,data_len-1] residual signal
 */
void FLAC__lpc_compute_residual_from_qlp_coefficients(const FLAC__int32 *data, unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 residual[]);
void FLAC__lpc_compute_residual_from_qlp_coefficients_wide(const FLAC__int32 *data, unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 residual[]);
#ifndef FLAC__NO_ASM
#  ifdef FLAC__CPU_IA32
#    ifdef FLAC__HAS_NASM
void FLAC__lpc_compute_residual_from_qlp_coefficients_asm_ia32(const FLAC__int32 *data, unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 residual[]);
void FLAC__lpc_compute_residual_from_qlp_coefficients_asm_ia32_mmx(const FLAC__int32 *data, unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 residual[]);
#    endif
#  endif
#endif

#endif /* !defined FLAC__INTEGER_ONLY_LIBRARY */

/*
 *	FLAC__lpc_restore_signal()
 *	--------------------------------------------------------------------
 *	Restore the original signal by summing the residual and the
 *	predictor.
 *
 *	IN residual[0,data_len-1]  residual signal
 *	IN data_len                length of original signal
 *	IN qlp_coeff[0,order-1]    quantized LP coefficients
 *	IN order > 0               LP order
 *	IN lp_quantization         quantization of LP coefficients in bits
 *	*** IMPORTANT: the caller must pass in the historical samples:
 *	IN  data[-order,-1]        previously-reconstructed historical samples
 *	OUT data[0,data_len-1]     original signal
 */
void FLAC__lpc_restore_signal(const FLAC__int32 residual[], unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 data[]);
void FLAC__lpc_restore_signal_wide(const FLAC__int32 residual[], unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 data[]);
#ifndef FLAC__NO_ASM
#  ifdef FLAC__CPU_IA32
#    ifdef FLAC__HAS_NASM
void FLAC__lpc_restore_signal_asm_ia32(const FLAC__int32 residual[], unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 data[]);
void FLAC__lpc_restore_signal_asm_ia32_mmx(const FLAC__int32 residual[], unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 data[]);
#    endif /* FLAC__HAS_NASM */
#  elif defined FLAC__CPU_PPC
void FLAC__lpc_restore_signal_asm_ppc_altivec_16(const FLAC__int32 residual[], unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 data[]);
void FLAC__lpc_restore_signal_asm_ppc_altivec_16_order8(const FLAC__int32 residual[], unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 data[]);
#  endif/* FLAC__CPU_IA32 || FLAC__CPU_PPC */
#endif /* FLAC__NO_ASM */

#ifndef FLAC__INTEGER_ONLY_LIBRARY

/*
 *	FLAC__lpc_compute_expected_bits_per_residual_sample()
 *	--------------------------------------------------------------------
 *	Compute the expected number of bits per residual signal sample
 *	based on the LP error (which is related to the residual variance).
 *
 *	IN lpc_error >= 0.0   error returned from calculating LP coefficients
 *	IN total_samples > 0  # of samples in residual signal
 *	RETURN                expected bits per sample
 */
FLAC__double FLAC__lpc_compute_expected_bits_per_residual_sample(FLAC__double lpc_error, unsigned total_samples);
FLAC__double FLAC__lpc_compute_expected_bits_per_residual_sample_with_error_scale(FLAC__double lpc_error, FLAC__double error_scale);

/*
 *	FLAC__lpc_compute_best_order()
 *	--------------------------------------------------------------------
 *	Compute the best order from the array of signal errors returned
 *	during coefficient computation.
 *
 *	IN lpc_error[0,max_order-1] >= 0.0  error returned from calculating LP coefficients
 *	IN max_order > 0                    max LP order
 *	IN total_samples > 0                # of samples in residual signal
 *	IN overhead_bits_per_order          # of bits overhead for each increased LP order
 *	                                    (includes warmup sample size and quantized LP coefficient)
 *	RETURN [1,max_order]                best order
 */
unsigned FLAC__lpc_compute_best_order(const FLAC__double lpc_error[], unsigned max_order, unsigned total_samples, unsigned overhead_bits_per_order);

#endif /* !defined FLAC__INTEGER_ONLY_LIBRARY */

#endif

#endif //++-- lpc.h 


#if 1 //++ metadata.h 

#ifndef FLAC__PRIVATE__METADATA_H
#define FLAC__PRIVATE__METADATA_H


/* WATCHOUT: all malloc()ed data in the block is free()ed; this may not
 * be a consistent state (e.g. PICTURE) or equivalent to the initial
 * state after FLAC__metadata_object_new()
 */
void FLAC__metadata_object_delete_data(FLAC__StreamMetadata *object);

void FLAC__metadata_object_cuesheet_track_delete_data(FLAC__StreamMetadata_CueSheet_Track *object);


/** State values for a FLAC__StreamDecoder
 *
 * The decoder's state can be obtained by calling FLAC__stream_decoder_get_state().
 */
typedef enum {

	FLAC__STREAM_DECODER_SEARCH_FOR_METADATA = 0,
	/**< The decoder is ready to search for metadata. */

	FLAC__STREAM_DECODER_READ_METADATA,
	/**< The decoder is ready to or is in the process of reading metadata. */

	FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC,
	/**< The decoder is ready to or is in the process of searching for the
	 * frame sync code.
	 */

	FLAC__STREAM_DECODER_READ_FRAME,
	/**< The decoder is ready to or is in the process of reading a frame. */

	FLAC__STREAM_DECODER_END_OF_STREAM,
	/**< The decoder has reached the end of the stream. */

	FLAC__STREAM_DECODER_OGG_ERROR,
	/**< An error occurred in the underlying Ogg layer.  */

	FLAC__STREAM_DECODER_SEEK_ERROR,
	/**< An error occurred while seeking.  The decoder must be flushed
	 * with FLAC__stream_decoder_flush() or reset with
	 * FLAC__stream_decoder_reset() before decoding can continue.
	 */

	FLAC__STREAM_DECODER_ABORTED,
	/**< The decoder was aborted by the read callback. */

	FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR,
	/**< An error occurred allocating memory.  The decoder is in an invalid
	 * state and can no longer be used.
	 */

	FLAC__STREAM_DECODER_UNINITIALIZED
	/**< The decoder is in the uninitialized state; one of the
	 * FLAC__stream_decoder_init_*() functions must be called before samples
	 * can be processed.
	 */

} FLAC__StreamDecoderState;

#endif
#endif //++-- metadata.h 


