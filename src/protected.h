#define USE_DECODE 1
#if USE_DECODE

#include "flac_stream.h" /* for FLAC__StreamDecoderReadStatus */

#if 1 //++ ogg_encoder_aspect.h 

#ifndef FLAC__PRIVATE__OGG_ENCODER_ASPECT_H
#define FLAC__PRIVATE__OGG_ENCODER_ASPECT_H

#include "ogg.h"

typedef struct FLAC__OggEncoderAspect {
	/* these are storage for values that can be set through the API */
	long serial_number;
	unsigned num_metadata;

	/* these are for internal state related to Ogg encoding */
	ogg_stream_state stream_state;
	ogg_page page;
	FLAC__bool seen_magic; /* true if we've seen the fLaC magic in the write callback yet */
	FLAC__bool is_first_packet;
	FLAC__uint64 samples_written;
} FLAC__OggEncoderAspect;

void FLAC__ogg_encoder_aspect_set_serial_number(FLAC__OggEncoderAspect *aspect, long value);
FLAC__bool FLAC__ogg_encoder_aspect_set_num_metadata(FLAC__OggEncoderAspect *aspect, unsigned value);
void FLAC__ogg_encoder_aspect_set_defaults(FLAC__OggEncoderAspect *aspect);
FLAC__bool FLAC__ogg_encoder_aspect_init(FLAC__OggEncoderAspect *aspect);
void FLAC__ogg_encoder_aspect_finish(FLAC__OggEncoderAspect *aspect);

typedef FLAC__StreamEncoderWriteStatus (*FLAC__OggEncoderAspectWriteCallbackProxy)(const void *encoder, const FLAC__byte buffer[], size_t bytes, unsigned samples, unsigned current_frame, void *client_data);

FLAC__StreamEncoderWriteStatus FLAC__ogg_encoder_aspect_write_callback_wrapper(FLAC__OggEncoderAspect *aspect, const FLAC__byte buffer[], size_t bytes, unsigned samples, unsigned current_frame, FLAC__bool is_last_block, FLAC__OggEncoderAspectWriteCallbackProxy write_callback, void *encoder, void *client_data);
#endif
#endif //++-- ogg_encoder_aspect.h 

#if 1 //++ ogg_decoder_aspect.h 
#ifndef FLAC__PRIVATE__OGG_DECODER_ASPECT_H
#define FLAC__PRIVATE__OGG_DECODER_ASPECT_H


typedef struct FLAC__OggDecoderAspect {
	/* these are storage for values that can be set through the API */
	FLAC__bool use_first_serial_number;
	long serial_number;

	/* these are for internal state related to Ogg decoding */
	ogg_stream_state stream_state;
	ogg_sync_state sync_state;
	unsigned version_major, version_minor;
	FLAC__bool need_serial_number;
	FLAC__bool end_of_stream;
	FLAC__bool have_working_page; /* only if true will the following vars be valid */
	ogg_page working_page;
	FLAC__bool have_working_packet; /* only if true will the following vars be valid */
	ogg_packet working_packet; /* as we work through the packet we will move working_packet.packet forward and working_packet.bytes down */
} FLAC__OggDecoderAspect;

void FLAC__ogg_decoder_aspect_set_serial_number(FLAC__OggDecoderAspect *aspect, long value);
void FLAC__ogg_decoder_aspect_set_defaults(FLAC__OggDecoderAspect *aspect);
FLAC__bool FLAC__ogg_decoder_aspect_init(FLAC__OggDecoderAspect *aspect);
void FLAC__ogg_decoder_aspect_finish(FLAC__OggDecoderAspect *aspect);
void FLAC__ogg_decoder_aspect_flush(FLAC__OggDecoderAspect *aspect);
void FLAC__ogg_decoder_aspect_reset(FLAC__OggDecoderAspect *aspect);

typedef enum {
	FLAC__OGG_DECODER_ASPECT_READ_STATUS_OK = 0,
	FLAC__OGG_DECODER_ASPECT_READ_STATUS_END_OF_STREAM,
	FLAC__OGG_DECODER_ASPECT_READ_STATUS_LOST_SYNC,
	FLAC__OGG_DECODER_ASPECT_READ_STATUS_NOT_FLAC,
	FLAC__OGG_DECODER_ASPECT_READ_STATUS_UNSUPPORTED_MAPPING_VERSION,
	FLAC__OGG_DECODER_ASPECT_READ_STATUS_ABORT,
	FLAC__OGG_DECODER_ASPECT_READ_STATUS_ERROR,
	FLAC__OGG_DECODER_ASPECT_READ_STATUS_MEMORY_ALLOCATION_ERROR
} FLAC__OggDecoderAspectReadStatus;

typedef FLAC__OggDecoderAspectReadStatus (*FLAC__OggDecoderAspectReadCallbackProxy)(const void *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data);

FLAC__OggDecoderAspectReadStatus FLAC__ogg_decoder_aspect_read_callback_wrapper(FLAC__OggDecoderAspect *aspect, FLAC__byte buffer[], size_t *bytes, FLAC__OggDecoderAspectReadCallbackProxy read_callback, const FLAC__StreamDecoder *decoder, void *client_data);

#endif // USE_DECODE
#endif
#endif //++-- ogg_decoder_aspect.h 

#if 1 //++ ogg_helper.h 


#ifndef FLAC__PRIVATE__OGG_HELPER_H
#define FLAC__PRIVATE__OGG_HELPER_H

void simple_ogg_page__init(ogg_page *page);
void simple_ogg_page__clear(ogg_page *page);
FLAC__bool simple_ogg_page__get_at(FLAC__StreamEncoder *encoder, FLAC__uint64 position, ogg_page *page, FLAC__StreamEncoderSeekCallback seek_callback, FLAC__StreamEncoderReadCallback read_callback, void *client_data);
FLAC__bool simple_ogg_page__set_at(FLAC__StreamEncoder *encoder, FLAC__uint64 position, ogg_page *page, FLAC__StreamEncoderSeekCallback seek_callback, FLAC__StreamEncoderWriteCallback write_callback, void *client_data);

#endif
#endif //++-- ogg_helper.h 

#if 1 //++ ogg_mapping.h 


#ifndef FLAC__PRIVATE__OGG_MAPPING_H
#define FLAC__PRIVATE__OGG_MAPPING_H

/** The length of the 'FLAC' magic in bytes. */
#define FLAC__OGG_MAPPING_PACKET_TYPE_LENGTH (1u)

extern const unsigned FLAC__OGG_MAPPING_PACKET_TYPE_LEN; /* = 8 bits */

extern const FLAC__byte FLAC__OGG_MAPPING_FIRST_HEADER_PACKET_TYPE; /* = 0x7f */

/** The length of the 'FLAC' magic in bytes. */
#define FLAC__OGG_MAPPING_MAGIC_LENGTH (4u)

extern const FLAC__byte * const FLAC__OGG_MAPPING_MAGIC; /* = "FLAC" */

extern const unsigned FLAC__OGG_MAPPING_VERSION_MAJOR_LEN; /* = 8 bits */
extern const unsigned FLAC__OGG_MAPPING_VERSION_MINOR_LEN; /* = 8 bits */

/** The length of the Ogg FLAC mapping major version number in bytes. */
#define FLAC__OGG_MAPPING_VERSION_MAJOR_LENGTH (1u)

/** The length of the Ogg FLAC mapping minor version number in bytes. */
#define FLAC__OGG_MAPPING_VERSION_MINOR_LENGTH (1u)

extern const unsigned FLAC__OGG_MAPPING_NUM_HEADERS_LEN; /* = 16 bits */

/** The length of the #-of-header-packets number bytes. */
#define FLAC__OGG_MAPPING_NUM_HEADERS_LENGTH (2u)

#endif
#endif //++-- ogg_mapping.h 


#if 1 //++ stream_encoder.h 
#ifndef FLAC__PROTECTED__STREAM_ENCODER_H
#define FLAC__PROTECTED__STREAM_ENCODER_H

#ifndef FLAC__INTEGER_ONLY_LIBRARY

#define FLAC__MAX_APODIZATION_FUNCTIONS 32

typedef enum {
	FLAC__APODIZATION_BARTLETT,
	FLAC__APODIZATION_BARTLETT_HANN,
	FLAC__APODIZATION_BLACKMAN,
	FLAC__APODIZATION_BLACKMAN_HARRIS_4TERM_92DB_SIDELOBE,
	FLAC__APODIZATION_CONNES,
	FLAC__APODIZATION_FLATTOP,
	FLAC__APODIZATION_GAUSS,
	FLAC__APODIZATION_HAMMING,
	FLAC__APODIZATION_HANN,
	FLAC__APODIZATION_KAISER_BESSEL,
	FLAC__APODIZATION_NUTTALL,
	FLAC__APODIZATION_RECTANGLE,
	FLAC__APODIZATION_TRIANGLE,
	FLAC__APODIZATION_TUKEY,
	FLAC__APODIZATION_WELCH
} FLAC__ApodizationFunction;

typedef struct {
	FLAC__ApodizationFunction type;
	union {
		struct {
			FLAC__real stddev;
		} gauss;
		struct {
			FLAC__real p;
		} tukey;
	} parameters;
} FLAC__ApodizationSpecification;

#endif // #ifndef FLAC__INTEGER_ONLY_LIBRARY

typedef struct FLAC__StreamEncoderProtected {
	FLAC__StreamEncoderState state;
	FLAC__bool verify;
	FLAC__bool streamable_subset;
	FLAC__bool do_md5;
	FLAC__bool do_mid_side_stereo;
	FLAC__bool loose_mid_side_stereo;
	unsigned channels;
	unsigned bits_per_sample;
	unsigned sample_rate;
	unsigned blocksize;
#ifndef FLAC__INTEGER_ONLY_LIBRARY
	unsigned num_apodizations;
	FLAC__ApodizationSpecification apodizations[FLAC__MAX_APODIZATION_FUNCTIONS];
#endif
	unsigned max_lpc_order;
	unsigned qlp_coeff_precision;
	FLAC__bool do_qlp_coeff_prec_search;
	FLAC__bool do_exhaustive_model_search;
	FLAC__bool do_escape_coding;
	unsigned min_residual_partition_order;
	unsigned max_residual_partition_order;
	unsigned rice_parameter_search_dist;
	FLAC__uint64 total_samples_estimate;
	FLAC__StreamMetadata **metadata;
	unsigned num_metadata_blocks;
	FLAC__uint64 streaminfo_offset, seektable_offset, audio_offset;
#if FLAC__HAS_OGG
	FLAC__OggEncoderAspect ogg_encoder_aspect;
#endif
} FLAC__StreamEncoderProtected;

#endif
#endif //++-- stream_encoder.h 

#if 1 //++ stream_decoder.h 
#ifndef FLAC__PROTECTED__STREAM_DECODER_H
#define FLAC__PROTECTED__STREAM_DECODER_H
#ifdef USE_DECODE
#if FLAC__HAS_OGG
#endif

typedef struct FLAC__StreamDecoderProtected {
	FLAC__StreamDecoderState state;
	unsigned channels;
	FLAC__ChannelAssignment channel_assignment;
	unsigned bits_per_sample;
	unsigned sample_rate; /* in Hz */
	unsigned blocksize; /* in samples (per channel) */
	FLAC__bool md5_checking; /* if true, generate MD5 signature of decoded data and compare against signature in the STREAMINFO metadata block */
#if FLAC__HAS_OGG
	FLAC__OggDecoderAspect ogg_decoder_aspect;
#endif
} FLAC__StreamDecoderProtected;

/*
 * return the number of input bytes consumed
 */
unsigned FLAC__stream_decoder_get_input_bytes_unconsumed(const FLAC__StreamDecoder *decoder);
#endif // USE_DECODE
#endif

#endif //++-- stream_decoder.h 

#if 1 //++ stream_encoder_framing.h 

#ifndef FLAC__PRIVATE__STREAM_ENCODER_FRAMING_H
#define FLAC__PRIVATE__STREAM_ENCODER_FRAMING_H

FLAC__bool FLAC__add_metadata_block(const FLAC__StreamMetadata *metadata, FLAC__BitWriter *bw);
FLAC__bool FLAC__frame_add_header(const FLAC__FrameHeader *header, FLAC__BitWriter *bw);
FLAC__bool FLAC__subframe_add_constant(const FLAC__Subframe_Constant *subframe, unsigned subframe_bps, unsigned wasted_bits, FLAC__BitWriter *bw);
FLAC__bool FLAC__subframe_add_fixed(const FLAC__Subframe_Fixed *subframe, unsigned residual_samples, unsigned subframe_bps, unsigned wasted_bits, FLAC__BitWriter *bw);
FLAC__bool FLAC__subframe_add_lpc(const FLAC__Subframe_LPC *subframe, unsigned residual_samples, unsigned subframe_bps, unsigned wasted_bits, FLAC__BitWriter *bw);
FLAC__bool FLAC__subframe_add_verbatim(const FLAC__Subframe_Verbatim *subframe, unsigned samples, unsigned subframe_bps, unsigned wasted_bits, FLAC__BitWriter *bw);

#endif
#endif //++-- stream_encoder_framing.h 
