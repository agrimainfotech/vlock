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

#include <stdio.h> /* for FILE */
#include "flac.h"

#if 1 //++ stream_decoder.h 
#ifndef FLAC__STREAM_DECODER_H
#define FLAC__STREAM_DECODER_H

#ifdef __cplusplus
extern "C" {
#endif


/** \file include/FLAC/stream_decoder.h
 *
 *  \brief
 *  This module contains the functions which implement the stream
 *  decoder.
 *
 *  See the detailed documentation in the
 *  \link flac_stream_decoder stream decoder \endlink module.
 */

/** \defgroup flac_decoder FLAC/ \*_decoder.h: decoder interfaces
 *  \ingroup flac
 *
 *  \brief
 *  This module describes the decoder layers provided by libFLAC.
 *
 * The stream decoder can be used to decode complete streams either from
 * the client via callbacks, or directly from a file, depending on how
 * it is initialized.  When decoding via callbacks, the client provides
 * callbacks for reading FLAC data and writing decoded samples, and
 * handling metadata and errors.  If the client also supplies seek-related
 * callback, the decoder function for sample-accurate seeking within the
 * FLAC input is also available.  When decoding from a file, the client
 * needs only supply a filename or open \c FILE* and write/metadata/error
 * callbacks; the rest of the callbacks are supplied internally.  For more
 * info see the \link flac_stream_decoder stream decoder \endlink module.
 */

/** \defgroup flac_stream_decoder FLAC/stream_decoder.h: stream decoder interface
 *  \ingroup flac_decoder
 *
 *  \brief
 *  This module contains the functions which implement the stream
 *  decoder.
 *
 * The stream decoder can decode native FLAC, and optionally Ogg FLAC
 * (check FLAC_API_SUPPORTS_OGG_FLAC) streams and files.
 *
 * The basic usage of this decoder is as follows:
 * - The program creates an instance of a decoder using
 *   FLAC__stream_decoder_new().
 * - The program overrides the default settings using
 *   FLAC__stream_decoder_set_*() functions.
 * - The program initializes the instance to validate the settings and
 *   prepare for decoding using
 *   - FLAC__stream_decoder_init_stream() or FLAC__stream_decoder_init_FILE()
 *     or FLAC__stream_decoder_init_file() for native FLAC,
 *   - FLAC__stream_decoder_init_ogg_stream() or FLAC__stream_decoder_init_ogg_FILE()
 *     or FLAC__stream_decoder_init_ogg_file() for Ogg FLAC
 * - The program calls the FLAC__stream_decoder_process_*() functions
 *   to decode data, which subsequently calls the callbacks.
 * - The program finishes the decoding with FLAC__stream_decoder_finish(),
 *   which flushes the input and output and resets the decoder to the
 *   uninitialized state.
 * - The instance may be used again or deleted with
 *   FLAC__stream_decoder_delete().
 *
 * In more detail, the program will create a new instance by calling
 * FLAC__stream_decoder_new(), then call FLAC__stream_decoder_set_*()
 * functions to override the default decoder options, and call
 * one of the FLAC__stream_decoder_init_*() functions.
 *
 * There are three initialization functions for native FLAC, one for
 * setting up the decoder to decode FLAC data from the client via
 * callbacks, and two for decoding directly from a FLAC file.
 *
 * For decoding via callbacks, use FLAC__stream_decoder_init_stream().
 * You must also supply several callbacks for handling I/O.  Some (like
 * seeking) are optional, depending on the capabilities of the input.
 *
 * For decoding directly from a file, use FLAC__stream_decoder_init_FILE()
 * or FLAC__stream_decoder_init_file().  Then you must only supply an open
 * \c FILE* or filename and fewer callbacks; the decoder will handle
 * the other callbacks internally.
 *
 * There are three similarly-named init functions for decoding from Ogg
 * FLAC streams.  Check \c FLAC_API_SUPPORTS_OGG_FLAC to find out if the
 * library has been built with Ogg support.
 *
 * Once the decoder is initialized, your program will call one of several
 * functions to start the decoding process:
 *
 * - FLAC__stream_decoder_process_single() - Tells the decoder to process at
 *   most one metadata block or audio frame and return, calling either the
 *   metadata callback or write callback, respectively, once.  If the decoder
 *   loses sync it will return with only the error callback being called.
 * - FLAC__stream_decoder_process_until_end_of_metadata() - Tells the decoder
 *   to process the stream from the current location and stop upon reaching
 *   the first audio frame.  The client will get one metadata, write, or error
 *   callback per metadata block, audio frame, or sync error, respectively.
 * - FLAC__stream_decoder_process_until_end_of_stream() - Tells the decoder
 *   to process the stream from the current location until the read callback
 *   returns FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM or
 *   FLAC__STREAM_DECODER_READ_STATUS_ABORT.  The client will get one metadata,
 *   write, or error callback per metadata block, audio frame, or sync error,
 *   respectively.
 *
 * When the decoder has finished decoding (normally or through an abort),
 * the instance is finished by calling FLAC__stream_decoder_finish(), which
 * ensures the decoder is in the correct state and frees memory.  Then the
 * instance may be deleted with FLAC__stream_decoder_delete() or initialized
 * again to decode another stream.
 *
 * Seeking is exposed through the FLAC__stream_decoder_seek_absolute() method.
 * At any point after the stream decoder has been initialized, the client can
 * call this function to seek to an exact sample within the stream.
 * Subsequently, the first time the write callback is called it will be
 * passed a (possibly partial) block starting at that sample.
 *
 * If the client cannot seek via the callback interface provided, but still
 * has another way of seeking, it can flush the decoder using
 * FLAC__stream_decoder_flush() and start feeding data from the new position
 * through the read callback.
 *
 * The stream decoder also provides MD5 signature checking.  If this is
 * turned on before initialization, FLAC__stream_decoder_finish() will
 * report when the decoded MD5 signature does not match the one stored
 * in the STREAMINFO block.  MD5 checking is automatically turned off
 * (until the next FLAC__stream_decoder_reset()) if there is no signature
 * in the STREAMINFO block or when a seek is attempted.
 *
 * The FLAC__stream_decoder_set_metadata_*() functions deserve special
 * attention.  By default, the decoder only calls the metadata_callback for
 * the STREAMINFO block.  These functions allow you to tell the decoder
 * explicitly which blocks to parse and return via the metadata_callback
 * and/or which to skip.  Use a FLAC__stream_decoder_set_metadata_respond_all(),
 * FLAC__stream_decoder_set_metadata_ignore() ... or FLAC__stream_decoder_set_metadata_ignore_all(),
 * FLAC__stream_decoder_set_metadata_respond() ... sequence to exactly specify
 * which blocks to return.  Remember that metadata blocks can potentially
 * be big (for example, cover art) so filtering out the ones you don't
 * use can reduce the memory requirements of the decoder.  Also note the
 * special forms FLAC__stream_decoder_set_metadata_respond_application(id)
 * and FLAC__stream_decoder_set_metadata_ignore_application(id) for
 * filtering APPLICATION blocks based on the application ID.
 *
 * STREAMINFO and SEEKTABLE blocks are always parsed and used internally, but
 * they still can legally be filtered from the metadata_callback.
 *
 * \note
 * The "set" functions may only be called when the decoder is in the
 * state FLAC__STREAM_DECODER_UNINITIALIZED, i.e. after
 * FLAC__stream_decoder_new() or FLAC__stream_decoder_finish(), but
 * before FLAC__stream_decoder_init_*().  If this is the case they will
 * return \c true, otherwise \c false.
 *
 * \note
 * FLAC__stream_decoder_finish() resets all settings to the constructor
 * defaults, including the callbacks.
 *
 * \{
 */
// 
// 
// /** State values for a FLAC__StreamDecoder
//  *
//  * The decoder's state can be obtained by calling FLAC__stream_decoder_get_state().
//  */
// typedef enum {
// 
// 	FLAC__STREAM_DECODER_SEARCH_FOR_METADATA = 0,
// 	/**< The decoder is ready to search for metadata. */
// 
// 	FLAC__STREAM_DECODER_READ_METADATA,
// 	/**< The decoder is ready to or is in the process of reading metadata. */
// 
// 	FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC,
// 	/**< The decoder is ready to or is in the process of searching for the
// 	 * frame sync code.
// 	 */
// 
// 	FLAC__STREAM_DECODER_READ_FRAME,
// 	/**< The decoder is ready to or is in the process of reading a frame. */
// 
// 	FLAC__STREAM_DECODER_END_OF_STREAM,
// 	/**< The decoder has reached the end of the stream. */
// 
// 	FLAC__STREAM_DECODER_OGG_ERROR,
// 	/**< An error occurred in the underlying Ogg layer.  */
// 
// 	FLAC__STREAM_DECODER_SEEK_ERROR,
// 	/**< An error occurred while seeking.  The decoder must be flushed
// 	 * with FLAC__stream_decoder_flush() or reset with
// 	 * FLAC__stream_decoder_reset() before decoding can continue.
// 	 */
// 
// 	FLAC__STREAM_DECODER_ABORTED,
// 	/**< The decoder was aborted by the read callback. */
// 
// 	FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR,
// 	/**< An error occurred allocating memory.  The decoder is in an invalid
// 	 * state and can no longer be used.
// 	 */
// 
// 	FLAC__STREAM_DECODER_UNINITIALIZED
// 	/**< The decoder is in the uninitialized state; one of the
// 	 * FLAC__stream_decoder_init_*() functions must be called before samples
// 	 * can be processed.
// 	 */
// 
// } FLAC__StreamDecoderState;

/** Maps a FLAC__StreamDecoderState to a C string.
 *
 *  Using a FLAC__StreamDecoderState as the index to this array
 *  will give the string equivalent.  The contents should not be modified.
 */
extern FLAC_API const char * const FLAC__StreamDecoderStateString[];


/** Possible return values for the FLAC__stream_decoder_init_*() functions.
 */
typedef enum {

	FLAC__STREAM_DECODER_INIT_STATUS_OK = 0,
	/**< Initialization was successful. */

	FLAC__STREAM_DECODER_INIT_STATUS_UNSUPPORTED_CONTAINER,
	/**< The library was not compiled with support for the given container
	 * format.
	 */

	FLAC__STREAM_DECODER_INIT_STATUS_INVALID_CALLBACKS,
	/**< A required callback was not supplied. */

	FLAC__STREAM_DECODER_INIT_STATUS_MEMORY_ALLOCATION_ERROR,
	/**< An error occurred allocating memory. */

	FLAC__STREAM_DECODER_INIT_STATUS_ERROR_OPENING_FILE,
	/**< fopen() failed in FLAC__stream_decoder_init_file() or
	 * FLAC__stream_decoder_init_ogg_file(). */

	FLAC__STREAM_DECODER_INIT_STATUS_ALREADY_INITIALIZED
	/**< FLAC__stream_decoder_init_*() was called when the decoder was
	 * already initialized, usually because
	 * FLAC__stream_decoder_finish() was not called.
	 */

} FLAC__StreamDecoderInitStatus;

/** Maps a FLAC__StreamDecoderInitStatus to a C string.
 *
 *  Using a FLAC__StreamDecoderInitStatus as the index to this array
 *  will give the string equivalent.  The contents should not be modified.
 */
extern FLAC_API const char * const FLAC__StreamDecoderInitStatusString[];


/** Return values for the FLAC__StreamDecoder read callback.
 */
typedef enum {

	FLAC__STREAM_DECODER_READ_STATUS_CONTINUE,
	/**< The read was OK and decoding can continue. */

	FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM,
	/**< The read was attempted while at the end of the stream.  Note that
	 * the client must only return this value when the read callback was
	 * called when already at the end of the stream.  Otherwise, if the read
	 * itself moves to the end of the stream, the client should still return
	 * the data and \c FLAC__STREAM_DECODER_READ_STATUS_CONTINUE, and then on
	 * the next read callback it should return
	 * \c FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM with a byte count
	 * of \c 0.
	 */

	FLAC__STREAM_DECODER_READ_STATUS_ABORT
	/**< An unrecoverable error occurred.  The decoder will return from the process call. */

} FLAC__StreamDecoderReadStatus;

/** Maps a FLAC__StreamDecoderReadStatus to a C string.
 *
 *  Using a FLAC__StreamDecoderReadStatus as the index to this array
 *  will give the string equivalent.  The contents should not be modified.
 */
extern FLAC_API const char * const FLAC__StreamDecoderReadStatusString[];


/** Return values for the FLAC__StreamDecoder seek callback.
 */
typedef enum {

	FLAC__STREAM_DECODER_SEEK_STATUS_OK,
	/**< The seek was OK and decoding can continue. */

	FLAC__STREAM_DECODER_SEEK_STATUS_ERROR,
	/**< An unrecoverable error occurred.  The decoder will return from the process call. */

	FLAC__STREAM_DECODER_SEEK_STATUS_UNSUPPORTED
	/**< Client does not support seeking. */

} FLAC__StreamDecoderSeekStatus;

/** Maps a FLAC__StreamDecoderSeekStatus to a C string.
 *
 *  Using a FLAC__StreamDecoderSeekStatus as the index to this array
 *  will give the string equivalent.  The contents should not be modified.
 */
extern FLAC_API const char * const FLAC__StreamDecoderSeekStatusString[];


/** Return values for the FLAC__StreamDecoder tell callback.
 */
typedef enum {

	FLAC__STREAM_DECODER_TELL_STATUS_OK,
	/**< The tell was OK and decoding can continue. */

	FLAC__STREAM_DECODER_TELL_STATUS_ERROR,
	/**< An unrecoverable error occurred.  The decoder will return from the process call. */

	FLAC__STREAM_DECODER_TELL_STATUS_UNSUPPORTED
	/**< Client does not support telling the position. */

} FLAC__StreamDecoderTellStatus;

/** Maps a FLAC__StreamDecoderTellStatus to a C string.
 *
 *  Using a FLAC__StreamDecoderTellStatus as the index to this array
 *  will give the string equivalent.  The contents should not be modified.
 */
extern FLAC_API const char * const FLAC__StreamDecoderTellStatusString[];


/** Return values for the FLAC__StreamDecoder length callback.
 */
typedef enum {

	FLAC__STREAM_DECODER_LENGTH_STATUS_OK,
	/**< The length call was OK and decoding can continue. */

	FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR,
	/**< An unrecoverable error occurred.  The decoder will return from the process call. */

	FLAC__STREAM_DECODER_LENGTH_STATUS_UNSUPPORTED
	/**< Client does not support reporting the length. */

} FLAC__StreamDecoderLengthStatus;

/** Maps a FLAC__StreamDecoderLengthStatus to a C string.
 *
 *  Using a FLAC__StreamDecoderLengthStatus as the index to this array
 *  will give the string equivalent.  The contents should not be modified.
 */
extern FLAC_API const char * const FLAC__StreamDecoderLengthStatusString[];


/** Return values for the FLAC__StreamDecoder write callback.
 */
typedef enum {

	FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE,
	/**< The write was OK and decoding can continue. */

	FLAC__STREAM_DECODER_WRITE_STATUS_ABORT
	/**< An unrecoverable error occurred.  The decoder will return from the process call. */

} FLAC__StreamDecoderWriteStatus;

/** Maps a FLAC__StreamDecoderWriteStatus to a C string.
 *
 *  Using a FLAC__StreamDecoderWriteStatus as the index to this array
 *  will give the string equivalent.  The contents should not be modified.
 */
extern FLAC_API const char * const FLAC__StreamDecoderWriteStatusString[];


/** Possible values passed back to the FLAC__StreamDecoder error callback.
 *  \c FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC is the generic catch-
 *  all.  The rest could be caused by bad sync (false synchronization on
 *  data that is not the start of a frame) or corrupted data.  The error
 *  itself is the decoder's best guess at what happened assuming a correct
 *  sync.  For example \c FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER
 *  could be caused by a correct sync on the start of a frame, but some
 *  data in the frame header was corrupted.  Or it could be the result of
 *  syncing on a point the stream that looked like the starting of a frame
 *  but was not.  \c FLAC__STREAM_DECODER_ERROR_STATUS_UNPARSEABLE_STREAM
 *  could be because the decoder encountered a valid frame made by a future
 *  version of the encoder which it cannot parse, or because of a false
 *  sync making it appear as though an encountered frame was generated by
 *  a future encoder.
 */
typedef enum {

	FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC,
	/**< An error in the stream caused the decoder to lose synchronization. */

	FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER,
	/**< The decoder encountered a corrupted frame header. */

	FLAC__STREAM_DECODER_ERROR_STATUS_FRAME_CRC_MISMATCH,
	/**< The frame's data did not match the CRC in the footer. */

	FLAC__STREAM_DECODER_ERROR_STATUS_UNPARSEABLE_STREAM
	/**< The decoder encountered reserved fields in use in the stream. */

} FLAC__StreamDecoderErrorStatus;

/** Maps a FLAC__StreamDecoderErrorStatus to a C string.
 *
 *  Using a FLAC__StreamDecoderErrorStatus as the index to this array
 *  will give the string equivalent.  The contents should not be modified.
 */
extern FLAC_API const char * const FLAC__StreamDecoderErrorStatusString[];


/***********************************************************************
 *
 * class FLAC__StreamDecoder
 *
 ***********************************************************************/

struct FLAC__StreamDecoderProtected;
struct FLAC__StreamDecoderPrivate;
/** The opaque structure definition for the stream decoder type.
 *  See the \link flac_stream_decoder stream decoder module \endlink
 *  for a detailed description.
 */
typedef struct {
	struct FLAC__StreamDecoderProtected *protected_; /* avoid the C++ keyword 'protected' */
	struct FLAC__StreamDecoderPrivate *private_; /* avoid the C++ keyword 'private' */
} FLAC__StreamDecoder;

/** Signature for the read callback.
 *
 *  A function pointer matching this signature must be passed to
 *  FLAC__stream_decoder_init*_stream(). The supplied function will be
 *  called when the decoder needs more input data.  The address of the
 *  buffer to be filled is supplied, along with the number of bytes the
 *  buffer can hold.  The callback may choose to supply less data and
 *  modify the byte count but must be careful not to overflow the buffer.
 *  The callback then returns a status code chosen from
 *  FLAC__StreamDecoderReadStatus.
 *
 * Here is an example of a read callback for stdio streams:
 * \code
 * FLAC__StreamDecoderReadStatus read_cb(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data)
 * {
 *   FILE *file = ((MyClientData*)client_data)->file;
 *   if(*bytes > 0) {
 *     *bytes = fread(buffer, sizeof(FLAC__byte), *bytes, file);
 *     if(ferror(file))
 *       return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
 *     else if(*bytes == 0)
 *       return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
 *     else
 *       return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
 *   }
 *   else
 *     return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
 * }
 * \endcode
 *
 * \note In general, FLAC__StreamDecoder functions which change the
 * state should not be called on the \a decoder while in the callback.
 *
 * \param  decoder  The decoder instance calling the callback.
 * \param  buffer   A pointer to a location for the callee to store
 *                  data to be decoded.
 * \param  bytes    A pointer to the size of the buffer.  On entry
 *                  to the callback, it contains the maximum number
 *                  of bytes that may be stored in \a buffer.  The
 *                  callee must set it to the actual number of bytes
 *                  stored (0 in case of error or end-of-stream) before
 *                  returning.
 * \param  client_data  The callee's client data set through
 *                      FLAC__stream_decoder_init_*().
 * \retval FLAC__StreamDecoderReadStatus
 *    The callee's return status.  Note that the callback should return
 *    \c FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM if and only if
 *    zero bytes were read and there is no more data to be read.
 */
typedef FLAC__StreamDecoderReadStatus (*FLAC__StreamDecoderReadCallback)(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data);

/** Signature for the seek callback.
 *
 *  A function pointer matching this signature may be passed to
 *  FLAC__stream_decoder_init*_stream().  The supplied function will be
 *  called when the decoder needs to seek the input stream.  The decoder
 *  will pass the absolute byte offset to seek to, 0 meaning the
 *  beginning of the stream.
 *
 * Here is an example of a seek callback for stdio streams:
 * \code
 * FLAC__StreamDecoderSeekStatus seek_cb(const FLAC__StreamDecoder *decoder, FLAC__uint64 absolute_byte_offset, void *client_data)
 * {
 *   FILE *file = ((MyClientData*)client_data)->file;
 *   if(file == stdin)
 *     return FLAC__STREAM_DECODER_SEEK_STATUS_UNSUPPORTED;
 *   else if(fseeko(file, (off_t)absolute_byte_offset, SEEK_SET) < 0)
 *     return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
 *   else
 *     return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
 * }
 * \endcode
 *
 * \note In general, FLAC__StreamDecoder functions which change the
 * state should not be called on the \a decoder while in the callback.
 *
 * \param  decoder  The decoder instance calling the callback.
 * \param  absolute_byte_offset  The offset from the beginning of the stream
 *                               to seek to.
 * \param  client_data  The callee's client data set through
 *                      FLAC__stream_decoder_init_*().
 * \retval FLAC__StreamDecoderSeekStatus
 *    The callee's return status.
 */
typedef FLAC__StreamDecoderSeekStatus (*FLAC__StreamDecoderSeekCallback)(const FLAC__StreamDecoder *decoder, FLAC__uint64 absolute_byte_offset, void *client_data);

/** Signature for the tell callback.
 *
 *  A function pointer matching this signature may be passed to
 *  FLAC__stream_decoder_init*_stream().  The supplied function will be
 *  called when the decoder wants to know the current position of the
 *  stream.  The callback should return the byte offset from the
 *  beginning of the stream.
 *
 * Here is an example of a tell callback for stdio streams:
 * \code
 * FLAC__StreamDecoderTellStatus tell_cb(const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *client_data)
 * {
 *   FILE *file = ((MyClientData*)client_data)->file;
 *   off_t pos;
 *   if(file == stdin)
 *     return FLAC__STREAM_DECODER_TELL_STATUS_UNSUPPORTED;
 *   else if((pos = ftello(file)) < 0)
 *     return FLAC__STREAM_DECODER_TELL_STATUS_ERROR;
 *   else {
 *     *absolute_byte_offset = (FLAC__uint64)pos;
 *     return FLAC__STREAM_DECODER_TELL_STATUS_OK;
 *   }
 * }
 * \endcode
 *
 * \note In general, FLAC__StreamDecoder functions which change the
 * state should not be called on the \a decoder while in the callback.
 *
 * \param  decoder  The decoder instance calling the callback.
 * \param  absolute_byte_offset  A pointer to storage for the current offset
 *                               from the beginning of the stream.
 * \param  client_data  The callee's client data set through
 *                      FLAC__stream_decoder_init_*().
 * \retval FLAC__StreamDecoderTellStatus
 *    The callee's return status.
 */
typedef FLAC__StreamDecoderTellStatus (*FLAC__StreamDecoderTellCallback)(const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *client_data);

/** Signature for the length callback.
 *
 *  A function pointer matching this signature may be passed to
 *  FLAC__stream_decoder_init*_stream().  The supplied function will be
 *  called when the decoder wants to know the total length of the stream
 *  in bytes.
 *
 * Here is an example of a length callback for stdio streams:
 * \code
 * FLAC__StreamDecoderLengthStatus length_cb(const FLAC__StreamDecoder *decoder, FLAC__uint64 *stream_length, void *client_data)
 * {
 *   FILE *file = ((MyClientData*)client_data)->file;
 *   struct stat filestats;
 *
 *   if(file == stdin)
 *     return FLAC__STREAM_DECODER_LENGTH_STATUS_UNSUPPORTED;
 *   else if(fstat(fileno(file), &filestats) != 0)
 *     return FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;
 *   else {
 *     *stream_length = (FLAC__uint64)filestats.st_size;
 *     return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
 *   }
 * }
 * \endcode
 *
 * \note In general, FLAC__StreamDecoder functions which change the
 * state should not be called on the \a decoder while in the callback.
 *
 * \param  decoder  The decoder instance calling the callback.
 * \param  stream_length  A pointer to storage for the length of the stream
 *                        in bytes.
 * \param  client_data  The callee's client data set through
 *                      FLAC__stream_decoder_init_*().
 * \retval FLAC__StreamDecoderLengthStatus
 *    The callee's return status.
 */
typedef FLAC__StreamDecoderLengthStatus (*FLAC__StreamDecoderLengthCallback)(const FLAC__StreamDecoder *decoder, FLAC__uint64 *stream_length, void *client_data);

/** Signature for the EOF callback.
 *
 *  A function pointer matching this signature may be passed to
 *  FLAC__stream_decoder_init*_stream().  The supplied function will be
 *  called when the decoder needs to know if the end of the stream has
 *  been reached.
 *
 * Here is an example of a EOF callback for stdio streams:
 * FLAC__bool eof_cb(const FLAC__StreamDecoder *decoder, void *client_data)
 * \code
 * {
 *   FILE *file = ((MyClientData*)client_data)->file;
 *   return feof(file)? true : false;
 * }
 * \endcode
 *
 * \note In general, FLAC__StreamDecoder functions which change the
 * state should not be called on the \a decoder while in the callback.
 *
 * \param  decoder  The decoder instance calling the callback.
 * \param  client_data  The callee's client data set through
 *                      FLAC__stream_decoder_init_*().
 * \retval FLAC__bool
 *    \c true if the currently at the end of the stream, else \c false.
 */
typedef FLAC__bool (*FLAC__StreamDecoderEofCallback)(const FLAC__StreamDecoder *decoder, void *client_data);

/** Signature for the write callback.
 *
 *  A function pointer matching this signature must be passed to one of
 *  the FLAC__stream_decoder_init_*() functions.
 *  The supplied function will be called when the decoder has decoded a
 *  single audio frame.  The decoder will pass the frame metadata as well
 *  as an array of pointers (one for each channel) pointing to the
 *  decoded audio.
 *
 * \note In general, FLAC__StreamDecoder functions which change the
 * state should not be called on the \a decoder while in the callback.
 *
 * \param  decoder  The decoder instance calling the callback.
 * \param  frame    The description of the decoded frame.  See
 *                  FLAC__Frame.
 * \param  buffer   An array of pointers to decoded channels of data.
 *                  Each pointer will point to an array of signed
 *                  samples of length \a frame->header.blocksize.
 *                  Channels will be ordered according to the FLAC
 *                  specification; see the documentation for the
 *                  <A HREF="../format.html#frame_header">frame header</A>.
 * \param  client_data  The callee's client data set through
 *                      FLAC__stream_decoder_init_*().
 * \retval FLAC__StreamDecoderWriteStatus
 *    The callee's return status.
 */
typedef FLAC__StreamDecoderWriteStatus (*FLAC__StreamDecoderWriteCallback)(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[], void *client_data);

/** Signature for the metadata callback.
 *
 *  A function pointer matching this signature must be passed to one of
 *  the FLAC__stream_decoder_init_*() functions.
 *  The supplied function will be called when the decoder has decoded a
 *  metadata block.  In a valid FLAC file there will always be one
 *  \c STREAMINFO block, followed by zero or more other metadata blocks.
 *  These will be supplied by the decoder in the same order as they
 *  appear in the stream and always before the first audio frame (i.e.
 *  write callback).  The metadata block that is passed in must not be
 *  modified, and it doesn't live beyond the callback, so you should make
 *  a copy of it with FLAC__metadata_object_clone() if you will need it
 *  elsewhere.  Since metadata blocks can potentially be large, by
 *  default the decoder only calls the metadata callback for the
 *  \c STREAMINFO block; you can instruct the decoder to pass or filter
 *  other blocks with FLAC__stream_decoder_set_metadata_*() calls.
 *
 * \note In general, FLAC__StreamDecoder functions which change the
 * state should not be called on the \a decoder while in the callback.
 *
 * \param  decoder  The decoder instance calling the callback.
 * \param  metadata The decoded metadata block.
 * \param  client_data  The callee's client data set through
 *                      FLAC__stream_decoder_init_*().
 */
typedef void (*FLAC__StreamDecoderMetadataCallback)(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data);

/** Signature for the error callback.
 *
 *  A function pointer matching this signature must be passed to one of
 *  the FLAC__stream_decoder_init_*() functions.
 *  The supplied function will be called whenever an error occurs during
 *  decoding.
 *
 * \note In general, FLAC__StreamDecoder functions which change the
 * state should not be called on the \a decoder while in the callback.
 *
 * \param  decoder  The decoder instance calling the callback.
 * \param  status   The error encountered by the decoder.
 * \param  client_data  The callee's client data set through
 *                      FLAC__stream_decoder_init_*().
 */
typedef void (*FLAC__StreamDecoderErrorCallback)(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data);


/***********************************************************************
 *
 * Class constructor/destructor
 *
 ***********************************************************************/

/** Create a new stream decoder instance.  The instance is created with
 *  default settings; see the individual FLAC__stream_decoder_set_*()
 *  functions for each setting's default.
 *
 * \retval FLAC__StreamDecoder*
 *    \c NULL if there was an error allocating memory, else the new instance.
 */
FLAC_API FLAC__StreamDecoder *FLAC__stream_decoder_new(void);

/** Free a decoder instance.  Deletes the object pointed to by \a decoder.
 *
 * \param decoder  A pointer to an existing decoder.
 * \assert
 *    \code decoder != NULL \endcode
 */
FLAC_API void FLAC__stream_decoder_delete(FLAC__StreamDecoder *decoder);


/***********************************************************************
 *
 * Public class method prototypes
 *
 ***********************************************************************/

/** Set the serial number for the FLAC stream within the Ogg container.
 *  The default behavior is to use the serial number of the first Ogg
 *  page.  Setting a serial number here will explicitly specify which
 *  stream is to be decoded.
 *
 * \note
 * This does not need to be set for native FLAC decoding.
 *
 * \default \c use serial number of first page
 * \param  decoder        A decoder instance to set.
 * \param  serial_number  See above.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the decoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_decoder_set_ogg_serial_number(FLAC__StreamDecoder *decoder, long serial_number);

/** Set the "MD5 signature checking" flag.  If \c true, the decoder will
 *  compute the MD5 signature of the unencoded audio data while decoding
 *  and compare it to the signature from the STREAMINFO block, if it
 *  exists, during FLAC__stream_decoder_finish().
 *
 *  MD5 signature checking will be turned off (until the next
 *  FLAC__stream_decoder_reset()) if there is no signature in the
 *  STREAMINFO block or when a seek is attempted.
 *
 *  Clients that do not use the MD5 check should leave this off to speed
 *  up decoding.
 *
 * \default \c false
 * \param  decoder  A decoder instance to set.
 * \param  value    Flag value (see above).
 * \assert
 *    \code decoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the decoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_decoder_set_md5_checking(FLAC__StreamDecoder *decoder, FLAC__bool value);

/** Direct the decoder to pass on all metadata blocks of type \a type.
 *
 * \default By default, only the \c STREAMINFO block is returned via the
 *          metadata callback.
 * \param  decoder  A decoder instance to set.
 * \param  type     See above.
 * \assert
 *    \code decoder != NULL \endcode
 *    \a type is valid
 * \retval FLAC__bool
 *    \c false if the decoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_respond(FLAC__StreamDecoder *decoder, FLAC__MetadataType type);

/** Direct the decoder to pass on all APPLICATION metadata blocks of the
 *  given \a id.
 *
 * \default By default, only the \c STREAMINFO block is returned via the
 *          metadata callback.
 * \param  decoder  A decoder instance to set.
 * \param  id       See above.
 * \assert
 *    \code decoder != NULL \endcode
 *    \code id != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the decoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_respond_application(FLAC__StreamDecoder *decoder, const FLAC__byte id[4]);

/** Direct the decoder to pass on all metadata blocks of any type.
 *
 * \default By default, only the \c STREAMINFO block is returned via the
 *          metadata callback.
 * \param  decoder  A decoder instance to set.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the decoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_respond_all(FLAC__StreamDecoder *decoder);

/** Direct the decoder to filter out all metadata blocks of type \a type.
 *
 * \default By default, only the \c STREAMINFO block is returned via the
 *          metadata callback.
 * \param  decoder  A decoder instance to set.
 * \param  type     See above.
 * \assert
 *    \code decoder != NULL \endcode
 *    \a type is valid
 * \retval FLAC__bool
 *    \c false if the decoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_ignore(FLAC__StreamDecoder *decoder, FLAC__MetadataType type);

/** Direct the decoder to filter out all APPLICATION metadata blocks of
 *  the given \a id.
 *
 * \default By default, only the \c STREAMINFO block is returned via the
 *          metadata callback.
 * \param  decoder  A decoder instance to set.
 * \param  id       See above.
 * \assert
 *    \code decoder != NULL \endcode
 *    \code id != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the decoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_ignore_application(FLAC__StreamDecoder *decoder, const FLAC__byte id[4]);

/** Direct the decoder to filter out all metadata blocks of any type.
 *
 * \default By default, only the \c STREAMINFO block is returned via the
 *          metadata callback.
 * \param  decoder  A decoder instance to set.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the decoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_ignore_all(FLAC__StreamDecoder *decoder);

/** Get the current decoder state.
 *
 * \param  decoder  A decoder instance to query.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval FLAC__StreamDecoderState
 *    The current decoder state.
 */
FLAC_API FLAC__StreamDecoderState FLAC__stream_decoder_get_state(const FLAC__StreamDecoder *decoder);

/** Get the current decoder state as a C string.
 *
 * \param  decoder  A decoder instance to query.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval const char *
 *    The decoder state as a C string.  Do not modify the contents.
 */
FLAC_API const char *FLAC__stream_decoder_get_resolved_state_string(const FLAC__StreamDecoder *decoder);

/** Get the "MD5 signature checking" flag.
 *  This is the value of the setting, not whether or not the decoder is
 *  currently checking the MD5 (remember, it can be turned off automatically
 *  by a seek).  When the decoder is reset the flag will be restored to the
 *  value returned by this function.
 *
 * \param  decoder  A decoder instance to query.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval FLAC__bool
 *    See above.
 */
FLAC_API FLAC__bool FLAC__stream_decoder_get_md5_checking(const FLAC__StreamDecoder *decoder);

/** Get the total number of samples in the stream being decoded.
 *  Will only be valid after decoding has started and will contain the
 *  value from the \c STREAMINFO block.  A value of \c 0 means "unknown".
 *
 * \param  decoder  A decoder instance to query.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval unsigned
 *    See above.
 */
FLAC_API FLAC__uint64 FLAC__stream_decoder_get_total_samples(const FLAC__StreamDecoder *decoder);

/** Get the current number of channels in the stream being decoded.
 *  Will only be valid after decoding has started and will contain the
 *  value from the most recently decoded frame header.
 *
 * \param  decoder  A decoder instance to query.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval unsigned
 *    See above.
 */
FLAC_API unsigned FLAC__stream_decoder_get_channels(const FLAC__StreamDecoder *decoder);

/** Get the current channel assignment in the stream being decoded.
 *  Will only be valid after decoding has started and will contain the
 *  value from the most recently decoded frame header.
 *
 * \param  decoder  A decoder instance to query.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval FLAC__ChannelAssignment
 *    See above.
 */
FLAC_API FLAC__ChannelAssignment FLAC__stream_decoder_get_channel_assignment(const FLAC__StreamDecoder *decoder);

/** Get the current sample resolution in the stream being decoded.
 *  Will only be valid after decoding has started and will contain the
 *  value from the most recently decoded frame header.
 *
 * \param  decoder  A decoder instance to query.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval unsigned
 *    See above.
 */
FLAC_API unsigned FLAC__stream_decoder_get_bits_per_sample(const FLAC__StreamDecoder *decoder);

/** Get the current sample rate in Hz of the stream being decoded.
 *  Will only be valid after decoding has started and will contain the
 *  value from the most recently decoded frame header.
 *
 * \param  decoder  A decoder instance to query.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval unsigned
 *    See above.
 */
FLAC_API unsigned FLAC__stream_decoder_get_sample_rate(const FLAC__StreamDecoder *decoder);

/** Get the current blocksize of the stream being decoded.
 *  Will only be valid after decoding has started and will contain the
 *  value from the most recently decoded frame header.
 *
 * \param  decoder  A decoder instance to query.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval unsigned
 *    See above.
 */
FLAC_API unsigned FLAC__stream_decoder_get_blocksize(const FLAC__StreamDecoder *decoder);

/** Returns the decoder's current read position within the stream.
 *  The position is the byte offset from the start of the stream.
 *  Bytes before this position have been fully decoded.  Note that
 *  there may still be undecoded bytes in the decoder's read FIFO.
 *  The returned position is correct even after a seek.
 *
 *  \warning This function currently only works for native FLAC,
 *           not Ogg FLAC streams.
 *
 * \param  decoder   A decoder instance to query.
 * \param  position  Address at which to return the desired position.
 * \assert
 *    \code decoder != NULL \endcode
 *    \code position != NULL \endcode
 * \retval FLAC__bool
 *    \c true if successful, \c false if the stream is not native FLAC,
 *    or there was an error from the 'tell' callback or it returned
 *    \c FLAC__STREAM_DECODER_TELL_STATUS_UNSUPPORTED.
 */
FLAC_API FLAC__bool FLAC__stream_decoder_get_decode_position(const FLAC__StreamDecoder *decoder, FLAC__uint64 *position);

/** Initialize the decoder instance to decode native FLAC streams.
 *
 *  This flavor of initialization sets up the decoder to decode from a
 *  native FLAC stream. I/O is performed via callbacks to the client.
 *  For decoding from a plain file via filename or open FILE*,
 *  FLAC__stream_decoder_init_file() and FLAC__stream_decoder_init_FILE()
 *  provide a simpler interface.
 *
 *  This function should be called after FLAC__stream_decoder_new() and
 *  FLAC__stream_decoder_set_*() but before any of the
 *  FLAC__stream_decoder_process_*() functions.  Will set and return the
 *  decoder state, which will be FLAC__STREAM_DECODER_SEARCH_FOR_METADATA
 *  if initialization succeeded.
 *
 * \param  decoder            An uninitialized decoder instance.
 * \param  read_callback      See FLAC__StreamDecoderReadCallback.  This
 *                            pointer must not be \c NULL.
 * \param  seek_callback      See FLAC__StreamDecoderSeekCallback.  This
 *                            pointer may be \c NULL if seeking is not
 *                            supported.  If \a seek_callback is not \c NULL then a
 *                            \a tell_callback, \a length_callback, and \a eof_callback must also be supplied.
 *                            Alternatively, a dummy seek callback that just
 *                            returns \c FLAC__STREAM_DECODER_SEEK_STATUS_UNSUPPORTED
 *                            may also be supplied, all though this is slightly
 *                            less efficient for the decoder.
 * \param  tell_callback      See FLAC__StreamDecoderTellCallback.  This
 *                            pointer may be \c NULL if not supported by the client.  If
 *                            \a seek_callback is not \c NULL then a
 *                            \a tell_callback must also be supplied.
 *                            Alternatively, a dummy tell callback that just
 *                            returns \c FLAC__STREAM_DECODER_TELL_STATUS_UNSUPPORTED
 *                            may also be supplied, all though this is slightly
 *                            less efficient for the decoder.
 * \param  length_callback    See FLAC__StreamDecoderLengthCallback.  This
 *                            pointer may be \c NULL if not supported by the client.  If
 *                            \a seek_callback is not \c NULL then a
 *                            \a length_callback must also be supplied.
 *                            Alternatively, a dummy length callback that just
 *                            returns \c FLAC__STREAM_DECODER_LENGTH_STATUS_UNSUPPORTED
 *                            may also be supplied, all though this is slightly
 *                            less efficient for the decoder.
 * \param  eof_callback       See FLAC__StreamDecoderEofCallback.  This
 *                            pointer may be \c NULL if not supported by the client.  If
 *                            \a seek_callback is not \c NULL then a
 *                            \a eof_callback must also be supplied.
 *                            Alternatively, a dummy length callback that just
 *                            returns \c false
 *                            may also be supplied, all though this is slightly
 *                            less efficient for the decoder.
 * \param  write_callback     See FLAC__StreamDecoderWriteCallback.  This
 *                            pointer must not be \c NULL.
 * \param  metadata_callback  See FLAC__StreamDecoderMetadataCallback.  This
 *                            pointer may be \c NULL if the callback is not
 *                            desired.
 * \param  error_callback     See FLAC__StreamDecoderErrorCallback.  This
 *                            pointer must not be \c NULL.
 * \param  client_data        This value will be supplied to callbacks in their
 *                            \a client_data argument.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval FLAC__StreamDecoderInitStatus
 *    \c FLAC__STREAM_DECODER_INIT_STATUS_OK if initialization was successful;
 *    see FLAC__StreamDecoderInitStatus for the meanings of other return values.
 */
FLAC_API FLAC__StreamDecoderInitStatus FLAC__stream_decoder_init_stream(
	FLAC__StreamDecoder *decoder,
	FLAC__StreamDecoderReadCallback read_callback,
	FLAC__StreamDecoderSeekCallback seek_callback,
	FLAC__StreamDecoderTellCallback tell_callback,
	FLAC__StreamDecoderLengthCallback length_callback,
	FLAC__StreamDecoderEofCallback eof_callback,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data
);

/** Initialize the decoder instance to decode Ogg FLAC streams.
 *
 *  This flavor of initialization sets up the decoder to decode from a
 *  FLAC stream in an Ogg container. I/O is performed via callbacks to the
 *  client.  For decoding from a plain file via filename or open FILE*,
 *  FLAC__stream_decoder_init_ogg_file() and FLAC__stream_decoder_init_ogg_FILE()
 *  provide a simpler interface.
 *
 *  This function should be called after FLAC__stream_decoder_new() and
 *  FLAC__stream_decoder_set_*() but before any of the
 *  FLAC__stream_decoder_process_*() functions.  Will set and return the
 *  decoder state, which will be FLAC__STREAM_DECODER_SEARCH_FOR_METADATA
 *  if initialization succeeded.
 *
 *  \note Support for Ogg FLAC in the library is optional.  If this
 *  library has been built without support for Ogg FLAC, this function
 *  will return \c FLAC__STREAM_DECODER_INIT_STATUS_UNSUPPORTED_CONTAINER.
 *
 * \param  decoder            An uninitialized decoder instance.
 * \param  read_callback      See FLAC__StreamDecoderReadCallback.  This
 *                            pointer must not be \c NULL.
 * \param  seek_callback      See FLAC__StreamDecoderSeekCallback.  This
 *                            pointer may be \c NULL if seeking is not
 *                            supported.  If \a seek_callback is not \c NULL then a
 *                            \a tell_callback, \a length_callback, and \a eof_callback must also be supplied.
 *                            Alternatively, a dummy seek callback that just
 *                            returns \c FLAC__STREAM_DECODER_SEEK_STATUS_UNSUPPORTED
 *                            may also be supplied, all though this is slightly
 *                            less efficient for the decoder.
 * \param  tell_callback      See FLAC__StreamDecoderTellCallback.  This
 *                            pointer may be \c NULL if not supported by the client.  If
 *                            \a seek_callback is not \c NULL then a
 *                            \a tell_callback must also be supplied.
 *                            Alternatively, a dummy tell callback that just
 *                            returns \c FLAC__STREAM_DECODER_TELL_STATUS_UNSUPPORTED
 *                            may also be supplied, all though this is slightly
 *                            less efficient for the decoder.
 * \param  length_callback    See FLAC__StreamDecoderLengthCallback.  This
 *                            pointer may be \c NULL if not supported by the client.  If
 *                            \a seek_callback is not \c NULL then a
 *                            \a length_callback must also be supplied.
 *                            Alternatively, a dummy length callback that just
 *                            returns \c FLAC__STREAM_DECODER_LENGTH_STATUS_UNSUPPORTED
 *                            may also be supplied, all though this is slightly
 *                            less efficient for the decoder.
 * \param  eof_callback       See FLAC__StreamDecoderEofCallback.  This
 *                            pointer may be \c NULL if not supported by the client.  If
 *                            \a seek_callback is not \c NULL then a
 *                            \a eof_callback must also be supplied.
 *                            Alternatively, a dummy length callback that just
 *                            returns \c false
 *                            may also be supplied, all though this is slightly
 *                            less efficient for the decoder.
 * \param  write_callback     See FLAC__StreamDecoderWriteCallback.  This
 *                            pointer must not be \c NULL.
 * \param  metadata_callback  See FLAC__StreamDecoderMetadataCallback.  This
 *                            pointer may be \c NULL if the callback is not
 *                            desired.
 * \param  error_callback     See FLAC__StreamDecoderErrorCallback.  This
 *                            pointer must not be \c NULL.
 * \param  client_data        This value will be supplied to callbacks in their
 *                            \a client_data argument.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval FLAC__StreamDecoderInitStatus
 *    \c FLAC__STREAM_DECODER_INIT_STATUS_OK if initialization was successful;
 *    see FLAC__StreamDecoderInitStatus for the meanings of other return values.
 */
FLAC_API FLAC__StreamDecoderInitStatus FLAC__stream_decoder_init_ogg_stream(
	FLAC__StreamDecoder *decoder,
	FLAC__StreamDecoderReadCallback read_callback,
	FLAC__StreamDecoderSeekCallback seek_callback,
	FLAC__StreamDecoderTellCallback tell_callback,
	FLAC__StreamDecoderLengthCallback length_callback,
	FLAC__StreamDecoderEofCallback eof_callback,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data
);

/** Initialize the decoder instance to decode native FLAC files.
 *
 *  This flavor of initialization sets up the decoder to decode from a
 *  plain native FLAC file.  For non-stdio streams, you must use
 *  FLAC__stream_decoder_init_stream() and provide callbacks for the I/O.
 *
 *  This function should be called after FLAC__stream_decoder_new() and
 *  FLAC__stream_decoder_set_*() but before any of the
 *  FLAC__stream_decoder_process_*() functions.  Will set and return the
 *  decoder state, which will be FLAC__STREAM_DECODER_SEARCH_FOR_METADATA
 *  if initialization succeeded.
 *
 * \param  decoder            An uninitialized decoder instance.
 * \param  file               An open FLAC file.  The file should have been
 *                            opened with mode \c "rb" and rewound.  The file
 *                            becomes owned by the decoder and should not be
 *                            manipulated by the client while decoding.
 *                            Unless \a file is \c stdin, it will be closed
 *                            when FLAC__stream_decoder_finish() is called.
 *                            Note however that seeking will not work when
 *                            decoding from \c stdout since it is not seekable.
 * \param  write_callback     See FLAC__StreamDecoderWriteCallback.  This
 *                            pointer must not be \c NULL.
 * \param  metadata_callback  See FLAC__StreamDecoderMetadataCallback.  This
 *                            pointer may be \c NULL if the callback is not
 *                            desired.
 * \param  error_callback     See FLAC__StreamDecoderErrorCallback.  This
 *                            pointer must not be \c NULL.
 * \param  client_data        This value will be supplied to callbacks in their
 *                            \a client_data argument.
 * \assert
 *    \code decoder != NULL \endcode
 *    \code file != NULL \endcode
 * \retval FLAC__StreamDecoderInitStatus
 *    \c FLAC__STREAM_DECODER_INIT_STATUS_OK if initialization was successful;
 *    see FLAC__StreamDecoderInitStatus for the meanings of other return values.
 */
FLAC_API FLAC__StreamDecoderInitStatus FLAC__stream_decoder_init_FILE(
	FLAC__StreamDecoder *decoder,
	FILE *file,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data
);

/** Initialize the decoder instance to decode Ogg FLAC files.
 *
 *  This flavor of initialization sets up the decoder to decode from a
 *  plain Ogg FLAC file.  For non-stdio streams, you must use
 *  FLAC__stream_decoder_init_ogg_stream() and provide callbacks for the I/O.
 *
 *  This function should be called after FLAC__stream_decoder_new() and
 *  FLAC__stream_decoder_set_*() but before any of the
 *  FLAC__stream_decoder_process_*() functions.  Will set and return the
 *  decoder state, which will be FLAC__STREAM_DECODER_SEARCH_FOR_METADATA
 *  if initialization succeeded.
 *
 *  \note Support for Ogg FLAC in the library is optional.  If this
 *  library has been built without support for Ogg FLAC, this function
 *  will return \c FLAC__STREAM_DECODER_INIT_STATUS_UNSUPPORTED_CONTAINER.
 *
 * \param  decoder            An uninitialized decoder instance.
 * \param  file               An open FLAC file.  The file should have been
 *                            opened with mode \c "rb" and rewound.  The file
 *                            becomes owned by the decoder and should not be
 *                            manipulated by the client while decoding.
 *                            Unless \a file is \c stdin, it will be closed
 *                            when FLAC__stream_decoder_finish() is called.
 *                            Note however that seeking will not work when
 *                            decoding from \c stdout since it is not seekable.
 * \param  write_callback     See FLAC__StreamDecoderWriteCallback.  This
 *                            pointer must not be \c NULL.
 * \param  metadata_callback  See FLAC__StreamDecoderMetadataCallback.  This
 *                            pointer may be \c NULL if the callback is not
 *                            desired.
 * \param  error_callback     See FLAC__StreamDecoderErrorCallback.  This
 *                            pointer must not be \c NULL.
 * \param  client_data        This value will be supplied to callbacks in their
 *                            \a client_data argument.
 * \assert
 *    \code decoder != NULL \endcode
 *    \code file != NULL \endcode
 * \retval FLAC__StreamDecoderInitStatus
 *    \c FLAC__STREAM_DECODER_INIT_STATUS_OK if initialization was successful;
 *    see FLAC__StreamDecoderInitStatus for the meanings of other return values.
 */
FLAC_API FLAC__StreamDecoderInitStatus FLAC__stream_decoder_init_ogg_FILE(
	FLAC__StreamDecoder *decoder,
	FILE *file,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data
);

/** Initialize the decoder instance to decode native FLAC files.
 *
 *  This flavor of initialization sets up the decoder to decode from a plain
 *  native FLAC file.  If POSIX fopen() semantics are not sufficient, (for
 *  example, with Unicode filenames on Windows), you must use
 *  FLAC__stream_decoder_init_FILE(), or FLAC__stream_decoder_init_stream()
 *  and provide callbacks for the I/O.
 *
 *  This function should be called after FLAC__stream_decoder_new() and
 *  FLAC__stream_decoder_set_*() but before any of the
 *  FLAC__stream_decoder_process_*() functions.  Will set and return the
 *  decoder state, which will be FLAC__STREAM_DECODER_SEARCH_FOR_METADATA
 *  if initialization succeeded.
 *
 * \param  decoder            An uninitialized decoder instance.
 * \param  filename           The name of the file to decode from.  The file will
 *                            be opened with fopen().  Use \c NULL to decode from
 *                            \c stdin.  Note that \c stdin is not seekable.
 * \param  write_callback     See FLAC__StreamDecoderWriteCallback.  This
 *                            pointer must not be \c NULL.
 * \param  metadata_callback  See FLAC__StreamDecoderMetadataCallback.  This
 *                            pointer may be \c NULL if the callback is not
 *                            desired.
 * \param  error_callback     See FLAC__StreamDecoderErrorCallback.  This
 *                            pointer must not be \c NULL.
 * \param  client_data        This value will be supplied to callbacks in their
 *                            \a client_data argument.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval FLAC__StreamDecoderInitStatus
 *    \c FLAC__STREAM_DECODER_INIT_STATUS_OK if initialization was successful;
 *    see FLAC__StreamDecoderInitStatus for the meanings of other return values.
 */
FLAC_API FLAC__StreamDecoderInitStatus FLAC__stream_decoder_init_file(
	FLAC__StreamDecoder *decoder,
	const char *filename,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data
);

/** Initialize the decoder instance to decode Ogg FLAC files.
 *
 *  This flavor of initialization sets up the decoder to decode from a plain
 *  Ogg FLAC file.  If POSIX fopen() semantics are not sufficient, (for
 *  example, with Unicode filenames on Windows), you must use
 *  FLAC__stream_decoder_init_ogg_FILE(), or FLAC__stream_decoder_init_ogg_stream()
 *  and provide callbacks for the I/O.
 *
 *  This function should be called after FLAC__stream_decoder_new() and
 *  FLAC__stream_decoder_set_*() but before any of the
 *  FLAC__stream_decoder_process_*() functions.  Will set and return the
 *  decoder state, which will be FLAC__STREAM_DECODER_SEARCH_FOR_METADATA
 *  if initialization succeeded.
 *
 *  \note Support for Ogg FLAC in the library is optional.  If this
 *  library has been built without support for Ogg FLAC, this function
 *  will return \c FLAC__STREAM_DECODER_INIT_STATUS_UNSUPPORTED_CONTAINER.
 *
 * \param  decoder            An uninitialized decoder instance.
 * \param  filename           The name of the file to decode from.  The file will
 *                            be opened with fopen().  Use \c NULL to decode from
 *                            \c stdin.  Note that \c stdin is not seekable.
 * \param  write_callback     See FLAC__StreamDecoderWriteCallback.  This
 *                            pointer must not be \c NULL.
 * \param  metadata_callback  See FLAC__StreamDecoderMetadataCallback.  This
 *                            pointer may be \c NULL if the callback is not
 *                            desired.
 * \param  error_callback     See FLAC__StreamDecoderErrorCallback.  This
 *                            pointer must not be \c NULL.
 * \param  client_data        This value will be supplied to callbacks in their
 *                            \a client_data argument.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval FLAC__StreamDecoderInitStatus
 *    \c FLAC__STREAM_DECODER_INIT_STATUS_OK if initialization was successful;
 *    see FLAC__StreamDecoderInitStatus for the meanings of other return values.
 */
FLAC_API FLAC__StreamDecoderInitStatus FLAC__stream_decoder_init_ogg_file(
	FLAC__StreamDecoder *decoder,
	const char *filename,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data
);

/** Finish the decoding process.
 *  Flushes the decoding buffer, releases resources, resets the decoder
 *  settings to their defaults, and returns the decoder state to
 *  FLAC__STREAM_DECODER_UNINITIALIZED.
 *
 *  In the event of a prematurely-terminated decode, it is not strictly
 *  necessary to call this immediately before FLAC__stream_decoder_delete()
 *  but it is good practice to match every FLAC__stream_decoder_init_*()
 *  with a FLAC__stream_decoder_finish().
 *
 * \param  decoder  An uninitialized decoder instance.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if MD5 checking is on AND a STREAMINFO block was available
 *    AND the MD5 signature in the STREAMINFO block was non-zero AND the
 *    signature does not match the one computed by the decoder; else
 *    \c true.
 */
FLAC_API FLAC__bool FLAC__stream_decoder_finish(FLAC__StreamDecoder *decoder);

/** Flush the stream input.
 *  The decoder's input buffer will be cleared and the state set to
 *  \c FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC.  This will also turn
 *  off MD5 checking.
 *
 * \param  decoder  A decoder instance.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval FLAC__bool
 *    \c true if successful, else \c false if a memory allocation
 *    error occurs (in which case the state will be set to
 *    \c FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR).
 */
FLAC_API FLAC__bool FLAC__stream_decoder_flush(FLAC__StreamDecoder *decoder);

/** Reset the decoding process.
 *  The decoder's input buffer will be cleared and the state set to
 *  \c FLAC__STREAM_DECODER_SEARCH_FOR_METADATA.  This is similar to
 *  FLAC__stream_decoder_finish() except that the settings are
 *  preserved; there is no need to call FLAC__stream_decoder_init_*()
 *  before decoding again.  MD5 checking will be restored to its original
 *  setting.
 *
 *  If the decoder is seekable, or was initialized with
 *  FLAC__stream_decoder_init*_FILE() or FLAC__stream_decoder_init*_file(),
 *  the decoder will also attempt to seek to the beginning of the file.
 *  If this rewind fails, this function will return \c false.  It follows
 *  that FLAC__stream_decoder_reset() cannot be used when decoding from
 *  \c stdin.
 *
 *  If the decoder was initialized with FLAC__stream_encoder_init*_stream()
 *  and is not seekable (i.e. no seek callback was provided or the seek
 *  callback returns \c FLAC__STREAM_DECODER_SEEK_STATUS_UNSUPPORTED), it
 *  is the duty of the client to start feeding data from the beginning of
 *  the stream on the next FLAC__stream_decoder_process() or
 *  FLAC__stream_decoder_process_interleaved() call.
 *
 * \param  decoder  A decoder instance.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval FLAC__bool
 *    \c true if successful, else \c false if a memory allocation occurs
 *    (in which case the state will be set to
 *    \c FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR) or a seek error
 *    occurs (the state will be unchanged).
 */
FLAC_API FLAC__bool FLAC__stream_decoder_reset(FLAC__StreamDecoder *decoder);

/** Decode one metadata block or audio frame.
 *  This version instructs the decoder to decode a either a single metadata
 *  block or a single frame and stop, unless the callbacks return a fatal
 *  error or the read callback returns
 *  \c FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM.
 *
 *  As the decoder needs more input it will call the read callback.
 *  Depending on what was decoded, the metadata or write callback will be
 *  called with the decoded metadata block or audio frame.
 *
 *  Unless there is a fatal read error or end of stream, this function
 *  will return once one whole frame is decoded.  In other words, if the
 *  stream is not synchronized or points to a corrupt frame header, the
 *  decoder will continue to try and resync until it gets to a valid
 *  frame, then decode one frame, then return.  If the decoder points to
 *  a frame whose frame CRC in the frame footer does not match the
 *  computed frame CRC, this function will issue a
 *  FLAC__STREAM_DECODER_ERROR_STATUS_FRAME_CRC_MISMATCH error to the
 *  error callback, and return, having decoded one complete, although
 *  corrupt, frame.  (Such corrupted frames are sent as silence of the
 *  correct length to the write callback.)
 *
 * \param  decoder  An initialized decoder instance.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if any fatal read, write, or memory allocation error
 *    occurred (meaning decoding must stop), else \c true; for more
 *    information about the decoder, check the decoder state with
 *    FLAC__stream_decoder_get_state().
 */
FLAC_API FLAC__bool FLAC__stream_decoder_process_single(FLAC__StreamDecoder *decoder);

/** Decode until the end of the metadata.
 *  This version instructs the decoder to decode from the current position
 *  and continue until all the metadata has been read, or until the
 *  callbacks return a fatal error or the read callback returns
 *  \c FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM.
 *
 *  As the decoder needs more input it will call the read callback.
 *  As each metadata block is decoded, the metadata callback will be called
 *  with the decoded metadata.
 *
 * \param  decoder  An initialized decoder instance.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if any fatal read, write, or memory allocation error
 *    occurred (meaning decoding must stop), else \c true; for more
 *    information about the decoder, check the decoder state with
 *    FLAC__stream_decoder_get_state().
 */
FLAC_API FLAC__bool FLAC__stream_decoder_process_until_end_of_metadata(FLAC__StreamDecoder *decoder);

/** Decode until the end of the stream.
 *  This version instructs the decoder to decode from the current position
 *  and continue until the end of stream (the read callback returns
 *  \c FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM), or until the
 *  callbacks return a fatal error.
 *
 *  As the decoder needs more input it will call the read callback.
 *  As each metadata block and frame is decoded, the metadata or write
 *  callback will be called with the decoded metadata or frame.
 *
 * \param  decoder  An initialized decoder instance.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if any fatal read, write, or memory allocation error
 *    occurred (meaning decoding must stop), else \c true; for more
 *    information about the decoder, check the decoder state with
 *    FLAC__stream_decoder_get_state().
 */
FLAC_API FLAC__bool FLAC__stream_decoder_process_until_end_of_stream(FLAC__StreamDecoder *decoder);

/** Skip one audio frame.
 *  This version instructs the decoder to 'skip' a single frame and stop,
 *  unless the callbacks return a fatal error or the read callback returns
 *  \c FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM.
 *
 *  The decoding flow is the same as what occurs when
 *  FLAC__stream_decoder_process_single() is called to process an audio
 *  frame, except that this function does not decode the parsed data into
 *  PCM or call the write callback.  The integrity of the frame is still
 *  checked the same way as in the other process functions.
 *
 *  This function will return once one whole frame is skipped, in the
 *  same way that FLAC__stream_decoder_process_single() will return once
 *  one whole frame is decoded.
 *
 *  This function can be used in more quickly determining FLAC frame
 *  boundaries when decoding of the actual data is not needed, for
 *  example when an application is separating a FLAC stream into frames
 *  for editing or storing in a container.  To do this, the application
 *  can use FLAC__stream_decoder_skip_single_frame() to quickly advance
 *  to the next frame, then use
 *  FLAC__stream_decoder_get_decode_position() to find the new frame
 *  boundary.
 *
 *  This function should only be called when the stream has advanced
 *  past all the metadata, otherwise it will return \c false.
 *
 * \param  decoder  An initialized decoder instance not in a metadata
 *                  state.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if any fatal read, write, or memory allocation error
 *    occurred (meaning decoding must stop), or if the decoder
 *    is in the FLAC__STREAM_DECODER_SEARCH_FOR_METADATA or
 *    FLAC__STREAM_DECODER_READ_METADATA state, else \c true; for more
 *    information about the decoder, check the decoder state with
 *    FLAC__stream_decoder_get_state().
 */
FLAC_API FLAC__bool FLAC__stream_decoder_skip_single_frame(FLAC__StreamDecoder *decoder);

/** Flush the input and seek to an absolute sample.
 *  Decoding will resume at the given sample.  Note that because of
 *  this, the next write callback may contain a partial block.  The
 *  client must support seeking the input or this function will fail
 *  and return \c false.  Furthermore, if the decoder state is
 *  \c FLAC__STREAM_DECODER_SEEK_ERROR, then the decoder must be flushed
 *  with FLAC__stream_decoder_flush() or reset with
 *  FLAC__stream_decoder_reset() before decoding can continue.
 *
 * \param  decoder  A decoder instance.
 * \param  sample   The target sample number to seek to.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval FLAC__bool
 *    \c true if successful, else \c false.
 */
FLAC_API FLAC__bool FLAC__stream_decoder_seek_absolute(FLAC__StreamDecoder *decoder, FLAC__uint64 sample);

/* \} */

#ifdef __cplusplus
}
#endif

#endif
#endif //++-- stream_decoder.h 


#if 1 //++ stream_encoder.h

#ifndef FLAC__STREAM_ENCODER_H
#define FLAC__STREAM_ENCODER_H

#ifdef __cplusplus
extern "C" {
#endif


/** \file include/FLAC/stream_encoder.h
 *
 *  \brief
 *  This module contains the functions which implement the stream
 *  encoder.
 *
 *  See the detailed documentation in the
 *  \link flac_stream_encoder stream encoder \endlink module.
 */

/** \defgroup flac_encoder FLAC/ \*_encoder.h: encoder interfaces
 *  \ingroup flac
 *
 *  \brief
 *  This module describes the encoder layers provided by libFLAC.
 *
 * The stream encoder can be used to encode complete streams either to the
 * client via callbacks, or directly to a file, depending on how it is
 * initialized.  When encoding via callbacks, the client provides a write
 * callback which will be called whenever FLAC data is ready to be written.
 * If the client also supplies a seek callback, the encoder will also
 * automatically handle the writing back of metadata discovered while
 * encoding, like stream info, seek points offsets, etc.  When encoding to
 * a file, the client needs only supply a filename or open \c FILE* and an
 * optional progress callback for periodic notification of progress; the
 * write and seek callbacks are supplied internally.  For more info see the
 * \link flac_stream_encoder stream encoder \endlink module.
 */

/** \defgroup flac_stream_encoder FLAC/stream_encoder.h: stream encoder interface
 *  \ingroup flac_encoder
 *
 *  \brief
 *  This module contains the functions which implement the stream
 *  encoder.
 *
 * The stream encoder can encode to native FLAC, and optionally Ogg FLAC
 * (check FLAC_API_SUPPORTS_OGG_FLAC) streams and files.
 *
 * The basic usage of this encoder is as follows:
 * - The program creates an instance of an encoder using
 *   FLAC__stream_encoder_new().
 * - The program overrides the default settings using
 *   FLAC__stream_encoder_set_*() functions.  At a minimum, the following
 *   functions should be called:
 *   - FLAC__stream_encoder_set_channels()
 *   - FLAC__stream_encoder_set_bits_per_sample()
 *   - FLAC__stream_encoder_set_sample_rate()
 *   - FLAC__stream_encoder_set_ogg_serial_number() (if encoding to Ogg FLAC)
 *   - FLAC__stream_encoder_set_total_samples_estimate() (if known)
 * - If the application wants to control the compression level or set its own
 *   metadata, then the following should also be called:
 *   - FLAC__stream_encoder_set_compression_level()
 *   - FLAC__stream_encoder_set_verify()
 *   - FLAC__stream_encoder_set_metadata()
 * - The rest of the set functions should only be called if the client needs
 *   exact control over how the audio is compressed; thorough understanding
 *   of the FLAC format is necessary to achieve good results.
 * - The program initializes the instance to validate the settings and
 *   prepare for encoding using
 *   - FLAC__stream_encoder_init_stream() or FLAC__stream_encoder_init_FILE()
 *     or FLAC__stream_encoder_init_file() for native FLAC
 *   - FLAC__stream_encoder_init_ogg_stream() or FLAC__stream_encoder_init_ogg_FILE()
 *     or FLAC__stream_encoder_init_ogg_file() for Ogg FLAC
 * - The program calls FLAC__stream_encoder_process() or
 *   FLAC__stream_encoder_process_interleaved() to encode data, which
 *   subsequently calls the callbacks when there is encoder data ready
 *   to be written.
 * - The program finishes the encoding with FLAC__stream_encoder_finish(),
 *   which causes the encoder to encode any data still in its input pipe,
 *   update the metadata with the final encoding statistics if output
 *   seeking is possible, and finally reset the encoder to the
 *   uninitialized state.
 * - The instance may be used again or deleted with
 *   FLAC__stream_encoder_delete().
 *
 * In more detail, the stream encoder functions similarly to the
 * \link flac_stream_decoder stream decoder \endlink, but has fewer
 * callbacks and more options.  Typically the client will create a new
 * instance by calling FLAC__stream_encoder_new(), then set the necessary
 * parameters with FLAC__stream_encoder_set_*(), and initialize it by
 * calling one of the FLAC__stream_encoder_init_*() functions.
 *
 * Unlike the decoders, the stream encoder has many options that can
 * affect the speed and compression ratio.  When setting these parameters
 * you should have some basic knowledge of the format (see the
 * <A HREF="../documentation.html#format">user-level documentation</A>
 * or the <A HREF="../format.html">formal description</A>).  The
 * FLAC__stream_encoder_set_*() functions themselves do not validate the
 * values as many are interdependent.  The FLAC__stream_encoder_init_*()
 * functions will do this, so make sure to pay attention to the state
 * returned by FLAC__stream_encoder_init_*() to make sure that it is
 * FLAC__STREAM_ENCODER_INIT_STATUS_OK.  Any parameters that are not set
 * before FLAC__stream_encoder_init_*() will take on the defaults from
 * the constructor.
 *
 * There are three initialization functions for native FLAC, one for
 * setting up the encoder to encode FLAC data to the client via
 * callbacks, and two for encoding directly to a file.
 *
 * For encoding via callbacks, use FLAC__stream_encoder_init_stream().
 * You must also supply a write callback which will be called anytime
 * there is raw encoded data to write.  If the client can seek the output
 * it is best to also supply seek and tell callbacks, as this allows the
 * encoder to go back after encoding is finished to write back
 * information that was collected while encoding, like seek point offsets,
 * frame sizes, etc.
 *
 * For encoding directly to a file, use FLAC__stream_encoder_init_FILE()
 * or FLAC__stream_encoder_init_file().  Then you must only supply a
 * filename or open \c FILE*; the encoder will handle all the callbacks
 * internally.  You may also supply a progress callback for periodic
 * notification of the encoding progress.
 *
 * There are three similarly-named init functions for encoding to Ogg
 * FLAC streams.  Check \c FLAC_API_SUPPORTS_OGG_FLAC to find out if the
 * library has been built with Ogg support.
 *
 * The call to FLAC__stream_encoder_init_*() currently will also immediately
 * call the write callback several times, once with the \c fLaC signature,
 * and once for each encoded metadata block.  Note that for Ogg FLAC
 * encoding you will usually get at least twice the number of callbacks than
 * with native FLAC, one for the Ogg page header and one for the page body.
 *
 * After initializing the instance, the client may feed audio data to the
 * encoder in one of two ways:
 *
 * - Channel separate, through FLAC__stream_encoder_process() - The client
 *   will pass an array of pointers to buffers, one for each channel, to
 *   the encoder, each of the same length.  The samples need not be
 *   block-aligned, but each channel should have the same number of samples.
 * - Channel interleaved, through
 *   FLAC__stream_encoder_process_interleaved() - The client will pass a single
 *   pointer to data that is channel-interleaved (i.e. channel0_sample0,
 *   channel1_sample0, ... , channelN_sample0, channel0_sample1, ...).
 *   Again, the samples need not be block-aligned but they must be
 *   sample-aligned, i.e. the first value should be channel0_sample0 and
 *   the last value channelN_sampleM.
 *
 * Note that for either process call, each sample in the buffers should be a
 * signed integer, right-justified to the resolution set by
 * FLAC__stream_encoder_set_bits_per_sample().  For example, if the resolution
 * is 16 bits per sample, the samples should all be in the range [-32768,32767].
 *
 * When the client is finished encoding data, it calls
 * FLAC__stream_encoder_finish(), which causes the encoder to encode any
 * data still in its input pipe, and call the metadata callback with the
 * final encoding statistics.  Then the instance may be deleted with
 * FLAC__stream_encoder_delete() or initialized again to encode another
 * stream.
 *
 * For programs that write their own metadata, but that do not know the
 * actual metadata until after encoding, it is advantageous to instruct
 * the encoder to write a PADDING block of the correct size, so that
 * instead of rewriting the whole stream after encoding, the program can
 * just overwrite the PADDING block.  If only the maximum size of the
 * metadata is known, the program can write a slightly larger padding
 * block, then split it after encoding.
 *
 * Make sure you understand how lengths are calculated.  All FLAC metadata
 * blocks have a 4 byte header which contains the type and length.  This
 * length does not include the 4 bytes of the header.  See the format page
 * for the specification of metadata blocks and their lengths.
 *
 * \note
 * If you are writing the FLAC data to a file via callbacks, make sure it
 * is open for update (e.g. mode "w+" for stdio streams).  This is because
 * after the first encoding pass, the encoder will try to seek back to the
 * beginning of the stream, to the STREAMINFO block, to write some data
 * there.  (If using FLAC__stream_encoder_init*_file() or
 * FLAC__stream_encoder_init*_FILE(), the file is managed internally.)
 *
 * \note
 * The "set" functions may only be called when the encoder is in the
 * state FLAC__STREAM_ENCODER_UNINITIALIZED, i.e. after
 * FLAC__stream_encoder_new() or FLAC__stream_encoder_finish(), but
 * before FLAC__stream_encoder_init_*().  If this is the case they will
 * return \c true, otherwise \c false.
 *
 * \note
 * FLAC__stream_encoder_finish() resets all settings to the constructor
 * defaults.
 *
 * \{
 */


/** State values for a FLAC__StreamEncoder.
 *
 * The encoder's state can be obtained by calling FLAC__stream_encoder_get_state().
 *
 * If the encoder gets into any other state besides \c FLAC__STREAM_ENCODER_OK
 * or \c FLAC__STREAM_ENCODER_UNINITIALIZED, it becomes invalid for encoding and
 * must be deleted with FLAC__stream_encoder_delete().
 */
typedef enum {

	FLAC__STREAM_ENCODER_OK = 0,
	/**< The encoder is in the normal OK state and samples can be processed. */

	FLAC__STREAM_ENCODER_UNINITIALIZED,
	/**< The encoder is in the uninitialized state; one of the
	 * FLAC__stream_encoder_init_*() functions must be called before samples
	 * can be processed.
	 */

	FLAC__STREAM_ENCODER_OGG_ERROR,
	/**< An error occurred in the underlying Ogg layer.  */

	FLAC__STREAM_ENCODER_VERIFY_DECODER_ERROR,
	/**< An error occurred in the underlying verify stream decoder;
	 * check FLAC__stream_encoder_get_verify_decoder_state().
	 */

	FLAC__STREAM_ENCODER_VERIFY_MISMATCH_IN_AUDIO_DATA,
	/**< The verify decoder detected a mismatch between the original
	 * audio signal and the decoded audio signal.
	 */

	FLAC__STREAM_ENCODER_CLIENT_ERROR,
	/**< One of the callbacks returned a fatal error. */

	FLAC__STREAM_ENCODER_IO_ERROR,
	/**< An I/O error occurred while opening/reading/writing a file.
	 * Check \c errno.
	 */

	FLAC__STREAM_ENCODER_FRAMING_ERROR,
	/**< An error occurred while writing the stream; usually, the
	 * write_callback returned an error.
	 */

	FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR
	/**< Memory allocation failed. */

} FLAC__StreamEncoderState;

/** Maps a FLAC__StreamEncoderState to a C string.
 *
 *  Using a FLAC__StreamEncoderState as the index to this array
 *  will give the string equivalent.  The contents should not be modified.
 */
extern FLAC_API const char * const FLAC__StreamEncoderStateString[];


/** Possible return values for the FLAC__stream_encoder_init_*() functions.
 */
typedef enum {

	FLAC__STREAM_ENCODER_INIT_STATUS_OK = 0,
	/**< Initialization was successful. */

	FLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR,
	/**< General failure to set up encoder; call FLAC__stream_encoder_get_state() for cause. */

	FLAC__STREAM_ENCODER_INIT_STATUS_UNSUPPORTED_CONTAINER,
	/**< The library was not compiled with support for the given container
	 * format.
	 */

	FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_CALLBACKS,
	/**< A required callback was not supplied. */

	FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_NUMBER_OF_CHANNELS,
	/**< The encoder has an invalid setting for number of channels. */

	FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_BITS_PER_SAMPLE,
	/**< The encoder has an invalid setting for bits-per-sample.
	 * FLAC supports 4-32 bps but the reference encoder currently supports
	 * only up to 24 bps.
	 */

	FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_SAMPLE_RATE,
	/**< The encoder has an invalid setting for the input sample rate. */

	FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_BLOCK_SIZE,
	/**< The encoder has an invalid setting for the block size. */

	FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_MAX_LPC_ORDER,
	/**< The encoder has an invalid setting for the maximum LPC order. */

	FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_QLP_COEFF_PRECISION,
	/**< The encoder has an invalid setting for the precision of the quantized linear predictor coefficients. */

	FLAC__STREAM_ENCODER_INIT_STATUS_BLOCK_SIZE_TOO_SMALL_FOR_LPC_ORDER,
	/**< The specified block size is less than the maximum LPC order. */

	FLAC__STREAM_ENCODER_INIT_STATUS_NOT_STREAMABLE,
	/**< The encoder is bound to the <A HREF="../format.html#subset">Subset</A> but other settings violate it. */

	FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_METADATA,
	/**< The metadata input to the encoder is invalid, in one of the following ways:
	 * - FLAC__stream_encoder_set_metadata() was called with a null pointer but a block count > 0
	 * - One of the metadata blocks contains an undefined type
	 * - It contains an illegal CUESHEET as checked by FLAC__format_cuesheet_is_legal()
	 * - It contains an illegal SEEKTABLE as checked by FLAC__format_seektable_is_legal()
	 * - It contains more than one SEEKTABLE block or more than one VORBIS_COMMENT block
	 */

	FLAC__STREAM_ENCODER_INIT_STATUS_ALREADY_INITIALIZED
	/**< FLAC__stream_encoder_init_*() was called when the encoder was
	 * already initialized, usually because
	 * FLAC__stream_encoder_finish() was not called.
	 */

} FLAC__StreamEncoderInitStatus;

/** Maps a FLAC__StreamEncoderInitStatus to a C string.
 *
 *  Using a FLAC__StreamEncoderInitStatus as the index to this array
 *  will give the string equivalent.  The contents should not be modified.
 */
extern FLAC_API const char * const FLAC__StreamEncoderInitStatusString[];


/** Return values for the FLAC__StreamEncoder read callback.
 */
typedef enum {

	FLAC__STREAM_ENCODER_READ_STATUS_CONTINUE,
	/**< The read was OK and decoding can continue. */

	FLAC__STREAM_ENCODER_READ_STATUS_END_OF_STREAM,
	/**< The read was attempted at the end of the stream. */

	FLAC__STREAM_ENCODER_READ_STATUS_ABORT,
	/**< An unrecoverable error occurred. */

	FLAC__STREAM_ENCODER_READ_STATUS_UNSUPPORTED
	/**< Client does not support reading back from the output. */

} FLAC__StreamEncoderReadStatus;

/** Maps a FLAC__StreamEncoderReadStatus to a C string.
 *
 *  Using a FLAC__StreamEncoderReadStatus as the index to this array
 *  will give the string equivalent.  The contents should not be modified.
 */
extern FLAC_API const char * const FLAC__StreamEncoderReadStatusString[];


/** Return values for the FLAC__StreamEncoder write callback.
 */
typedef enum {

	FLAC__STREAM_ENCODER_WRITE_STATUS_OK = 0,
	/**< The write was OK and encoding can continue. */

	FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR
	/**< An unrecoverable error occurred.  The encoder will return from the process call. */

} FLAC__StreamEncoderWriteStatus;

/** Maps a FLAC__StreamEncoderWriteStatus to a C string.
 *
 *  Using a FLAC__StreamEncoderWriteStatus as the index to this array
 *  will give the string equivalent.  The contents should not be modified.
 */
extern FLAC_API const char * const FLAC__StreamEncoderWriteStatusString[];


/** Return values for the FLAC__StreamEncoder seek callback.
 */
typedef enum {

	FLAC__STREAM_ENCODER_SEEK_STATUS_OK,
	/**< The seek was OK and encoding can continue. */

	FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR,
	/**< An unrecoverable error occurred. */

	FLAC__STREAM_ENCODER_SEEK_STATUS_UNSUPPORTED
	/**< Client does not support seeking. */

} FLAC__StreamEncoderSeekStatus;

/** Maps a FLAC__StreamEncoderSeekStatus to a C string.
 *
 *  Using a FLAC__StreamEncoderSeekStatus as the index to this array
 *  will give the string equivalent.  The contents should not be modified.
 */
extern FLAC_API const char * const FLAC__StreamEncoderSeekStatusString[];


/** Return values for the FLAC__StreamEncoder tell callback.
 */
typedef enum {

	FLAC__STREAM_ENCODER_TELL_STATUS_OK,
	/**< The tell was OK and encoding can continue. */

	FLAC__STREAM_ENCODER_TELL_STATUS_ERROR,
	/**< An unrecoverable error occurred. */

	FLAC__STREAM_ENCODER_TELL_STATUS_UNSUPPORTED
	/**< Client does not support seeking. */

} FLAC__StreamEncoderTellStatus;

/** Maps a FLAC__StreamEncoderTellStatus to a C string.
 *
 *  Using a FLAC__StreamEncoderTellStatus as the index to this array
 *  will give the string equivalent.  The contents should not be modified.
 */
extern FLAC_API const char * const FLAC__StreamEncoderTellStatusString[];


/***********************************************************************
 *
 * class FLAC__StreamEncoder
 *
 ***********************************************************************/

struct FLAC__StreamEncoderProtected;
struct FLAC__StreamEncoderPrivate;
/** The opaque structure definition for the stream encoder type.
 *  See the \link flac_stream_encoder stream encoder module \endlink
 *  for a detailed description.
 */
typedef struct {
	struct FLAC__StreamEncoderProtected *protected_; /* avoid the C++ keyword 'protected' */
	struct FLAC__StreamEncoderPrivate *private_; /* avoid the C++ keyword 'private' */
} FLAC__StreamEncoder;

/** Signature for the read callback.
 *
 *  A function pointer matching this signature must be passed to
 *  FLAC__stream_encoder_init_ogg_stream() if seeking is supported.
 *  The supplied function will be called when the encoder needs to read back
 *  encoded data.  This happens during the metadata callback, when the encoder
 *  has to read, modify, and rewrite the metadata (e.g. seekpoints) gathered
 *  while encoding.  The address of the buffer to be filled is supplied, along
 *  with the number of bytes the buffer can hold.  The callback may choose to
 *  supply less data and modify the byte count but must be careful not to
 *  overflow the buffer.  The callback then returns a status code chosen from
 *  FLAC__StreamEncoderReadStatus.
 *
 * Here is an example of a read callback for stdio streams:
 * \code
 * FLAC__StreamEncoderReadStatus read_cb(const FLAC__StreamEncoder *encoder, FLAC__byte buffer[], size_t *bytes, void *client_data)
 * {
 *   FILE *file = ((MyClientData*)client_data)->file;
 *   if(*bytes > 0) {
 *     *bytes = fread(buffer, sizeof(FLAC__byte), *bytes, file);
 *     if(ferror(file))
 *       return FLAC__STREAM_ENCODER_READ_STATUS_ABORT;
 *     else if(*bytes == 0)
 *       return FLAC__STREAM_ENCODER_READ_STATUS_END_OF_STREAM;
 *     else
 *       return FLAC__STREAM_ENCODER_READ_STATUS_CONTINUE;
 *   }
 *   else
 *     return FLAC__STREAM_ENCODER_READ_STATUS_ABORT;
 * }
 * \endcode
 *
 * \note In general, FLAC__StreamEncoder functions which change the
 * state should not be called on the \a encoder while in the callback.
 *
 * \param  encoder  The encoder instance calling the callback.
 * \param  buffer   A pointer to a location for the callee to store
 *                  data to be encoded.
 * \param  bytes    A pointer to the size of the buffer.  On entry
 *                  to the callback, it contains the maximum number
 *                  of bytes that may be stored in \a buffer.  The
 *                  callee must set it to the actual number of bytes
 *                  stored (0 in case of error or end-of-stream) before
 *                  returning.
 * \param  client_data  The callee's client data set through
 *                      FLAC__stream_encoder_set_client_data().
 * \retval FLAC__StreamEncoderReadStatus
 *    The callee's return status.
 */
typedef FLAC__StreamEncoderReadStatus (*FLAC__StreamEncoderReadCallback)(const FLAC__StreamEncoder *encoder, FLAC__byte buffer[], size_t *bytes, void *client_data);

/** Signature for the write callback.
 *
 *  A function pointer matching this signature must be passed to
 *  FLAC__stream_encoder_init*_stream().  The supplied function will be called
 *  by the encoder anytime there is raw encoded data ready to write.  It may
 *  include metadata mixed with encoded audio frames and the data is not
 *  guaranteed to be aligned on frame or metadata block boundaries.
 *
 *  The only duty of the callback is to write out the \a bytes worth of data
 *  in \a buffer to the current position in the output stream.  The arguments
 *  \a samples and \a current_frame are purely informational.  If \a samples
 *  is greater than \c 0, then \a current_frame will hold the current frame
 *  number that is being written; otherwise it indicates that the write
 *  callback is being called to write metadata.
 *
 * \note
 * Unlike when writing to native FLAC, when writing to Ogg FLAC the
 * write callback will be called twice when writing each audio
 * frame; once for the page header, and once for the page body.
 * When writing the page header, the \a samples argument to the
 * write callback will be \c 0.
 *
 * \note In general, FLAC__StreamEncoder functions which change the
 * state should not be called on the \a encoder while in the callback.
 *
 * \param  encoder  The encoder instance calling the callback.
 * \param  buffer   An array of encoded data of length \a bytes.
 * \param  bytes    The byte length of \a buffer.
 * \param  samples  The number of samples encoded by \a buffer.
 *                  \c 0 has a special meaning; see above.
 * \param  current_frame  The number of the current frame being encoded.
 * \param  client_data  The callee's client data set through
 *                      FLAC__stream_encoder_init_*().
 * \retval FLAC__StreamEncoderWriteStatus
 *    The callee's return status.
 */
typedef FLAC__StreamEncoderWriteStatus (*FLAC__StreamEncoderWriteCallback)(const FLAC__StreamEncoder *encoder, const FLAC__byte buffer[], size_t bytes, unsigned samples, unsigned current_frame, void *client_data);

/** Signature for the seek callback.
 *
 *  A function pointer matching this signature may be passed to
 *  FLAC__stream_encoder_init*_stream().  The supplied function will be called
 *  when the encoder needs to seek the output stream.  The encoder will pass
 *  the absolute byte offset to seek to, 0 meaning the beginning of the stream.
 *
 * Here is an example of a seek callback for stdio streams:
 * \code
 * FLAC__StreamEncoderSeekStatus seek_cb(const FLAC__StreamEncoder *encoder, FLAC__uint64 absolute_byte_offset, void *client_data)
 * {
 *   FILE *file = ((MyClientData*)client_data)->file;
 *   if(file == stdin)
 *     return FLAC__STREAM_ENCODER_SEEK_STATUS_UNSUPPORTED;
 *   else if(fseeko(file, (off_t)absolute_byte_offset, SEEK_SET) < 0)
 *     return FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR;
 *   else
 *     return FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
 * }
 * \endcode
 *
 * \note In general, FLAC__StreamEncoder functions which change the
 * state should not be called on the \a encoder while in the callback.
 *
 * \param  encoder  The encoder instance calling the callback.
 * \param  absolute_byte_offset  The offset from the beginning of the stream
 *                               to seek to.
 * \param  client_data  The callee's client data set through
 *                      FLAC__stream_encoder_init_*().
 * \retval FLAC__StreamEncoderSeekStatus
 *    The callee's return status.
 */
typedef FLAC__StreamEncoderSeekStatus (*FLAC__StreamEncoderSeekCallback)(const FLAC__StreamEncoder *encoder, FLAC__uint64 absolute_byte_offset, void *client_data);

/** Signature for the tell callback.
 *
 *  A function pointer matching this signature may be passed to
 *  FLAC__stream_encoder_init*_stream().  The supplied function will be called
 *  when the encoder needs to know the current position of the output stream.
 *
 * \warning
 * The callback must return the true current byte offset of the output to
 * which the encoder is writing.  If you are buffering the output, make
 * sure and take this into account.  If you are writing directly to a
 * FILE* from your write callback, ftell() is sufficient.  If you are
 * writing directly to a file descriptor from your write callback, you
 * can use lseek(fd, SEEK_CUR, 0).  The encoder may later seek back to
 * these points to rewrite metadata after encoding.
 *
 * Here is an example of a tell callback for stdio streams:
 * \code
 * FLAC__StreamEncoderTellStatus tell_cb(const FLAC__StreamEncoder *encoder, FLAC__uint64 *absolute_byte_offset, void *client_data)
 * {
 *   FILE *file = ((MyClientData*)client_data)->file;
 *   off_t pos;
 *   if(file == stdin)
 *     return FLAC__STREAM_ENCODER_TELL_STATUS_UNSUPPORTED;
 *   else if((pos = ftello(file)) < 0)
 *     return FLAC__STREAM_ENCODER_TELL_STATUS_ERROR;
 *   else {
 *     *absolute_byte_offset = (FLAC__uint64)pos;
 *     return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
 *   }
 * }
 * \endcode
 *
 * \note In general, FLAC__StreamEncoder functions which change the
 * state should not be called on the \a encoder while in the callback.
 *
 * \param  encoder  The encoder instance calling the callback.
 * \param  absolute_byte_offset  The address at which to store the current
 *                               position of the output.
 * \param  client_data  The callee's client data set through
 *                      FLAC__stream_encoder_init_*().
 * \retval FLAC__StreamEncoderTellStatus
 *    The callee's return status.
 */
typedef FLAC__StreamEncoderTellStatus (*FLAC__StreamEncoderTellCallback)(const FLAC__StreamEncoder *encoder, FLAC__uint64 *absolute_byte_offset, void *client_data);

/** Signature for the metadata callback.
 *
 *  A function pointer matching this signature may be passed to
 *  FLAC__stream_encoder_init*_stream().  The supplied function will be called
 *  once at the end of encoding with the populated STREAMINFO structure.  This
 *  is so the client can seek back to the beginning of the file and write the
 *  STREAMINFO block with the correct statistics after encoding (like
 *  minimum/maximum frame size and total samples).
 *
 * \note In general, FLAC__StreamEncoder functions which change the
 * state should not be called on the \a encoder while in the callback.
 *
 * \param  encoder      The encoder instance calling the callback.
 * \param  metadata     The final populated STREAMINFO block.
 * \param  client_data  The callee's client data set through
 *                      FLAC__stream_encoder_init_*().
 */
typedef void (*FLAC__StreamEncoderMetadataCallback)(const FLAC__StreamEncoder *encoder, const FLAC__StreamMetadata *metadata, void *client_data);

/** Signature for the progress callback.
 *
 *  A function pointer matching this signature may be passed to
 *  FLAC__stream_encoder_init*_file() or FLAC__stream_encoder_init*_FILE().
 *  The supplied function will be called when the encoder has finished
 *  writing a frame.  The \c total_frames_estimate argument to the
 *  callback will be based on the value from
 *  FLAC__stream_encoder_set_total_samples_estimate().
 *
 * \note In general, FLAC__StreamEncoder functions which change the
 * state should not be called on the \a encoder while in the callback.
 *
 * \param  encoder          The encoder instance calling the callback.
 * \param  bytes_written    Bytes written so far.
 * \param  samples_written  Samples written so far.
 * \param  frames_written   Frames written so far.
 * \param  total_frames_estimate  The estimate of the total number of
 *                                frames to be written.
 * \param  client_data      The callee's client data set through
 *                          FLAC__stream_encoder_init_*().
 */
typedef void (*FLAC__StreamEncoderProgressCallback)(const FLAC__StreamEncoder *encoder, FLAC__uint64 bytes_written, FLAC__uint64 samples_written, unsigned frames_written, unsigned total_frames_estimate, void *client_data);


/***********************************************************************
 *
 * Class constructor/destructor
 *
 ***********************************************************************/

/** Create a new stream encoder instance.  The instance is created with
 *  default settings; see the individual FLAC__stream_encoder_set_*()
 *  functions for each setting's default.
 *
 * \retval FLAC__StreamEncoder*
 *    \c NULL if there was an error allocating memory, else the new instance.
 */
FLAC_API FLAC__StreamEncoder *FLAC__stream_encoder_new(void);

/** Free an encoder instance.  Deletes the object pointed to by \a encoder.
 *
 * \param encoder  A pointer to an existing encoder.
 * \assert
 *    \code encoder != NULL \endcode
 */
FLAC_API void FLAC__stream_encoder_delete(FLAC__StreamEncoder *encoder);


/***********************************************************************
 *
 * Public class method prototypes
 *
 ***********************************************************************/

/** Set the serial number for the FLAC stream to use in the Ogg container.
 *
 * \note
 * This does not need to be set for native FLAC encoding.
 *
 * \note
 * It is recommended to set a serial number explicitly as the default of '0'
 * may collide with other streams.
 *
 * \default \c 0
 * \param  encoder        An encoder instance to set.
 * \param  serial_number  See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the encoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_set_ogg_serial_number(FLAC__StreamEncoder *encoder, long serial_number);

/** Set the "verify" flag.  If \c true, the encoder will verify it's own
 *  encoded output by feeding it through an internal decoder and comparing
 *  the original signal against the decoded signal.  If a mismatch occurs,
 *  the process call will return \c false.  Note that this will slow the
 *  encoding process by the extra time required for decoding and comparison.
 *
 * \default \c false
 * \param  encoder  An encoder instance to set.
 * \param  value    Flag value (see above).
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the encoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_set_verify(FLAC__StreamEncoder *encoder, FLAC__bool value);

/** Set the <A HREF="../format.html#subset">Subset</A> flag.  If \c true,
 *  the encoder will comply with the Subset and will check the
 *  settings during FLAC__stream_encoder_init_*() to see if all settings
 *  comply.  If \c false, the settings may take advantage of the full
 *  range that the format allows.
 *
 *  Make sure you know what it entails before setting this to \c false.
 *
 * \default \c true
 * \param  encoder  An encoder instance to set.
 * \param  value    Flag value (see above).
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the encoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_set_streamable_subset(FLAC__StreamEncoder *encoder, FLAC__bool value);

/** Set the number of channels to be encoded.
 *
 * \default \c 2
 * \param  encoder  An encoder instance to set.
 * \param  value    See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the encoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_set_channels(FLAC__StreamEncoder *encoder, unsigned value);

/** Set the sample resolution of the input to be encoded.
 *
 * \warning
 * Do not feed the encoder data that is wider than the value you
 * set here or you will generate an invalid stream.
 *
 * \default \c 16
 * \param  encoder  An encoder instance to set.
 * \param  value    See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the encoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_set_bits_per_sample(FLAC__StreamEncoder *encoder, unsigned value);

/** Set the sample rate (in Hz) of the input to be encoded.
 *
 * \default \c 44100
 * \param  encoder  An encoder instance to set.
 * \param  value    See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the encoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_set_sample_rate(FLAC__StreamEncoder *encoder, unsigned value);

/** Set the compression level
 *
 * The compression level is roughly proportional to the amount of effort
 * the encoder expends to compress the file.  A higher level usually
 * means more computation but higher compression.  The default level is
 * suitable for most applications.
 *
 * Currently the levels range from \c 0 (fastest, least compression) to
 * \c 8 (slowest, most compression).  A value larger than \c 8 will be
 * treated as \c 8.
 *
 * This function automatically calls the following other \c _set_
 * functions with appropriate values, so the client does not need to
 * unless it specifically wants to override them:
 * - FLAC__stream_encoder_set_do_mid_side_stereo()
 * - FLAC__stream_encoder_set_loose_mid_side_stereo()
 * - FLAC__stream_encoder_set_apodization()
 * - FLAC__stream_encoder_set_max_lpc_order()
 * - FLAC__stream_encoder_set_qlp_coeff_precision()
 * - FLAC__stream_encoder_set_do_qlp_coeff_prec_search()
 * - FLAC__stream_encoder_set_do_escape_coding()
 * - FLAC__stream_encoder_set_do_exhaustive_model_search()
 * - FLAC__stream_encoder_set_min_residual_partition_order()
 * - FLAC__stream_encoder_set_max_residual_partition_order()
 * - FLAC__stream_encoder_set_rice_parameter_search_dist()
 *
 * The actual values set for each level are:
 * <table>
 * <tr>
 *  <td><b>level</b><td>
 *  <td>do mid-side stereo<td>
 *  <td>loose mid-side stereo<td>
 *  <td>apodization<td>
 *  <td>max lpc order<td>
 *  <td>qlp coeff precision<td>
 *  <td>qlp coeff prec search<td>
 *  <td>escape coding<td>
 *  <td>exhaustive model search<td>
 *  <td>min residual partition order<td>
 *  <td>max residual partition order<td>
 *  <td>rice parameter search dist<td>
 * </tr>
 * <tr>  <td><b>0</b><td>  <td>false<td>  <td>false<td>  <td>tukey(0.5)<td>  <td>0<td>   <td>0<td>  <td>false<td>  <td>false<td>  <td>false<td>  <td>0<td>  <td>3<td>  <td>0<td>  </tr>
 * <tr>  <td><b>1</b><td>  <td>true<td>   <td>true<td>   <td>tukey(0.5)<td>  <td>0<td>   <td>0<td>  <td>false<td>  <td>false<td>  <td>false<td>  <td>0<td>  <td>3<td>  <td>0<td>  </tr>
 * <tr>  <td><b>2</b><td>  <td>true<td>   <td>false<td>  <td>tukey(0.5)<td>  <td>0<td>   <td>0<td>  <td>false<td>  <td>false<td>  <td>false<td>  <td>0<td>  <td>3<td>  <td>0<td>  </tr>
 * <tr>  <td><b>3</b><td>  <td>false<td>  <td>false<td>  <td>tukey(0.5)<td>  <td>6<td>   <td>0<td>  <td>false<td>  <td>false<td>  <td>false<td>  <td>0<td>  <td>4<td>  <td>0<td>  </tr>
 * <tr>  <td><b>4</b><td>  <td>true<td>   <td>true<td>   <td>tukey(0.5)<td>  <td>8<td>   <td>0<td>  <td>false<td>  <td>false<td>  <td>false<td>  <td>0<td>  <td>4<td>  <td>0<td>  </tr>
 * <tr>  <td><b>5</b><td>  <td>true<td>   <td>false<td>  <td>tukey(0.5)<td>  <td>8<td>   <td>0<td>  <td>false<td>  <td>false<td>  <td>false<td>  <td>0<td>  <td>5<td>  <td>0<td>  </tr>
 * <tr>  <td><b>6</b><td>  <td>true<td>   <td>false<td>  <td>tukey(0.5)<td>  <td>8<td>   <td>0<td>  <td>false<td>  <td>false<td>  <td>false<td>  <td>0<td>  <td>6<td>  <td>0<td>  </tr>
 * <tr>  <td><b>7</b><td>  <td>true<td>   <td>false<td>  <td>tukey(0.5)<td>  <td>8<td>   <td>0<td>  <td>false<td>  <td>false<td>  <td>true<td>   <td>0<td>  <td>6<td>  <td>0<td>  </tr>
 * <tr>  <td><b>8</b><td>  <td>true<td>   <td>false<td>  <td>tukey(0.5)<td>  <td>12<td>  <td>0<td>  <td>false<td>  <td>false<td>  <td>true<td>   <td>0<td>  <td>6<td>  <td>0<td>  </tr>
 * </table>
 *
 * \default \c 5
 * \param  encoder  An encoder instance to set.
 * \param  value    See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the encoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_set_compression_level(FLAC__StreamEncoder *encoder, unsigned value);

/** Set the blocksize to use while encoding.
 *
 * The number of samples to use per frame.  Use \c 0 to let the encoder
 * estimate a blocksize; this is usually best.
 *
 * \default \c 0
 * \param  encoder  An encoder instance to set.
 * \param  value    See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the encoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_set_blocksize(FLAC__StreamEncoder *encoder, unsigned value);

/** Set to \c true to enable mid-side encoding on stereo input.  The
 *  number of channels must be 2 for this to have any effect.  Set to
 *  \c false to use only independent channel coding.
 *
 * \default \c false
 * \param  encoder  An encoder instance to set.
 * \param  value    Flag value (see above).
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the encoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_set_do_mid_side_stereo(FLAC__StreamEncoder *encoder, FLAC__bool value);

/** Set to \c true to enable adaptive switching between mid-side and
 *  left-right encoding on stereo input.  Set to \c false to use
 *  exhaustive searching.  Setting this to \c true requires
 *  FLAC__stream_encoder_set_do_mid_side_stereo() to also be set to
 *  \c true in order to have any effect.
 *
 * \default \c false
 * \param  encoder  An encoder instance to set.
 * \param  value    Flag value (see above).
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the encoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_set_loose_mid_side_stereo(FLAC__StreamEncoder *encoder, FLAC__bool value);

/** Sets the apodization function(s) the encoder will use when windowing
 *  audio data for LPC analysis.
 *
 * The \a specification is a plain ASCII string which specifies exactly
 * which functions to use.  There may be more than one (up to 32),
 * separated by \c ';' characters.  Some functions take one or more
 * comma-separated arguments in parentheses.
 *
 * The available functions are \c bartlett, \c bartlett_hann,
 * \c blackman, \c blackman_harris_4term_92db, \c connes, \c flattop,
 * \c gauss(STDDEV), \c hamming, \c hann, \c kaiser_bessel, \c nuttall,
 * \c rectangle, \c triangle, \c tukey(P), \c welch.
 *
 * For \c gauss(STDDEV), STDDEV specifies the standard deviation
 * (0<STDDEV<=0.5).
 *
 * For \c tukey(P), P specifies the fraction of the window that is
 * tapered (0<=P<=1).  P=0 corresponds to \c rectangle and P=1
 * corresponds to \c hann.
 *
 * Example specifications are \c "blackman" or
 * \c "hann;triangle;tukey(0.5);tukey(0.25);tukey(0.125)"
 *
 * Any function that is specified erroneously is silently dropped.  Up
 * to 32 functions are kept, the rest are dropped.  If the specification
 * is empty the encoder defaults to \c "tukey(0.5)".
 *
 * When more than one function is specified, then for every subframe the
 * encoder will try each of them separately and choose the window that
 * results in the smallest compressed subframe.
 *
 * Note that each function specified causes the encoder to occupy a
 * floating point array in which to store the window.
 *
 * \default \c "tukey(0.5)"
 * \param  encoder        An encoder instance to set.
 * \param  specification  See above.
 * \assert
 *    \code encoder != NULL \endcode
 *    \code specification != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the encoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_set_apodization(FLAC__StreamEncoder *encoder, const char *specification);

/** Set the maximum LPC order, or \c 0 to use only the fixed predictors.
 *
 * \default \c 0
 * \param  encoder  An encoder instance to set.
 * \param  value    See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the encoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_set_max_lpc_order(FLAC__StreamEncoder *encoder, unsigned value);

/** Set the precision, in bits, of the quantized linear predictor
 *  coefficients, or \c 0 to let the encoder select it based on the
 *  blocksize.
 *
 * \note
 * In the current implementation, qlp_coeff_precision + bits_per_sample must
 * be less than 32.
 *
 * \default \c 0
 * \param  encoder  An encoder instance to set.
 * \param  value    See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the encoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_set_qlp_coeff_precision(FLAC__StreamEncoder *encoder, unsigned value);

/** Set to \c false to use only the specified quantized linear predictor
 *  coefficient precision, or \c true to search neighboring precision
 *  values and use the best one.
 *
 * \default \c false
 * \param  encoder  An encoder instance to set.
 * \param  value    See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the encoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_set_do_qlp_coeff_prec_search(FLAC__StreamEncoder *encoder, FLAC__bool value);

/** Deprecated.  Setting this value has no effect.
 *
 * \default \c false
 * \param  encoder  An encoder instance to set.
 * \param  value    See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the encoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_set_do_escape_coding(FLAC__StreamEncoder *encoder, FLAC__bool value);

/** Set to \c false to let the encoder estimate the best model order
 *  based on the residual signal energy, or \c true to force the
 *  encoder to evaluate all order models and select the best.
 *
 * \default \c false
 * \param  encoder  An encoder instance to set.
 * \param  value    See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the encoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_set_do_exhaustive_model_search(FLAC__StreamEncoder *encoder, FLAC__bool value);

/** Set the minimum partition order to search when coding the residual.
 *  This is used in tandem with
 *  FLAC__stream_encoder_set_max_residual_partition_order().
 *
 *  The partition order determines the context size in the residual.
 *  The context size will be approximately <tt>blocksize / (2 ^ order)</tt>.
 *
 *  Set both min and max values to \c 0 to force a single context,
 *  whose Rice parameter is based on the residual signal variance.
 *  Otherwise, set a min and max order, and the encoder will search
 *  all orders, using the mean of each context for its Rice parameter,
 *  and use the best.
 *
 * \default \c 0
 * \param  encoder  An encoder instance to set.
 * \param  value    See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the encoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_set_min_residual_partition_order(FLAC__StreamEncoder *encoder, unsigned value);

/** Set the maximum partition order to search when coding the residual.
 *  This is used in tandem with
 *  FLAC__stream_encoder_set_min_residual_partition_order().
 *
 *  The partition order determines the context size in the residual.
 *  The context size will be approximately <tt>blocksize / (2 ^ order)</tt>.
 *
 *  Set both min and max values to \c 0 to force a single context,
 *  whose Rice parameter is based on the residual signal variance.
 *  Otherwise, set a min and max order, and the encoder will search
 *  all orders, using the mean of each context for its Rice parameter,
 *  and use the best.
 *
 * \default \c 0
 * \param  encoder  An encoder instance to set.
 * \param  value    See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the encoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_set_max_residual_partition_order(FLAC__StreamEncoder *encoder, unsigned value);

/** Deprecated.  Setting this value has no effect.
 *
 * \default \c 0
 * \param  encoder  An encoder instance to set.
 * \param  value    See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the encoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_set_rice_parameter_search_dist(FLAC__StreamEncoder *encoder, unsigned value);

/** Set an estimate of the total samples that will be encoded.
 *  This is merely an estimate and may be set to \c 0 if unknown.
 *  This value will be written to the STREAMINFO block before encoding,
 *  and can remove the need for the caller to rewrite the value later
 *  if the value is known before encoding.
 *
 * \default \c 0
 * \param  encoder  An encoder instance to set.
 * \param  value    See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the encoder is already initialized, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_set_total_samples_estimate(FLAC__StreamEncoder *encoder, FLAC__uint64 value);

/** Set the metadata blocks to be emitted to the stream before encoding.
 *  A value of \c NULL, \c 0 implies no metadata; otherwise, supply an
 *  array of pointers to metadata blocks.  The array is non-const since
 *  the encoder may need to change the \a is_last flag inside them, and
 *  in some cases update seek point offsets.  Otherwise, the encoder will
 *  not modify or free the blocks.  It is up to the caller to free the
 *  metadata blocks after encoding finishes.
 *
 * \note
 * The encoder stores only copies of the pointers in the \a metadata array;
 * the metadata blocks themselves must survive at least until after
 * FLAC__stream_encoder_finish() returns.  Do not free the blocks until then.
 *
 * \note
 * The STREAMINFO block is always written and no STREAMINFO block may
 * occur in the supplied array.
 *
 * \note
 * By default the encoder does not create a SEEKTABLE.  If one is supplied
 * in the \a metadata array, but the client has specified that it does not
 * support seeking, then the SEEKTABLE will be written verbatim.  However
 * by itself this is not very useful as the client will not know the stream
 * offsets for the seekpoints ahead of time.  In order to get a proper
 * seektable the client must support seeking.  See next note.
 *
 * \note
 * SEEKTABLE blocks are handled specially.  Since you will not know
 * the values for the seek point stream offsets, you should pass in
 * a SEEKTABLE 'template', that is, a SEEKTABLE object with the
 * required sample numbers (or placeholder points), with \c 0 for the
 * \a frame_samples and \a stream_offset fields for each point.  If the
 * client has specified that it supports seeking by providing a seek
 * callback to FLAC__stream_encoder_init_stream() or both seek AND read
 * callback to FLAC__stream_encoder_init_ogg_stream() (or by using
 * FLAC__stream_encoder_init*_file() or FLAC__stream_encoder_init*_FILE()),
 * then while it is encoding the encoder will fill the stream offsets in
 * for you and when encoding is finished, it will seek back and write the
 * real values into the SEEKTABLE block in the stream.  There are helper
 * routines for manipulating seektable template blocks; see metadata.h:
 * FLAC__metadata_object_seektable_template_*().  If the client does
 * not support seeking, the SEEKTABLE will have inaccurate offsets which
 * will slow down or remove the ability to seek in the FLAC stream.
 *
 * \note
 * The encoder instance \b will modify the first \c SEEKTABLE block
 * as it transforms the template to a valid seektable while encoding,
 * but it is still up to the caller to free all metadata blocks after
 * encoding.
 *
 * \note
 * A VORBIS_COMMENT block may be supplied.  The vendor string in it
 * will be ignored.  libFLAC will use it's own vendor string. libFLAC
 * will not modify the passed-in VORBIS_COMMENT's vendor string, it
 * will simply write it's own into the stream.  If no VORBIS_COMMENT
 * block is present in the \a metadata array, libFLAC will write an
 * empty one, containing only the vendor string.
 *
 * \note The Ogg FLAC mapping requires that the VORBIS_COMMENT block be
 * the second metadata block of the stream.  The encoder already supplies
 * the STREAMINFO block automatically.  If \a metadata does not contain a
 * VORBIS_COMMENT block, the encoder will supply that too.  Otherwise, if
 * \a metadata does contain a VORBIS_COMMENT block and it is not the
 * first, the init function will reorder \a metadata by moving the
 * VORBIS_COMMENT block to the front; the relative ordering of the other
 * blocks will remain as they were.
 *
 * \note The Ogg FLAC mapping limits the number of metadata blocks per
 * stream to \c 65535.  If \a num_blocks exceeds this the function will
 * return \c false.
 *
 * \default \c NULL, 0
 * \param  encoder     An encoder instance to set.
 * \param  metadata    See above.
 * \param  num_blocks  See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if the encoder is already initialized, else \c true.
 *    \c false if the encoder is already initialized, or if
 *    \a num_blocks > 65535 if encoding to Ogg FLAC, else \c true.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_set_metadata(FLAC__StreamEncoder *encoder, FLAC__StreamMetadata **metadata, unsigned num_blocks);

/** Get the current encoder state.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__StreamEncoderState
 *    The current encoder state.
 */
FLAC_API FLAC__StreamEncoderState FLAC__stream_encoder_get_state(const FLAC__StreamEncoder *encoder);

/** Get the state of the verify stream decoder.
 *  Useful when the stream encoder state is
 *  \c FLAC__STREAM_ENCODER_VERIFY_DECODER_ERROR.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__StreamDecoderState
 *    The verify stream decoder state.
 */
FLAC_API FLAC__StreamDecoderState FLAC__stream_encoder_get_verify_decoder_state(const FLAC__StreamEncoder *encoder);

/** Get the current encoder state as a C string.
 *  This version automatically resolves
 *  \c FLAC__STREAM_ENCODER_VERIFY_DECODER_ERROR by getting the
 *  verify decoder's state.
 *
 * \param  encoder  A encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval const char *
 *    The encoder state as a C string.  Do not modify the contents.
 */
FLAC_API const char *FLAC__stream_encoder_get_resolved_state_string(const FLAC__StreamEncoder *encoder);

/** Get relevant values about the nature of a verify decoder error.
 *  Useful when the stream encoder state is
 *  \c FLAC__STREAM_ENCODER_VERIFY_DECODER_ERROR.  The arguments should
 *  be addresses in which the stats will be returned, or NULL if value
 *  is not desired.
 *
 * \param  encoder  An encoder instance to query.
 * \param  absolute_sample  The absolute sample number of the mismatch.
 * \param  frame_number  The number of the frame in which the mismatch occurred.
 * \param  channel       The channel in which the mismatch occurred.
 * \param  sample        The number of the sample (relative to the frame) in
 *                       which the mismatch occurred.
 * \param  expected      The expected value for the sample in question.
 * \param  got           The actual value returned by the decoder.
 * \assert
 *    \code encoder != NULL \endcode
 */
FLAC_API void FLAC__stream_encoder_get_verify_decoder_error_stats(const FLAC__StreamEncoder *encoder, FLAC__uint64 *absolute_sample, unsigned *frame_number, unsigned *channel, unsigned *sample, FLAC__int32 *expected, FLAC__int32 *got);

/** Get the "verify" flag.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    See FLAC__stream_encoder_set_verify().
 */
FLAC_API FLAC__bool FLAC__stream_encoder_get_verify(const FLAC__StreamEncoder *encoder);

/** Get the <A HREF="../format.html#subset>Subset</A> flag.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    See FLAC__stream_encoder_set_streamable_subset().
 */
FLAC_API FLAC__bool FLAC__stream_encoder_get_streamable_subset(const FLAC__StreamEncoder *encoder);

/** Get the number of input channels being processed.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval unsigned
 *    See FLAC__stream_encoder_set_channels().
 */
FLAC_API unsigned FLAC__stream_encoder_get_channels(const FLAC__StreamEncoder *encoder);

/** Get the input sample resolution setting.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval unsigned
 *    See FLAC__stream_encoder_set_bits_per_sample().
 */
FLAC_API unsigned FLAC__stream_encoder_get_bits_per_sample(const FLAC__StreamEncoder *encoder);

/** Get the input sample rate setting.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval unsigned
 *    See FLAC__stream_encoder_set_sample_rate().
 */
FLAC_API unsigned FLAC__stream_encoder_get_sample_rate(const FLAC__StreamEncoder *encoder);

/** Get the blocksize setting.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval unsigned
 *    See FLAC__stream_encoder_set_blocksize().
 */
FLAC_API unsigned FLAC__stream_encoder_get_blocksize(const FLAC__StreamEncoder *encoder);

/** Get the "mid/side stereo coding" flag.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    See FLAC__stream_encoder_get_do_mid_side_stereo().
 */
FLAC_API FLAC__bool FLAC__stream_encoder_get_do_mid_side_stereo(const FLAC__StreamEncoder *encoder);

/** Get the "adaptive mid/side switching" flag.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    See FLAC__stream_encoder_set_loose_mid_side_stereo().
 */
FLAC_API FLAC__bool FLAC__stream_encoder_get_loose_mid_side_stereo(const FLAC__StreamEncoder *encoder);

/** Get the maximum LPC order setting.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval unsigned
 *    See FLAC__stream_encoder_set_max_lpc_order().
 */
FLAC_API unsigned FLAC__stream_encoder_get_max_lpc_order(const FLAC__StreamEncoder *encoder);

/** Get the quantized linear predictor coefficient precision setting.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval unsigned
 *    See FLAC__stream_encoder_set_qlp_coeff_precision().
 */
FLAC_API unsigned FLAC__stream_encoder_get_qlp_coeff_precision(const FLAC__StreamEncoder *encoder);

/** Get the qlp coefficient precision search flag.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    See FLAC__stream_encoder_set_do_qlp_coeff_prec_search().
 */
FLAC_API FLAC__bool FLAC__stream_encoder_get_do_qlp_coeff_prec_search(const FLAC__StreamEncoder *encoder);

/** Get the "escape coding" flag.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    See FLAC__stream_encoder_set_do_escape_coding().
 */
FLAC_API FLAC__bool FLAC__stream_encoder_get_do_escape_coding(const FLAC__StreamEncoder *encoder);

/** Get the exhaustive model search flag.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    See FLAC__stream_encoder_set_do_exhaustive_model_search().
 */
FLAC_API FLAC__bool FLAC__stream_encoder_get_do_exhaustive_model_search(const FLAC__StreamEncoder *encoder);

/** Get the minimum residual partition order setting.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval unsigned
 *    See FLAC__stream_encoder_set_min_residual_partition_order().
 */
FLAC_API unsigned FLAC__stream_encoder_get_min_residual_partition_order(const FLAC__StreamEncoder *encoder);

/** Get maximum residual partition order setting.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval unsigned
 *    See FLAC__stream_encoder_set_max_residual_partition_order().
 */
FLAC_API unsigned FLAC__stream_encoder_get_max_residual_partition_order(const FLAC__StreamEncoder *encoder);

/** Get the Rice parameter search distance setting.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval unsigned
 *    See FLAC__stream_encoder_set_rice_parameter_search_dist().
 */
FLAC_API unsigned FLAC__stream_encoder_get_rice_parameter_search_dist(const FLAC__StreamEncoder *encoder);

/** Get the previously set estimate of the total samples to be encoded.
 *  The encoder merely mimics back the value given to
 *  FLAC__stream_encoder_set_total_samples_estimate() since it has no
 *  other way of knowing how many samples the client will encode.
 *
 * \param  encoder  An encoder instance to set.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__uint64
 *    See FLAC__stream_encoder_get_total_samples_estimate().
 */
FLAC_API FLAC__uint64 FLAC__stream_encoder_get_total_samples_estimate(const FLAC__StreamEncoder *encoder);

/** Initialize the encoder instance to encode native FLAC streams.
 *
 *  This flavor of initialization sets up the encoder to encode to a
 *  native FLAC stream. I/O is performed via callbacks to the client.
 *  For encoding to a plain file via filename or open \c FILE*,
 *  FLAC__stream_encoder_init_file() and FLAC__stream_encoder_init_FILE()
 *  provide a simpler interface.
 *
 *  This function should be called after FLAC__stream_encoder_new() and
 *  FLAC__stream_encoder_set_*() but before FLAC__stream_encoder_process()
 *  or FLAC__stream_encoder_process_interleaved().
 *  initialization succeeded.
 *
 *  The call to FLAC__stream_encoder_init_stream() currently will also
 *  immediately call the write callback several times, once with the \c fLaC
 *  signature, and once for each encoded metadata block.
 *
 * \param  encoder            An uninitialized encoder instance.
 * \param  write_callback     See FLAC__StreamEncoderWriteCallback.  This
 *                            pointer must not be \c NULL.
 * \param  seek_callback      See FLAC__StreamEncoderSeekCallback.  This
 *                            pointer may be \c NULL if seeking is not
 *                            supported.  The encoder uses seeking to go back
 *                            and write some some stream statistics to the
 *                            STREAMINFO block; this is recommended but not
 *                            necessary to create a valid FLAC stream.  If
 *                            \a seek_callback is not \c NULL then a
 *                            \a tell_callback must also be supplied.
 *                            Alternatively, a dummy seek callback that just
 *                            returns \c FLAC__STREAM_ENCODER_SEEK_STATUS_UNSUPPORTED
 *                            may also be supplied, all though this is slightly
 *                            less efficient for the encoder.
 * \param  tell_callback      See FLAC__StreamEncoderTellCallback.  This
 *                            pointer may be \c NULL if seeking is not
 *                            supported.  If \a seek_callback is \c NULL then
 *                            this argument will be ignored.  If
 *                            \a seek_callback is not \c NULL then a
 *                            \a tell_callback must also be supplied.
 *                            Alternatively, a dummy tell callback that just
 *                            returns \c FLAC__STREAM_ENCODER_TELL_STATUS_UNSUPPORTED
 *                            may also be supplied, all though this is slightly
 *                            less efficient for the encoder.
 * \param  metadata_callback  See FLAC__StreamEncoderMetadataCallback.  This
 *                            pointer may be \c NULL if the callback is not
 *                            desired.  If the client provides a seek callback,
 *                            this function is not necessary as the encoder
 *                            will automatically seek back and update the
 *                            STREAMINFO block.  It may also be \c NULL if the
 *                            client does not support seeking, since it will
 *                            have no way of going back to update the
 *                            STREAMINFO.  However the client can still supply
 *                            a callback if it would like to know the details
 *                            from the STREAMINFO.
 * \param  client_data        This value will be supplied to callbacks in their
 *                            \a client_data argument.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__StreamEncoderInitStatus
 *    \c FLAC__STREAM_ENCODER_INIT_STATUS_OK if initialization was successful;
 *    see FLAC__StreamEncoderInitStatus for the meanings of other return values.
 */
FLAC_API FLAC__StreamEncoderInitStatus FLAC__stream_encoder_init_stream(FLAC__StreamEncoder *encoder, FLAC__StreamEncoderWriteCallback write_callback, FLAC__StreamEncoderSeekCallback seek_callback, FLAC__StreamEncoderTellCallback tell_callback, FLAC__StreamEncoderMetadataCallback metadata_callback, void *client_data);

/** Initialize the encoder instance to encode Ogg FLAC streams.
 *
 *  This flavor of initialization sets up the encoder to encode to a FLAC
 *  stream in an Ogg container.  I/O is performed via callbacks to the
 *  client.  For encoding to a plain file via filename or open \c FILE*,
 *  FLAC__stream_encoder_init_ogg_file() and FLAC__stream_encoder_init_ogg_FILE()
 *  provide a simpler interface.
 *
 *  This function should be called after FLAC__stream_encoder_new() and
 *  FLAC__stream_encoder_set_*() but before FLAC__stream_encoder_process()
 *  or FLAC__stream_encoder_process_interleaved().
 *  initialization succeeded.
 *
 *  The call to FLAC__stream_encoder_init_ogg_stream() currently will also
 *  immediately call the write callback several times to write the metadata
 *  packets.
 *
 * \param  encoder            An uninitialized encoder instance.
 * \param  read_callback      See FLAC__StreamEncoderReadCallback.  This
 *                            pointer must not be \c NULL if \a seek_callback
 *                            is non-NULL since they are both needed to be
 *                            able to write data back to the Ogg FLAC stream
 *                            in the post-encode phase.
 * \param  write_callback     See FLAC__StreamEncoderWriteCallback.  This
 *                            pointer must not be \c NULL.
 * \param  seek_callback      See FLAC__StreamEncoderSeekCallback.  This
 *                            pointer may be \c NULL if seeking is not
 *                            supported.  The encoder uses seeking to go back
 *                            and write some some stream statistics to the
 *                            STREAMINFO block; this is recommended but not
 *                            necessary to create a valid FLAC stream.  If
 *                            \a seek_callback is not \c NULL then a
 *                            \a tell_callback must also be supplied.
 *                            Alternatively, a dummy seek callback that just
 *                            returns \c FLAC__STREAM_ENCODER_SEEK_STATUS_UNSUPPORTED
 *                            may also be supplied, all though this is slightly
 *                            less efficient for the encoder.
 * \param  tell_callback      See FLAC__StreamEncoderTellCallback.  This
 *                            pointer may be \c NULL if seeking is not
 *                            supported.  If \a seek_callback is \c NULL then
 *                            this argument will be ignored.  If
 *                            \a seek_callback is not \c NULL then a
 *                            \a tell_callback must also be supplied.
 *                            Alternatively, a dummy tell callback that just
 *                            returns \c FLAC__STREAM_ENCODER_TELL_STATUS_UNSUPPORTED
 *                            may also be supplied, all though this is slightly
 *                            less efficient for the encoder.
 * \param  metadata_callback  See FLAC__StreamEncoderMetadataCallback.  This
 *                            pointer may be \c NULL if the callback is not
 *                            desired.  If the client provides a seek callback,
 *                            this function is not necessary as the encoder
 *                            will automatically seek back and update the
 *                            STREAMINFO block.  It may also be \c NULL if the
 *                            client does not support seeking, since it will
 *                            have no way of going back to update the
 *                            STREAMINFO.  However the client can still supply
 *                            a callback if it would like to know the details
 *                            from the STREAMINFO.
 * \param  client_data        This value will be supplied to callbacks in their
 *                            \a client_data argument.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__StreamEncoderInitStatus
 *    \c FLAC__STREAM_ENCODER_INIT_STATUS_OK if initialization was successful;
 *    see FLAC__StreamEncoderInitStatus for the meanings of other return values.
 */
FLAC_API FLAC__StreamEncoderInitStatus FLAC__stream_encoder_init_ogg_stream(FLAC__StreamEncoder *encoder, FLAC__StreamEncoderReadCallback read_callback, FLAC__StreamEncoderWriteCallback write_callback, FLAC__StreamEncoderSeekCallback seek_callback, FLAC__StreamEncoderTellCallback tell_callback, FLAC__StreamEncoderMetadataCallback metadata_callback, void *client_data);

/** Initialize the encoder instance to encode native FLAC files.
 *
 *  This flavor of initialization sets up the encoder to encode to a
 *  plain native FLAC file.  For non-stdio streams, you must use
 *  FLAC__stream_encoder_init_stream() and provide callbacks for the I/O.
 *
 *  This function should be called after FLAC__stream_encoder_new() and
 *  FLAC__stream_encoder_set_*() but before FLAC__stream_encoder_process()
 *  or FLAC__stream_encoder_process_interleaved().
 *  initialization succeeded.
 *
 * \param  encoder            An uninitialized encoder instance.
 * \param  file               An open file.  The file should have been opened
 *                            with mode \c "w+b" and rewound.  The file
 *                            becomes owned by the encoder and should not be
 *                            manipulated by the client while encoding.
 *                            Unless \a file is \c stdout, it will be closed
 *                            when FLAC__stream_encoder_finish() is called.
 *                            Note however that a proper SEEKTABLE cannot be
 *                            created when encoding to \c stdout since it is
 *                            not seekable.
 * \param  progress_callback  See FLAC__StreamEncoderProgressCallback.  This
 *                            pointer may be \c NULL if the callback is not
 *                            desired.
 * \param  client_data        This value will be supplied to callbacks in their
 *                            \a client_data argument.
 * \assert
 *    \code encoder != NULL \endcode
 *    \code file != NULL \endcode
 * \retval FLAC__StreamEncoderInitStatus
 *    \c FLAC__STREAM_ENCODER_INIT_STATUS_OK if initialization was successful;
 *    see FLAC__StreamEncoderInitStatus for the meanings of other return values.
 */
FLAC_API FLAC__StreamEncoderInitStatus FLAC__stream_encoder_init_FILE(FLAC__StreamEncoder *encoder, FILE *file, FLAC__StreamEncoderProgressCallback progress_callback, void *client_data);

/** Initialize the encoder instance to encode Ogg FLAC files.
 *
 *  This flavor of initialization sets up the encoder to encode to a
 *  plain Ogg FLAC file.  For non-stdio streams, you must use
 *  FLAC__stream_encoder_init_ogg_stream() and provide callbacks for the I/O.
 *
 *  This function should be called after FLAC__stream_encoder_new() and
 *  FLAC__stream_encoder_set_*() but before FLAC__stream_encoder_process()
 *  or FLAC__stream_encoder_process_interleaved().
 *  initialization succeeded.
 *
 * \param  encoder            An uninitialized encoder instance.
 * \param  file               An open file.  The file should have been opened
 *                            with mode \c "w+b" and rewound.  The file
 *                            becomes owned by the encoder and should not be
 *                            manipulated by the client while encoding.
 *                            Unless \a file is \c stdout, it will be closed
 *                            when FLAC__stream_encoder_finish() is called.
 *                            Note however that a proper SEEKTABLE cannot be
 *                            created when encoding to \c stdout since it is
 *                            not seekable.
 * \param  progress_callback  See FLAC__StreamEncoderProgressCallback.  This
 *                            pointer may be \c NULL if the callback is not
 *                            desired.
 * \param  client_data        This value will be supplied to callbacks in their
 *                            \a client_data argument.
 * \assert
 *    \code encoder != NULL \endcode
 *    \code file != NULL \endcode
 * \retval FLAC__StreamEncoderInitStatus
 *    \c FLAC__STREAM_ENCODER_INIT_STATUS_OK if initialization was successful;
 *    see FLAC__StreamEncoderInitStatus for the meanings of other return values.
 */
FLAC_API FLAC__StreamEncoderInitStatus FLAC__stream_encoder_init_ogg_FILE(FLAC__StreamEncoder *encoder, FILE *file, FLAC__StreamEncoderProgressCallback progress_callback, void *client_data);

/** Initialize the encoder instance to encode native FLAC files.
 *
 *  This flavor of initialization sets up the encoder to encode to a plain
 *  FLAC file.  If POSIX fopen() semantics are not sufficient (for example,
 *  with Unicode filenames on Windows), you must use
 *  FLAC__stream_encoder_init_FILE(), or FLAC__stream_encoder_init_stream()
 *  and provide callbacks for the I/O.
 *
 *  This function should be called after FLAC__stream_encoder_new() and
 *  FLAC__stream_encoder_set_*() but before FLAC__stream_encoder_process()
 *  or FLAC__stream_encoder_process_interleaved().
 *  initialization succeeded.
 *
 * \param  encoder            An uninitialized encoder instance.
 * \param  filename           The name of the file to encode to.  The file will
 *                            be opened with fopen().  Use \c NULL to encode to
 *                            \c stdout.  Note however that a proper SEEKTABLE
 *                            cannot be created when encoding to \c stdout since
 *                            it is not seekable.
 * \param  progress_callback  See FLAC__StreamEncoderProgressCallback.  This
 *                            pointer may be \c NULL if the callback is not
 *                            desired.
 * \param  client_data        This value will be supplied to callbacks in their
 *                            \a client_data argument.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__StreamEncoderInitStatus
 *    \c FLAC__STREAM_ENCODER_INIT_STATUS_OK if initialization was successful;
 *    see FLAC__StreamEncoderInitStatus for the meanings of other return values.
 */
FLAC_API FLAC__StreamEncoderInitStatus FLAC__stream_encoder_init_file(FLAC__StreamEncoder *encoder, const char *filename, FLAC__StreamEncoderProgressCallback progress_callback, void *client_data);

/** Initialize the encoder instance to encode Ogg FLAC files.
 *
 *  This flavor of initialization sets up the encoder to encode to a plain
 *  Ogg FLAC file.  If POSIX fopen() semantics are not sufficient (for example,
 *  with Unicode filenames on Windows), you must use
 *  FLAC__stream_encoder_init_ogg_FILE(), or FLAC__stream_encoder_init_ogg_stream()
 *  and provide callbacks for the I/O.
 *
 *  This function should be called after FLAC__stream_encoder_new() and
 *  FLAC__stream_encoder_set_*() but before FLAC__stream_encoder_process()
 *  or FLAC__stream_encoder_process_interleaved().
 *  initialization succeeded.
 *
 * \param  encoder            An uninitialized encoder instance.
 * \param  filename           The name of the file to encode to.  The file will
 *                            be opened with fopen().  Use \c NULL to encode to
 *                            \c stdout.  Note however that a proper SEEKTABLE
 *                            cannot be created when encoding to \c stdout since
 *                            it is not seekable.
 * \param  progress_callback  See FLAC__StreamEncoderProgressCallback.  This
 *                            pointer may be \c NULL if the callback is not
 *                            desired.
 * \param  client_data        This value will be supplied to callbacks in their
 *                            \a client_data argument.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__StreamEncoderInitStatus
 *    \c FLAC__STREAM_ENCODER_INIT_STATUS_OK if initialization was successful;
 *    see FLAC__StreamEncoderInitStatus for the meanings of other return values.
 */
FLAC_API FLAC__StreamEncoderInitStatus FLAC__stream_encoder_init_ogg_file(FLAC__StreamEncoder *encoder, const char *filename, FLAC__StreamEncoderProgressCallback progress_callback, void *client_data);

/** Finish the encoding process.
 *  Flushes the encoding buffer, releases resources, resets the encoder
 *  settings to their defaults, and returns the encoder state to
 *  FLAC__STREAM_ENCODER_UNINITIALIZED.  Note that this can generate
 *  one or more write callbacks before returning, and will generate
 *  a metadata callback.
 *
 *  Note that in the course of processing the last frame, errors can
 *  occur, so the caller should be sure to check the return value to
 *  ensure the file was encoded properly.
 *
 *  In the event of a prematurely-terminated encode, it is not strictly
 *  necessary to call this immediately before FLAC__stream_encoder_delete()
 *  but it is good practice to match every FLAC__stream_encoder_init_*()
 *  with a FLAC__stream_encoder_finish().
 *
 * \param  encoder  An uninitialized encoder instance.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval FLAC__bool
 *    \c false if an error occurred processing the last frame; or if verify
 *    mode is set (see FLAC__stream_encoder_set_verify()), there was a
 *    verify mismatch; else \c true.  If \c false, caller should check the
 *    state with FLAC__stream_encoder_get_state() for more information
 *    about the error.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_finish(FLAC__StreamEncoder *encoder);

/** Submit data for encoding.
 *  This version allows you to supply the input data via an array of
 *  pointers, each pointer pointing to an array of \a samples samples
 *  representing one channel.  The samples need not be block-aligned,
 *  but each channel should have the same number of samples.  Each sample
 *  should be a signed integer, right-justified to the resolution set by
 *  FLAC__stream_encoder_set_bits_per_sample().  For example, if the
 *  resolution is 16 bits per sample, the samples should all be in the
 *  range [-32768,32767].
 *
 *  For applications where channel order is important, channels must
 *  follow the order as described in the
 *  <A HREF="../format.html#frame_header">frame header</A>.
 *
 * \param  encoder  An initialized encoder instance in the OK state.
 * \param  buffer   An array of pointers to each channel's signal.
 * \param  samples  The number of samples in one channel.
 * \assert
 *    \code encoder != NULL \endcode
 *    \code FLAC__stream_encoder_get_state(encoder) == FLAC__STREAM_ENCODER_OK \endcode
 * \retval FLAC__bool
 *    \c true if successful, else \c false; in this case, check the
 *    encoder state with FLAC__stream_encoder_get_state() to see what
 *    went wrong.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_process(FLAC__StreamEncoder *encoder, const FLAC__int32 * const buffer[], unsigned samples);

/** Submit data for encoding.
 *  This version allows you to supply the input data where the channels
 *  are interleaved into a single array (i.e. channel0_sample0,
 *  channel1_sample0, ... , channelN_sample0, channel0_sample1, ...).
 *  The samples need not be block-aligned but they must be
 *  sample-aligned, i.e. the first value should be channel0_sample0
 *  and the last value channelN_sampleM.  Each sample should be a signed
 *  integer, right-justified to the resolution set by
 *  FLAC__stream_encoder_set_bits_per_sample().  For example, if the
 *  resolution is 16 bits per sample, the samples should all be in the
 *  range [-32768,32767].
 *
 *  For applications where channel order is important, channels must
 *  follow the order as described in the
 *  <A HREF="../format.html#frame_header">frame header</A>.
 *
 * \param  encoder  An initialized encoder instance in the OK state.
 * \param  buffer   An array of channel-interleaved data (see above).
 * \param  samples  The number of samples in one channel, the same as for
 *                  FLAC__stream_encoder_process().  For example, if
 *                  encoding two channels, \c 1000 \a samples corresponds
 *                  to a \a buffer of 2000 values.
 * \assert
 *    \code encoder != NULL \endcode
 *    \code FLAC__stream_encoder_get_state(encoder) == FLAC__STREAM_ENCODER_OK \endcode
 * \retval FLAC__bool
 *    \c true if successful, else \c false; in this case, check the
 *    encoder state with FLAC__stream_encoder_get_state() to see what
 *    went wrong.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_process_interleaved(FLAC__StreamEncoder *encoder, const FLAC__int32 buffer[], unsigned samples);

/* \} */

#ifdef __cplusplus
}
#endif

#endif
#endif //++-- stream_encoder.h
