/********************************************************************
 *                                                                  *
 * THIS FILE IS PART OF THE OggVorbis SOFTWARE CODEC SOURCE CODE.   *
 * USE, DISTRIBUTION AND REPRODUCTION OF THIS LIBRARY SOURCE IS     *
 * GOVERNED BY A BSD-STYLE SOURCE LICENSE INCLUDED WITH THIS SOURCE *
 * IN 'COPYING'. PLEASE READ THESE TERMS BEFORE DISTRIBUTING.       *
 *                                                                  *
 * THE OggVorbis SOURCE CODE IS (C) COPYRIGHT 1994-2007             *
 * by the Xiph.Org Foundation http://www.xiph.org/                  *
 *                                                                  *
 ********************************************************************

 function: toplevel libogg include
 last mod: $Id: ogg.h 18044 2011-08-01 17:55:20Z gmaxwell $

 ********************************************************************/
#ifndef _OGG_H
#define _OGG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
//++ os_types.h#include <ogg/os_types.h>
#ifndef _OS_TYPES_H
#define _OS_TYPES_H

/* make it easy on the folks that want to compile the libs with a
   different malloc than stdlib */
#define _ogg_malloc  malloc
#define _ogg_calloc  calloc
#define _ogg_realloc realloc
#define _ogg_free    free

#if defined(_WIN32) 

#  if defined(__CYGWIN__)
#    include <stdint.h>
     typedef int16_t ogg_int16_t;
     typedef uint16_t ogg_uint16_t;
     typedef int32_t ogg_int32_t;
     typedef uint32_t ogg_uint32_t;
     typedef int64_t ogg_int64_t;
     typedef uint64_t ogg_uint64_t;
#  elif defined(__MINGW32__)
#    include <sys/types.h>
     typedef short ogg_int16_t;
     typedef unsigned short ogg_uint16_t;
     typedef int ogg_int32_t;
     typedef unsigned int ogg_uint32_t;
     typedef long long ogg_int64_t;
     typedef unsigned long long ogg_uint64_t;
#  elif defined(__MWERKS__)
     typedef long long ogg_int64_t;
     typedef int ogg_int32_t;
     typedef unsigned int ogg_uint32_t;
     typedef short ogg_int16_t;
     typedef unsigned short ogg_uint16_t;
#  else
     /* MSVC/Borland */
     typedef __int64 ogg_int64_t;
     typedef __int32 ogg_int32_t;
     typedef unsigned __int32 ogg_uint32_t;
     typedef __int16 ogg_int16_t;
     typedef unsigned __int16 ogg_uint16_t;
#  endif

#elif defined(__MACOS__)

#  include <sys/types.h>
   typedef SInt16 ogg_int16_t;
   typedef UInt16 ogg_uint16_t;
   typedef SInt32 ogg_int32_t;
   typedef UInt32 ogg_uint32_t;
   typedef SInt64 ogg_int64_t;

#elif (defined(__APPLE__) && defined(__MACH__)) /* MacOS X Framework build */

#  include <inttypes.h>
   typedef int16_t ogg_int16_t;
   typedef uint16_t ogg_uint16_t;
   typedef int32_t ogg_int32_t;
   typedef uint32_t ogg_uint32_t;
   typedef int64_t ogg_int64_t;

#elif defined(__HAIKU__)

  /* Haiku */
#  include <sys/types.h>
   typedef short ogg_int16_t;
   typedef unsigned short ogg_uint16_t;
   typedef int ogg_int32_t;
   typedef unsigned int ogg_uint32_t;
   typedef long long ogg_int64_t;

#elif defined(__BEOS__)

   /* Be */
#  include <inttypes.h>
   typedef int16_t ogg_int16_t;
   typedef uint16_t ogg_uint16_t;
   typedef int32_t ogg_int32_t;
   typedef uint32_t ogg_uint32_t;
   typedef int64_t ogg_int64_t;

#elif defined (__EMX__)

   /* OS/2 GCC */
   typedef short ogg_int16_t;
   typedef unsigned short ogg_uint16_t;
   typedef int ogg_int32_t;
   typedef unsigned int ogg_uint32_t;
   typedef long long ogg_int64_t;

#elif defined (DJGPP)

   /* DJGPP */
   typedef short ogg_int16_t;
   typedef int ogg_int32_t;
   typedef unsigned int ogg_uint32_t;
   typedef long long ogg_int64_t;

#elif defined(R5900)

   /* PS2 EE */
   typedef long ogg_int64_t;
   typedef int ogg_int32_t;
   typedef unsigned ogg_uint32_t;
   typedef short ogg_int16_t;

#elif defined(__SYMBIAN32__)

   /* Symbian GCC */
   typedef signed short ogg_int16_t;
   typedef unsigned short ogg_uint16_t;
   typedef signed int ogg_int32_t;
   typedef unsigned int ogg_uint32_t;
   typedef long long int ogg_int64_t;

#elif defined(__TMS320C6X__)

   /* TI C64x compiler */
   typedef signed short ogg_int16_t;
   typedef unsigned short ogg_uint16_t;
   typedef signed int ogg_int32_t;
   typedef unsigned int ogg_uint32_t;
   typedef long long int ogg_int64_t;

#else

//++ config_types.h #include <ogg/config_types.h>
#ifndef __CONFIG_TYPES_H__
#define __CONFIG_TYPES_H__

/* these are filled in by configure */
#define INCLUDE_INTTYPES_H 1
#define INCLUDE_STDINT_H 1
#define INCLUDE_SYS_TYPES_H 1

#if INCLUDE_INTTYPES_H
#  include <inttypes.h>
#endif
#if INCLUDE_STDINT_H
#  include <stdint.h>
#endif
#if INCLUDE_SYS_TYPES_H
#  include <sys/types.h>
#endif

typedef short ogg_int16_t;
typedef unsigned short ogg_uint16_t;
typedef int ogg_int32_t;
typedef unsigned int ogg_uint32_t;
typedef long long ogg_int64_t;

#endif

//++-- config_types.h

#endif

#endif  /* _OS_TYPES_H */

//++-- os_types.h

typedef struct {
  void *iov_base;
  size_t iov_len;
} ogg_iovec_t;

typedef struct {
  long endbyte;
  int  endbit;

  unsigned char *buffer;
  unsigned char *ptr;
  long storage;
} oggpack_buffer;

/* ogg_page is used to encapsulate the data in one Ogg bitstream page *****/

typedef struct {
  unsigned char *header;
  long header_len;
  unsigned char *body;
  long body_len;
} ogg_page;

/* ogg_stream_state contains the current encode/decode state of a logical
   Ogg bitstream **********************************************************/

typedef struct {
  unsigned char   *body_data;    /* bytes from packet bodies */
  long    body_storage;          /* storage elements allocated */
  long    body_fill;             /* elements stored; fill mark */
  long    body_returned;         /* elements of fill returned */


  int     *lacing_vals;      /* The values that will go to the segment table */
  ogg_int64_t *granule_vals; /* granulepos values for headers. Not compact
                                this way, but it is simple coupled to the
                                lacing fifo */
  long    lacing_storage;
  long    lacing_fill;
  long    lacing_packet;
  long    lacing_returned;

  unsigned char    header[282];      /* working space for header encode */
  int              header_fill;

  int     e_o_s;          /* set when we have buffered the last packet in the
                             logical bitstream */
  int     b_o_s;          /* set after we've written the initial page
                             of a logical bitstream */
  long    serialno;
  long    pageno;
  ogg_int64_t  packetno;  /* sequence number for decode; the framing
                             knows where there's a hole in the data,
                             but we need coupling so that the codec
                             (which is in a separate abstraction
                             layer) also knows about the gap */
  ogg_int64_t   granulepos;

} ogg_stream_state;

/* ogg_packet is used to encapsulate the data and metadata belonging
   to a single raw Ogg/Vorbis packet *************************************/

typedef struct {
  unsigned char *packet;
  long  bytes;
  long  b_o_s;
  long  e_o_s;

  ogg_int64_t  granulepos;

  ogg_int64_t  packetno;     /* sequence number for decode; the framing
                                knows where there's a hole in the data,
                                but we need coupling so that the codec
                                (which is in a separate abstraction
                                layer) also knows about the gap */
} ogg_packet;

typedef struct {
  unsigned char *data;
  int storage;
  int fill;
  int returned;

  int unsynced;
  int headerbytes;
  int bodybytes;
} ogg_sync_state;

/* Ogg BITSTREAM PRIMITIVES: bitstream ************************/

extern void  oggpack_writeinit(oggpack_buffer *b);
extern int   oggpack_writecheck(oggpack_buffer *b);
extern void  oggpack_writetrunc(oggpack_buffer *b,long bits);
extern void  oggpack_writealign(oggpack_buffer *b);
extern void  oggpack_writecopy(oggpack_buffer *b,void *source,long bits);
extern void  oggpack_reset(oggpack_buffer *b);
extern void  oggpack_writeclear(oggpack_buffer *b);
extern void  oggpack_readinit(oggpack_buffer *b,unsigned char *buf,int bytes);
extern void  oggpack_write(oggpack_buffer *b,unsigned long value,int bits);
extern long  oggpack_look(oggpack_buffer *b,int bits);
extern long  oggpack_look1(oggpack_buffer *b);
extern void  oggpack_adv(oggpack_buffer *b,int bits);
extern void  oggpack_adv1(oggpack_buffer *b);
extern long  oggpack_read(oggpack_buffer *b,int bits);
extern long  oggpack_read1(oggpack_buffer *b);
extern long  oggpack_bytes(oggpack_buffer *b);
extern long  oggpack_bits(oggpack_buffer *b);
extern unsigned char *oggpack_get_buffer(oggpack_buffer *b);

extern void  oggpackB_writeinit(oggpack_buffer *b);
extern int   oggpackB_writecheck(oggpack_buffer *b);
extern void  oggpackB_writetrunc(oggpack_buffer *b,long bits);
extern void  oggpackB_writealign(oggpack_buffer *b);
extern void  oggpackB_writecopy(oggpack_buffer *b,void *source,long bits);
extern void  oggpackB_reset(oggpack_buffer *b);
extern void  oggpackB_writeclear(oggpack_buffer *b);
extern void  oggpackB_readinit(oggpack_buffer *b,unsigned char *buf,int bytes);
extern void  oggpackB_write(oggpack_buffer *b,unsigned long value,int bits);
extern long  oggpackB_look(oggpack_buffer *b,int bits);
extern long  oggpackB_look1(oggpack_buffer *b);
extern void  oggpackB_adv(oggpack_buffer *b,int bits);
extern void  oggpackB_adv1(oggpack_buffer *b);
extern long  oggpackB_read(oggpack_buffer *b,int bits);
extern long  oggpackB_read1(oggpack_buffer *b);
extern long  oggpackB_bytes(oggpack_buffer *b);
extern long  oggpackB_bits(oggpack_buffer *b);
extern unsigned char *oggpackB_get_buffer(oggpack_buffer *b);

/* Ogg BITSTREAM PRIMITIVES: encoding **************************/

extern int      ogg_stream_packetin(ogg_stream_state *os, ogg_packet *op);
extern int      ogg_stream_iovecin(ogg_stream_state *os, ogg_iovec_t *iov,
                                   int count, long e_o_s, ogg_int64_t granulepos);
extern int      ogg_stream_pageout(ogg_stream_state *os, ogg_page *og);
extern int      ogg_stream_pageout_fill(ogg_stream_state *os, ogg_page *og, int nfill);
extern int      ogg_stream_flush(ogg_stream_state *os, ogg_page *og);
extern int      ogg_stream_flush_fill(ogg_stream_state *os, ogg_page *og, int nfill);

/* Ogg BITSTREAM PRIMITIVES: decoding **************************/

extern int      ogg_sync_init(ogg_sync_state *oy);
extern int      ogg_sync_clear(ogg_sync_state *oy);
extern int      ogg_sync_reset(ogg_sync_state *oy);
extern int      ogg_sync_destroy(ogg_sync_state *oy);
extern int      ogg_sync_check(ogg_sync_state *oy);

extern char    *ogg_sync_buffer(ogg_sync_state *oy, long size);
extern int      ogg_sync_wrote(ogg_sync_state *oy, long bytes);
extern long     ogg_sync_pageseek(ogg_sync_state *oy,ogg_page *og);
extern int      ogg_sync_pageout(ogg_sync_state *oy, ogg_page *og);
extern int      ogg_stream_pagein(ogg_stream_state *os, ogg_page *og);
extern int      ogg_stream_packetout(ogg_stream_state *os,ogg_packet *op);
extern int      ogg_stream_packetpeek(ogg_stream_state *os,ogg_packet *op);

/* Ogg BITSTREAM PRIMITIVES: general ***************************/

extern int      ogg_stream_init(ogg_stream_state *os,int serialno);
extern int      ogg_stream_clear(ogg_stream_state *os);
extern int      ogg_stream_reset(ogg_stream_state *os);
extern int      ogg_stream_reset_serialno(ogg_stream_state *os,int serialno);
extern int      ogg_stream_destroy(ogg_stream_state *os);
extern int      ogg_stream_check(ogg_stream_state *os);
extern int      ogg_stream_eos(ogg_stream_state *os);

extern void     ogg_page_checksum_set(ogg_page *og);

extern int      ogg_page_version(const ogg_page *og);
extern int      ogg_page_continued(const ogg_page *og);
extern int      ogg_page_bos(const ogg_page *og);
extern int      ogg_page_eos(const ogg_page *og);
extern ogg_int64_t  ogg_page_granulepos(const ogg_page *og);
extern int      ogg_page_serialno(const ogg_page *og);
extern long     ogg_page_pageno(const ogg_page *og);
extern int      ogg_page_packets(const ogg_page *og);

extern void     ogg_packet_clear(ogg_packet *op);


#ifdef __cplusplus
}
#endif

#endif  /* _OGG_H */
