
//++ stream_encoder.c 
#if HAVE_CONFIG_H
#  include <config.h>
#endif

#if defined _MSC_VER || defined __MINGW32__
#include <io.h> /* for _setmode() */
#include <fcntl.h> /* for _O_BINARY */
#endif
#if defined __CYGWIN__ || defined __EMX__
#include <io.h> /* for setmode(), O_BINARY */
#include <fcntl.h> /* for _O_BINARY */
#endif
#include <limits.h>
#include <stdio.h>
#include <stdlib.h> /* for malloc() */
#include <string.h> /* for memcpy() */
#include <sys/types.h> /* for off_t */
#if defined _MSC_VER || defined __BORLANDC__ || defined __MINGW32__
#if _MSC_VER <= 1600 || defined __BORLANDC__ /* @@@ [2G limit] */
#define fseeko fseek
#define ftello ftell
#endif
#endif

#include "protected.h"

#ifndef FLaC__INLINE
#define FLaC__INLINE
#endif

#ifdef min
#undef min
#endif
#define min(x,y) ((x)<(y)?(x):(y))

#ifdef max
#undef max
#endif
#define max(x,y) ((x)>(y)?(x):(y))

/* Exact Rice codeword length calculation is off by default.  The simple
 * (and fast) estimation (of how many bits a residual value will be
 * encoded with) in this encoder is very good, almost always yielding
 * compression within 0.1% of exact calculation.
 */
#undef EXACT_RICE_BITS_CALCULATION
/* Rice parameter searching is off by default.  The simple (and fast)
 * parameter estimation in this encoder is very good, almost always
 * yielding compression within 0.1% of the optimal parameters.
 */
#undef ENABLE_RICE_PARAMETER_SEARCH 


typedef struct {
	FLAC__int32 *data[FLAC__MAX_CHANNELS];
	unsigned size; /* of each data[] in samples */
	unsigned tail;
} verify_input_fifo;

typedef struct {
	const FLAC__byte *data;
	unsigned capacity;
	unsigned bytes;
} verify_output;

typedef enum {
	ENCODER_IN_MAGIC = 0,
	ENCODER_IN_METADATA = 1,
	ENCODER_IN_AUDIO = 2
} EncoderStateHint;

static struct CompressionLevels {
	FLAC__bool do_mid_side_stereo;
	FLAC__bool loose_mid_side_stereo;
	unsigned max_lpc_order;
	unsigned qlp_coeff_precision;
	FLAC__bool do_qlp_coeff_prec_search;
	FLAC__bool do_escape_coding;
	FLAC__bool do_exhaustive_model_search;
	unsigned min_residual_partition_order;
	unsigned max_residual_partition_order;
	unsigned rice_parameter_search_dist;
} compression_levels_[] = {
	{ false, false,  0, 0, false, false, false, 0, 3, 0 },
	{ true , true ,  0, 0, false, false, false, 0, 3, 0 },
	{ true , false,  0, 0, false, false, false, 0, 3, 0 },
	{ false, false,  6, 0, false, false, false, 0, 4, 0 },
	{ true , true ,  8, 0, false, false, false, 0, 4, 0 },
	{ true , false,  8, 0, false, false, false, 0, 5, 0 },
	{ true , false,  8, 0, false, false, false, 0, 6, 0 },
	{ true , false,  8, 0, false, false, true , 0, 6, 0 },
	{ true , false, 12, 0, false, false, true , 0, 6, 0 }
};


/***********************************************************************
 *
 * Private class method prototypes
 *
 ***********************************************************************/

static void set_defaults_(FLAC__StreamEncoder *encoder);
static void free_(FLAC__StreamEncoder *encoder);
static FLAC__bool resize_buffers_(FLAC__StreamEncoder *encoder, unsigned new_blocksize);
static FLAC__bool write_bitbuffer_(FLAC__StreamEncoder *encoder, unsigned samples, FLAC__bool is_last_block);
static FLAC__StreamEncoderWriteStatus write_frame_(FLAC__StreamEncoder *encoder, const FLAC__byte buffer[], size_t bytes, unsigned samples, FLAC__bool is_last_block);
static void update_metadata_(const FLAC__StreamEncoder *encoder);
#if FLAC__HAS_OGG
static void update_ogg_metadata_(FLAC__StreamEncoder *encoder);
#endif
static FLAC__bool process_frame_(FLAC__StreamEncoder *encoder, FLAC__bool is_fractional_block, FLAC__bool is_last_block);
static FLAC__bool process_subframes_(FLAC__StreamEncoder *encoder, FLAC__bool is_fractional_block);

static FLAC__bool process_subframe_(
	FLAC__StreamEncoder *encoder,
	unsigned min_partition_order,
	unsigned max_partition_order,
	const FLAC__FrameHeader *frame_header,
	unsigned subframe_bps,
	const FLAC__int32 integer_signal[],
	FLAC__Subframe *subframe[2],
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents[2],
	FLAC__int32 *residual[2],
	unsigned *best_subframe,
	unsigned *best_bits
);

static FLAC__bool add_subframe_(
	FLAC__StreamEncoder *encoder,
	unsigned blocksize,
	unsigned subframe_bps,
	const FLAC__Subframe *subframe,
	FLAC__BitWriter *frame
);

static unsigned evaluate_constant_subframe_(
	FLAC__StreamEncoder *encoder,
	const FLAC__int32 signal,
	unsigned blocksize,
	unsigned subframe_bps,
	FLAC__Subframe *subframe
);

static unsigned evaluate_fixed_subframe_(
	FLAC__StreamEncoder *encoder,
	const FLAC__int32 signal[],
	FLAC__int32 residual[],
	FLAC__uint64 abs_residual_partition_sums[],
	unsigned raw_bits_per_partition[],
	unsigned blocksize,
	unsigned subframe_bps,
	unsigned order,
	unsigned rice_parameter,
	unsigned rice_parameter_limit,
	unsigned min_partition_order,
	unsigned max_partition_order,
	FLAC__bool do_escape_coding,
	unsigned rice_parameter_search_dist,
	FLAC__Subframe *subframe,
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents
);

#ifndef FLAC__INTEGER_ONLY_LIBRARY
static unsigned evaluate_lpc_subframe_(
	FLAC__StreamEncoder *encoder,
	const FLAC__int32 signal[],
	FLAC__int32 residual[],
	FLAC__uint64 abs_residual_partition_sums[],
	unsigned raw_bits_per_partition[],
	const FLAC__real lp_coeff[],
	unsigned blocksize,
	unsigned subframe_bps,
	unsigned order,
	unsigned qlp_coeff_precision,
	unsigned rice_parameter,
	unsigned rice_parameter_limit,
	unsigned min_partition_order,
	unsigned max_partition_order,
	FLAC__bool do_escape_coding,
	unsigned rice_parameter_search_dist,
	FLAC__Subframe *subframe,
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents
);
#endif

static unsigned evaluate_verbatim_subframe_(
	FLAC__StreamEncoder *encoder, 
	const FLAC__int32 signal[],
	unsigned blocksize,
	unsigned subframe_bps,
	FLAC__Subframe *subframe
);

static unsigned find_best_partition_order_(
	struct FLAC__StreamEncoderPrivate *private_,
	const FLAC__int32 residual[],
	FLAC__uint64 abs_residual_partition_sums[],
	unsigned raw_bits_per_partition[],
	unsigned residual_samples,
	unsigned predictor_order,
	unsigned rice_parameter,
	unsigned rice_parameter_limit,
	unsigned min_partition_order,
	unsigned max_partition_order,
	unsigned bps,
	FLAC__bool do_escape_coding,
	unsigned rice_parameter_search_dist,
	FLAC__EntropyCodingMethod *best_ecm
);

static void precompute_partition_info_sums_(
	const FLAC__int32 residual[],
	FLAC__uint64 abs_residual_partition_sums[],
	unsigned residual_samples,
	unsigned predictor_order,
	unsigned min_partition_order,
	unsigned max_partition_order,
	unsigned bps
);

static void precompute_partition_info_escapes_(
	const FLAC__int32 residual[],
	unsigned raw_bits_per_partition[],
	unsigned residual_samples,
	unsigned predictor_order,
	unsigned min_partition_order,
	unsigned max_partition_order
);

static FLAC__bool set_partitioned_rice_(
	const FLAC__uint64 abs_residual_partition_sums[],
	const unsigned raw_bits_per_partition[],
	const unsigned residual_samples,
	const unsigned predictor_order,
	const unsigned suggested_rice_parameter,
	const unsigned rice_parameter_limit,
	const unsigned rice_parameter_search_dist,
	const unsigned partition_order,
	const FLAC__bool search_for_escapes,
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents,
	unsigned *bits
);

static unsigned get_wasted_bits_(FLAC__int32 signal[], unsigned samples);

/* verify-related routines: */
static void append_to_verify_fifo_(
	verify_input_fifo *fifo,
	const FLAC__int32 * const input[],
	unsigned input_offset,
	unsigned channels,
	unsigned wide_samples
);

static void append_to_verify_fifo_interleaved_(
	verify_input_fifo *fifo,
	const FLAC__int32 input[],
	unsigned input_offset,
	unsigned channels,
	unsigned wide_samples
);

static FLAC__StreamDecoderReadStatus verify_read_callback_(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data);
static FLAC__StreamDecoderWriteStatus verify_write_callback_(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[], void *client_data);
static void verify_metadata_callback_(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data);
static void verify_error_callback_(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data);

static FLAC__StreamEncoderReadStatus file_read_callback_(const FLAC__StreamEncoder *encoder, FLAC__byte buffer[], size_t *bytes, void *client_data);
static FLAC__StreamEncoderSeekStatus file_seek_callback_(const FLAC__StreamEncoder *encoder, FLAC__uint64 absolute_byte_offset, void *client_data);
static FLAC__StreamEncoderTellStatus file_tell_callback_(const FLAC__StreamEncoder *encoder, FLAC__uint64 *absolute_byte_offset, void *client_data);
static FLAC__StreamEncoderWriteStatus file_write_callback_(const FLAC__StreamEncoder *encoder, const FLAC__byte buffer[], size_t bytes, unsigned samples, unsigned current_frame, void *client_data);
static FILE *get_binary_stdout_(void);


/***********************************************************************
 *
 * Private class data
 *
 ***********************************************************************/

typedef struct FLAC__StreamEncoderPrivate {
	unsigned input_capacity;                          /* current size (in samples) of the signal and residual buffers */
	FLAC__int32 *integer_signal[FLAC__MAX_CHANNELS];  /* the integer version of the input signal */
	FLAC__int32 *integer_signal_mid_side[2];          /* the integer version of the mid-side input signal (stereo only) */
#ifndef FLAC__INTEGER_ONLY_LIBRARY
	FLAC__real *real_signal[FLAC__MAX_CHANNELS];      /* (@@@ currently unused) the floating-point version of the input signal */
	FLAC__real *real_signal_mid_side[2];              /* (@@@ currently unused) the floating-point version of the mid-side input signal (stereo only) */
	FLAC__real *window[FLAC__MAX_APODIZATION_FUNCTIONS]; /* the pre-computed floating-point window for each apodization function */
	FLAC__real *windowed_signal;                      /* the integer_signal[] * current window[] */
#endif
	unsigned subframe_bps[FLAC__MAX_CHANNELS];        /* the effective bits per sample of the input signal (stream bps - wasted bits) */
	unsigned subframe_bps_mid_side[2];                /* the effective bits per sample of the mid-side input signal (stream bps - wasted bits + 0/1) */
	FLAC__int32 *residual_workspace[FLAC__MAX_CHANNELS][2]; /* each channel has a candidate and best workspace where the subframe residual signals will be stored */
	FLAC__int32 *residual_workspace_mid_side[2][2];
	FLAC__Subframe subframe_workspace[FLAC__MAX_CHANNELS][2];
	FLAC__Subframe subframe_workspace_mid_side[2][2];
	FLAC__Subframe *subframe_workspace_ptr[FLAC__MAX_CHANNELS][2];
	FLAC__Subframe *subframe_workspace_ptr_mid_side[2][2];
	FLAC__EntropyCodingMethod_PartitionedRiceContents partitioned_rice_contents_workspace[FLAC__MAX_CHANNELS][2];
	FLAC__EntropyCodingMethod_PartitionedRiceContents partitioned_rice_contents_workspace_mid_side[FLAC__MAX_CHANNELS][2];
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents_workspace_ptr[FLAC__MAX_CHANNELS][2];
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents_workspace_ptr_mid_side[FLAC__MAX_CHANNELS][2];
	unsigned best_subframe[FLAC__MAX_CHANNELS];       /* index (0 or 1) into 2nd dimension of the above workspaces */
	unsigned best_subframe_mid_side[2];
	unsigned best_subframe_bits[FLAC__MAX_CHANNELS];  /* size in bits of the best subframe for each channel */
	unsigned best_subframe_bits_mid_side[2];
	FLAC__uint64 *abs_residual_partition_sums;        /* workspace where the sum of abs(candidate residual) for each partition is stored */
	unsigned *raw_bits_per_partition;                 /* workspace where the sum of silog2(candidate residual) for each partition is stored */
	FLAC__BitWriter *frame;                           /* the current frame being worked on */
	unsigned loose_mid_side_stereo_frames;            /* rounded number of frames the encoder will use before trying both independent and mid/side frames again */
	unsigned loose_mid_side_stereo_frame_count;       /* number of frames using the current channel assignment */
	FLAC__ChannelAssignment last_channel_assignment;
	FLAC__StreamMetadata streaminfo;                  /* scratchpad for STREAMINFO as it is built */
	FLAC__StreamMetadata_SeekTable *seek_table;       /* pointer into encoder->protected_->metadata_ where the seek table is */
	unsigned current_sample_number;
	unsigned current_frame_number;
	FLAC__MD5Context md5context;
	FLAC__CPUInfo cpuinfo;
#ifndef FLAC__INTEGER_ONLY_LIBRARY
	unsigned (*local_fixed_compute_best_predictor)(const FLAC__int32 data[], unsigned data_len, FLAC__float residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1]);
#else
	unsigned (*local_fixed_compute_best_predictor)(const FLAC__int32 data[], unsigned data_len, FLAC__fixedpoint residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1]);
#endif
#ifndef FLAC__INTEGER_ONLY_LIBRARY
	void (*local_lpc_compute_autocorrelation)(const FLAC__real data[], unsigned data_len, unsigned lag, FLAC__real autoc[]);
	void (*local_lpc_compute_residual_from_qlp_coefficients)(const FLAC__int32 *data, unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 residual[]);
	void (*local_lpc_compute_residual_from_qlp_coefficients_64bit)(const FLAC__int32 *data, unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 residual[]);
	void (*local_lpc_compute_residual_from_qlp_coefficients_16bit)(const FLAC__int32 *data, unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 residual[]);
#endif
	FLAC__bool use_wide_by_block;          /* use slow 64-bit versions of some functions because of the block size */
	FLAC__bool use_wide_by_partition;      /* use slow 64-bit versions of some functions because of the min partition order and blocksize */
	FLAC__bool use_wide_by_order;          /* use slow 64-bit versions of some functions because of the lpc order */
	FLAC__bool disable_constant_subframes;
	FLAC__bool disable_fixed_subframes;
	FLAC__bool disable_verbatim_subframes;
#if FLAC__HAS_OGG
	FLAC__bool is_ogg;
#endif
	FLAC__StreamEncoderReadCallback read_callback; /* currently only needed for Ogg FLAC */
	FLAC__StreamEncoderSeekCallback seek_callback;
	FLAC__StreamEncoderTellCallback tell_callback;
	FLAC__StreamEncoderWriteCallback write_callback;
	FLAC__StreamEncoderMetadataCallback metadata_callback;
	FLAC__StreamEncoderProgressCallback progress_callback;
	void *client_data;
	unsigned first_seekpoint_to_check;
	FILE *file;                            /* only used when encoding to a file */
	FLAC__uint64 bytes_written;
	FLAC__uint64 samples_written;
	unsigned frames_written;
	unsigned total_frames_estimate;
	/* unaligned (original) pointers to allocated data */
	FLAC__int32 *integer_signal_unaligned[FLAC__MAX_CHANNELS];
	FLAC__int32 *integer_signal_mid_side_unaligned[2];
#ifndef FLAC__INTEGER_ONLY_LIBRARY
	FLAC__real *real_signal_unaligned[FLAC__MAX_CHANNELS]; /* (@@@ currently unused) */
	FLAC__real *real_signal_mid_side_unaligned[2]; /* (@@@ currently unused) */
	FLAC__real *window_unaligned[FLAC__MAX_APODIZATION_FUNCTIONS];
	FLAC__real *windowed_signal_unaligned;
#endif
	FLAC__int32 *residual_workspace_unaligned[FLAC__MAX_CHANNELS][2];
	FLAC__int32 *residual_workspace_mid_side_unaligned[2][2];
	FLAC__uint64 *abs_residual_partition_sums_unaligned;
	unsigned *raw_bits_per_partition_unaligned;
	/*
	 * These fields have been moved here from private function local
	 * declarations merely to save stack space during encoding.
	 */
#ifndef FLAC__INTEGER_ONLY_LIBRARY
	FLAC__real lp_coeff[FLAC__MAX_LPC_ORDER][FLAC__MAX_LPC_ORDER]; /* from process_subframe_() */
#endif
	FLAC__EntropyCodingMethod_PartitionedRiceContents partitioned_rice_contents_extra[2]; /* from find_best_partition_order_() */
	/*
	 * The data for the verify section
	 */
	struct {
		FLAC__StreamDecoder *decoder;
		EncoderStateHint state_hint;
		FLAC__bool needs_magic_hack;
		verify_input_fifo input_fifo;
		verify_output output;
		struct {
			FLAC__uint64 absolute_sample;
			unsigned frame_number;
			unsigned channel;
			unsigned sample;
			FLAC__int32 expected;
			FLAC__int32 got;
		} error_stats;
	} verify;
	FLAC__bool is_being_deleted; /* if true, call to ..._finish() from ..._delete() will not call the callbacks */
} FLAC__StreamEncoderPrivate;

/***********************************************************************
 *
 * Public static class data
 *
 ***********************************************************************/

FLAC_API const char * const FLAC__StreamEncoderStateString[] = {
	"FLAC__STREAM_ENCODER_OK",
	"FLAC__STREAM_ENCODER_UNINITIALIZED",
	"FLAC__STREAM_ENCODER_OGG_ERROR",
	"FLAC__STREAM_ENCODER_VERIFY_DECODER_ERROR",
	"FLAC__STREAM_ENCODER_VERIFY_MISMATCH_IN_AUDIO_DATA",
	"FLAC__STREAM_ENCODER_CLIENT_ERROR",
	"FLAC__STREAM_ENCODER_IO_ERROR",
	"FLAC__STREAM_ENCODER_FRAMING_ERROR",
	"FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR"
};

FLAC_API const char * const FLAC__StreamEncoderInitStatusString[] = {
	"FLAC__STREAM_ENCODER_INIT_STATUS_OK",
	"FLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR",
	"FLAC__STREAM_ENCODER_INIT_STATUS_UNSUPPORTED_CONTAINER",
	"FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_CALLBACKS",
	"FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_NUMBER_OF_CHANNELS",
	"FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_BITS_PER_SAMPLE",
	"FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_SAMPLE_RATE",
	"FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_BLOCK_SIZE",
	"FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_MAX_LPC_ORDER",
	"FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_QLP_COEFF_PRECISION",
	"FLAC__STREAM_ENCODER_INIT_STATUS_BLOCK_SIZE_TOO_SMALL_FOR_LPC_ORDER",
	"FLAC__STREAM_ENCODER_INIT_STATUS_NOT_STREAMABLE",
	"FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_METADATA",
	"FLAC__STREAM_ENCODER_INIT_STATUS_ALREADY_INITIALIZED"
};

FLAC_API const char * const FLAC__treamEncoderReadStatusString[] = {
	"FLAC__STREAM_ENCODER_READ_STATUS_CONTINUE",
	"FLAC__STREAM_ENCODER_READ_STATUS_END_OF_STREAM",
	"FLAC__STREAM_ENCODER_READ_STATUS_ABORT",
	"FLAC__STREAM_ENCODER_READ_STATUS_UNSUPPORTED"
};

FLAC_API const char * const FLAC__StreamEncoderWriteStatusString[] = {
	"FLAC__STREAM_ENCODER_WRITE_STATUS_OK",
	"FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR"
};

FLAC_API const char * const FLAC__StreamEncoderSeekStatusString[] = {
	"FLAC__STREAM_ENCODER_SEEK_STATUS_OK",
	"FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR",
	"FLAC__STREAM_ENCODER_SEEK_STATUS_UNSUPPORTED"
};

FLAC_API const char * const FLAC__StreamEncoderTellStatusString[] = {
	"FLAC__STREAM_ENCODER_TELL_STATUS_OK",
	"FLAC__STREAM_ENCODER_TELL_STATUS_ERROR",
	"FLAC__STREAM_ENCODER_TELL_STATUS_UNSUPPORTED"
};

/* Number of samples that will be overread to watch for end of stream.  By
 * 'overread', we mean that the FLAC__stream_encoder_process*() calls will
 * always try to read blocksize+1 samples before encoding a block, so that
 * even if the stream has a total sample count that is an integral multiple
 * of the blocksize, we will still notice when we are encoding the last
 * block.  This is needed, for example, to correctly set the end-of-stream
 * marker in Ogg FLAC.
 *
 * WATCHOUT: some parts of the code assert that OVERREAD_ == 1 and there's
 * not really any reason to change it.
 */
static const unsigned OVERREAD_ = 1;

/***********************************************************************
 *
 * Class constructor/destructor
 *
 */
FLAC_API FLAC__StreamEncoder *FLAC__stream_encoder_new(void)
{
	FLAC__StreamEncoder *encoder;
	unsigned i;

	FLAC__ASSERT(sizeof(int) >= 4); /* we want to die right away if this is not true */

	encoder = (FLAC__StreamEncoder*)calloc(1, sizeof(FLAC__StreamEncoder));
	if(encoder == 0) {
		return 0;
	}

	encoder->protected_ = (FLAC__StreamEncoderProtected*)calloc(1, sizeof(FLAC__StreamEncoderProtected));
	if(encoder->protected_ == 0) {
		free(encoder);
		return 0;
	}

	encoder->private_ = (FLAC__StreamEncoderPrivate*)calloc(1, sizeof(FLAC__StreamEncoderPrivate));
	if(encoder->private_ == 0) {
		free(encoder->protected_);
		free(encoder);
		return 0;
	}

	encoder->private_->frame = FLAC__bitwriter_new();
	if(encoder->private_->frame == 0) {
		free(encoder->private_);
		free(encoder->protected_);
		free(encoder);
		return 0;
	}

	encoder->private_->file = 0;

	set_defaults_(encoder);

	encoder->private_->is_being_deleted = false;

	for(i = 0; i < FLAC__MAX_CHANNELS; i++) {
		encoder->private_->subframe_workspace_ptr[i][0] = &encoder->private_->subframe_workspace[i][0];
		encoder->private_->subframe_workspace_ptr[i][1] = &encoder->private_->subframe_workspace[i][1];
	}
	for(i = 0; i < 2; i++) {
		encoder->private_->subframe_workspace_ptr_mid_side[i][0] = &encoder->private_->subframe_workspace_mid_side[i][0];
		encoder->private_->subframe_workspace_ptr_mid_side[i][1] = &encoder->private_->subframe_workspace_mid_side[i][1];
	}
	for(i = 0; i < FLAC__MAX_CHANNELS; i++) {
		encoder->private_->partitioned_rice_contents_workspace_ptr[i][0] = &encoder->private_->partitioned_rice_contents_workspace[i][0];
		encoder->private_->partitioned_rice_contents_workspace_ptr[i][1] = &encoder->private_->partitioned_rice_contents_workspace[i][1];
	}
	for(i = 0; i < 2; i++) {
		encoder->private_->partitioned_rice_contents_workspace_ptr_mid_side[i][0] = &encoder->private_->partitioned_rice_contents_workspace_mid_side[i][0];
		encoder->private_->partitioned_rice_contents_workspace_ptr_mid_side[i][1] = &encoder->private_->partitioned_rice_contents_workspace_mid_side[i][1];
	}

	for(i = 0; i < FLAC__MAX_CHANNELS; i++) {
		FLAC__format_entropy_coding_method_partitioned_rice_contents_init(&encoder->private_->partitioned_rice_contents_workspace[i][0]);
		FLAC__format_entropy_coding_method_partitioned_rice_contents_init(&encoder->private_->partitioned_rice_contents_workspace[i][1]);
	}
	for(i = 0; i < 2; i++) {
		FLAC__format_entropy_coding_method_partitioned_rice_contents_init(&encoder->private_->partitioned_rice_contents_workspace_mid_side[i][0]);
		FLAC__format_entropy_coding_method_partitioned_rice_contents_init(&encoder->private_->partitioned_rice_contents_workspace_mid_side[i][1]);
	}
	for(i = 0; i < 2; i++)
		FLAC__format_entropy_coding_method_partitioned_rice_contents_init(&encoder->private_->partitioned_rice_contents_extra[i]);

	encoder->protected_->state = FLAC__STREAM_ENCODER_UNINITIALIZED;

	return encoder;
}

FLAC_API void FLAC__stream_encoder_delete(FLAC__StreamEncoder *encoder)
{
	unsigned i;

	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->private_->frame);

	encoder->private_->is_being_deleted = true;

	(void)FLAC__stream_encoder_finish(encoder);

	if(0 != encoder->private_->verify.decoder)
		FLAC__stream_decoder_delete(encoder->private_->verify.decoder);

	for(i = 0; i < FLAC__MAX_CHANNELS; i++) {
		FLAC__format_entropy_coding_method_partitioned_rice_contents_clear(&encoder->private_->partitioned_rice_contents_workspace[i][0]);
		FLAC__format_entropy_coding_method_partitioned_rice_contents_clear(&encoder->private_->partitioned_rice_contents_workspace[i][1]);
	}
	for(i = 0; i < 2; i++) {
		FLAC__format_entropy_coding_method_partitioned_rice_contents_clear(&encoder->private_->partitioned_rice_contents_workspace_mid_side[i][0]);
		FLAC__format_entropy_coding_method_partitioned_rice_contents_clear(&encoder->private_->partitioned_rice_contents_workspace_mid_side[i][1]);
	}
	for(i = 0; i < 2; i++)
		FLAC__format_entropy_coding_method_partitioned_rice_contents_clear(&encoder->private_->partitioned_rice_contents_extra[i]);

	FLAC__bitwriter_delete(encoder->private_->frame);
	free(encoder->private_);
	free(encoder->protected_);
	free(encoder);
}

/***********************************************************************
 *
 * Public class methods
 *
 ***********************************************************************/

static FLAC__StreamEncoderInitStatus init_stream_internal_(
	FLAC__StreamEncoder *encoder,
	FLAC__StreamEncoderReadCallback read_callback,
	FLAC__StreamEncoderWriteCallback write_callback,
	FLAC__StreamEncoderSeekCallback seek_callback,
	FLAC__StreamEncoderTellCallback tell_callback,
	FLAC__StreamEncoderMetadataCallback metadata_callback,
	void *client_data,
	FLAC__bool is_ogg
)
{
	unsigned i;
	FLAC__bool metadata_has_seektable, metadata_has_vorbis_comment, metadata_picture_has_type1, metadata_picture_has_type2;

	FLAC__ASSERT(0 != encoder);

	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return FLAC__STREAM_ENCODER_INIT_STATUS_ALREADY_INITIALIZED;

#if !FLAC__HAS_OGG
	if(is_ogg)
		return FLAC__STREAM_ENCODER_INIT_STATUS_UNSUPPORTED_CONTAINER;
#endif

	if(0 == write_callback || (seek_callback && 0 == tell_callback))
		return FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_CALLBACKS;

	if(encoder->protected_->channels == 0 || encoder->protected_->channels > FLAC__MAX_CHANNELS)
		return FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_NUMBER_OF_CHANNELS;

	if(encoder->protected_->channels != 2) {
		encoder->protected_->do_mid_side_stereo = false;
		encoder->protected_->loose_mid_side_stereo = false;
	}
	else if(!encoder->protected_->do_mid_side_stereo)
		encoder->protected_->loose_mid_side_stereo = false;

	if(encoder->protected_->bits_per_sample >= 32)
		encoder->protected_->do_mid_side_stereo = false; /* since we currenty do 32-bit math, the side channel would have 33 bps and overflow */

	if(encoder->protected_->bits_per_sample < FLAC__MIN_BITS_PER_SAMPLE || encoder->protected_->bits_per_sample > FLAC__REFERENCE_CODEC_MAX_BITS_PER_SAMPLE)
		return FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_BITS_PER_SAMPLE;

	if(!FLAC__format_sample_rate_is_valid(encoder->protected_->sample_rate))
		return FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_SAMPLE_RATE;

	if(encoder->protected_->blocksize == 0) {
		if(encoder->protected_->max_lpc_order == 0)
			encoder->protected_->blocksize = 1152;
		else
			encoder->protected_->blocksize = 4096;
	}

	if(encoder->protected_->blocksize < FLAC__MIN_BLOCK_SIZE || encoder->protected_->blocksize > FLAC__MAX_BLOCK_SIZE)
		return FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_BLOCK_SIZE;

	if(encoder->protected_->max_lpc_order > FLAC__MAX_LPC_ORDER)
		return FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_MAX_LPC_ORDER;

	if(encoder->protected_->blocksize < encoder->protected_->max_lpc_order)
		return FLAC__STREAM_ENCODER_INIT_STATUS_BLOCK_SIZE_TOO_SMALL_FOR_LPC_ORDER;

	if(encoder->protected_->qlp_coeff_precision == 0) {
		if(encoder->protected_->bits_per_sample < 16) {
			/* @@@ need some data about how to set this here w.r.t. blocksize and sample rate */
			/* @@@ until then we'll make a guess */
			encoder->protected_->qlp_coeff_precision = max(FLAC__MIN_QLP_COEFF_PRECISION, 2 + encoder->protected_->bits_per_sample / 2);
		}
		else if(encoder->protected_->bits_per_sample == 16) {
			if(encoder->protected_->blocksize <= 192)
				encoder->protected_->qlp_coeff_precision = 7;
			else if(encoder->protected_->blocksize <= 384)
				encoder->protected_->qlp_coeff_precision = 8;
			else if(encoder->protected_->blocksize <= 576)
				encoder->protected_->qlp_coeff_precision = 9;
			else if(encoder->protected_->blocksize <= 1152)
				encoder->protected_->qlp_coeff_precision = 10;
			else if(encoder->protected_->blocksize <= 2304)
				encoder->protected_->qlp_coeff_precision = 11;
			else if(encoder->protected_->blocksize <= 4608)
				encoder->protected_->qlp_coeff_precision = 12;
			else
				encoder->protected_->qlp_coeff_precision = 13;
		}
		else {
			if(encoder->protected_->blocksize <= 384)
				encoder->protected_->qlp_coeff_precision = FLAC__MAX_QLP_COEFF_PRECISION-2;
			else if(encoder->protected_->blocksize <= 1152)
				encoder->protected_->qlp_coeff_precision = FLAC__MAX_QLP_COEFF_PRECISION-1;
			else
				encoder->protected_->qlp_coeff_precision = FLAC__MAX_QLP_COEFF_PRECISION;
		}
		FLAC__ASSERT(encoder->protected_->qlp_coeff_precision <= FLAC__MAX_QLP_COEFF_PRECISION);
	}
	else if(encoder->protected_->qlp_coeff_precision < FLAC__MIN_QLP_COEFF_PRECISION || encoder->protected_->qlp_coeff_precision > FLAC__MAX_QLP_COEFF_PRECISION)
		return FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_QLP_COEFF_PRECISION;

	if(encoder->protected_->streamable_subset) {
		if(
			encoder->protected_->blocksize != 192 &&
			encoder->protected_->blocksize != 576 &&
			encoder->protected_->blocksize != 1152 &&
			encoder->protected_->blocksize != 2304 &&
			encoder->protected_->blocksize != 4608 &&
			encoder->protected_->blocksize != 256 &&
			encoder->protected_->blocksize != 512 &&
			encoder->protected_->blocksize != 1024 &&
			encoder->protected_->blocksize != 2048 &&
			encoder->protected_->blocksize != 4096 &&
			encoder->protected_->blocksize != 8192 &&
			encoder->protected_->blocksize != 16384
		)
			return FLAC__STREAM_ENCODER_INIT_STATUS_NOT_STREAMABLE;
		if(!FLAC__format_sample_rate_is_subset(encoder->protected_->sample_rate))
			return FLAC__STREAM_ENCODER_INIT_STATUS_NOT_STREAMABLE;
		if(
			encoder->protected_->bits_per_sample != 8 &&
			encoder->protected_->bits_per_sample != 12 &&
			encoder->protected_->bits_per_sample != 16 &&
			encoder->protected_->bits_per_sample != 20 &&
			encoder->protected_->bits_per_sample != 24
		)
			return FLAC__STREAM_ENCODER_INIT_STATUS_NOT_STREAMABLE;
		if(encoder->protected_->max_residual_partition_order > FLAC__SUBSET_MAX_RICE_PARTITION_ORDER)
			return FLAC__STREAM_ENCODER_INIT_STATUS_NOT_STREAMABLE;
		if(
			encoder->protected_->sample_rate <= 48000 &&
			(
				encoder->protected_->blocksize > FLAC__SUBSET_MAX_BLOCK_SIZE_48000HZ ||
				encoder->protected_->max_lpc_order > FLAC__SUBSET_MAX_LPC_ORDER_48000HZ
			)
		) {
			return FLAC__STREAM_ENCODER_INIT_STATUS_NOT_STREAMABLE;
		}
	}

	if(encoder->protected_->max_residual_partition_order >= (1u << FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ORDER_LEN))
		encoder->protected_->max_residual_partition_order = (1u << FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ORDER_LEN) - 1;
	if(encoder->protected_->min_residual_partition_order >= encoder->protected_->max_residual_partition_order)
		encoder->protected_->min_residual_partition_order = encoder->protected_->max_residual_partition_order;

#if FLAC__HAS_OGG
	/* reorder metadata if necessary to ensure that any VORBIS_COMMENT is the first, according to the mapping spec */
	if(is_ogg && 0 != encoder->protected_->metadata && encoder->protected_->num_metadata_blocks > 1) {
		unsigned i;
		for(i = 1; i < encoder->protected_->num_metadata_blocks; i++) {
			if(0 != encoder->protected_->metadata[i] && encoder->protected_->metadata[i]->type == FLAC__METADATA_TYPE_VORBIS_COMMENT) {
				FLAC__StreamMetadata *vc = encoder->protected_->metadata[i];
				for( ; i > 0; i--)
					encoder->protected_->metadata[i] = encoder->protected_->metadata[i-1];
				encoder->protected_->metadata[0] = vc;
				break;
			}
		}
	}
#endif
	/* keep track of any SEEKTABLE block */
	if(0 != encoder->protected_->metadata && encoder->protected_->num_metadata_blocks > 0) {
		unsigned i;
		for(i = 0; i < encoder->protected_->num_metadata_blocks; i++) {
			if(0 != encoder->protected_->metadata[i] && encoder->protected_->metadata[i]->type == FLAC__METADATA_TYPE_SEEKTABLE) {
				encoder->private_->seek_table = &encoder->protected_->metadata[i]->data.seek_table;
				break; /* take only the first one */
			}
		}
	}

	/* validate metadata */
	if(0 == encoder->protected_->metadata && encoder->protected_->num_metadata_blocks > 0)
		return FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_METADATA;
	metadata_has_seektable = false;
	metadata_has_vorbis_comment = false;
	metadata_picture_has_type1 = false;
	metadata_picture_has_type2 = false;
	for(i = 0; i < encoder->protected_->num_metadata_blocks; i++) {
		const FLAC__StreamMetadata *m = encoder->protected_->metadata[i];
		if(m->type == FLAC__METADATA_TYPE_STREAMINFO)
			return FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_METADATA;
		else if(m->type == FLAC__METADATA_TYPE_SEEKTABLE) {
			if(metadata_has_seektable) /* only one is allowed */
				return FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_METADATA;
			metadata_has_seektable = true;
			if(!FLAC__format_seektable_is_legal(&m->data.seek_table))
				return FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_METADATA;
		}
		else if(m->type == FLAC__METADATA_TYPE_VORBIS_COMMENT) {
			if(metadata_has_vorbis_comment) /* only one is allowed */
				return FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_METADATA;
			metadata_has_vorbis_comment = true;
		}
		else if(m->type == FLAC__METADATA_TYPE_CUESHEET) {
			if(!FLAC__format_cuesheet_is_legal(&m->data.cue_sheet, m->data.cue_sheet.is_cd, /*violation=*/0))
				return FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_METADATA;
		}
		else if(m->type == FLAC__METADATA_TYPE_PICTURE) {
			if(!FLAC__format_picture_is_legal(&m->data.picture, /*violation=*/0))
				return FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_METADATA;
			if(m->data.picture.type == FLAC__STREAM_METADATA_PICTURE_TYPE_FILE_ICON_STANDARD) {
				if(metadata_picture_has_type1) /* there should only be 1 per stream */
					return FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_METADATA;
				metadata_picture_has_type1 = true;
				/* standard icon must be 32x32 pixel PNG */
				if(
					m->data.picture.type == FLAC__STREAM_METADATA_PICTURE_TYPE_FILE_ICON_STANDARD && 
					(
						(strcmp(m->data.picture.mime_type, "image/png") && strcmp(m->data.picture.mime_type, "-->")) ||
						m->data.picture.width != 32 ||
						m->data.picture.height != 32
					)
				)
					return FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_METADATA;
			}
			else if(m->data.picture.type == FLAC__STREAM_METADATA_PICTURE_TYPE_FILE_ICON) {
				if(metadata_picture_has_type2) /* there should only be 1 per stream */
					return FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_METADATA;
				metadata_picture_has_type2 = true;
			}
		}
	}

	encoder->private_->input_capacity = 0;
	for(i = 0; i < encoder->protected_->channels; i++) {
		encoder->private_->integer_signal_unaligned[i] = encoder->private_->integer_signal[i] = 0;
#ifndef FLAC__INTEGER_ONLY_LIBRARY
		encoder->private_->real_signal_unaligned[i] = encoder->private_->real_signal[i] = 0;
#endif
	}
	for(i = 0; i < 2; i++) {
		encoder->private_->integer_signal_mid_side_unaligned[i] = encoder->private_->integer_signal_mid_side[i] = 0;
#ifndef FLAC__INTEGER_ONLY_LIBRARY
		encoder->private_->real_signal_mid_side_unaligned[i] = encoder->private_->real_signal_mid_side[i] = 0;
#endif
	}
#ifndef FLAC__INTEGER_ONLY_LIBRARY
	for(i = 0; i < encoder->protected_->num_apodizations; i++)
		encoder->private_->window_unaligned[i] = encoder->private_->window[i] = 0;
	encoder->private_->windowed_signal_unaligned = encoder->private_->windowed_signal = 0;
#endif
	for(i = 0; i < encoder->protected_->channels; i++) {
		encoder->private_->residual_workspace_unaligned[i][0] = encoder->private_->residual_workspace[i][0] = 0;
		encoder->private_->residual_workspace_unaligned[i][1] = encoder->private_->residual_workspace[i][1] = 0;
		encoder->private_->best_subframe[i] = 0;
	}
	for(i = 0; i < 2; i++) {
		encoder->private_->residual_workspace_mid_side_unaligned[i][0] = encoder->private_->residual_workspace_mid_side[i][0] = 0;
		encoder->private_->residual_workspace_mid_side_unaligned[i][1] = encoder->private_->residual_workspace_mid_side[i][1] = 0;
		encoder->private_->best_subframe_mid_side[i] = 0;
	}
	encoder->private_->abs_residual_partition_sums_unaligned = encoder->private_->abs_residual_partition_sums = 0;
	encoder->private_->raw_bits_per_partition_unaligned = encoder->private_->raw_bits_per_partition = 0;
#ifndef FLAC__INTEGER_ONLY_LIBRARY
	encoder->private_->loose_mid_side_stereo_frames = (unsigned)((FLAC__double)encoder->protected_->sample_rate * 0.4 / (FLAC__double)encoder->protected_->blocksize + 0.5);
#else
	/* 26214 is the approximate fixed-point equivalent to 0.4 (0.4 * 2^16) */
	/* sample rate can be up to 655350 Hz, and thus use 20 bits, so we do the multiply&divide by hand */
	FLAC__ASSERT(FLAC__MAX_SAMPLE_RATE <= 655350);
	FLAC__ASSERT(FLAC__MAX_BLOCK_SIZE <= 65535);
	FLAC__ASSERT(encoder->protected_->sample_rate <= 655350);
	FLAC__ASSERT(encoder->protected_->blocksize <= 65535);
	encoder->private_->loose_mid_side_stereo_frames = (unsigned)FLAC__fixedpoint_trunc((((FLAC__uint64)(encoder->protected_->sample_rate) * (FLAC__uint64)(26214)) << 16) / (encoder->protected_->blocksize<<16) + FLAC__FP_ONE_HALF);
#endif
	if(encoder->private_->loose_mid_side_stereo_frames == 0)
		encoder->private_->loose_mid_side_stereo_frames = 1;
	encoder->private_->loose_mid_side_stereo_frame_count = 0;
	encoder->private_->current_sample_number = 0;
	encoder->private_->current_frame_number = 0;

	encoder->private_->use_wide_by_block = (encoder->protected_->bits_per_sample + FLAC__bitmath_ilog2(encoder->protected_->blocksize)+1 > 30);
	encoder->private_->use_wide_by_order = (encoder->protected_->bits_per_sample + FLAC__bitmath_ilog2(max(encoder->protected_->max_lpc_order, FLAC__MAX_FIXED_ORDER))+1 > 30); /*@@@ need to use this? */
	encoder->private_->use_wide_by_partition = (false); /*@@@ need to set this */

	/*
	 * get the CPU info and set the function pointers
	 */
	FLAC__cpu_info(&encoder->private_->cpuinfo);
	/* first default to the non-asm routines */
#ifndef FLAC__INTEGER_ONLY_LIBRARY
	encoder->private_->local_lpc_compute_autocorrelation = FLAC__lpc_compute_autocorrelation;
#endif
	encoder->private_->local_fixed_compute_best_predictor = FLAC__fixed_compute_best_predictor;
#ifndef FLAC__INTEGER_ONLY_LIBRARY
	encoder->private_->local_lpc_compute_residual_from_qlp_coefficients = FLAC__lpc_compute_residual_from_qlp_coefficients;
	encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_64bit = FLAC__lpc_compute_residual_from_qlp_coefficients_wide;
	encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_16bit = FLAC__lpc_compute_residual_from_qlp_coefficients;
#endif
	/* now override with asm where appropriate */
#ifndef FLAC__INTEGER_ONLY_LIBRARY
# ifndef FLAC__NO_ASM
	if(encoder->private_->cpuinfo.use_asm) {
#  ifdef FLAC__CPU_IA32
		FLAC__ASSERT(encoder->private_->cpuinfo.type == FLAC__CPUINFO_TYPE_IA32);
#   ifdef FLAC__HAS_NASM
		if(encoder->private_->cpuinfo.data.ia32.sse) {
			if(encoder->protected_->max_lpc_order < 4)
				encoder->private_->local_lpc_compute_autocorrelation = FLAC__lpc_compute_autocorrelation_asm_ia32_sse_lag_4;
			else if(encoder->protected_->max_lpc_order < 8)
				encoder->private_->local_lpc_compute_autocorrelation = FLAC__lpc_compute_autocorrelation_asm_ia32_sse_lag_8;
			else if(encoder->protected_->max_lpc_order < 12)
				encoder->private_->local_lpc_compute_autocorrelation = FLAC__lpc_compute_autocorrelation_asm_ia32_sse_lag_12;
			else
				encoder->private_->local_lpc_compute_autocorrelation = FLAC__lpc_compute_autocorrelation_asm_ia32;
		}
		else if(encoder->private_->cpuinfo.data.ia32._3dnow)
			encoder->private_->local_lpc_compute_autocorrelation = FLAC__lpc_compute_autocorrelation_asm_ia32_3dnow;
		else
			encoder->private_->local_lpc_compute_autocorrelation = FLAC__lpc_compute_autocorrelation_asm_ia32;
		if(encoder->private_->cpuinfo.data.ia32.mmx) {
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients = FLAC__lpc_compute_residual_from_qlp_coefficients_asm_ia32;
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_16bit = FLAC__lpc_compute_residual_from_qlp_coefficients_asm_ia32_mmx;
		}
		else {
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients = FLAC__lpc_compute_residual_from_qlp_coefficients_asm_ia32;
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_16bit = FLAC__lpc_compute_residual_from_qlp_coefficients_asm_ia32;
		}
		if(encoder->private_->cpuinfo.data.ia32.mmx && encoder->private_->cpuinfo.data.ia32.cmov)
			encoder->private_->local_fixed_compute_best_predictor = FLAC__fixed_compute_best_predictor_asm_ia32_mmx_cmov;
#   endif /* FLAC__HAS_NASM */
#  endif /* FLAC__CPU_IA32 */
	}
# endif /* !FLAC__NO_ASM */
#endif /* !FLAC__INTEGER_ONLY_LIBRARY */
	/* finally override based on wide-ness if necessary */
	if(encoder->private_->use_wide_by_block) {
		encoder->private_->local_fixed_compute_best_predictor = FLAC__fixed_compute_best_predictor_wide;
	}

	/* set state to OK; from here on, errors are fatal and we'll override the state then */
	encoder->protected_->state = FLAC__STREAM_ENCODER_OK;

#if FLAC__HAS_OGG
	encoder->private_->is_ogg = is_ogg;
	if(is_ogg && !FLAC__ogg_encoder_aspect_init(&encoder->protected_->ogg_encoder_aspect)) {
		encoder->protected_->state = FLAC__STREAM_ENCODER_OGG_ERROR;
		return FLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR;
	}
#endif

	encoder->private_->read_callback = read_callback;
	encoder->private_->write_callback = write_callback;
	encoder->private_->seek_callback = seek_callback;
	encoder->private_->tell_callback = tell_callback;
	encoder->private_->metadata_callback = metadata_callback;
	encoder->private_->client_data = client_data;

	if(!resize_buffers_(encoder, encoder->protected_->blocksize)) {
		/* the above function sets the state for us in case of an error */
		return FLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR;
	}

	if(!FLAC__bitwriter_init(encoder->private_->frame)) {
		encoder->protected_->state = FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR;
		return FLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR;
	}

	/*
	 * Set up the verify stuff if necessary
	 */
	if(encoder->protected_->verify) {
		/*
		 * First, set up the fifo which will hold the
		 * original signal to compare against
		 */
		encoder->private_->verify.input_fifo.size = encoder->protected_->blocksize+OVERREAD_;
		for(i = 0; i < encoder->protected_->channels; i++) {
			if(0 == (encoder->private_->verify.input_fifo.data[i] = (FLAC__int32*)safe_malloc_mul_2op_(sizeof(FLAC__int32), /*times*/encoder->private_->verify.input_fifo.size))) {
				encoder->protected_->state = FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR;
				return FLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR;
			}
		}
		encoder->private_->verify.input_fifo.tail = 0;

		/*
		 * Now set up a stream decoder for verification
		 */
		encoder->private_->verify.decoder = FLAC__stream_decoder_new();
		if(0 == encoder->private_->verify.decoder) {
			encoder->protected_->state = FLAC__STREAM_ENCODER_VERIFY_DECODER_ERROR;
			return FLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR;
		}

		if(FLAC__stream_decoder_init_stream(encoder->private_->verify.decoder, verify_read_callback_, /*seek_callback=*/0, /*tell_callback=*/0, /*length_callback=*/0, /*eof_callback=*/0, verify_write_callback_, verify_metadata_callback_, verify_error_callback_, /*client_data=*/encoder) != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
			encoder->protected_->state = FLAC__STREAM_ENCODER_VERIFY_DECODER_ERROR;
			return FLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR;
		}
	}
	encoder->private_->verify.error_stats.absolute_sample = 0;
	encoder->private_->verify.error_stats.frame_number = 0;
	encoder->private_->verify.error_stats.channel = 0;
	encoder->private_->verify.error_stats.sample = 0;
	encoder->private_->verify.error_stats.expected = 0;
	encoder->private_->verify.error_stats.got = 0;

	/*
	 * These must be done before we write any metadata, because that
	 * calls the write_callback, which uses these values.
	 */
	encoder->private_->first_seekpoint_to_check = 0;
	encoder->private_->samples_written = 0;
	encoder->protected_->streaminfo_offset = 0;
	encoder->protected_->seektable_offset = 0;
	encoder->protected_->audio_offset = 0;

	/*
	 * write the stream header
	 */
	if(encoder->protected_->verify)
		encoder->private_->verify.state_hint = ENCODER_IN_MAGIC;
	if(!FLAC__bitwriter_write_raw_uint32(encoder->private_->frame, FLAC__STREAM_SYNC, FLAC__STREAM_SYNC_LEN)) {
		encoder->protected_->state = FLAC__STREAM_ENCODER_FRAMING_ERROR;
		return FLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR;
	}
	if(!write_bitbuffer_(encoder, 0, /*is_last_block=*/false)) {
		/* the above function sets the state for us in case of an error */
		return FLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR;
	}

	/*
	 * write the STREAMINFO metadata block
	 */
	if(encoder->protected_->verify)
		encoder->private_->verify.state_hint = ENCODER_IN_METADATA;
	encoder->private_->streaminfo.type = FLAC__METADATA_TYPE_STREAMINFO;
	encoder->private_->streaminfo.is_last = false; /* we will have at a minimum a VORBIS_COMMENT afterwards */
	encoder->private_->streaminfo.length = FLAC__STREAM_METADATA_STREAMINFO_LENGTH;
	encoder->private_->streaminfo.data.stream_info.min_blocksize = encoder->protected_->blocksize; /* this encoder uses the same blocksize for the whole stream */
	encoder->private_->streaminfo.data.stream_info.max_blocksize = encoder->protected_->blocksize;
	encoder->private_->streaminfo.data.stream_info.min_framesize = 0; /* we don't know this yet; have to fill it in later */
	encoder->private_->streaminfo.data.stream_info.max_framesize = 0; /* we don't know this yet; have to fill it in later */
	encoder->private_->streaminfo.data.stream_info.sample_rate = encoder->protected_->sample_rate;
	encoder->private_->streaminfo.data.stream_info.channels = encoder->protected_->channels;
	encoder->private_->streaminfo.data.stream_info.bits_per_sample = encoder->protected_->bits_per_sample;
	encoder->private_->streaminfo.data.stream_info.total_samples = encoder->protected_->total_samples_estimate; /* we will replace this later with the real total */
	memset(encoder->private_->streaminfo.data.stream_info.md5sum, 0, 16); /* we don't know this yet; have to fill it in later */
	if(encoder->protected_->do_md5)
		FLAC__MD5Init(&encoder->private_->md5context);
	if(!FLAC__add_metadata_block(&encoder->private_->streaminfo, encoder->private_->frame)) {
		encoder->protected_->state = FLAC__STREAM_ENCODER_FRAMING_ERROR;
		return FLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR;
	}
	if(!write_bitbuffer_(encoder, 0, /*is_last_block=*/false)) {
		/* the above function sets the state for us in case of an error */
		return FLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR;
	}

	/*
	 * Now that the STREAMINFO block is written, we can init this to an
	 * absurdly-high value...
	 */
	encoder->private_->streaminfo.data.stream_info.min_framesize = (1u << FLAC__STREAM_METADATA_STREAMINFO_MIN_FRAME_SIZE_LEN) - 1;
	/* ... and clear this to 0 */
	encoder->private_->streaminfo.data.stream_info.total_samples = 0;

	/*
	 * Check to see if the supplied metadata contains a VORBIS_COMMENT;
	 * if not, we will write an empty one (FLAC__add_metadata_block()
	 * automatically supplies the vendor string).
	 *
	 * WATCHOUT: the Ogg FLAC mapping requires us to write this block after
	 * the STREAMINFO.  (In the case that metadata_has_vorbis_comment is
	 * true it will have already insured that the metadata list is properly
	 * ordered.)
	 */
	if(!metadata_has_vorbis_comment) {
		FLAC__StreamMetadata vorbis_comment;
		vorbis_comment.type = FLAC__METADATA_TYPE_VORBIS_COMMENT;
		vorbis_comment.is_last = (encoder->protected_->num_metadata_blocks == 0);
		vorbis_comment.length = 4 + 4; /* MAGIC NUMBER */
		vorbis_comment.data.vorbis_comment.vendor_string.length = 0;
		vorbis_comment.data.vorbis_comment.vendor_string.entry = 0;
		vorbis_comment.data.vorbis_comment.num_comments = 0;
		vorbis_comment.data.vorbis_comment.comments = 0;
		if(!FLAC__add_metadata_block(&vorbis_comment, encoder->private_->frame)) {
			encoder->protected_->state = FLAC__STREAM_ENCODER_FRAMING_ERROR;
			return FLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR;
		}
		if(!write_bitbuffer_(encoder, 0, /*is_last_block=*/false)) {
			/* the above function sets the state for us in case of an error */
			return FLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR;
		}
	}

	/*
	 * write the user's metadata blocks
	 */
	for(i = 0; i < encoder->protected_->num_metadata_blocks; i++) {
		encoder->protected_->metadata[i]->is_last = (i == encoder->protected_->num_metadata_blocks - 1);
		if(!FLAC__add_metadata_block(encoder->protected_->metadata[i], encoder->private_->frame)) {
			encoder->protected_->state = FLAC__STREAM_ENCODER_FRAMING_ERROR;
			return FLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR;
		}
		if(!write_bitbuffer_(encoder, 0, /*is_last_block=*/false)) {
			/* the above function sets the state for us in case of an error */
			return FLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR;
		}
	}

	/* now that all the metadata is written, we save the stream offset */
	if(encoder->private_->tell_callback && encoder->private_->tell_callback(encoder, &encoder->protected_->audio_offset, encoder->private_->client_data) == FLAC__STREAM_ENCODER_TELL_STATUS_ERROR) { /* FLAC__STREAM_ENCODER_TELL_STATUS_UNSUPPORTED just means we didn't get the offset; no error */
		encoder->protected_->state = FLAC__STREAM_ENCODER_CLIENT_ERROR;
		return FLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR;
	}

	if(encoder->protected_->verify)
		encoder->private_->verify.state_hint = ENCODER_IN_AUDIO;

	return FLAC__STREAM_ENCODER_INIT_STATUS_OK;
}

FLAC_API FLAC__StreamEncoderInitStatus FLAC__stream_encoder_init_stream(
	FLAC__StreamEncoder *encoder,
	FLAC__StreamEncoderWriteCallback write_callback,
	FLAC__StreamEncoderSeekCallback seek_callback,
	FLAC__StreamEncoderTellCallback tell_callback,
	FLAC__StreamEncoderMetadataCallback metadata_callback,
	void *client_data
)
{
	return init_stream_internal_(
		encoder,
		/*read_callback=*/0,
		write_callback,
		seek_callback,
		tell_callback,
		metadata_callback,
		client_data,
		/*is_ogg=*/false
	);
}

FLAC_API FLAC__StreamEncoderInitStatus FLAC__stream_encoder_init_ogg_stream(
	FLAC__StreamEncoder *encoder,
	FLAC__StreamEncoderReadCallback read_callback,
	FLAC__StreamEncoderWriteCallback write_callback,
	FLAC__StreamEncoderSeekCallback seek_callback,
	FLAC__StreamEncoderTellCallback tell_callback,
	FLAC__StreamEncoderMetadataCallback metadata_callback,
	void *client_data
)
{
	return init_stream_internal_(
		encoder,
		read_callback,
		write_callback,
		seek_callback,
		tell_callback,
		metadata_callback,
		client_data,
		/*is_ogg=*/true
	);
}
 
static FLAC__StreamEncoderInitStatus init_FILE_internal_(
	FLAC__StreamEncoder *encoder,
	FILE *file,
	FLAC__StreamEncoderProgressCallback progress_callback,
	void *client_data,
	FLAC__bool is_ogg
)
{
	FLAC__StreamEncoderInitStatus init_status;

	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != file);

	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return FLAC__STREAM_ENCODER_INIT_STATUS_ALREADY_INITIALIZED;

	/* double protection */
	if(file == 0) {
		encoder->protected_->state = FLAC__STREAM_ENCODER_IO_ERROR;
		return FLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR;
	}

	/*
	 * To make sure that our file does not go unclosed after an error, we
	 * must assign the FILE pointer before any further error can occur in
	 * this routine.
	 */
	if(file == stdout)
		file = get_binary_stdout_(); /* just to be safe */

	encoder->private_->file = file;

	encoder->private_->progress_callback = progress_callback;
	encoder->private_->bytes_written = 0;
	encoder->private_->samples_written = 0;
	encoder->private_->frames_written = 0;

	init_status = init_stream_internal_(
		encoder,
		encoder->private_->file == stdout? 0 : is_ogg? file_read_callback_ : 0,
		file_write_callback_,
		encoder->private_->file == stdout? 0 : file_seek_callback_,
		encoder->private_->file == stdout? 0 : file_tell_callback_,
		/*metadata_callback=*/0,
		client_data,
		is_ogg
	);
	if(init_status != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
		/* the above function sets the state for us in case of an error */
		return init_status;
	}

	{
		unsigned blocksize = FLAC__stream_encoder_get_blocksize(encoder);

		FLAC__ASSERT(blocksize != 0);
		encoder->private_->total_frames_estimate = (unsigned)((FLAC__stream_encoder_get_total_samples_estimate(encoder) + blocksize - 1) / blocksize);
	}

	return init_status;
}
 
FLAC_API FLAC__StreamEncoderInitStatus FLAC__stream_encoder_init_FILE(
	FLAC__StreamEncoder *encoder,
	FILE *file,
	FLAC__StreamEncoderProgressCallback progress_callback,
	void *client_data
)
{
	return init_FILE_internal_(encoder, file, progress_callback, client_data, /*is_ogg=*/false);
}
 
FLAC_API FLAC__StreamEncoderInitStatus FLAC__stream_encoder_init_ogg_FILE(
	FLAC__StreamEncoder *encoder,
	FILE *file,
	FLAC__StreamEncoderProgressCallback progress_callback,
	void *client_data
)
{
	return init_FILE_internal_(encoder, file, progress_callback, client_data, /*is_ogg=*/true);
}

static FLAC__StreamEncoderInitStatus init_file_internal_(
	FLAC__StreamEncoder *encoder,
	const char *filename,
	FLAC__StreamEncoderProgressCallback progress_callback,
	void *client_data,
	FLAC__bool is_ogg
)
{
	FILE *file;

	FLAC__ASSERT(0 != encoder);

	/*
	 * To make sure that our file does not go unclosed after an error, we
	 * have to do the same entrance checks here that are later performed
	 * in FLAC__stream_encoder_init_FILE() before the FILE* is assigned.
	 */
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return FLAC__STREAM_ENCODER_INIT_STATUS_ALREADY_INITIALIZED;

	file = filename? fopen(filename, "w+b") : stdout;

	if(file == 0) {
		encoder->protected_->state = FLAC__STREAM_ENCODER_IO_ERROR;
		return FLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR;
	}

	return init_FILE_internal_(encoder, file, progress_callback, client_data, is_ogg);
}

FLAC_API FLAC__StreamEncoderInitStatus FLAC__stream_encoder_init_file(
	FLAC__StreamEncoder *encoder,
	const char *filename,
	FLAC__StreamEncoderProgressCallback progress_callback,
	void *client_data
)
{
	return init_file_internal_(encoder, filename, progress_callback, client_data, /*is_ogg=*/false);
}

FLAC_API FLAC__StreamEncoderInitStatus FLAC__stream_encoder_init_ogg_file(
	FLAC__StreamEncoder *encoder,
	const char *filename,
	FLAC__StreamEncoderProgressCallback progress_callback,
	void *client_data
)
{
	return init_file_internal_(encoder, filename, progress_callback, client_data, /*is_ogg=*/true);
}

FLAC_API FLAC__bool FLAC__stream_encoder_finish(FLAC__StreamEncoder *encoder)
{
	FLAC__bool error = false;

	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);

	if(encoder->protected_->state == FLAC__STREAM_ENCODER_UNINITIALIZED)
		return true;

	if(encoder->protected_->state == FLAC__STREAM_ENCODER_OK && !encoder->private_->is_being_deleted) {
		if(encoder->private_->current_sample_number != 0) {
			const FLAC__bool is_fractional_block = encoder->protected_->blocksize != encoder->private_->current_sample_number;
			encoder->protected_->blocksize = encoder->private_->current_sample_number;
			if(!process_frame_(encoder, is_fractional_block, /*is_last_block=*/true))
				error = true;
		}
	}

	if(encoder->protected_->do_md5)
		FLAC__MD5Final(encoder->private_->streaminfo.data.stream_info.md5sum, &encoder->private_->md5context);

	if(!encoder->private_->is_being_deleted) {
		if(encoder->protected_->state == FLAC__STREAM_ENCODER_OK) {
			if(encoder->private_->seek_callback) {
#if FLAC__HAS_OGG
				if(encoder->private_->is_ogg)
					update_ogg_metadata_(encoder);
				else
#endif
				update_metadata_(encoder);

				/* check if an error occurred while updating metadata */
				if(encoder->protected_->state != FLAC__STREAM_ENCODER_OK)
					error = true;
			}
			if(encoder->private_->metadata_callback)
				encoder->private_->metadata_callback(encoder, &encoder->private_->streaminfo, encoder->private_->client_data);
		}

		if(encoder->protected_->verify && 0 != encoder->private_->verify.decoder && !FLAC__stream_decoder_finish(encoder->private_->verify.decoder)) {
			if(!error)
				encoder->protected_->state = FLAC__STREAM_ENCODER_VERIFY_MISMATCH_IN_AUDIO_DATA;
			error = true;
		}
	}

	if(0 != encoder->private_->file) {
		if(encoder->private_->file != stdout)
			fclose(encoder->private_->file);
		encoder->private_->file = 0;
	}

#if FLAC__HAS_OGG
	if(encoder->private_->is_ogg)
		FLAC__ogg_encoder_aspect_finish(&encoder->protected_->ogg_encoder_aspect);
#endif

	free_(encoder);
	set_defaults_(encoder);

	if(!error)
		encoder->protected_->state = FLAC__STREAM_ENCODER_UNINITIALIZED;

	return !error;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_ogg_serial_number(FLAC__StreamEncoder *encoder, long value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
#if FLAC__HAS_OGG
	/* can't check encoder->private_->is_ogg since that's not set until init time */
	FLAC__ogg_encoder_aspect_set_serial_number(&encoder->protected_->ogg_encoder_aspect, value);
	return true;
#else
	(void)value;
	return false;
#endif
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_verify(FLAC__StreamEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
#ifndef FLAC__MANDATORY_VERIFY_WHILE_ENCODING
	encoder->protected_->verify = value;
#endif
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_streamable_subset(FLAC__StreamEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->streamable_subset = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_do_md5(FLAC__StreamEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->do_md5 = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_channels(FLAC__StreamEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->channels = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_bits_per_sample(FLAC__StreamEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->bits_per_sample = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_sample_rate(FLAC__StreamEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->sample_rate = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_compression_level(FLAC__StreamEncoder *encoder, unsigned value)
{
	FLAC__bool ok = true;
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	if(value >= sizeof(compression_levels_)/sizeof(compression_levels_[0]))
		value = sizeof(compression_levels_)/sizeof(compression_levels_[0]) - 1;
	ok &= FLAC__stream_encoder_set_do_mid_side_stereo          (encoder, compression_levels_[value].do_mid_side_stereo);
	ok &= FLAC__stream_encoder_set_loose_mid_side_stereo       (encoder, compression_levels_[value].loose_mid_side_stereo);
#ifndef FLAC__INTEGER_ONLY_LIBRARY
#if 0
	/* was: */
	ok &= FLAC__stream_encoder_set_apodization                 (encoder, compression_levels_[value].apodization);
	/* but it's too hard to specify the string in a locale-specific way */
#else
	encoder->protected_->num_apodizations = 1;
	encoder->protected_->apodizations[0].type = FLAC__APODIZATION_TUKEY;
	encoder->protected_->apodizations[0].parameters.tukey.p = 0.5;
#endif
#endif
	ok &= FLAC__stream_encoder_set_max_lpc_order               (encoder, compression_levels_[value].max_lpc_order);
	ok &= FLAC__stream_encoder_set_qlp_coeff_precision         (encoder, compression_levels_[value].qlp_coeff_precision);
	ok &= FLAC__stream_encoder_set_do_qlp_coeff_prec_search    (encoder, compression_levels_[value].do_qlp_coeff_prec_search);
	ok &= FLAC__stream_encoder_set_do_escape_coding            (encoder, compression_levels_[value].do_escape_coding);
	ok &= FLAC__stream_encoder_set_do_exhaustive_model_search  (encoder, compression_levels_[value].do_exhaustive_model_search);
	ok &= FLAC__stream_encoder_set_min_residual_partition_order(encoder, compression_levels_[value].min_residual_partition_order);
	ok &= FLAC__stream_encoder_set_max_residual_partition_order(encoder, compression_levels_[value].max_residual_partition_order);
	ok &= FLAC__stream_encoder_set_rice_parameter_search_dist  (encoder, compression_levels_[value].rice_parameter_search_dist);
	return ok;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_blocksize(FLAC__StreamEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->blocksize = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_do_mid_side_stereo(FLAC__StreamEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->do_mid_side_stereo = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_loose_mid_side_stereo(FLAC__StreamEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->loose_mid_side_stereo = value;
	return true;
}

/*@@@@add to tests*/
FLAC_API FLAC__bool FLAC__stream_encoder_set_apodization(FLAC__StreamEncoder *encoder, const char *specification)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(0 != specification);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
#ifdef FLAC__INTEGER_ONLY_LIBRARY
	(void)specification; /* silently ignore since we haven't integerized; will always use a rectangular window */
#else
	encoder->protected_->num_apodizations = 0;
	while(1) {
		const char *s = strchr(specification, ';');
		const size_t n = s? (size_t)(s - specification) : strlen(specification);
		if     (n==8  && 0 == strncmp("bartlett"     , specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = FLAC__APODIZATION_BARTLETT;
		else if(n==13 && 0 == strncmp("bartlett_hann", specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = FLAC__APODIZATION_BARTLETT_HANN;
		else if(n==8  && 0 == strncmp("blackman"     , specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = FLAC__APODIZATION_BLACKMAN;
		else if(n==26 && 0 == strncmp("blackman_harris_4term_92db", specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = FLAC__APODIZATION_BLACKMAN_HARRIS_4TERM_92DB_SIDELOBE;
		else if(n==6  && 0 == strncmp("connes"       , specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = FLAC__APODIZATION_CONNES;
		else if(n==7  && 0 == strncmp("flattop"      , specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = FLAC__APODIZATION_FLATTOP;
		else if(n>7   && 0 == strncmp("gauss("       , specification, 6)) {
			FLAC__real stddev = (FLAC__real)strtod(specification+6, 0);
			if (stddev > 0.0 && stddev <= 0.5) {
				encoder->protected_->apodizations[encoder->protected_->num_apodizations].parameters.gauss.stddev = stddev;
				encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = FLAC__APODIZATION_GAUSS;
			}
		}
		else if(n==7  && 0 == strncmp("hamming"      , specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = FLAC__APODIZATION_HAMMING;
		else if(n==4  && 0 == strncmp("hann"         , specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = FLAC__APODIZATION_HANN;
		else if(n==13 && 0 == strncmp("kaiser_bessel", specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = FLAC__APODIZATION_KAISER_BESSEL;
		else if(n==7  && 0 == strncmp("nuttall"      , specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = FLAC__APODIZATION_NUTTALL;
		else if(n==9  && 0 == strncmp("rectangle"    , specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = FLAC__APODIZATION_RECTANGLE;
		else if(n==8  && 0 == strncmp("triangle"     , specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = FLAC__APODIZATION_TRIANGLE;
		else if(n>7   && 0 == strncmp("tukey("       , specification, 6)) {
			FLAC__real p = (FLAC__real)strtod(specification+6, 0);
			if (p >= 0.0 && p <= 1.0) {
				encoder->protected_->apodizations[encoder->protected_->num_apodizations].parameters.tukey.p = p;
				encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = FLAC__APODIZATION_TUKEY;
			}
		}
		else if(n==5  && 0 == strncmp("welch"        , specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = FLAC__APODIZATION_WELCH;
		if (encoder->protected_->num_apodizations == 32)
			break;
		if (s)
			specification = s+1;
		else
			break;
	}
	if(encoder->protected_->num_apodizations == 0) {
		encoder->protected_->num_apodizations = 1;
		encoder->protected_->apodizations[0].type = FLAC__APODIZATION_TUKEY;
		encoder->protected_->apodizations[0].parameters.tukey.p = 0.5;
	}
#endif
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_max_lpc_order(FLAC__StreamEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->max_lpc_order = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_qlp_coeff_precision(FLAC__StreamEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->qlp_coeff_precision = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_do_qlp_coeff_prec_search(FLAC__StreamEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->do_qlp_coeff_prec_search = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_do_escape_coding(FLAC__StreamEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
#if 0
	/*@@@ deprecated: */
	encoder->protected_->do_escape_coding = value;
#else
	(void)value;
#endif
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_do_exhaustive_model_search(FLAC__StreamEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->do_exhaustive_model_search = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_min_residual_partition_order(FLAC__StreamEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->min_residual_partition_order = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_max_residual_partition_order(FLAC__StreamEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->max_residual_partition_order = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_rice_parameter_search_dist(FLAC__StreamEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
#if 0
	/*@@@ deprecated: */
	encoder->protected_->rice_parameter_search_dist = value;
#else
	(void)value;
#endif
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_total_samples_estimate(FLAC__StreamEncoder *encoder, FLAC__uint64 value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->total_samples_estimate = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_metadata(FLAC__StreamEncoder *encoder, FLAC__StreamMetadata **metadata, unsigned num_blocks)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	if(0 == metadata)
		num_blocks = 0;
	if(0 == num_blocks)
		metadata = 0;
	/* realloc() does not do exactly what we want so... */
	if(encoder->protected_->metadata) {
		free(encoder->protected_->metadata);
		encoder->protected_->metadata = 0;
		encoder->protected_->num_metadata_blocks = 0;
	}
	if(num_blocks) {
		FLAC__StreamMetadata **m;
		if(0 == (m = (FLAC__StreamMetadata**)safe_malloc_mul_2op_(sizeof(m[0]), /*times*/num_blocks)))
			return false;
		memcpy(m, metadata, sizeof(m[0]) * num_blocks);
		encoder->protected_->metadata = m;
		encoder->protected_->num_metadata_blocks = num_blocks;
	}
#if FLAC__HAS_OGG
	if(!FLAC__ogg_encoder_aspect_set_num_metadata(&encoder->protected_->ogg_encoder_aspect, num_blocks))
		return false;
#endif
	return true;
}

/*
 * These three functions are not static, but not publically exposed in
 * include/FLAC/ either.  They are used by the test suite.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_disable_constant_subframes(FLAC__StreamEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->private_->disable_constant_subframes = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_disable_fixed_subframes(FLAC__StreamEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->private_->disable_fixed_subframes = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_disable_verbatim_subframes(FLAC__StreamEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->private_->disable_verbatim_subframes = value;
	return true;
}

FLAC_API FLAC__StreamEncoderState FLAC__stream_encoder_get_state(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->state;
}

FLAC_API FLAC__StreamDecoderState FLAC__stream_encoder_get_verify_decoder_state(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->verify)
		return FLAC__stream_decoder_get_state(encoder->private_->verify.decoder);
	else
		return FLAC__STREAM_DECODER_UNINITIALIZED;
}

FLAC_API const char *FLAC__stream_encoder_get_resolved_state_string(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_VERIFY_DECODER_ERROR)
		return FLAC__StreamEncoderStateString[encoder->protected_->state];
	else
		return FLAC__stream_decoder_get_resolved_state_string(encoder->private_->verify.decoder);
}

FLAC_API void FLAC__stream_encoder_get_verify_decoder_error_stats(const FLAC__StreamEncoder *encoder, FLAC__uint64 *absolute_sample, unsigned *frame_number, unsigned *channel, unsigned *sample, FLAC__int32 *expected, FLAC__int32 *got)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	if(0 != absolute_sample)
		*absolute_sample = encoder->private_->verify.error_stats.absolute_sample;
	if(0 != frame_number)
		*frame_number = encoder->private_->verify.error_stats.frame_number;
	if(0 != channel)
		*channel = encoder->private_->verify.error_stats.channel;
	if(0 != sample)
		*sample = encoder->private_->verify.error_stats.sample;
	if(0 != expected)
		*expected = encoder->private_->verify.error_stats.expected;
	if(0 != got)
		*got = encoder->private_->verify.error_stats.got;
}

FLAC_API FLAC__bool FLAC__stream_encoder_get_verify(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->verify;
}

FLAC_API FLAC__bool FLAC__stream_encoder_get_streamable_subset(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->streamable_subset;
}

FLAC_API FLAC__bool FLAC__stream_encoder_get_do_md5(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->do_md5;
}

FLAC_API unsigned FLAC__stream_encoder_get_channels(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->channels;
}

FLAC_API unsigned FLAC__stream_encoder_get_bits_per_sample(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->bits_per_sample;
}

FLAC_API unsigned FLAC__stream_encoder_get_sample_rate(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->sample_rate;
}

FLAC_API unsigned FLAC__stream_encoder_get_blocksize(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->blocksize;
}

FLAC_API FLAC__bool FLAC__stream_encoder_get_do_mid_side_stereo(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->do_mid_side_stereo;
}

FLAC_API FLAC__bool FLAC__stream_encoder_get_loose_mid_side_stereo(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->loose_mid_side_stereo;
}

FLAC_API unsigned FLAC__stream_encoder_get_max_lpc_order(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->max_lpc_order;
}

FLAC_API unsigned FLAC__stream_encoder_get_qlp_coeff_precision(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->qlp_coeff_precision;
}

FLAC_API FLAC__bool FLAC__stream_encoder_get_do_qlp_coeff_prec_search(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->do_qlp_coeff_prec_search;
}

FLAC_API FLAC__bool FLAC__stream_encoder_get_do_escape_coding(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->do_escape_coding;
}

FLAC_API FLAC__bool FLAC__stream_encoder_get_do_exhaustive_model_search(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->do_exhaustive_model_search;
}

FLAC_API unsigned FLAC__stream_encoder_get_min_residual_partition_order(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->min_residual_partition_order;
}

FLAC_API unsigned FLAC__stream_encoder_get_max_residual_partition_order(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->max_residual_partition_order;
}

FLAC_API unsigned FLAC__stream_encoder_get_rice_parameter_search_dist(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->rice_parameter_search_dist;
}

FLAC_API FLAC__uint64 FLAC__stream_encoder_get_total_samples_estimate(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->total_samples_estimate;
}

FLAC_API FLAC__bool FLAC__stream_encoder_process(FLAC__StreamEncoder *encoder, const FLAC__int32 * const buffer[], unsigned samples)
{
	unsigned i, j = 0, channel;
	const unsigned channels = encoder->protected_->channels, blocksize = encoder->protected_->blocksize;

	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(encoder->protected_->state == FLAC__STREAM_ENCODER_OK);

	do {
		const unsigned n = min(blocksize+OVERREAD_-encoder->private_->current_sample_number, samples-j);

		if(encoder->protected_->verify)
			append_to_verify_fifo_(&encoder->private_->verify.input_fifo, buffer, j, channels, n);

		for(channel = 0; channel < channels; channel++)
			memcpy(&encoder->private_->integer_signal[channel][encoder->private_->current_sample_number], &buffer[channel][j], sizeof(buffer[channel][0]) * n);

		if(encoder->protected_->do_mid_side_stereo) {
			FLAC__ASSERT(channels == 2);
			/* "i <= blocksize" to overread 1 sample; see comment in OVERREAD_ decl */
			for(i = encoder->private_->current_sample_number; i <= blocksize && j < samples; i++, j++) {
				encoder->private_->integer_signal_mid_side[1][i] = buffer[0][j] - buffer[1][j];
				encoder->private_->integer_signal_mid_side[0][i] = (buffer[0][j] + buffer[1][j]) >> 1; /* NOTE: not the same as 'mid = (buffer[0][j] + buffer[1][j]) / 2' ! */
			}
		}
		else
			j += n;

		encoder->private_->current_sample_number += n;

		/* we only process if we have a full block + 1 extra sample; final block is always handled by FLAC__stream_encoder_finish() */
		if(encoder->private_->current_sample_number > blocksize) {
			FLAC__ASSERT(encoder->private_->current_sample_number == blocksize+OVERREAD_);
			FLAC__ASSERT(OVERREAD_ == 1); /* assert we only overread 1 sample which simplifies the rest of the code below */
			if(!process_frame_(encoder, /*is_fractional_block=*/false, /*is_last_block=*/false))
				return false;
			/* move unprocessed overread samples to beginnings of arrays */
			for(channel = 0; channel < channels; channel++)
				encoder->private_->integer_signal[channel][0] = encoder->private_->integer_signal[channel][blocksize];
			if(encoder->protected_->do_mid_side_stereo) {
				encoder->private_->integer_signal_mid_side[0][0] = encoder->private_->integer_signal_mid_side[0][blocksize];
				encoder->private_->integer_signal_mid_side[1][0] = encoder->private_->integer_signal_mid_side[1][blocksize];
			}
			encoder->private_->current_sample_number = 1;
		}
	} while(j < samples);

	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_process_interleaved(FLAC__StreamEncoder *encoder, const FLAC__int32 buffer[], unsigned samples)
{
	unsigned i, j, k, channel;
	FLAC__int32 x, mid, side;
	const unsigned channels = encoder->protected_->channels, blocksize = encoder->protected_->blocksize;

	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(encoder->protected_->state == FLAC__STREAM_ENCODER_OK);

	j = k = 0;
	/*
	 * we have several flavors of the same basic loop, optimized for
	 * different conditions:
	 */
	if(encoder->protected_->do_mid_side_stereo && channels == 2) {
		/*
		 * stereo coding: unroll channel loop
		 */
		do {
			if(encoder->protected_->verify)
				append_to_verify_fifo_interleaved_(&encoder->private_->verify.input_fifo, buffer, j, channels, min(blocksize+OVERREAD_-encoder->private_->current_sample_number, samples-j));

			/* "i <= blocksize" to overread 1 sample; see comment in OVERREAD_ decl */
			for(i = encoder->private_->current_sample_number; i <= blocksize && j < samples; i++, j++) {
				encoder->private_->integer_signal[0][i] = mid = side = buffer[k++];
				x = buffer[k++];
				encoder->private_->integer_signal[1][i] = x;
				mid += x;
				side -= x;
				mid >>= 1; /* NOTE: not the same as 'mid = (left + right) / 2' ! */
				encoder->private_->integer_signal_mid_side[1][i] = side;
				encoder->private_->integer_signal_mid_side[0][i] = mid;
			}
			encoder->private_->current_sample_number = i;
			/* we only process if we have a full block + 1 extra sample; final block is always handled by FLAC__stream_encoder_finish() */
			if(i > blocksize) {
				if(!process_frame_(encoder, /*is_fractional_block=*/false, /*is_last_block=*/false))
					return false;
				/* move unprocessed overread samples to beginnings of arrays */
				FLAC__ASSERT(i == blocksize+OVERREAD_);
				FLAC__ASSERT(OVERREAD_ == 1); /* assert we only overread 1 sample which simplifies the rest of the code below */
				encoder->private_->integer_signal[0][0] = encoder->private_->integer_signal[0][blocksize];
				encoder->private_->integer_signal[1][0] = encoder->private_->integer_signal[1][blocksize];
				encoder->private_->integer_signal_mid_side[0][0] = encoder->private_->integer_signal_mid_side[0][blocksize];
				encoder->private_->integer_signal_mid_side[1][0] = encoder->private_->integer_signal_mid_side[1][blocksize];
				encoder->private_->current_sample_number = 1;
			}
		} while(j < samples);
	}
	else {
		/*
		 * independent channel coding: buffer each channel in inner loop
		 */
		do {
			if(encoder->protected_->verify)
				append_to_verify_fifo_interleaved_(&encoder->private_->verify.input_fifo, buffer, j, channels, min(blocksize+OVERREAD_-encoder->private_->current_sample_number, samples-j));

			/* "i <= blocksize" to overread 1 sample; see comment in OVERREAD_ decl */
			for(i = encoder->private_->current_sample_number; i <= blocksize && j < samples; i++, j++) {
				for(channel = 0; channel < channels; channel++)
					encoder->private_->integer_signal[channel][i] = buffer[k++];
			}
			encoder->private_->current_sample_number = i;
			/* we only process if we have a full block + 1 extra sample; final block is always handled by FLAC__stream_encoder_finish() */
			if(i > blocksize) {
				if(!process_frame_(encoder, /*is_fractional_block=*/false, /*is_last_block=*/false))
					return false;
				/* move unprocessed overread samples to beginnings of arrays */
				FLAC__ASSERT(i == blocksize+OVERREAD_);
				FLAC__ASSERT(OVERREAD_ == 1); /* assert we only overread 1 sample which simplifies the rest of the code below */
				for(channel = 0; channel < channels; channel++)
					encoder->private_->integer_signal[channel][0] = encoder->private_->integer_signal[channel][blocksize];
				encoder->private_->current_sample_number = 1;
			}
		} while(j < samples);
	}

	return true;
}

/***********************************************************************
 *
 * Private class methods
 *
 ***********************************************************************/

void set_defaults_(FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);

#ifdef FLAC__MANDATORY_VERIFY_WHILE_ENCODING
	encoder->protected_->verify = true;
#else
	encoder->protected_->verify = false;
#endif
	encoder->protected_->streamable_subset = true;
	encoder->protected_->do_md5 = true;
	encoder->protected_->do_mid_side_stereo = false;
	encoder->protected_->loose_mid_side_stereo = false;
	encoder->protected_->channels = 2;
	encoder->protected_->bits_per_sample = 16;
	encoder->protected_->sample_rate = 44100;
	encoder->protected_->blocksize = 0;
#ifndef FLAC__INTEGER_ONLY_LIBRARY
	encoder->protected_->num_apodizations = 1;
	encoder->protected_->apodizations[0].type = FLAC__APODIZATION_TUKEY;
	encoder->protected_->apodizations[0].parameters.tukey.p = 0.5;
#endif
	encoder->protected_->max_lpc_order = 0;
	encoder->protected_->qlp_coeff_precision = 0;
	encoder->protected_->do_qlp_coeff_prec_search = false;
	encoder->protected_->do_exhaustive_model_search = false;
	encoder->protected_->do_escape_coding = false;
	encoder->protected_->min_residual_partition_order = 0;
	encoder->protected_->max_residual_partition_order = 0;
	encoder->protected_->rice_parameter_search_dist = 0;
	encoder->protected_->total_samples_estimate = 0;
	encoder->protected_->metadata = 0;
	encoder->protected_->num_metadata_blocks = 0;

	encoder->private_->seek_table = 0;
	encoder->private_->disable_constant_subframes = false;
	encoder->private_->disable_fixed_subframes = false;
	encoder->private_->disable_verbatim_subframes = false;
#if FLAC__HAS_OGG
	encoder->private_->is_ogg = false;
#endif
	encoder->private_->read_callback = 0;
	encoder->private_->write_callback = 0;
	encoder->private_->seek_callback = 0;
	encoder->private_->tell_callback = 0;
	encoder->private_->metadata_callback = 0;
	encoder->private_->progress_callback = 0;
	encoder->private_->client_data = 0;

#if FLAC__HAS_OGG
	FLAC__ogg_encoder_aspect_set_defaults(&encoder->protected_->ogg_encoder_aspect);
#endif
}

void free_(FLAC__StreamEncoder *encoder)
{
	unsigned i, channel;

	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->metadata) {
		free(encoder->protected_->metadata);
		encoder->protected_->metadata = 0;
		encoder->protected_->num_metadata_blocks = 0;
	}
	for(i = 0; i < encoder->protected_->channels; i++) {
		if(0 != encoder->private_->integer_signal_unaligned[i]) {
			free(encoder->private_->integer_signal_unaligned[i]);
			encoder->private_->integer_signal_unaligned[i] = 0;
		}
#ifndef FLAC__INTEGER_ONLY_LIBRARY
		if(0 != encoder->private_->real_signal_unaligned[i]) {
			free(encoder->private_->real_signal_unaligned[i]);
			encoder->private_->real_signal_unaligned[i] = 0;
		}
#endif
	}
	for(i = 0; i < 2; i++) {
		if(0 != encoder->private_->integer_signal_mid_side_unaligned[i]) {
			free(encoder->private_->integer_signal_mid_side_unaligned[i]);
			encoder->private_->integer_signal_mid_side_unaligned[i] = 0;
		}
#ifndef FLAC__INTEGER_ONLY_LIBRARY
		if(0 != encoder->private_->real_signal_mid_side_unaligned[i]) {
			free(encoder->private_->real_signal_mid_side_unaligned[i]);
			encoder->private_->real_signal_mid_side_unaligned[i] = 0;
		}
#endif
	}
#ifndef FLAC__INTEGER_ONLY_LIBRARY
	for(i = 0; i < encoder->protected_->num_apodizations; i++) {
		if(0 != encoder->private_->window_unaligned[i]) {
			free(encoder->private_->window_unaligned[i]);
			encoder->private_->window_unaligned[i] = 0;
		}
	}
	if(0 != encoder->private_->windowed_signal_unaligned) {
		free(encoder->private_->windowed_signal_unaligned);
		encoder->private_->windowed_signal_unaligned = 0;
	}
#endif
	for(channel = 0; channel < encoder->protected_->channels; channel++) {
		for(i = 0; i < 2; i++) {
			if(0 != encoder->private_->residual_workspace_unaligned[channel][i]) {
				free(encoder->private_->residual_workspace_unaligned[channel][i]);
				encoder->private_->residual_workspace_unaligned[channel][i] = 0;
			}
		}
	}
	for(channel = 0; channel < 2; channel++) {
		for(i = 0; i < 2; i++) {
			if(0 != encoder->private_->residual_workspace_mid_side_unaligned[channel][i]) {
				free(encoder->private_->residual_workspace_mid_side_unaligned[channel][i]);
				encoder->private_->residual_workspace_mid_side_unaligned[channel][i] = 0;
			}
		}
	}
	if(0 != encoder->private_->abs_residual_partition_sums_unaligned) {
		free(encoder->private_->abs_residual_partition_sums_unaligned);
		encoder->private_->abs_residual_partition_sums_unaligned = 0;
	}
	if(0 != encoder->private_->raw_bits_per_partition_unaligned) {
		free(encoder->private_->raw_bits_per_partition_unaligned);
		encoder->private_->raw_bits_per_partition_unaligned = 0;
	}
	if(encoder->protected_->verify) {
		for(i = 0; i < encoder->protected_->channels; i++) {
			if(0 != encoder->private_->verify.input_fifo.data[i]) {
				free(encoder->private_->verify.input_fifo.data[i]);
				encoder->private_->verify.input_fifo.data[i] = 0;
			}
		}
	}
	FLAC__bitwriter_free(encoder->private_->frame);
}

FLAC__bool resize_buffers_(FLAC__StreamEncoder *encoder, unsigned new_blocksize)
{
	FLAC__bool ok;
	unsigned i, channel;

	FLAC__ASSERT(new_blocksize > 0);
	FLAC__ASSERT(encoder->protected_->state == FLAC__STREAM_ENCODER_OK);
	FLAC__ASSERT(encoder->private_->current_sample_number == 0);

	/* To avoid excessive malloc'ing, we only grow the buffer; no shrinking. */
	if(new_blocksize <= encoder->private_->input_capacity)
		return true;

	ok = true;

	/* WATCHOUT: FLAC__lpc_compute_residual_from_qlp_coefficients_asm_ia32_mmx()
	 * requires that the input arrays (in our case the integer signals)
	 * have a buffer of up to 3 zeroes in front (at negative indices) for
	 * alignment purposes; we use 4 in front to keep the data well-aligned.
	 */

	for(i = 0; ok && i < encoder->protected_->channels; i++) {
		ok = ok && FLAC__memory_alloc_aligned_int32_array(new_blocksize+4+OVERREAD_, &encoder->private_->integer_signal_unaligned[i], &encoder->private_->integer_signal[i]);
		memset(encoder->private_->integer_signal[i], 0, sizeof(FLAC__int32)*4);
		encoder->private_->integer_signal[i] += 4;
#ifndef FLAC__INTEGER_ONLY_LIBRARY
#if 0 /* @@@ currently unused */
		if(encoder->protected_->max_lpc_order > 0)
			ok = ok && FLAC__memory_alloc_aligned_real_array(new_blocksize+OVERREAD_, &encoder->private_->real_signal_unaligned[i], &encoder->private_->real_signal[i]);
#endif
#endif
	}
	for(i = 0; ok && i < 2; i++) {
		ok = ok && FLAC__memory_alloc_aligned_int32_array(new_blocksize+4+OVERREAD_, &encoder->private_->integer_signal_mid_side_unaligned[i], &encoder->private_->integer_signal_mid_side[i]);
		memset(encoder->private_->integer_signal_mid_side[i], 0, sizeof(FLAC__int32)*4);
		encoder->private_->integer_signal_mid_side[i] += 4;
#ifndef FLAC__INTEGER_ONLY_LIBRARY
#if 0 /* @@@ currently unused */
		if(encoder->protected_->max_lpc_order > 0)
			ok = ok && FLAC__memory_alloc_aligned_real_array(new_blocksize+OVERREAD_, &encoder->private_->real_signal_mid_side_unaligned[i], &encoder->private_->real_signal_mid_side[i]);
#endif
#endif
	}
#ifndef FLAC__INTEGER_ONLY_LIBRARY
	if(ok && encoder->protected_->max_lpc_order > 0) {
		for(i = 0; ok && i < encoder->protected_->num_apodizations; i++)
			ok = ok && FLAC__memory_alloc_aligned_real_array(new_blocksize, &encoder->private_->window_unaligned[i], &encoder->private_->window[i]);
		ok = ok && FLAC__memory_alloc_aligned_real_array(new_blocksize, &encoder->private_->windowed_signal_unaligned, &encoder->private_->windowed_signal);
	}
#endif
	for(channel = 0; ok && channel < encoder->protected_->channels; channel++) {
		for(i = 0; ok && i < 2; i++) {
			ok = ok && FLAC__memory_alloc_aligned_int32_array(new_blocksize, &encoder->private_->residual_workspace_unaligned[channel][i], &encoder->private_->residual_workspace[channel][i]);
		}
	}
	for(channel = 0; ok && channel < 2; channel++) {
		for(i = 0; ok && i < 2; i++) {
			ok = ok && FLAC__memory_alloc_aligned_int32_array(new_blocksize, &encoder->private_->residual_workspace_mid_side_unaligned[channel][i], &encoder->private_->residual_workspace_mid_side[channel][i]);
		}
	}
	/* the *2 is an approximation to the series 1 + 1/2 + 1/4 + ... that sums tree occupies in a flat array */
	/*@@@ new_blocksize*2 is too pessimistic, but to fix, we need smarter logic because a smaller new_blocksize can actually increase the # of partitions; would require moving this out into a separate function, then checking its capacity against the need of the current blocksize&min/max_partition_order (and maybe predictor order) */
	ok = ok && FLAC__memory_alloc_aligned_uint64_array(new_blocksize * 2, &encoder->private_->abs_residual_partition_sums_unaligned, &encoder->private_->abs_residual_partition_sums);
	if(encoder->protected_->do_escape_coding)
		ok = ok && FLAC__memory_alloc_aligned_unsigned_array(new_blocksize * 2, &encoder->private_->raw_bits_per_partition_unaligned, &encoder->private_->raw_bits_per_partition);

	/* now adjust the windows if the blocksize has changed */
#ifndef FLAC__INTEGER_ONLY_LIBRARY
	if(ok && new_blocksize != encoder->private_->input_capacity && encoder->protected_->max_lpc_order > 0) {
		for(i = 0; ok && i < encoder->protected_->num_apodizations; i++) {
			switch(encoder->protected_->apodizations[i].type) {
				case FLAC__APODIZATION_BARTLETT:
					FLAC__window_bartlett(encoder->private_->window[i], new_blocksize);
					break;
				case FLAC__APODIZATION_BARTLETT_HANN:
					FLAC__window_bartlett_hann(encoder->private_->window[i], new_blocksize);
					break;
				case FLAC__APODIZATION_BLACKMAN:
					FLAC__window_blackman(encoder->private_->window[i], new_blocksize);
					break;
				case FLAC__APODIZATION_BLACKMAN_HARRIS_4TERM_92DB_SIDELOBE:
					FLAC__window_blackman_harris_4term_92db_sidelobe(encoder->private_->window[i], new_blocksize);
					break;
				case FLAC__APODIZATION_CONNES:
					FLAC__window_connes(encoder->private_->window[i], new_blocksize);
					break;
				case FLAC__APODIZATION_FLATTOP:
					FLAC__window_flattop(encoder->private_->window[i], new_blocksize);
					break;
				case FLAC__APODIZATION_GAUSS:
					FLAC__window_gauss(encoder->private_->window[i], new_blocksize, encoder->protected_->apodizations[i].parameters.gauss.stddev);
					break;
				case FLAC__APODIZATION_HAMMING:
					FLAC__window_hamming(encoder->private_->window[i], new_blocksize);
					break;
				case FLAC__APODIZATION_HANN:
					FLAC__window_hann(encoder->private_->window[i], new_blocksize);
					break;
				case FLAC__APODIZATION_KAISER_BESSEL:
					FLAC__window_kaiser_bessel(encoder->private_->window[i], new_blocksize);
					break;
				case FLAC__APODIZATION_NUTTALL:
					FLAC__window_nuttall(encoder->private_->window[i], new_blocksize);
					break;
				case FLAC__APODIZATION_RECTANGLE:
					FLAC__window_rectangle(encoder->private_->window[i], new_blocksize);
					break;
				case FLAC__APODIZATION_TRIANGLE:
					FLAC__window_triangle(encoder->private_->window[i], new_blocksize);
					break;
				case FLAC__APODIZATION_TUKEY:
					FLAC__window_tukey(encoder->private_->window[i], new_blocksize, encoder->protected_->apodizations[i].parameters.tukey.p);
					break;
				case FLAC__APODIZATION_WELCH:
					FLAC__window_welch(encoder->private_->window[i], new_blocksize);
					break;
				default:
					FLAC__ASSERT(0);
					/* double protection */
					FLAC__window_hann(encoder->private_->window[i], new_blocksize);
					break;
			}
		}
	}
#endif

	if(ok)
		encoder->private_->input_capacity = new_blocksize;
	else
		encoder->protected_->state = FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR;

	return ok;
}

FLAC__bool write_bitbuffer_(FLAC__StreamEncoder *encoder, unsigned samples, FLAC__bool is_last_block)
{
	const FLAC__byte *buffer;
	size_t bytes;

	FLAC__ASSERT(FLAC__bitwriter_is_byte_aligned(encoder->private_->frame));

	if(!FLAC__bitwriter_get_buffer(encoder->private_->frame, &buffer, &bytes)) {
		encoder->protected_->state = FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}

	if(encoder->protected_->verify) {
		encoder->private_->verify.output.data = buffer;
		encoder->private_->verify.output.bytes = bytes;
		if(encoder->private_->verify.state_hint == ENCODER_IN_MAGIC) {
			encoder->private_->verify.needs_magic_hack = true;
		}
		else {
			if(!FLAC__stream_decoder_process_single(encoder->private_->verify.decoder)) {
				FLAC__bitwriter_release_buffer(encoder->private_->frame);
				FLAC__bitwriter_clear(encoder->private_->frame);
				if(encoder->protected_->state != FLAC__STREAM_ENCODER_VERIFY_MISMATCH_IN_AUDIO_DATA)
					encoder->protected_->state = FLAC__STREAM_ENCODER_VERIFY_DECODER_ERROR;
				return false;
			}
		}
	}

	if(write_frame_(encoder, buffer, bytes, samples, is_last_block) != FLAC__STREAM_ENCODER_WRITE_STATUS_OK) {
		FLAC__bitwriter_release_buffer(encoder->private_->frame);
		FLAC__bitwriter_clear(encoder->private_->frame);
		encoder->protected_->state = FLAC__STREAM_ENCODER_CLIENT_ERROR;
		return false;
	}

	FLAC__bitwriter_release_buffer(encoder->private_->frame);
	FLAC__bitwriter_clear(encoder->private_->frame);

	if(samples > 0) {
		encoder->private_->streaminfo.data.stream_info.min_framesize = min(bytes, encoder->private_->streaminfo.data.stream_info.min_framesize);
		encoder->private_->streaminfo.data.stream_info.max_framesize = max(bytes, encoder->private_->streaminfo.data.stream_info.max_framesize);
	}

	return true;
}

FLAC__StreamEncoderWriteStatus write_frame_(FLAC__StreamEncoder *encoder, const FLAC__byte buffer[], size_t bytes, unsigned samples, FLAC__bool is_last_block)
{
	FLAC__StreamEncoderWriteStatus status;
	FLAC__uint64 output_position = 0;

	/* FLAC__STREAM_ENCODER_TELL_STATUS_UNSUPPORTED just means we didn't get the offset; no error */
	if(encoder->private_->tell_callback && encoder->private_->tell_callback(encoder, &output_position, encoder->private_->client_data) == FLAC__STREAM_ENCODER_TELL_STATUS_ERROR) {
		encoder->protected_->state = FLAC__STREAM_ENCODER_CLIENT_ERROR;
		return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
	}

	/*
	 * Watch for the STREAMINFO block and first SEEKTABLE block to go by and store their offsets.
	 */
	if(samples == 0) {
		FLAC__MetadataType type = (FLAC__MetadataType)(buffer[0] & 0x7f);
		if(type == FLAC__METADATA_TYPE_STREAMINFO)
			encoder->protected_->streaminfo_offset = output_position;
		else if(type == FLAC__METADATA_TYPE_SEEKTABLE && encoder->protected_->seektable_offset == 0)
			encoder->protected_->seektable_offset = output_position;
	}

	/*
	 * Mark the current seek point if hit (if audio_offset == 0 that
	 * means we're still writing metadata and haven't hit the first
	 * frame yet)
	 */
	if(0 != encoder->private_->seek_table && encoder->protected_->audio_offset > 0 && encoder->private_->seek_table->num_points > 0) {
		const unsigned blocksize = FLAC__stream_encoder_get_blocksize(encoder);
		const FLAC__uint64 frame_first_sample = encoder->private_->samples_written;
		const FLAC__uint64 frame_last_sample = frame_first_sample + (FLAC__uint64)blocksize - 1;
		FLAC__uint64 test_sample;
		unsigned i;
		for(i = encoder->private_->first_seekpoint_to_check; i < encoder->private_->seek_table->num_points; i++) {
			test_sample = encoder->private_->seek_table->points[i].sample_number;
			if(test_sample > frame_last_sample) {
				break;
			}
			else if(test_sample >= frame_first_sample) {
				encoder->private_->seek_table->points[i].sample_number = frame_first_sample;
				encoder->private_->seek_table->points[i].stream_offset = output_position - encoder->protected_->audio_offset;
				encoder->private_->seek_table->points[i].frame_samples = blocksize;
				encoder->private_->first_seekpoint_to_check++;
				/* DO NOT: "break;" and here's why:
				 * The seektable template may contain more than one target
				 * sample for any given frame; we will keep looping, generating
				 * duplicate seekpoints for them, and we'll clean it up later,
				 * just before writing the seektable back to the metadata.
				 */
			}
			else {
				encoder->private_->first_seekpoint_to_check++;
			}
		}
	}

#if FLAC__HAS_OGG
	if(encoder->private_->is_ogg) {
		status = FLAC__ogg_encoder_aspect_write_callback_wrapper(
			&encoder->protected_->ogg_encoder_aspect,
			buffer,
			bytes,
			samples,
			encoder->private_->current_frame_number,
			is_last_block,
			(FLAC__OggEncoderAspectWriteCallbackProxy)encoder->private_->write_callback,
			encoder,
			encoder->private_->client_data
		);
	}
	else
#endif
	status = encoder->private_->write_callback(encoder, buffer, bytes, samples, encoder->private_->current_frame_number, encoder->private_->client_data);

	if(status == FLAC__STREAM_ENCODER_WRITE_STATUS_OK) {
		encoder->private_->bytes_written += bytes;
		encoder->private_->samples_written += samples;
		/* we keep a high watermark on the number of frames written because
		 * when the encoder goes back to write metadata, 'current_frame'
		 * will drop back to 0.
		 */
		encoder->private_->frames_written = max(encoder->private_->frames_written, encoder->private_->current_frame_number+1);
	}
	else
		encoder->protected_->state = FLAC__STREAM_ENCODER_CLIENT_ERROR;

	return status;
}

/* Gets called when the encoding process has finished so that we can update the STREAMINFO and SEEKTABLE blocks.  */
void update_metadata_(const FLAC__StreamEncoder *encoder)
{
	FLAC__byte b[max(6, FLAC__STREAM_METADATA_SEEKPOINT_LENGTH)];
	const FLAC__StreamMetadata *metadata = &encoder->private_->streaminfo;
	const FLAC__uint64 samples = metadata->data.stream_info.total_samples;
	const unsigned min_framesize = metadata->data.stream_info.min_framesize;
	const unsigned max_framesize = metadata->data.stream_info.max_framesize;
	const unsigned bps = metadata->data.stream_info.bits_per_sample;
	FLAC__StreamEncoderSeekStatus seek_status;

	FLAC__ASSERT(metadata->type == FLAC__METADATA_TYPE_STREAMINFO);

	/* All this is based on intimate knowledge of the stream header
	 * layout, but a change to the header format that would break this
	 * would also break all streams encoded in the previous format.
	 */

	/*
	 * Write MD5 signature
	 */
	{
		const unsigned md5_offset =
			FLAC__STREAM_METADATA_HEADER_LENGTH +
			(
				FLAC__STREAM_METADATA_STREAMINFO_MIN_BLOCK_SIZE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_MAX_BLOCK_SIZE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_MIN_FRAME_SIZE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_MAX_FRAME_SIZE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_SAMPLE_RATE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_CHANNELS_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_BITS_PER_SAMPLE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_TOTAL_SAMPLES_LEN
			) / 8;

		if((seek_status = encoder->private_->seek_callback(encoder, encoder->protected_->streaminfo_offset + md5_offset, encoder->private_->client_data)) != FLAC__STREAM_ENCODER_SEEK_STATUS_OK) {
			if(seek_status == FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR)
				encoder->protected_->state = FLAC__STREAM_ENCODER_CLIENT_ERROR;
			return;
		}
		if(encoder->private_->write_callback(encoder, metadata->data.stream_info.md5sum, 16, 0, 0, encoder->private_->client_data) != FLAC__STREAM_ENCODER_WRITE_STATUS_OK) {
			encoder->protected_->state = FLAC__STREAM_ENCODER_CLIENT_ERROR;
			return;
		}
	}

	/*
	 * Write total samples
	 */
	{
		const unsigned total_samples_byte_offset =
			FLAC__STREAM_METADATA_HEADER_LENGTH +
			(
				FLAC__STREAM_METADATA_STREAMINFO_MIN_BLOCK_SIZE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_MAX_BLOCK_SIZE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_MIN_FRAME_SIZE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_MAX_FRAME_SIZE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_SAMPLE_RATE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_CHANNELS_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_BITS_PER_SAMPLE_LEN
				- 4
			) / 8;

		b[0] = ((FLAC__byte)(bps-1) << 4) | (FLAC__byte)((samples >> 32) & 0x0F);
		b[1] = (FLAC__byte)((samples >> 24) & 0xFF);
		b[2] = (FLAC__byte)((samples >> 16) & 0xFF);
		b[3] = (FLAC__byte)((samples >> 8) & 0xFF);
		b[4] = (FLAC__byte)(samples & 0xFF);
		if((seek_status = encoder->private_->seek_callback(encoder, encoder->protected_->streaminfo_offset + total_samples_byte_offset, encoder->private_->client_data)) != FLAC__STREAM_ENCODER_SEEK_STATUS_OK) {
			if(seek_status == FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR)
				encoder->protected_->state = FLAC__STREAM_ENCODER_CLIENT_ERROR;
			return;
		}
		if(encoder->private_->write_callback(encoder, b, 5, 0, 0, encoder->private_->client_data) != FLAC__STREAM_ENCODER_WRITE_STATUS_OK) {
			encoder->protected_->state = FLAC__STREAM_ENCODER_CLIENT_ERROR;
			return;
		}
	}

	/*
	 * Write min/max framesize
	 */
	{
		const unsigned min_framesize_offset =
			FLAC__STREAM_METADATA_HEADER_LENGTH +
			(
				FLAC__STREAM_METADATA_STREAMINFO_MIN_BLOCK_SIZE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_MAX_BLOCK_SIZE_LEN
			) / 8;

		b[0] = (FLAC__byte)((min_framesize >> 16) & 0xFF);
		b[1] = (FLAC__byte)((min_framesize >> 8) & 0xFF);
		b[2] = (FLAC__byte)(min_framesize & 0xFF);
		b[3] = (FLAC__byte)((max_framesize >> 16) & 0xFF);
		b[4] = (FLAC__byte)((max_framesize >> 8) & 0xFF);
		b[5] = (FLAC__byte)(max_framesize & 0xFF);
		if((seek_status = encoder->private_->seek_callback(encoder, encoder->protected_->streaminfo_offset + min_framesize_offset, encoder->private_->client_data)) != FLAC__STREAM_ENCODER_SEEK_STATUS_OK) {
			if(seek_status == FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR)
				encoder->protected_->state = FLAC__STREAM_ENCODER_CLIENT_ERROR;
			return;
		}
		if(encoder->private_->write_callback(encoder, b, 6, 0, 0, encoder->private_->client_data) != FLAC__STREAM_ENCODER_WRITE_STATUS_OK) {
			encoder->protected_->state = FLAC__STREAM_ENCODER_CLIENT_ERROR;
			return;
		}
	}

	/*
	 * Write seektable
	 */
	if(0 != encoder->private_->seek_table && encoder->private_->seek_table->num_points > 0 && encoder->protected_->seektable_offset > 0) {
		unsigned i;

		FLAC__format_seektable_sort(encoder->private_->seek_table);

		FLAC__ASSERT(FLAC__format_seektable_is_legal(encoder->private_->seek_table));

		if((seek_status = encoder->private_->seek_callback(encoder, encoder->protected_->seektable_offset + FLAC__STREAM_METADATA_HEADER_LENGTH, encoder->private_->client_data)) != FLAC__STREAM_ENCODER_SEEK_STATUS_OK) {
			if(seek_status == FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR)
				encoder->protected_->state = FLAC__STREAM_ENCODER_CLIENT_ERROR;
			return;
		}

		for(i = 0; i < encoder->private_->seek_table->num_points; i++) {
			FLAC__uint64 xx;
			unsigned x;
			xx = encoder->private_->seek_table->points[i].sample_number;
			b[7] = (FLAC__byte)xx; xx >>= 8;
			b[6] = (FLAC__byte)xx; xx >>= 8;
			b[5] = (FLAC__byte)xx; xx >>= 8;
			b[4] = (FLAC__byte)xx; xx >>= 8;
			b[3] = (FLAC__byte)xx; xx >>= 8;
			b[2] = (FLAC__byte)xx; xx >>= 8;
			b[1] = (FLAC__byte)xx; xx >>= 8;
			b[0] = (FLAC__byte)xx; xx >>= 8;
			xx = encoder->private_->seek_table->points[i].stream_offset;
			b[15] = (FLAC__byte)xx; xx >>= 8;
			b[14] = (FLAC__byte)xx; xx >>= 8;
			b[13] = (FLAC__byte)xx; xx >>= 8;
			b[12] = (FLAC__byte)xx; xx >>= 8;
			b[11] = (FLAC__byte)xx; xx >>= 8;
			b[10] = (FLAC__byte)xx; xx >>= 8;
			b[9] = (FLAC__byte)xx; xx >>= 8;
			b[8] = (FLAC__byte)xx; xx >>= 8;
			x = encoder->private_->seek_table->points[i].frame_samples;
			b[17] = (FLAC__byte)x; x >>= 8;
			b[16] = (FLAC__byte)x; x >>= 8;
			if(encoder->private_->write_callback(encoder, b, 18, 0, 0, encoder->private_->client_data) != FLAC__STREAM_ENCODER_WRITE_STATUS_OK) {
				encoder->protected_->state = FLAC__STREAM_ENCODER_CLIENT_ERROR;
				return;
			}
		}
	}
}

#if FLAC__HAS_OGG
/* Gets called when the encoding process has finished so that we can update the STREAMINFO and SEEKTABLE blocks.  */
void update_ogg_metadata_(FLAC__StreamEncoder *encoder)
{
	/* the # of bytes in the 1st packet that precede the STREAMINFO */
	static const unsigned FIRST_OGG_PACKET_STREAMINFO_PREFIX_LENGTH =
		FLAC__OGG_MAPPING_PACKET_TYPE_LENGTH +
		FLAC__OGG_MAPPING_MAGIC_LENGTH +
		FLAC__OGG_MAPPING_VERSION_MAJOR_LENGTH +
		FLAC__OGG_MAPPING_VERSION_MINOR_LENGTH +
		FLAC__OGG_MAPPING_NUM_HEADERS_LENGTH +
		FLAC__STREAM_SYNC_LENGTH
	;
	FLAC__byte b[max(6, FLAC__STREAM_METADATA_SEEKPOINT_LENGTH)];
	const FLAC__StreamMetadata *metadata = &encoder->private_->streaminfo;
	const FLAC__uint64 samples = metadata->data.stream_info.total_samples;
	const unsigned min_framesize = metadata->data.stream_info.min_framesize;
	const unsigned max_framesize = metadata->data.stream_info.max_framesize;
	ogg_page page;

	FLAC__ASSERT(metadata->type == FLAC__METADATA_TYPE_STREAMINFO);
	FLAC__ASSERT(0 != encoder->private_->seek_callback);

	/* Pre-check that client supports seeking, since we don't want the
	 * ogg_helper code to ever have to deal with this condition.
	 */
	if(encoder->private_->seek_callback(encoder, 0, encoder->private_->client_data) == FLAC__STREAM_ENCODER_SEEK_STATUS_UNSUPPORTED)
		return;

	/* All this is based on intimate knowledge of the stream header
	 * layout, but a change to the header format that would break this
	 * would also break all streams encoded in the previous format.
	 */

	/**
	 ** Write STREAMINFO stats
	 **/
	simple_ogg_page__init(&page);
	if(!simple_ogg_page__get_at(encoder, encoder->protected_->streaminfo_offset, &page, encoder->private_->seek_callback, encoder->private_->read_callback, encoder->private_->client_data)) {
		simple_ogg_page__clear(&page);
		return; /* state already set */
	}

	/*
	 * Write MD5 signature
	 */
	{
		const unsigned md5_offset =
			FIRST_OGG_PACKET_STREAMINFO_PREFIX_LENGTH +
			FLAC__STREAM_METADATA_HEADER_LENGTH +
			(
				FLAC__STREAM_METADATA_STREAMINFO_MIN_BLOCK_SIZE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_MAX_BLOCK_SIZE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_MIN_FRAME_SIZE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_MAX_FRAME_SIZE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_SAMPLE_RATE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_CHANNELS_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_BITS_PER_SAMPLE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_TOTAL_SAMPLES_LEN
			) / 8;

		if(md5_offset + 16 > (unsigned)page.body_len) {
			encoder->protected_->state = FLAC__STREAM_ENCODER_OGG_ERROR;
			simple_ogg_page__clear(&page);
			return;
		}
		memcpy(page.body + md5_offset, metadata->data.stream_info.md5sum, 16);
	}

	/*
	 * Write total samples
	 */
	{
		const unsigned total_samples_byte_offset =
			FIRST_OGG_PACKET_STREAMINFO_PREFIX_LENGTH +
			FLAC__STREAM_METADATA_HEADER_LENGTH +
			(
				FLAC__STREAM_METADATA_STREAMINFO_MIN_BLOCK_SIZE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_MAX_BLOCK_SIZE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_MIN_FRAME_SIZE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_MAX_FRAME_SIZE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_SAMPLE_RATE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_CHANNELS_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_BITS_PER_SAMPLE_LEN
				- 4
			) / 8;

		if(total_samples_byte_offset + 5 > (unsigned)page.body_len) {
			encoder->protected_->state = FLAC__STREAM_ENCODER_OGG_ERROR;
			simple_ogg_page__clear(&page);
			return;
		}
		b[0] = (FLAC__byte)page.body[total_samples_byte_offset] & 0xF0;
		b[0] |= (FLAC__byte)((samples >> 32) & 0x0F);
		b[1] = (FLAC__byte)((samples >> 24) & 0xFF);
		b[2] = (FLAC__byte)((samples >> 16) & 0xFF);
		b[3] = (FLAC__byte)((samples >> 8) & 0xFF);
		b[4] = (FLAC__byte)(samples & 0xFF);
		memcpy(page.body + total_samples_byte_offset, b, 5);
	}

	/*
	 * Write min/max framesize
	 */
	{
		const unsigned min_framesize_offset =
			FIRST_OGG_PACKET_STREAMINFO_PREFIX_LENGTH +
			FLAC__STREAM_METADATA_HEADER_LENGTH +
			(
				FLAC__STREAM_METADATA_STREAMINFO_MIN_BLOCK_SIZE_LEN +
				FLAC__STREAM_METADATA_STREAMINFO_MAX_BLOCK_SIZE_LEN
			) / 8;

		if(min_framesize_offset + 6 > (unsigned)page.body_len) {
			encoder->protected_->state = FLAC__STREAM_ENCODER_OGG_ERROR;
			simple_ogg_page__clear(&page);
			return;
		}
		b[0] = (FLAC__byte)((min_framesize >> 16) & 0xFF);
		b[1] = (FLAC__byte)((min_framesize >> 8) & 0xFF);
		b[2] = (FLAC__byte)(min_framesize & 0xFF);
		b[3] = (FLAC__byte)((max_framesize >> 16) & 0xFF);
		b[4] = (FLAC__byte)((max_framesize >> 8) & 0xFF);
		b[5] = (FLAC__byte)(max_framesize & 0xFF);
		memcpy(page.body + min_framesize_offset, b, 6);
	}
	if(!simple_ogg_page__set_at(encoder, encoder->protected_->streaminfo_offset, &page, encoder->private_->seek_callback, encoder->private_->write_callback, encoder->private_->client_data)) {
		simple_ogg_page__clear(&page);
		return; /* state already set */
	}
	simple_ogg_page__clear(&page);

	/*
	 * Write seektable
	 */
	if(0 != encoder->private_->seek_table && encoder->private_->seek_table->num_points > 0 && encoder->protected_->seektable_offset > 0) {
		unsigned i;
		FLAC__byte *p;

		FLAC__format_seektable_sort(encoder->private_->seek_table);

		FLAC__ASSERT(FLAC__format_seektable_is_legal(encoder->private_->seek_table));

		simple_ogg_page__init(&page);
		if(!simple_ogg_page__get_at(encoder, encoder->protected_->seektable_offset, &page, encoder->private_->seek_callback, encoder->private_->read_callback, encoder->private_->client_data)) {
			simple_ogg_page__clear(&page);
			return; /* state already set */
		}

		if((FLAC__STREAM_METADATA_HEADER_LENGTH + 18*encoder->private_->seek_table->num_points) != (unsigned)page.body_len) {
			encoder->protected_->state = FLAC__STREAM_ENCODER_OGG_ERROR;
			simple_ogg_page__clear(&page);
			return;
		}

		for(i = 0, p = page.body + FLAC__STREAM_METADATA_HEADER_LENGTH; i < encoder->private_->seek_table->num_points; i++, p += 18) {
			FLAC__uint64 xx;
			unsigned x;
			xx = encoder->private_->seek_table->points[i].sample_number;
			b[7] = (FLAC__byte)xx; xx >>= 8;
			b[6] = (FLAC__byte)xx; xx >>= 8;
			b[5] = (FLAC__byte)xx; xx >>= 8;
			b[4] = (FLAC__byte)xx; xx >>= 8;
			b[3] = (FLAC__byte)xx; xx >>= 8;
			b[2] = (FLAC__byte)xx; xx >>= 8;
			b[1] = (FLAC__byte)xx; xx >>= 8;
			b[0] = (FLAC__byte)xx; xx >>= 8;
			xx = encoder->private_->seek_table->points[i].stream_offset;
			b[15] = (FLAC__byte)xx; xx >>= 8;
			b[14] = (FLAC__byte)xx; xx >>= 8;
			b[13] = (FLAC__byte)xx; xx >>= 8;
			b[12] = (FLAC__byte)xx; xx >>= 8;
			b[11] = (FLAC__byte)xx; xx >>= 8;
			b[10] = (FLAC__byte)xx; xx >>= 8;
			b[9] = (FLAC__byte)xx; xx >>= 8;
			b[8] = (FLAC__byte)xx; xx >>= 8;
			x = encoder->private_->seek_table->points[i].frame_samples;
			b[17] = (FLAC__byte)x; x >>= 8;
			b[16] = (FLAC__byte)x; x >>= 8;
			memcpy(p, b, 18);
		}

		if(!simple_ogg_page__set_at(encoder, encoder->protected_->seektable_offset, &page, encoder->private_->seek_callback, encoder->private_->write_callback, encoder->private_->client_data)) {
			simple_ogg_page__clear(&page);
			return; /* state already set */
		}
		simple_ogg_page__clear(&page);
	}
}
#endif

FLAC__bool process_frame_(FLAC__StreamEncoder *encoder, FLAC__bool is_fractional_block, FLAC__bool is_last_block)
{
	FLAC__uint16 crc;
	FLAC__ASSERT(encoder->protected_->state == FLAC__STREAM_ENCODER_OK);

	/*
	 * Accumulate raw signal to the MD5 signature
	 */
	if(encoder->protected_->do_md5 && !FLAC__MD5Accumulate(&encoder->private_->md5context, (const FLAC__int32 * const *)encoder->private_->integer_signal, encoder->protected_->channels, encoder->protected_->blocksize, (encoder->protected_->bits_per_sample+7) / 8)) {
		encoder->protected_->state = FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}

	/*
	 * Process the frame header and subframes into the frame bitbuffer
	 */
	if(!process_subframes_(encoder, is_fractional_block)) {
		/* the above function sets the state for us in case of an error */
		return false;
	}

	/*
	 * Zero-pad the frame to a byte_boundary
	 */
	if(!FLAC__bitwriter_zero_pad_to_byte_boundary(encoder->private_->frame)) {
		encoder->protected_->state = FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}

	/*
	 * CRC-16 the whole thing
	 */
	FLAC__ASSERT(FLAC__bitwriter_is_byte_aligned(encoder->private_->frame));
	if(
		!FLAC__bitwriter_get_write_crc16(encoder->private_->frame, &crc) ||
		!FLAC__bitwriter_write_raw_uint32(encoder->private_->frame, crc, FLAC__FRAME_FOOTER_CRC_LEN)
	) {
		encoder->protected_->state = FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}

	/*
	 * Write it
	 */
	if(!write_bitbuffer_(encoder, encoder->protected_->blocksize, is_last_block)) {
		/* the above function sets the state for us in case of an error */
		return false;
	}

	/*
	 * Get ready for the next frame
	 */
	encoder->private_->current_sample_number = 0;
	encoder->private_->current_frame_number++;
	encoder->private_->streaminfo.data.stream_info.total_samples += (FLAC__uint64)encoder->protected_->blocksize;

	return true;
}

FLAC__bool process_subframes_(FLAC__StreamEncoder *encoder, FLAC__bool is_fractional_block)
{
	FLAC__FrameHeader frame_header;
	unsigned channel, min_partition_order = encoder->protected_->min_residual_partition_order, max_partition_order;
	FLAC__bool do_independent, do_mid_side;

	/*
	 * Calculate the min,max Rice partition orders
	 */
	if(is_fractional_block) {
		max_partition_order = 0;
	}
	else {
		max_partition_order = FLAC__format_get_max_rice_partition_order_from_blocksize(encoder->protected_->blocksize);
		max_partition_order = min(max_partition_order, encoder->protected_->max_residual_partition_order);
	}
	min_partition_order = min(min_partition_order, max_partition_order);

	/*
	 * Setup the frame
	 */
	frame_header.blocksize = encoder->protected_->blocksize;
	frame_header.sample_rate = encoder->protected_->sample_rate;
	frame_header.channels = encoder->protected_->channels;
	frame_header.channel_assignment = FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT; /* the default unless the encoder determines otherwise */
	frame_header.bits_per_sample = encoder->protected_->bits_per_sample;
	frame_header.number_type = FLAC__FRAME_NUMBER_TYPE_FRAME_NUMBER;
	frame_header.number.frame_number = encoder->private_->current_frame_number;

	/*
	 * Figure out what channel assignments to try
	 */
	if(encoder->protected_->do_mid_side_stereo) {
		if(encoder->protected_->loose_mid_side_stereo) {
			if(encoder->private_->loose_mid_side_stereo_frame_count == 0) {
				do_independent = true;
				do_mid_side = true;
			}
			else {
				do_independent = (encoder->private_->last_channel_assignment == FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT);
				do_mid_side = !do_independent;
			}
		}
		else {
			do_independent = true;
			do_mid_side = true;
		}
	}
	else {
		do_independent = true;
		do_mid_side = false;
	}

	FLAC__ASSERT(do_independent || do_mid_side);

	/*
	 * Check for wasted bits; set effective bps for each subframe
	 */
	if(do_independent) {
		for(channel = 0; channel < encoder->protected_->channels; channel++) {
			const unsigned w = get_wasted_bits_(encoder->private_->integer_signal[channel], encoder->protected_->blocksize);
			encoder->private_->subframe_workspace[channel][0].wasted_bits = encoder->private_->subframe_workspace[channel][1].wasted_bits = w;
			encoder->private_->subframe_bps[channel] = encoder->protected_->bits_per_sample - w;
		}
	}
	if(do_mid_side) {
		FLAC__ASSERT(encoder->protected_->channels == 2);
		for(channel = 0; channel < 2; channel++) {
			const unsigned w = get_wasted_bits_(encoder->private_->integer_signal_mid_side[channel], encoder->protected_->blocksize);
			encoder->private_->subframe_workspace_mid_side[channel][0].wasted_bits = encoder->private_->subframe_workspace_mid_side[channel][1].wasted_bits = w;
			encoder->private_->subframe_bps_mid_side[channel] = encoder->protected_->bits_per_sample - w + (channel==0? 0:1);
		}
	}

	/*
	 * First do a normal encoding pass of each independent channel
	 */
	if(do_independent) {
		for(channel = 0; channel < encoder->protected_->channels; channel++) {
			if(!
				process_subframe_(
					encoder,
					min_partition_order,
					max_partition_order,
					&frame_header,
					encoder->private_->subframe_bps[channel],
					encoder->private_->integer_signal[channel],
					encoder->private_->subframe_workspace_ptr[channel],
					encoder->private_->partitioned_rice_contents_workspace_ptr[channel],
					encoder->private_->residual_workspace[channel],
					encoder->private_->best_subframe+channel,
					encoder->private_->best_subframe_bits+channel
				)
			)
				return false;
		}
	}

	/*
	 * Now do mid and side channels if requested
	 */
	if(do_mid_side) {
		FLAC__ASSERT(encoder->protected_->channels == 2);

		for(channel = 0; channel < 2; channel++) {
			if(!
				process_subframe_(
					encoder,
					min_partition_order,
					max_partition_order,
					&frame_header,
					encoder->private_->subframe_bps_mid_side[channel],
					encoder->private_->integer_signal_mid_side[channel],
					encoder->private_->subframe_workspace_ptr_mid_side[channel],
					encoder->private_->partitioned_rice_contents_workspace_ptr_mid_side[channel],
					encoder->private_->residual_workspace_mid_side[channel],
					encoder->private_->best_subframe_mid_side+channel,
					encoder->private_->best_subframe_bits_mid_side+channel
				)
			)
				return false;
		}
	}

	/*
	 * Compose the frame bitbuffer
	 */
	if(do_mid_side) {
		unsigned left_bps = 0, right_bps = 0; /* initialized only to prevent superfluous compiler warning */
		FLAC__Subframe *left_subframe = 0, *right_subframe = 0; /* initialized only to prevent superfluous compiler warning */
		FLAC__ChannelAssignment channel_assignment;

		FLAC__ASSERT(encoder->protected_->channels == 2);

		if(encoder->protected_->loose_mid_side_stereo && encoder->private_->loose_mid_side_stereo_frame_count > 0) {
			channel_assignment = (encoder->private_->last_channel_assignment == FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT? FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT : FLAC__CHANNEL_ASSIGNMENT_MID_SIDE);
		}
		else {
			unsigned bits[4]; /* WATCHOUT - indexed by FLAC__ChannelAssignment */
			unsigned min_bits;
			int ca;

			FLAC__ASSERT(FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT == 0);
			FLAC__ASSERT(FLAC__CHANNEL_ASSIGNMENT_LEFT_SIDE   == 1);
			FLAC__ASSERT(FLAC__CHANNEL_ASSIGNMENT_RIGHT_SIDE  == 2);
			FLAC__ASSERT(FLAC__CHANNEL_ASSIGNMENT_MID_SIDE    == 3);
			FLAC__ASSERT(do_independent && do_mid_side);

			/* We have to figure out which channel assignent results in the smallest frame */
			bits[FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT] = encoder->private_->best_subframe_bits         [0] + encoder->private_->best_subframe_bits         [1];
			bits[FLAC__CHANNEL_ASSIGNMENT_LEFT_SIDE  ] = encoder->private_->best_subframe_bits         [0] + encoder->private_->best_subframe_bits_mid_side[1];
			bits[FLAC__CHANNEL_ASSIGNMENT_RIGHT_SIDE ] = encoder->private_->best_subframe_bits         [1] + encoder->private_->best_subframe_bits_mid_side[1];
			bits[FLAC__CHANNEL_ASSIGNMENT_MID_SIDE   ] = encoder->private_->best_subframe_bits_mid_side[0] + encoder->private_->best_subframe_bits_mid_side[1];

			channel_assignment = FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT;
			min_bits = bits[channel_assignment];
			for(ca = 1; ca <= 3; ca++) {
				if(bits[ca] < min_bits) {
					min_bits = bits[ca];
					channel_assignment = (FLAC__ChannelAssignment)ca;
				}
			}
		}

		frame_header.channel_assignment = channel_assignment;

		if(!FLAC__frame_add_header(&frame_header, encoder->private_->frame)) {
			encoder->protected_->state = FLAC__STREAM_ENCODER_FRAMING_ERROR;
			return false;
		}

		switch(channel_assignment) {
			case FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT:
				left_subframe  = &encoder->private_->subframe_workspace         [0][encoder->private_->best_subframe         [0]];
				right_subframe = &encoder->private_->subframe_workspace         [1][encoder->private_->best_subframe         [1]];
				break;
			case FLAC__CHANNEL_ASSIGNMENT_LEFT_SIDE:
				left_subframe  = &encoder->private_->subframe_workspace         [0][encoder->private_->best_subframe         [0]];
				right_subframe = &encoder->private_->subframe_workspace_mid_side[1][encoder->private_->best_subframe_mid_side[1]];
				break;
			case FLAC__CHANNEL_ASSIGNMENT_RIGHT_SIDE:
				left_subframe  = &encoder->private_->subframe_workspace_mid_side[1][encoder->private_->best_subframe_mid_side[1]];
				right_subframe = &encoder->private_->subframe_workspace         [1][encoder->private_->best_subframe         [1]];
				break;
			case FLAC__CHANNEL_ASSIGNMENT_MID_SIDE:
				left_subframe  = &encoder->private_->subframe_workspace_mid_side[0][encoder->private_->best_subframe_mid_side[0]];
				right_subframe = &encoder->private_->subframe_workspace_mid_side[1][encoder->private_->best_subframe_mid_side[1]];
				break;
			default:
				FLAC__ASSERT(0);
		}

		switch(channel_assignment) {
			case FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT:
				left_bps  = encoder->private_->subframe_bps         [0];
				right_bps = encoder->private_->subframe_bps         [1];
				break;
			case FLAC__CHANNEL_ASSIGNMENT_LEFT_SIDE:
				left_bps  = encoder->private_->subframe_bps         [0];
				right_bps = encoder->private_->subframe_bps_mid_side[1];
				break;
			case FLAC__CHANNEL_ASSIGNMENT_RIGHT_SIDE:
				left_bps  = encoder->private_->subframe_bps_mid_side[1];
				right_bps = encoder->private_->subframe_bps         [1];
				break;
			case FLAC__CHANNEL_ASSIGNMENT_MID_SIDE:
				left_bps  = encoder->private_->subframe_bps_mid_side[0];
				right_bps = encoder->private_->subframe_bps_mid_side[1];
				break;
			default:
				FLAC__ASSERT(0);
		}

		/* note that encoder_add_subframe_ sets the state for us in case of an error */
		if(!add_subframe_(encoder, frame_header.blocksize, left_bps , left_subframe , encoder->private_->frame))
			return false;
		if(!add_subframe_(encoder, frame_header.blocksize, right_bps, right_subframe, encoder->private_->frame))
			return false;
	}
	else {
		if(!FLAC__frame_add_header(&frame_header, encoder->private_->frame)) {
			encoder->protected_->state = FLAC__STREAM_ENCODER_FRAMING_ERROR;
			return false;
		}

		for(channel = 0; channel < encoder->protected_->channels; channel++) {
			if(!add_subframe_(encoder, frame_header.blocksize, encoder->private_->subframe_bps[channel], &encoder->private_->subframe_workspace[channel][encoder->private_->best_subframe[channel]], encoder->private_->frame)) {
				/* the above function sets the state for us in case of an error */
				return false;
			}
		}
	}

	if(encoder->protected_->loose_mid_side_stereo) {
		encoder->private_->loose_mid_side_stereo_frame_count++;
		if(encoder->private_->loose_mid_side_stereo_frame_count >= encoder->private_->loose_mid_side_stereo_frames)
			encoder->private_->loose_mid_side_stereo_frame_count = 0;
	}

	encoder->private_->last_channel_assignment = frame_header.channel_assignment;

	return true;
}

FLAC__bool process_subframe_(
	FLAC__StreamEncoder *encoder,
	unsigned min_partition_order,
	unsigned max_partition_order,
	const FLAC__FrameHeader *frame_header,
	unsigned subframe_bps,
	const FLAC__int32 integer_signal[],
	FLAC__Subframe *subframe[2],
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents[2],
	FLAC__int32 *residual[2],
	unsigned *best_subframe,
	unsigned *best_bits
)
{
#ifndef FLAC__INTEGER_ONLY_LIBRARY
	FLAC__float fixed_residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1];
#else
	FLAC__fixedpoint fixed_residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1];
#endif
#ifndef FLAC__INTEGER_ONLY_LIBRARY
	FLAC__double lpc_residual_bits_per_sample;
	FLAC__real autoc[FLAC__MAX_LPC_ORDER+1]; /* WATCHOUT: the size is important even though encoder->protected_->max_lpc_order might be less; some asm routines need all the space */
	FLAC__double lpc_error[FLAC__MAX_LPC_ORDER];
	unsigned min_lpc_order, max_lpc_order, lpc_order;
	unsigned min_qlp_coeff_precision, max_qlp_coeff_precision, qlp_coeff_precision;
#endif
	unsigned min_fixed_order, max_fixed_order, guess_fixed_order, fixed_order;
	unsigned rice_parameter;
	unsigned _candidate_bits, _best_bits;
	unsigned _best_subframe;
	/* only use RICE2 partitions if stream bps > 16 */
	const unsigned rice_parameter_limit = FLAC__stream_encoder_get_bits_per_sample(encoder) > 16? FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2_ESCAPE_PARAMETER : FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER;

	FLAC__ASSERT(frame_header->blocksize > 0);

	/* verbatim subframe is the baseline against which we measure other compressed subframes */
	_best_subframe = 0;
	if(encoder->private_->disable_verbatim_subframes && frame_header->blocksize >= FLAC__MAX_FIXED_ORDER)
		_best_bits = UINT_MAX;
	else
		_best_bits = evaluate_verbatim_subframe_(encoder, integer_signal, frame_header->blocksize, subframe_bps, subframe[_best_subframe]);

	if(frame_header->blocksize >= FLAC__MAX_FIXED_ORDER) {
		unsigned signal_is_constant = false;
		guess_fixed_order = encoder->private_->local_fixed_compute_best_predictor(integer_signal+FLAC__MAX_FIXED_ORDER, frame_header->blocksize-FLAC__MAX_FIXED_ORDER, fixed_residual_bits_per_sample);
		/* check for constant subframe */
		if(
			!encoder->private_->disable_constant_subframes &&
#ifndef FLAC__INTEGER_ONLY_LIBRARY
			fixed_residual_bits_per_sample[1] == 0.0
#else
			fixed_residual_bits_per_sample[1] == FLAC__FP_ZERO
#endif
		) {
			/* the above means it's possible all samples are the same value; now double-check it: */
			unsigned i;
			signal_is_constant = true;
			for(i = 1; i < frame_header->blocksize; i++) {
				if(integer_signal[0] != integer_signal[i]) {
					signal_is_constant = false;
					break;
				}
			}
		}
		if(signal_is_constant) {
			_candidate_bits = evaluate_constant_subframe_(encoder, integer_signal[0], frame_header->blocksize, subframe_bps, subframe[!_best_subframe]);
			if(_candidate_bits < _best_bits) {
				_best_subframe = !_best_subframe;
				_best_bits = _candidate_bits;
			}
		}
		else {
			if(!encoder->private_->disable_fixed_subframes || (encoder->protected_->max_lpc_order == 0 && _best_bits == UINT_MAX)) {
				/* encode fixed */
				if(encoder->protected_->do_exhaustive_model_search) {
					min_fixed_order = 0;
					max_fixed_order = FLAC__MAX_FIXED_ORDER;
				}
				else {
					min_fixed_order = max_fixed_order = guess_fixed_order;
				}
				if(max_fixed_order >= frame_header->blocksize)
					max_fixed_order = frame_header->blocksize - 1;
				for(fixed_order = min_fixed_order; fixed_order <= max_fixed_order; fixed_order++) {
#ifndef FLAC__INTEGER_ONLY_LIBRARY
					if(fixed_residual_bits_per_sample[fixed_order] >= (FLAC__float)subframe_bps)
						continue; /* don't even try */
					rice_parameter = (fixed_residual_bits_per_sample[fixed_order] > 0.0)? (unsigned)(fixed_residual_bits_per_sample[fixed_order]+0.5) : 0; /* 0.5 is for rounding */
#else
					if(FLAC__fixedpoint_trunc(fixed_residual_bits_per_sample[fixed_order]) >= (int)subframe_bps)
						continue; /* don't even try */
					rice_parameter = (fixed_residual_bits_per_sample[fixed_order] > FLAC__FP_ZERO)? (unsigned)FLAC__fixedpoint_trunc(fixed_residual_bits_per_sample[fixed_order]+FLAC__FP_ONE_HALF) : 0; /* 0.5 is for rounding */
#endif
					rice_parameter++; /* to account for the signed->unsigned conversion during rice coding */
					if(rice_parameter >= rice_parameter_limit) {
#ifdef DEBUG_VERBOSE
						fprintf(stderr, "clipping rice_parameter (%u -> %u) @0\n", rice_parameter, rice_parameter_limit - 1);
#endif
						rice_parameter = rice_parameter_limit - 1;
					}
					_candidate_bits =
						evaluate_fixed_subframe_(
							encoder,
							integer_signal,
							residual[!_best_subframe],
							encoder->private_->abs_residual_partition_sums,
							encoder->private_->raw_bits_per_partition,
							frame_header->blocksize,
							subframe_bps,
							fixed_order,
							rice_parameter,
							rice_parameter_limit,
							min_partition_order,
							max_partition_order,
							encoder->protected_->do_escape_coding,
							encoder->protected_->rice_parameter_search_dist,
							subframe[!_best_subframe],
							partitioned_rice_contents[!_best_subframe]
						);
					if(_candidate_bits < _best_bits) {
						_best_subframe = !_best_subframe;
						_best_bits = _candidate_bits;
					}
				}
			}

#ifndef FLAC__INTEGER_ONLY_LIBRARY
			/* encode lpc */
			if(encoder->protected_->max_lpc_order > 0) {
				if(encoder->protected_->max_lpc_order >= frame_header->blocksize)
					max_lpc_order = frame_header->blocksize-1;
				else
					max_lpc_order = encoder->protected_->max_lpc_order;
				if(max_lpc_order > 0) {
					unsigned a;
					for (a = 0; a < encoder->protected_->num_apodizations; a++) {
						FLAC__lpc_window_data(integer_signal, encoder->private_->window[a], encoder->private_->windowed_signal, frame_header->blocksize);
						encoder->private_->local_lpc_compute_autocorrelation(encoder->private_->windowed_signal, frame_header->blocksize, max_lpc_order+1, autoc);
						/* if autoc[0] == 0.0, the signal is constant and we usually won't get here, but it can happen */
						if(autoc[0] != 0.0) {
							FLAC__lpc_compute_lp_coefficients(autoc, &max_lpc_order, encoder->private_->lp_coeff, lpc_error);
							if(encoder->protected_->do_exhaustive_model_search) {
								min_lpc_order = 1;
							}
							else {
								const unsigned guess_lpc_order =
									FLAC__lpc_compute_best_order(
										lpc_error,
										max_lpc_order,
										frame_header->blocksize,
										subframe_bps + (
											encoder->protected_->do_qlp_coeff_prec_search?
												FLAC__MIN_QLP_COEFF_PRECISION : /* have to guess; use the min possible size to avoid accidentally favoring lower orders */
												encoder->protected_->qlp_coeff_precision
										)
									);
								min_lpc_order = max_lpc_order = guess_lpc_order;
							}
							if(max_lpc_order >= frame_header->blocksize)
								max_lpc_order = frame_header->blocksize - 1;
							for(lpc_order = min_lpc_order; lpc_order <= max_lpc_order; lpc_order++) {
								lpc_residual_bits_per_sample = FLAC__lpc_compute_expected_bits_per_residual_sample(lpc_error[lpc_order-1], frame_header->blocksize-lpc_order);
								if(lpc_residual_bits_per_sample >= (FLAC__double)subframe_bps)
									continue; /* don't even try */
								rice_parameter = (lpc_residual_bits_per_sample > 0.0)? (unsigned)(lpc_residual_bits_per_sample+0.5) : 0; /* 0.5 is for rounding */
								rice_parameter++; /* to account for the signed->unsigned conversion during rice coding */
								if(rice_parameter >= rice_parameter_limit) {
#ifdef DEBUG_VERBOSE
									fprintf(stderr, "clipping rice_parameter (%u -> %u) @1\n", rice_parameter, rice_parameter_limit - 1);
#endif
									rice_parameter = rice_parameter_limit - 1;
								}
								if(encoder->protected_->do_qlp_coeff_prec_search) {
									min_qlp_coeff_precision = FLAC__MIN_QLP_COEFF_PRECISION;
									/* try to ensure a 32-bit datapath throughout for 16bps(+1bps for side channel) or less */
									if(subframe_bps <= 17) {
										max_qlp_coeff_precision = min(32 - subframe_bps - lpc_order, FLAC__MAX_QLP_COEFF_PRECISION);
										max_qlp_coeff_precision = max(max_qlp_coeff_precision, min_qlp_coeff_precision);
									}
									else
										max_qlp_coeff_precision = FLAC__MAX_QLP_COEFF_PRECISION;
								}
								else {
									min_qlp_coeff_precision = max_qlp_coeff_precision = encoder->protected_->qlp_coeff_precision;
								}
								for(qlp_coeff_precision = min_qlp_coeff_precision; qlp_coeff_precision <= max_qlp_coeff_precision; qlp_coeff_precision++) {
									_candidate_bits =
										evaluate_lpc_subframe_(
											encoder,
											integer_signal,
											residual[!_best_subframe],
											encoder->private_->abs_residual_partition_sums,
											encoder->private_->raw_bits_per_partition,
											encoder->private_->lp_coeff[lpc_order-1],
											frame_header->blocksize,
											subframe_bps,
											lpc_order,
											qlp_coeff_precision,
											rice_parameter,
											rice_parameter_limit,
											min_partition_order,
											max_partition_order,
											encoder->protected_->do_escape_coding,
											encoder->protected_->rice_parameter_search_dist,
											subframe[!_best_subframe],
											partitioned_rice_contents[!_best_subframe]
										);
									if(_candidate_bits > 0) { /* if == 0, there was a problem quantizing the lpcoeffs */
										if(_candidate_bits < _best_bits) {
											_best_subframe = !_best_subframe;
											_best_bits = _candidate_bits;
										}
									}
								}
							}
						}
					}
				}
			}
#endif /* !defined FLAC__INTEGER_ONLY_LIBRARY */
		}
	}

	/* under rare circumstances this can happen when all but lpc subframe types are disabled: */
	if(_best_bits == UINT_MAX) {
		FLAC__ASSERT(_best_subframe == 0);
		_best_bits = evaluate_verbatim_subframe_(encoder, integer_signal, frame_header->blocksize, subframe_bps, subframe[_best_subframe]);
	}

	*best_subframe = _best_subframe;
	*best_bits = _best_bits;

	return true;
}

FLAC__bool add_subframe_(
	FLAC__StreamEncoder *encoder,
	unsigned blocksize,
	unsigned subframe_bps,
	const FLAC__Subframe *subframe,
	FLAC__BitWriter *frame
)
{
	switch(subframe->type) {
		case FLAC__SUBFRAME_TYPE_CONSTANT:
			if(!FLAC__subframe_add_constant(&(subframe->data.constant), subframe_bps, subframe->wasted_bits, frame)) {
				encoder->protected_->state = FLAC__STREAM_ENCODER_FRAMING_ERROR;
				return false;
			}
			break;
		case FLAC__SUBFRAME_TYPE_FIXED:
			if(!FLAC__subframe_add_fixed(&(subframe->data.fixed), blocksize - subframe->data.fixed.order, subframe_bps, subframe->wasted_bits, frame)) {
				encoder->protected_->state = FLAC__STREAM_ENCODER_FRAMING_ERROR;
				return false;
			}
			break;
		case FLAC__SUBFRAME_TYPE_LPC:
			if(!FLAC__subframe_add_lpc(&(subframe->data.lpc), blocksize - subframe->data.lpc.order, subframe_bps, subframe->wasted_bits, frame)) {
				encoder->protected_->state = FLAC__STREAM_ENCODER_FRAMING_ERROR;
				return false;
			}
			break;
		case FLAC__SUBFRAME_TYPE_VERBATIM:
			if(!FLAC__subframe_add_verbatim(&(subframe->data.verbatim), blocksize, subframe_bps, subframe->wasted_bits, frame)) {
				encoder->protected_->state = FLAC__STREAM_ENCODER_FRAMING_ERROR;
				return false;
			}
			break;
		default:
			FLAC__ASSERT(0);
	}

	return true;
}

#define SPOTCHECK_ESTIMATE 0
#if SPOTCHECK_ESTIMATE
static void spotcheck_subframe_estimate_(
	FLAC__StreamEncoder *encoder,
	unsigned blocksize,
	unsigned subframe_bps,
	const FLAC__Subframe *subframe,
	unsigned estimate
)
{
	FLAC__bool ret;
	FLAC__BitWriter *frame = FLAC__bitwriter_new();
	if(frame == 0) {
		fprintf(stderr, "EST: can't allocate frame\n");
		return;
	}
	if(!FLAC__bitwriter_init(frame)) {
		fprintf(stderr, "EST: can't init frame\n");
		return;
	}
	ret = add_subframe_(encoder, blocksize, subframe_bps, subframe, frame);
	FLAC__ASSERT(ret);
	{
		const unsigned actual = FLAC__bitwriter_get_input_bits_unconsumed(frame);
		if(estimate != actual)
			fprintf(stderr, "EST: bad, frame#%u sub#%%d type=%8s est=%u, actual=%u, delta=%d\n", encoder->private_->current_frame_number, FLAC__SubframeTypeString[subframe->type], estimate, actual, (int)actual-(int)estimate);
	}
	FLAC__bitwriter_delete(frame);
}
#endif

unsigned evaluate_constant_subframe_(
	FLAC__StreamEncoder *encoder,
	const FLAC__int32 signal,
	unsigned blocksize,
	unsigned subframe_bps,
	FLAC__Subframe *subframe
)
{
	unsigned estimate;
	subframe->type = FLAC__SUBFRAME_TYPE_CONSTANT;
	subframe->data.constant.value = signal;

	estimate = FLAC__SUBFRAME_ZERO_PAD_LEN + FLAC__SUBFRAME_TYPE_LEN + FLAC__SUBFRAME_WASTED_BITS_FLAG_LEN + subframe->wasted_bits + subframe_bps;

#if SPOTCHECK_ESTIMATE
	spotcheck_subframe_estimate_(encoder, blocksize, subframe_bps, subframe, estimate);
#else
	(void)encoder, (void)blocksize;
#endif

	return estimate;
}

unsigned evaluate_fixed_subframe_(
	FLAC__StreamEncoder *encoder,
	const FLAC__int32 signal[],
	FLAC__int32 residual[],
	FLAC__uint64 abs_residual_partition_sums[],
	unsigned raw_bits_per_partition[],
	unsigned blocksize,
	unsigned subframe_bps,
	unsigned order,
	unsigned rice_parameter,
	unsigned rice_parameter_limit,
	unsigned min_partition_order,
	unsigned max_partition_order,
	FLAC__bool do_escape_coding,
	unsigned rice_parameter_search_dist,
	FLAC__Subframe *subframe,
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents
)
{
	unsigned i, residual_bits, estimate;
	const unsigned residual_samples = blocksize - order;

	FLAC__fixed_compute_residual(signal+order, residual_samples, order, residual);

	subframe->type = FLAC__SUBFRAME_TYPE_FIXED;

	subframe->data.fixed.entropy_coding_method.type = FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE;
	subframe->data.fixed.entropy_coding_method.data.partitioned_rice.contents = partitioned_rice_contents;
	subframe->data.fixed.residual = residual;

	residual_bits =
		find_best_partition_order_(
			encoder->private_,
			residual,
			abs_residual_partition_sums,
			raw_bits_per_partition,
			residual_samples,
			order,
			rice_parameter,
			rice_parameter_limit,
			min_partition_order,
			max_partition_order,
			subframe_bps,
			do_escape_coding,
			rice_parameter_search_dist,
			&subframe->data.fixed.entropy_coding_method
		);

	subframe->data.fixed.order = order;
	for(i = 0; i < order; i++)
		subframe->data.fixed.warmup[i] = signal[i];

	estimate = FLAC__SUBFRAME_ZERO_PAD_LEN + FLAC__SUBFRAME_TYPE_LEN + FLAC__SUBFRAME_WASTED_BITS_FLAG_LEN + subframe->wasted_bits + (order * subframe_bps) + residual_bits;

#if SPOTCHECK_ESTIMATE
	spotcheck_subframe_estimate_(encoder, blocksize, subframe_bps, subframe, estimate);
#endif

	return estimate;
}

#ifndef FLAC__INTEGER_ONLY_LIBRARY
unsigned evaluate_lpc_subframe_(
	FLAC__StreamEncoder *encoder,
	const FLAC__int32 signal[],
	FLAC__int32 residual[],
	FLAC__uint64 abs_residual_partition_sums[],
	unsigned raw_bits_per_partition[],
	const FLAC__real lp_coeff[],
	unsigned blocksize,
	unsigned subframe_bps,
	unsigned order,
	unsigned qlp_coeff_precision,
	unsigned rice_parameter,
	unsigned rice_parameter_limit,
	unsigned min_partition_order,
	unsigned max_partition_order,
	FLAC__bool do_escape_coding,
	unsigned rice_parameter_search_dist,
	FLAC__Subframe *subframe,
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents
)
{
	FLAC__int32 qlp_coeff[FLAC__MAX_LPC_ORDER];
	unsigned i, residual_bits, estimate;
	int quantization, ret;
	const unsigned residual_samples = blocksize - order;

	/* try to keep qlp coeff precision such that only 32-bit math is required for decode of <=16bps streams */
	if(subframe_bps <= 16) {
		FLAC__ASSERT(order > 0);
		FLAC__ASSERT(order <= FLAC__MAX_LPC_ORDER);
		qlp_coeff_precision = min(qlp_coeff_precision, 32 - subframe_bps - FLAC__bitmath_ilog2(order));
	}

	ret = FLAC__lpc_quantize_coefficients(lp_coeff, order, qlp_coeff_precision, qlp_coeff, &quantization);
	if(ret != 0)
		return 0; /* this is a hack to indicate to the caller that we can't do lp at this order on this subframe */

	if(subframe_bps + qlp_coeff_precision + FLAC__bitmath_ilog2(order) <= 32)
		if(subframe_bps <= 16 && qlp_coeff_precision <= 16)
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_16bit(signal+order, residual_samples, qlp_coeff, order, quantization, residual);
		else
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients(signal+order, residual_samples, qlp_coeff, order, quantization, residual);
	else
		encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_64bit(signal+order, residual_samples, qlp_coeff, order, quantization, residual);

	subframe->type = FLAC__SUBFRAME_TYPE_LPC;

	subframe->data.lpc.entropy_coding_method.type = FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE;
	subframe->data.lpc.entropy_coding_method.data.partitioned_rice.contents = partitioned_rice_contents;
	subframe->data.lpc.residual = residual;

	residual_bits =
		find_best_partition_order_(
			encoder->private_,
			residual,
			abs_residual_partition_sums,
			raw_bits_per_partition,
			residual_samples,
			order,
			rice_parameter,
			rice_parameter_limit,
			min_partition_order,
			max_partition_order,
			subframe_bps,
			do_escape_coding,
			rice_parameter_search_dist,
			&subframe->data.lpc.entropy_coding_method
		);

	subframe->data.lpc.order = order;
	subframe->data.lpc.qlp_coeff_precision = qlp_coeff_precision;
	subframe->data.lpc.quantization_level = quantization;
	memcpy(subframe->data.lpc.qlp_coeff, qlp_coeff, sizeof(FLAC__int32)*FLAC__MAX_LPC_ORDER);
	for(i = 0; i < order; i++)
		subframe->data.lpc.warmup[i] = signal[i];

	estimate = FLAC__SUBFRAME_ZERO_PAD_LEN + FLAC__SUBFRAME_TYPE_LEN + FLAC__SUBFRAME_WASTED_BITS_FLAG_LEN + subframe->wasted_bits + FLAC__SUBFRAME_LPC_QLP_COEFF_PRECISION_LEN + FLAC__SUBFRAME_LPC_QLP_SHIFT_LEN + (order * (qlp_coeff_precision + subframe_bps)) + residual_bits;

#if SPOTCHECK_ESTIMATE
	spotcheck_subframe_estimate_(encoder, blocksize, subframe_bps, subframe, estimate);
#endif

	return estimate;
}
#endif

unsigned evaluate_verbatim_subframe_(
	FLAC__StreamEncoder *encoder,
	const FLAC__int32 signal[],
	unsigned blocksize,
	unsigned subframe_bps,
	FLAC__Subframe *subframe
)
{
	unsigned estimate;

	subframe->type = FLAC__SUBFRAME_TYPE_VERBATIM;

	subframe->data.verbatim.data = signal;

	estimate = FLAC__SUBFRAME_ZERO_PAD_LEN + FLAC__SUBFRAME_TYPE_LEN + FLAC__SUBFRAME_WASTED_BITS_FLAG_LEN + subframe->wasted_bits + (blocksize * subframe_bps);

#if SPOTCHECK_ESTIMATE
	spotcheck_subframe_estimate_(encoder, blocksize, subframe_bps, subframe, estimate);
#else
	(void)encoder;
#endif

	return estimate;
}

unsigned find_best_partition_order_(
	FLAC__StreamEncoderPrivate *private_,
	const FLAC__int32 residual[],
	FLAC__uint64 abs_residual_partition_sums[],
	unsigned raw_bits_per_partition[],
	unsigned residual_samples,
	unsigned predictor_order,
	unsigned rice_parameter,
	unsigned rice_parameter_limit,
	unsigned min_partition_order,
	unsigned max_partition_order,
	unsigned bps,
	FLAC__bool do_escape_coding,
	unsigned rice_parameter_search_dist,
	FLAC__EntropyCodingMethod *best_ecm
)
{
	unsigned residual_bits, best_residual_bits = 0;
	unsigned best_parameters_index = 0;
	unsigned best_partition_order = 0;
	const unsigned blocksize = residual_samples + predictor_order;

	max_partition_order = FLAC__format_get_max_rice_partition_order_from_blocksize_limited_max_and_predictor_order(max_partition_order, blocksize, predictor_order);
	min_partition_order = min(min_partition_order, max_partition_order);

	precompute_partition_info_sums_(residual, abs_residual_partition_sums, residual_samples, predictor_order, min_partition_order, max_partition_order, bps);

	if(do_escape_coding)
		precompute_partition_info_escapes_(residual, raw_bits_per_partition, residual_samples, predictor_order, min_partition_order, max_partition_order);

	{
		int partition_order;
		unsigned sum;

		for(partition_order = (int)max_partition_order, sum = 0; partition_order >= (int)min_partition_order; partition_order--) {
			if(!
				set_partitioned_rice_(
					abs_residual_partition_sums+sum,
					raw_bits_per_partition+sum,
					residual_samples,
					predictor_order,
					rice_parameter,
					rice_parameter_limit,
					rice_parameter_search_dist,
					(unsigned)partition_order,
					do_escape_coding,
					&private_->partitioned_rice_contents_extra[!best_parameters_index],
					&residual_bits
				)
			)
			{
				FLAC__ASSERT(best_residual_bits != 0);
				break;
			}
			sum += 1u << partition_order;
			if(best_residual_bits == 0 || residual_bits < best_residual_bits) {
				best_residual_bits = residual_bits;
				best_parameters_index = !best_parameters_index;
				best_partition_order = partition_order;
			}
		}
	}

	best_ecm->data.partitioned_rice.order = best_partition_order;

	{
		/*
		 * We are allowed to de-const the pointer based on our special
		 * knowledge; it is const to the outside world.
		 */
		FLAC__EntropyCodingMethod_PartitionedRiceContents* prc = (FLAC__EntropyCodingMethod_PartitionedRiceContents*)best_ecm->data.partitioned_rice.contents;
		unsigned partition;

		/* save best parameters and raw_bits */
		FLAC__format_entropy_coding_method_partitioned_rice_contents_ensure_size(prc, max(6, best_partition_order));
		memcpy(prc->parameters, private_->partitioned_rice_contents_extra[best_parameters_index].parameters, sizeof(unsigned)*(1<<(best_partition_order)));
		if(do_escape_coding)
			memcpy(prc->raw_bits, private_->partitioned_rice_contents_extra[best_parameters_index].raw_bits, sizeof(unsigned)*(1<<(best_partition_order)));
		/*
		 * Now need to check if the type should be changed to
		 * FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2 based on the
		 * size of the rice parameters.
		 */
		for(partition = 0; partition < (1u<<best_partition_order); partition++) {
			if(prc->parameters[partition] >= FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER) {
				best_ecm->type = FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2;
				break;
			}
		}
	}

	return best_residual_bits;
}

#if defined(FLAC__CPU_IA32) && !defined FLAC__NO_ASM && defined FLAC__HAS_NASM
extern void precompute_partition_info_sums_32bit_asm_ia32_(
	const FLAC__int32 residual[],
	FLAC__uint64 abs_residual_partition_sums[],
	unsigned blocksize,
	unsigned predictor_order,
	unsigned min_partition_order,
	unsigned max_partition_order
);
#endif

void precompute_partition_info_sums_(
	const FLAC__int32 residual[],
	FLAC__uint64 abs_residual_partition_sums[],
	unsigned residual_samples,
	unsigned predictor_order,
	unsigned min_partition_order,
	unsigned max_partition_order,
	unsigned bps
)
{
	const unsigned default_partition_samples = (residual_samples + predictor_order) >> max_partition_order;
	unsigned partitions = 1u << max_partition_order;

	FLAC__ASSERT(default_partition_samples > predictor_order);

#if defined(FLAC__CPU_IA32) && !defined FLAC__NO_ASM && defined FLAC__HAS_NASM
	/* slightly pessimistic but still catches all common cases */
	/* WATCHOUT: "+ bps" is an assumption that the average residual magnitude will not be more than "bps" bits */
	if(FLAC__bitmath_ilog2(default_partition_samples) + bps < 32) {
		precompute_partition_info_sums_32bit_asm_ia32_(residual, abs_residual_partition_sums, residual_samples + predictor_order, predictor_order, min_partition_order, max_partition_order);
		return;
	}
#endif

	/* first do max_partition_order */
	{
		unsigned partition, residual_sample, end = (unsigned)(-(int)predictor_order);
		/* slightly pessimistic but still catches all common cases */
		/* WATCHOUT: "+ bps" is an assumption that the average residual magnitude will not be more than "bps" bits */
		if(FLAC__bitmath_ilog2(default_partition_samples) + bps < 32) {
			FLAC__uint32 abs_residual_partition_sum;

			for(partition = residual_sample = 0; partition < partitions; partition++) {
				end += default_partition_samples;
				abs_residual_partition_sum = 0;
				for( ; residual_sample < end; residual_sample++)
					abs_residual_partition_sum += abs(residual[residual_sample]); /* abs(INT_MIN) is undefined, but if the residual is INT_MIN we have bigger problems */
				abs_residual_partition_sums[partition] = abs_residual_partition_sum;
			}
		}
		else { /* have to pessimistically use 64 bits for accumulator */
			FLAC__uint64 abs_residual_partition_sum;

			for(partition = residual_sample = 0; partition < partitions; partition++) {
				end += default_partition_samples;
				abs_residual_partition_sum = 0;
				for( ; residual_sample < end; residual_sample++)
					abs_residual_partition_sum += abs(residual[residual_sample]); /* abs(INT_MIN) is undefined, but if the residual is INT_MIN we have bigger problems */
				abs_residual_partition_sums[partition] = abs_residual_partition_sum;
			}
		}
	}

	/* now merge partitions for lower orders */
	{
		unsigned from_partition = 0, to_partition = partitions;
		int partition_order;
		for(partition_order = (int)max_partition_order - 1; partition_order >= (int)min_partition_order; partition_order--) {
			unsigned i;
			partitions >>= 1;
			for(i = 0; i < partitions; i++) {
				abs_residual_partition_sums[to_partition++] =
					abs_residual_partition_sums[from_partition  ] +
					abs_residual_partition_sums[from_partition+1];
				from_partition += 2;
			}
		}
	}
}

void precompute_partition_info_escapes_(
	const FLAC__int32 residual[],
	unsigned raw_bits_per_partition[],
	unsigned residual_samples,
	unsigned predictor_order,
	unsigned min_partition_order,
	unsigned max_partition_order
)
{
	int partition_order;
	unsigned from_partition, to_partition = 0;
	const unsigned blocksize = residual_samples + predictor_order;

	/* first do max_partition_order */
	for(partition_order = (int)max_partition_order; partition_order >= 0; partition_order--) {
		FLAC__int32 r;
		FLAC__uint32 rmax;
		unsigned partition, partition_sample, partition_samples, residual_sample;
		const unsigned partitions = 1u << partition_order;
		const unsigned default_partition_samples = blocksize >> partition_order;

		FLAC__ASSERT(default_partition_samples > predictor_order);

		for(partition = residual_sample = 0; partition < partitions; partition++) {
			partition_samples = default_partition_samples;
			if(partition == 0)
				partition_samples -= predictor_order;
			rmax = 0;
			for(partition_sample = 0; partition_sample < partition_samples; partition_sample++) {
				r = residual[residual_sample++];
				/* OPT: maybe faster: rmax |= r ^ (r>>31) */
				if(r < 0)
					rmax |= ~r;
				else
					rmax |= r;
			}
			/* now we know all residual values are in the range [-rmax-1,rmax] */
			raw_bits_per_partition[partition] = rmax? FLAC__bitmath_ilog2(rmax) + 2 : 1;
		}
		to_partition = partitions;
		break; /*@@@ yuck, should remove the 'for' loop instead */
	}

	/* now merge partitions for lower orders */
	for(from_partition = 0, --partition_order; partition_order >= (int)min_partition_order; partition_order--) {
		unsigned m;
		unsigned i;
		const unsigned partitions = 1u << partition_order;
		for(i = 0; i < partitions; i++) {
			m = raw_bits_per_partition[from_partition];
			from_partition++;
			raw_bits_per_partition[to_partition] = max(m, raw_bits_per_partition[from_partition]);
			from_partition++;
			to_partition++;
		}
	}
}



static FLaC__INLINE unsigned count_rice_bits_in_partition_(
	const unsigned rice_parameter,
	const unsigned partition_samples,
	const FLAC__uint64 abs_residual_partition_sum
)
{
	return
		FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_PARAMETER_LEN + /* actually could end up being FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2_PARAMETER_LEN but err on side of 16bps */
		(1+rice_parameter) * partition_samples + /* 1 for unary stop bit + rice_parameter for the binary portion */
		(
			rice_parameter?
				(unsigned)(abs_residual_partition_sum >> (rice_parameter-1)) /* rice_parameter-1 because the real coder sign-folds instead of using a sign bit */
				: (unsigned)(abs_residual_partition_sum << 1) /* can't shift by negative number, so reverse */
		)
		- (partition_samples >> 1)
		/* -(partition_samples>>1) to subtract out extra contributions to the abs_residual_partition_sum.
		 * The actual number of bits used is closer to the sum(for all i in the partition) of  abs(residual[i])>>(rice_parameter-1)
		 * By using the abs_residual_partition sum, we also add in bits in the LSBs that would normally be shifted out.
		 * So the subtraction term tries to guess how many extra bits were contributed.
		 * If the LSBs are randomly distributed, this should average to 0.5 extra bits per sample.
		 */
	;
}

FLAC__bool set_partitioned_rice_(
	const FLAC__uint64 abs_residual_partition_sums[],
	const unsigned raw_bits_per_partition[],
	const unsigned residual_samples,
	const unsigned predictor_order,
	const unsigned suggested_rice_parameter,
	const unsigned rice_parameter_limit,
	const unsigned rice_parameter_search_dist,
	const unsigned partition_order,
	const FLAC__bool search_for_escapes,
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents,
	unsigned *bits
)
{
	unsigned rice_parameter, partition_bits;
	unsigned best_partition_bits, best_rice_parameter = 0;
	unsigned bits_ = FLAC__ENTROPY_CODING_METHOD_TYPE_LEN + FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ORDER_LEN;
	unsigned *parameters, *raw_bits;

	(void)rice_parameter_search_dist;

	FLAC__ASSERT(suggested_rice_parameter < FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2_ESCAPE_PARAMETER);
	FLAC__ASSERT(rice_parameter_limit <= FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2_ESCAPE_PARAMETER);

	FLAC__format_entropy_coding_method_partitioned_rice_contents_ensure_size(partitioned_rice_contents, max(6, partition_order));
	parameters = partitioned_rice_contents->parameters;
	raw_bits = partitioned_rice_contents->raw_bits;

	if(partition_order == 0) {
		best_partition_bits = (unsigned)(-1);

		rice_parameter = suggested_rice_parameter;

		partition_bits = count_rice_bits_in_partition_(rice_parameter, residual_samples, abs_residual_partition_sums[0]);
		if(partition_bits < best_partition_bits) {
			best_rice_parameter = rice_parameter;
			best_partition_bits = partition_bits;
		}

		if(search_for_escapes) {
			partition_bits = FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2_PARAMETER_LEN + FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_RAW_LEN + raw_bits_per_partition[0] * residual_samples;
			if(partition_bits <= best_partition_bits) {
				raw_bits[0] = raw_bits_per_partition[0];
				best_rice_parameter = 0; /* will be converted to appropriate escape parameter later */
				best_partition_bits = partition_bits;
			}
			else
				raw_bits[0] = 0;
		}
		parameters[0] = best_rice_parameter;
		bits_ += best_partition_bits;
	}
	else {
		unsigned partition, residual_sample;
		unsigned partition_samples;
		FLAC__uint64 mean, k;
		const unsigned partitions = 1u << partition_order;
		for(partition = residual_sample = 0; partition < partitions; partition++) {
			partition_samples = (residual_samples+predictor_order) >> partition_order;
			if(partition == 0) {
				if(partition_samples <= predictor_order)
					return false;
				else
					partition_samples -= predictor_order;
			}
			mean = abs_residual_partition_sums[partition];
			/* we are basically calculating the size in bits of the
			 * average residual magnitude in the partition:
			 *   rice_parameter = floor(log2(mean/partition_samples))
			 * 'mean' is not a good name for the variable, it is
			 * actually the sum of magnitudes of all residual values
			 * in the partition, so the actual mean is
			 * mean/partition_samples
			 */
			for(rice_parameter = 0, k = partition_samples; k < mean; rice_parameter++, k <<= 1)
				;
			if(rice_parameter >= rice_parameter_limit) {
#ifdef DEBUG_VERBOSE
				fprintf(stderr, "clipping rice_parameter (%u -> %u) @6\n", rice_parameter, rice_parameter_limit - 1);
#endif
				rice_parameter = rice_parameter_limit - 1;
			}

			best_partition_bits = (unsigned)(-1);
			partition_bits = count_rice_bits_in_partition_(rice_parameter, partition_samples, abs_residual_partition_sums[partition]);
			if(partition_bits < best_partition_bits) {
				best_rice_parameter = rice_parameter;
				best_partition_bits = partition_bits;
			}

			if(search_for_escapes) {
				partition_bits = FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2_PARAMETER_LEN + FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_RAW_LEN + raw_bits_per_partition[partition] * partition_samples;
				if(partition_bits <= best_partition_bits) {
					raw_bits[partition] = raw_bits_per_partition[partition];
					best_rice_parameter = 0; /* will be converted to appropriate escape parameter later */
					best_partition_bits = partition_bits;
				}
				else
					raw_bits[partition] = 0;
			}
			parameters[partition] = best_rice_parameter;
			bits_ += best_partition_bits;
			residual_sample += partition_samples;
		}
	}

	*bits = bits_;
	return true;
}

unsigned get_wasted_bits_(FLAC__int32 signal[], unsigned samples)
{
	unsigned i, shift;
	FLAC__int32 x = 0;

	for(i = 0; i < samples && !(x&1); i++)
		x |= signal[i];

	if(x == 0) {
		shift = 0;
	}
	else {
		for(shift = 0; !(x&1); shift++)
			x >>= 1;
	}

	if(shift > 0) {
		for(i = 0; i < samples; i++)
			 signal[i] >>= shift;
	}

	return shift;
}

void append_to_verify_fifo_(verify_input_fifo *fifo, const FLAC__int32 * const input[], unsigned input_offset, unsigned channels, unsigned wide_samples)
{
	unsigned channel;

	for(channel = 0; channel < channels; channel++)
		memcpy(&fifo->data[channel][fifo->tail], &input[channel][input_offset], sizeof(FLAC__int32) * wide_samples);

	fifo->tail += wide_samples;

	FLAC__ASSERT(fifo->tail <= fifo->size);
}

void append_to_verify_fifo_interleaved_(verify_input_fifo *fifo, const FLAC__int32 input[], unsigned input_offset, unsigned channels, unsigned wide_samples)
{
	unsigned channel;
	unsigned sample, wide_sample;
	unsigned tail = fifo->tail;

	sample = input_offset * channels;
	for(wide_sample = 0; wide_sample < wide_samples; wide_sample++) {
		for(channel = 0; channel < channels; channel++)
			fifo->data[channel][tail] = input[sample++];
		tail++;
	}
	fifo->tail = tail;

	FLAC__ASSERT(fifo->tail <= fifo->size);
}

FLAC__StreamDecoderReadStatus verify_read_callback_(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data)
{
	FLAC__StreamEncoder *encoder = (FLAC__StreamEncoder*)client_data;
	const size_t encoded_bytes = encoder->private_->verify.output.bytes;
	(void)decoder;

	if(encoder->private_->verify.needs_magic_hack) {
		FLAC__ASSERT(*bytes >= FLAC__STREAM_SYNC_LENGTH);
		*bytes = FLAC__STREAM_SYNC_LENGTH;
		memcpy(buffer, FLAC__STREAM_SYNC_STRING, *bytes);
		encoder->private_->verify.needs_magic_hack = false;
	}
	else {
		if(encoded_bytes == 0) {
			/*
			 * If we get here, a FIFO underflow has occurred,
			 * which means there is a bug somewhere.
			 */
			FLAC__ASSERT(0);
			return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
		}
		else if(encoded_bytes < *bytes)
			*bytes = encoded_bytes;
		memcpy(buffer, encoder->private_->verify.output.data, *bytes);
		encoder->private_->verify.output.data += *bytes;
		encoder->private_->verify.output.bytes -= *bytes;
	}

	return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

FLAC__StreamDecoderWriteStatus verify_write_callback_(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[], void *client_data)
{
	FLAC__StreamEncoder *encoder = (FLAC__StreamEncoder *)client_data;
	unsigned channel;
	const unsigned channels = frame->header.channels;
	const unsigned blocksize = frame->header.blocksize;
	const unsigned bytes_per_block = sizeof(FLAC__int32) * blocksize;

	(void)decoder;

	for(channel = 0; channel < channels; channel++) {
		if(0 != memcmp(buffer[channel], encoder->private_->verify.input_fifo.data[channel], bytes_per_block)) {
			unsigned i, sample = 0;
			FLAC__int32 expect = 0, got = 0;

			for(i = 0; i < blocksize; i++) {
				if(buffer[channel][i] != encoder->private_->verify.input_fifo.data[channel][i]) {
					sample = i;
					expect = (FLAC__int32)encoder->private_->verify.input_fifo.data[channel][i];
					got = (FLAC__int32)buffer[channel][i];
					break;
				}
			}
			FLAC__ASSERT(i < blocksize);
			FLAC__ASSERT(frame->header.number_type == FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER);
			encoder->private_->verify.error_stats.absolute_sample = frame->header.number.sample_number + sample;
			encoder->private_->verify.error_stats.frame_number = (unsigned)(frame->header.number.sample_number / blocksize);
			encoder->private_->verify.error_stats.channel = channel;
			encoder->private_->verify.error_stats.sample = sample;
			encoder->private_->verify.error_stats.expected = expect;
			encoder->private_->verify.error_stats.got = got;
			encoder->protected_->state = FLAC__STREAM_ENCODER_VERIFY_MISMATCH_IN_AUDIO_DATA;
			return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
		}
	}
	/* dequeue the frame from the fifo */
	encoder->private_->verify.input_fifo.tail -= blocksize;
	FLAC__ASSERT(encoder->private_->verify.input_fifo.tail <= OVERREAD_);
	for(channel = 0; channel < channels; channel++)
		memmove(&encoder->private_->verify.input_fifo.data[channel][0], &encoder->private_->verify.input_fifo.data[channel][blocksize], encoder->private_->verify.input_fifo.tail * sizeof(encoder->private_->verify.input_fifo.data[0][0]));
	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void verify_metadata_callback_(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data)
{
	(void)decoder, (void)metadata, (void)client_data;
}

void verify_error_callback_(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data)
{
	FLAC__StreamEncoder *encoder = (FLAC__StreamEncoder*)client_data;
	(void)decoder, (void)status;
	encoder->protected_->state = FLAC__STREAM_ENCODER_VERIFY_DECODER_ERROR;
}

FLAC__StreamEncoderReadStatus file_read_callback_(const FLAC__StreamEncoder *encoder, FLAC__byte buffer[], size_t *bytes, void *client_data)
{
	(void)client_data;

	*bytes = fread(buffer, 1, *bytes, encoder->private_->file);
	if (*bytes == 0) {
		if (feof(encoder->private_->file))
			return FLAC__STREAM_ENCODER_READ_STATUS_END_OF_STREAM;
		else if (ferror(encoder->private_->file))
			return FLAC__STREAM_ENCODER_READ_STATUS_ABORT;
	}
	return FLAC__STREAM_ENCODER_READ_STATUS_CONTINUE;
}

FLAC__StreamEncoderSeekStatus file_seek_callback_(const FLAC__StreamEncoder *encoder, FLAC__uint64 absolute_byte_offset, void *client_data)
{
	(void)client_data;

	if(fseeko(encoder->private_->file, (off_t)absolute_byte_offset, SEEK_SET) < 0)
		return FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR;
	else
		return FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
}

FLAC__StreamEncoderTellStatus file_tell_callback_(const FLAC__StreamEncoder *encoder, FLAC__uint64 *absolute_byte_offset, void *client_data)
{
	off_t offset;

	(void)client_data;

	offset = ftello(encoder->private_->file);

	if(offset < 0) {
		return FLAC__STREAM_ENCODER_TELL_STATUS_ERROR;
	}
	else {
		*absolute_byte_offset = (FLAC__uint64)offset;
		return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
	}
}

#ifdef FLAC__VALGRIND_TESTING
static size_t local__fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	size_t ret = fwrite(ptr, size, nmemb, stream);
	if(!ferror(stream))
		fflush(stream);
	return ret;
}
#else
#define local__fwrite fwrite
#endif

FLAC__StreamEncoderWriteStatus file_write_callback_(const FLAC__StreamEncoder *encoder, const FLAC__byte buffer[], size_t bytes, unsigned samples, unsigned current_frame, void *client_data)
{
	(void)client_data, (void)current_frame;

	if(local__fwrite(buffer, sizeof(FLAC__byte), bytes, encoder->private_->file) == bytes) {
		FLAC__bool call_it = 0 != encoder->private_->progress_callback && (
#if FLAC__HAS_OGG
			/* We would like to be able to use 'samples > 0' in the
			 * clause here but currently because of the nature of our
			 * Ogg writing implementation, 'samples' is always 0 (see
			 * ogg_encoder_aspect.c).  The downside is extra progress
			 * callbacks.
			 */
			encoder->private_->is_ogg? true :
#endif
			samples > 0
		);
		if(call_it) {
			/* NOTE: We have to add +bytes, +samples, and +1 to the stats
			 * because at this point in the callback chain, the stats
			 * have not been updated.  Only after we return and control
			 * gets back to write_frame_() are the stats updated
			 */
			encoder->private_->progress_callback(encoder, encoder->private_->bytes_written+bytes, encoder->private_->samples_written+samples, encoder->private_->frames_written+(samples?1:0), encoder->private_->total_frames_estimate, encoder->private_->client_data);
		}
		return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
	}
	else
		return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
}

/*
 * This will forcibly set stdout to binary mode (for OSes that require it)
 */
FILE *get_binary_stdout_(void)
{
	/* if something breaks here it is probably due to the presence or
	 * absence of an underscore before the identifiers 'setmode',
	 * 'fileno', and/or 'O_BINARY'; check your system header files.
	 */
#if defined _MSC_VER || defined __MINGW32__
	_setmode(_fileno(stdout), _O_BINARY);
#elif defined __CYGWIN__
	/* almost certainly not needed for any modern Cygwin, but let's be safe... */
	setmode(_fileno(stdout), _O_BINARY);
#elif defined __EMX__
	setmode(fileno(stdout), O_BINARY);
#endif

	return stdout;
}
//++-- stream_encoder.c 

//++ stream_encoder_framing.c 

static FLAC__bool add_entropy_coding_method_(FLAC__BitWriter *bw, const FLAC__EntropyCodingMethod *method);
static FLAC__bool add_residual_partitioned_rice_(FLAC__BitWriter *bw, const FLAC__int32 residual[], const unsigned residual_samples, const unsigned predictor_order, const unsigned rice_parameters[], const unsigned raw_bits[], const unsigned partition_order, const FLAC__bool is_extended);

FLAC__bool FLAC__add_metadata_block(const FLAC__StreamMetadata *metadata, FLAC__BitWriter *bw)
{
	unsigned i, j;
	const unsigned vendor_string_length = (unsigned)strlen(FLAC__VENDOR_STRING);

	if(!FLAC__bitwriter_write_raw_uint32(bw, metadata->is_last, FLAC__STREAM_METADATA_IS_LAST_LEN))
		return false;

	if(!FLAC__bitwriter_write_raw_uint32(bw, metadata->type, FLAC__STREAM_METADATA_TYPE_LEN))
		return false;

	/*
	 * First, for VORBIS_COMMENTs, adjust the length to reflect our vendor string
	 */
	i = metadata->length;
	if(metadata->type == FLAC__METADATA_TYPE_VORBIS_COMMENT) {
		FLAC__ASSERT(metadata->data.vorbis_comment.vendor_string.length == 0 || 0 != metadata->data.vorbis_comment.vendor_string.entry);
		i -= metadata->data.vorbis_comment.vendor_string.length;
		i += vendor_string_length;
	}
	FLAC__ASSERT(i < (1u << FLAC__STREAM_METADATA_LENGTH_LEN));
	if(!FLAC__bitwriter_write_raw_uint32(bw, i, FLAC__STREAM_METADATA_LENGTH_LEN))
		return false;

	switch(metadata->type) {
		case FLAC__METADATA_TYPE_STREAMINFO:
			FLAC__ASSERT(metadata->data.stream_info.min_blocksize < (1u << FLAC__STREAM_METADATA_STREAMINFO_MIN_BLOCK_SIZE_LEN));
			if(!FLAC__bitwriter_write_raw_uint32(bw, metadata->data.stream_info.min_blocksize, FLAC__STREAM_METADATA_STREAMINFO_MIN_BLOCK_SIZE_LEN))
				return false;
			FLAC__ASSERT(metadata->data.stream_info.max_blocksize < (1u << FLAC__STREAM_METADATA_STREAMINFO_MAX_BLOCK_SIZE_LEN));
			if(!FLAC__bitwriter_write_raw_uint32(bw, metadata->data.stream_info.max_blocksize, FLAC__STREAM_METADATA_STREAMINFO_MAX_BLOCK_SIZE_LEN))
				return false;
			FLAC__ASSERT(metadata->data.stream_info.min_framesize < (1u << FLAC__STREAM_METADATA_STREAMINFO_MIN_FRAME_SIZE_LEN));
			if(!FLAC__bitwriter_write_raw_uint32(bw, metadata->data.stream_info.min_framesize, FLAC__STREAM_METADATA_STREAMINFO_MIN_FRAME_SIZE_LEN))
				return false;
			FLAC__ASSERT(metadata->data.stream_info.max_framesize < (1u << FLAC__STREAM_METADATA_STREAMINFO_MAX_FRAME_SIZE_LEN));
			if(!FLAC__bitwriter_write_raw_uint32(bw, metadata->data.stream_info.max_framesize, FLAC__STREAM_METADATA_STREAMINFO_MAX_FRAME_SIZE_LEN))
				return false;
			FLAC__ASSERT(FLAC__format_sample_rate_is_valid(metadata->data.stream_info.sample_rate));
			if(!FLAC__bitwriter_write_raw_uint32(bw, metadata->data.stream_info.sample_rate, FLAC__STREAM_METADATA_STREAMINFO_SAMPLE_RATE_LEN))
				return false;
			FLAC__ASSERT(metadata->data.stream_info.channels > 0);
			FLAC__ASSERT(metadata->data.stream_info.channels <= (1u << FLAC__STREAM_METADATA_STREAMINFO_CHANNELS_LEN));
			if(!FLAC__bitwriter_write_raw_uint32(bw, metadata->data.stream_info.channels-1, FLAC__STREAM_METADATA_STREAMINFO_CHANNELS_LEN))
				return false;
			FLAC__ASSERT(metadata->data.stream_info.bits_per_sample > 0);
			FLAC__ASSERT(metadata->data.stream_info.bits_per_sample <= (1u << FLAC__STREAM_METADATA_STREAMINFO_BITS_PER_SAMPLE_LEN));
			if(!FLAC__bitwriter_write_raw_uint32(bw, metadata->data.stream_info.bits_per_sample-1, FLAC__STREAM_METADATA_STREAMINFO_BITS_PER_SAMPLE_LEN))
				return false;
			if(!FLAC__bitwriter_write_raw_uint64(bw, metadata->data.stream_info.total_samples, FLAC__STREAM_METADATA_STREAMINFO_TOTAL_SAMPLES_LEN))
				return false;
			if(!FLAC__bitwriter_write_byte_block(bw, metadata->data.stream_info.md5sum, 16))
				return false;
			break;
		case FLAC__METADATA_TYPE_PADDING:
			if(!FLAC__bitwriter_write_zeroes(bw, metadata->length * 8))
				return false;
			break;
		case FLAC__METADATA_TYPE_APPLICATION:
			if(!FLAC__bitwriter_write_byte_block(bw, metadata->data.application.id, FLAC__STREAM_METADATA_APPLICATION_ID_LEN / 8))
				return false;
			if(!FLAC__bitwriter_write_byte_block(bw, metadata->data.application.data, metadata->length - (FLAC__STREAM_METADATA_APPLICATION_ID_LEN / 8)))
				return false;
			break;
		case FLAC__METADATA_TYPE_SEEKTABLE:
			for(i = 0; i < metadata->data.seek_table.num_points; i++) {
				if(!FLAC__bitwriter_write_raw_uint64(bw, metadata->data.seek_table.points[i].sample_number, FLAC__STREAM_METADATA_SEEKPOINT_SAMPLE_NUMBER_LEN))
					return false;
				if(!FLAC__bitwriter_write_raw_uint64(bw, metadata->data.seek_table.points[i].stream_offset, FLAC__STREAM_METADATA_SEEKPOINT_STREAM_OFFSET_LEN))
					return false;
				if(!FLAC__bitwriter_write_raw_uint32(bw, metadata->data.seek_table.points[i].frame_samples, FLAC__STREAM_METADATA_SEEKPOINT_FRAME_SAMPLES_LEN))
					return false;
			}
			break;
		case FLAC__METADATA_TYPE_VORBIS_COMMENT:
			if(!FLAC__bitwriter_write_raw_uint32_little_endian(bw, vendor_string_length))
				return false;
			if(!FLAC__bitwriter_write_byte_block(bw, (const FLAC__byte*)FLAC__VENDOR_STRING, vendor_string_length))
				return false;
			if(!FLAC__bitwriter_write_raw_uint32_little_endian(bw, metadata->data.vorbis_comment.num_comments))
				return false;
			for(i = 0; i < metadata->data.vorbis_comment.num_comments; i++) {
				if(!FLAC__bitwriter_write_raw_uint32_little_endian(bw, metadata->data.vorbis_comment.comments[i].length))
					return false;
				if(!FLAC__bitwriter_write_byte_block(bw, metadata->data.vorbis_comment.comments[i].entry, metadata->data.vorbis_comment.comments[i].length))
					return false;
			}
			break;
		case FLAC__METADATA_TYPE_CUESHEET:
			FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_MEDIA_CATALOG_NUMBER_LEN % 8 == 0);
			if(!FLAC__bitwriter_write_byte_block(bw, (const FLAC__byte*)metadata->data.cue_sheet.media_catalog_number, FLAC__STREAM_METADATA_CUESHEET_MEDIA_CATALOG_NUMBER_LEN/8))
				return false;
			if(!FLAC__bitwriter_write_raw_uint64(bw, metadata->data.cue_sheet.lead_in, FLAC__STREAM_METADATA_CUESHEET_LEAD_IN_LEN))
				return false;
			if(!FLAC__bitwriter_write_raw_uint32(bw, metadata->data.cue_sheet.is_cd? 1 : 0, FLAC__STREAM_METADATA_CUESHEET_IS_CD_LEN))
				return false;
			if(!FLAC__bitwriter_write_zeroes(bw, FLAC__STREAM_METADATA_CUESHEET_RESERVED_LEN))
				return false;
			if(!FLAC__bitwriter_write_raw_uint32(bw, metadata->data.cue_sheet.num_tracks, FLAC__STREAM_METADATA_CUESHEET_NUM_TRACKS_LEN))
				return false;
			for(i = 0; i < metadata->data.cue_sheet.num_tracks; i++) {
				const FLAC__StreamMetadata_CueSheet_Track *track = metadata->data.cue_sheet.tracks + i;

				if(!FLAC__bitwriter_write_raw_uint64(bw, track->offset, FLAC__STREAM_METADATA_CUESHEET_TRACK_OFFSET_LEN))
					return false;
				if(!FLAC__bitwriter_write_raw_uint32(bw, track->number, FLAC__STREAM_METADATA_CUESHEET_TRACK_NUMBER_LEN))
					return false;
				FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_TRACK_ISRC_LEN % 8 == 0);
				if(!FLAC__bitwriter_write_byte_block(bw, (const FLAC__byte*)track->isrc, FLAC__STREAM_METADATA_CUESHEET_TRACK_ISRC_LEN/8))
					return false;
				if(!FLAC__bitwriter_write_raw_uint32(bw, track->type, FLAC__STREAM_METADATA_CUESHEET_TRACK_TYPE_LEN))
					return false;
				if(!FLAC__bitwriter_write_raw_uint32(bw, track->pre_emphasis, FLAC__STREAM_METADATA_CUESHEET_TRACK_PRE_EMPHASIS_LEN))
					return false;
				if(!FLAC__bitwriter_write_zeroes(bw, FLAC__STREAM_METADATA_CUESHEET_TRACK_RESERVED_LEN))
					return false;
				if(!FLAC__bitwriter_write_raw_uint32(bw, track->num_indices, FLAC__STREAM_METADATA_CUESHEET_TRACK_NUM_INDICES_LEN))
					return false;
				for(j = 0; j < track->num_indices; j++) {
					const FLAC__StreamMetadata_CueSheet_Index *index = track->indices + j;

					if(!FLAC__bitwriter_write_raw_uint64(bw, index->offset, FLAC__STREAM_METADATA_CUESHEET_INDEX_OFFSET_LEN))
						return false;
					if(!FLAC__bitwriter_write_raw_uint32(bw, index->number, FLAC__STREAM_METADATA_CUESHEET_INDEX_NUMBER_LEN))
						return false;
					if(!FLAC__bitwriter_write_zeroes(bw, FLAC__STREAM_METADATA_CUESHEET_INDEX_RESERVED_LEN))
						return false;
				}
			}
			break;
		case FLAC__METADATA_TYPE_PICTURE:
			{
				size_t len;
				if(!FLAC__bitwriter_write_raw_uint32(bw, metadata->data.picture.type, FLAC__STREAM_METADATA_PICTURE_TYPE_LEN))
					return false;
				len = strlen(metadata->data.picture.mime_type);
				if(!FLAC__bitwriter_write_raw_uint32(bw, len, FLAC__STREAM_METADATA_PICTURE_MIME_TYPE_LENGTH_LEN))
					return false;
				if(!FLAC__bitwriter_write_byte_block(bw, (const FLAC__byte*)metadata->data.picture.mime_type, len))
					return false;
				len = strlen((const char *)metadata->data.picture.description);
				if(!FLAC__bitwriter_write_raw_uint32(bw, len, FLAC__STREAM_METADATA_PICTURE_DESCRIPTION_LENGTH_LEN))
					return false;
				if(!FLAC__bitwriter_write_byte_block(bw, metadata->data.picture.description, len))
					return false;
				if(!FLAC__bitwriter_write_raw_uint32(bw, metadata->data.picture.width, FLAC__STREAM_METADATA_PICTURE_WIDTH_LEN))
					return false;
				if(!FLAC__bitwriter_write_raw_uint32(bw, metadata->data.picture.height, FLAC__STREAM_METADATA_PICTURE_HEIGHT_LEN))
					return false;
				if(!FLAC__bitwriter_write_raw_uint32(bw, metadata->data.picture.depth, FLAC__STREAM_METADATA_PICTURE_DEPTH_LEN))
					return false;
				if(!FLAC__bitwriter_write_raw_uint32(bw, metadata->data.picture.colors, FLAC__STREAM_METADATA_PICTURE_COLORS_LEN))
					return false;
				if(!FLAC__bitwriter_write_raw_uint32(bw, metadata->data.picture.data_length, FLAC__STREAM_METADATA_PICTURE_DATA_LENGTH_LEN))
					return false;
				if(!FLAC__bitwriter_write_byte_block(bw, metadata->data.picture.data, metadata->data.picture.data_length))
					return false;
			}
			break;
		default:
			if(!FLAC__bitwriter_write_byte_block(bw, metadata->data.unknown.data, metadata->length))
				return false;
			break;
	}

	FLAC__ASSERT(FLAC__bitwriter_is_byte_aligned(bw));
	return true;
}

FLAC__bool FLAC__frame_add_header(const FLAC__FrameHeader *header, FLAC__BitWriter *bw)
{
	unsigned u, blocksize_hint, sample_rate_hint;
	FLAC__byte crc;

	FLAC__ASSERT(FLAC__bitwriter_is_byte_aligned(bw));

	if(!FLAC__bitwriter_write_raw_uint32(bw, FLAC__FRAME_HEADER_SYNC, FLAC__FRAME_HEADER_SYNC_LEN))
		return false;

	if(!FLAC__bitwriter_write_raw_uint32(bw, 0, FLAC__FRAME_HEADER_RESERVED_LEN))
		return false;

	if(!FLAC__bitwriter_write_raw_uint32(bw, (header->number_type == FLAC__FRAME_NUMBER_TYPE_FRAME_NUMBER)? 0 : 1, FLAC__FRAME_HEADER_BLOCKING_STRATEGY_LEN))
		return false;

	FLAC__ASSERT(header->blocksize > 0 && header->blocksize <= FLAC__MAX_BLOCK_SIZE);
	/* when this assertion holds true, any legal blocksize can be expressed in the frame header */
	FLAC__ASSERT(FLAC__MAX_BLOCK_SIZE <= 65535u);
	blocksize_hint = 0;
	switch(header->blocksize) {
		case   192: u = 1; break;
		case   576: u = 2; break;
		case  1152: u = 3; break;
		case  2304: u = 4; break;
		case  4608: u = 5; break;
		case   256: u = 8; break;
		case   512: u = 9; break;
		case  1024: u = 10; break;
		case  2048: u = 11; break;
		case  4096: u = 12; break;
		case  8192: u = 13; break;
		case 16384: u = 14; break;
		case 32768: u = 15; break;
		default:
			if(header->blocksize <= 0x100)
				blocksize_hint = u = 6;
			else
				blocksize_hint = u = 7;
			break;
	}
	if(!FLAC__bitwriter_write_raw_uint32(bw, u, FLAC__FRAME_HEADER_BLOCK_SIZE_LEN))
		return false;

	FLAC__ASSERT(FLAC__format_sample_rate_is_valid(header->sample_rate));
	sample_rate_hint = 0;
	switch(header->sample_rate) {
		case  88200: u = 1; break;
		case 176400: u = 2; break;
		case 192000: u = 3; break;
		case   8000: u = 4; break;
		case  16000: u = 5; break;
		case  22050: u = 6; break;
		case  24000: u = 7; break;
		case  32000: u = 8; break;
		case  44100: u = 9; break;
		case  48000: u = 10; break;
		case  96000: u = 11; break;
		default:
			if(header->sample_rate <= 255000 && header->sample_rate % 1000 == 0)
				sample_rate_hint = u = 12;
			else if(header->sample_rate % 10 == 0)
				sample_rate_hint = u = 14;
			else if(header->sample_rate <= 0xffff)
				sample_rate_hint = u = 13;
			else
				u = 0;
			break;
	}
	if(!FLAC__bitwriter_write_raw_uint32(bw, u, FLAC__FRAME_HEADER_SAMPLE_RATE_LEN))
		return false;

	FLAC__ASSERT(header->channels > 0 && header->channels <= (1u << FLAC__STREAM_METADATA_STREAMINFO_CHANNELS_LEN) && header->channels <= FLAC__MAX_CHANNELS);
	switch(header->channel_assignment) {
		case FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT:
			u = header->channels - 1;
			break;
		case FLAC__CHANNEL_ASSIGNMENT_LEFT_SIDE:
			FLAC__ASSERT(header->channels == 2);
			u = 8;
			break;
		case FLAC__CHANNEL_ASSIGNMENT_RIGHT_SIDE:
			FLAC__ASSERT(header->channels == 2);
			u = 9;
			break;
		case FLAC__CHANNEL_ASSIGNMENT_MID_SIDE:
			FLAC__ASSERT(header->channels == 2);
			u = 10;
			break;
		default:
			FLAC__ASSERT(0);
	}
	if(!FLAC__bitwriter_write_raw_uint32(bw, u, FLAC__FRAME_HEADER_CHANNEL_ASSIGNMENT_LEN))
		return false;

	FLAC__ASSERT(header->bits_per_sample > 0 && header->bits_per_sample <= (1u << FLAC__STREAM_METADATA_STREAMINFO_BITS_PER_SAMPLE_LEN));
	switch(header->bits_per_sample) {
		case 8 : u = 1; break;
		case 12: u = 2; break;
		case 16: u = 4; break;
		case 20: u = 5; break;
		case 24: u = 6; break;
		default: u = 0; break;
	}
	if(!FLAC__bitwriter_write_raw_uint32(bw, u, FLAC__FRAME_HEADER_BITS_PER_SAMPLE_LEN))
		return false;

	if(!FLAC__bitwriter_write_raw_uint32(bw, 0, FLAC__FRAME_HEADER_ZERO_PAD_LEN))
		return false;

	if(header->number_type == FLAC__FRAME_NUMBER_TYPE_FRAME_NUMBER) {
		if(!FLAC__bitwriter_write_utf8_uint32(bw, header->number.frame_number))
			return false;
	}
	else {
		if(!FLAC__bitwriter_write_utf8_uint64(bw, header->number.sample_number))
			return false;
	}

	if(blocksize_hint)
		if(!FLAC__bitwriter_write_raw_uint32(bw, header->blocksize-1, (blocksize_hint==6)? 8:16))
			return false;

	switch(sample_rate_hint) {
		case 12:
			if(!FLAC__bitwriter_write_raw_uint32(bw, header->sample_rate / 1000, 8))
				return false;
			break;
		case 13:
			if(!FLAC__bitwriter_write_raw_uint32(bw, header->sample_rate, 16))
				return false;
			break;
		case 14:
			if(!FLAC__bitwriter_write_raw_uint32(bw, header->sample_rate / 10, 16))
				return false;
			break;
	}

	/* write the CRC */
	if(!FLAC__bitwriter_get_write_crc8(bw, &crc))
		return false;
	if(!FLAC__bitwriter_write_raw_uint32(bw, crc, FLAC__FRAME_HEADER_CRC_LEN))
		return false;

	return true;
}

FLAC__bool FLAC__subframe_add_constant(const FLAC__Subframe_Constant *subframe, unsigned subframe_bps, unsigned wasted_bits, FLAC__BitWriter *bw)
{
	FLAC__bool ok;

	ok =
		FLAC__bitwriter_write_raw_uint32(bw, FLAC__SUBFRAME_TYPE_CONSTANT_BYTE_ALIGNED_MASK | (wasted_bits? 1:0), FLAC__SUBFRAME_ZERO_PAD_LEN + FLAC__SUBFRAME_TYPE_LEN + FLAC__SUBFRAME_WASTED_BITS_FLAG_LEN) &&
		(wasted_bits? FLAC__bitwriter_write_unary_unsigned(bw, wasted_bits-1) : true) &&
		FLAC__bitwriter_write_raw_int32(bw, subframe->value, subframe_bps)
	;

	return ok;
}

FLAC__bool FLAC__subframe_add_fixed(const FLAC__Subframe_Fixed *subframe, unsigned residual_samples, unsigned subframe_bps, unsigned wasted_bits, FLAC__BitWriter *bw)
{
	unsigned i;

	if(!FLAC__bitwriter_write_raw_uint32(bw, FLAC__SUBFRAME_TYPE_FIXED_BYTE_ALIGNED_MASK | (subframe->order<<1) | (wasted_bits? 1:0), FLAC__SUBFRAME_ZERO_PAD_LEN + FLAC__SUBFRAME_TYPE_LEN + FLAC__SUBFRAME_WASTED_BITS_FLAG_LEN))
		return false;
	if(wasted_bits)
		if(!FLAC__bitwriter_write_unary_unsigned(bw, wasted_bits-1))
			return false;

	for(i = 0; i < subframe->order; i++)
		if(!FLAC__bitwriter_write_raw_int32(bw, subframe->warmup[i], subframe_bps))
			return false;

	if(!add_entropy_coding_method_(bw, &subframe->entropy_coding_method))
		return false;
	switch(subframe->entropy_coding_method.type) {
		case FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE:
		case FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2:
			if(!add_residual_partitioned_rice_(
				bw,
				subframe->residual,
				residual_samples,
				subframe->order,
				subframe->entropy_coding_method.data.partitioned_rice.contents->parameters,
				subframe->entropy_coding_method.data.partitioned_rice.contents->raw_bits,
				subframe->entropy_coding_method.data.partitioned_rice.order,
				/*is_extended=*/subframe->entropy_coding_method.type == FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2
			))
				return false;
			break;
		default:
			FLAC__ASSERT(0);
	}

	return true;
}

FLAC__bool FLAC__subframe_add_lpc(const FLAC__Subframe_LPC *subframe, unsigned residual_samples, unsigned subframe_bps, unsigned wasted_bits, FLAC__BitWriter *bw)
{
	unsigned i;

	if(!FLAC__bitwriter_write_raw_uint32(bw, FLAC__SUBFRAME_TYPE_LPC_BYTE_ALIGNED_MASK | ((subframe->order-1)<<1) | (wasted_bits? 1:0), FLAC__SUBFRAME_ZERO_PAD_LEN + FLAC__SUBFRAME_TYPE_LEN + FLAC__SUBFRAME_WASTED_BITS_FLAG_LEN))
		return false;
	if(wasted_bits)
		if(!FLAC__bitwriter_write_unary_unsigned(bw, wasted_bits-1))
			return false;

	for(i = 0; i < subframe->order; i++)
		if(!FLAC__bitwriter_write_raw_int32(bw, subframe->warmup[i], subframe_bps))
			return false;

	if(!FLAC__bitwriter_write_raw_uint32(bw, subframe->qlp_coeff_precision-1, FLAC__SUBFRAME_LPC_QLP_COEFF_PRECISION_LEN))
		return false;
	if(!FLAC__bitwriter_write_raw_int32(bw, subframe->quantization_level, FLAC__SUBFRAME_LPC_QLP_SHIFT_LEN))
		return false;
	for(i = 0; i < subframe->order; i++)
		if(!FLAC__bitwriter_write_raw_int32(bw, subframe->qlp_coeff[i], subframe->qlp_coeff_precision))
			return false;

	if(!add_entropy_coding_method_(bw, &subframe->entropy_coding_method))
		return false;
	switch(subframe->entropy_coding_method.type) {
		case FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE:
		case FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2:
			if(!add_residual_partitioned_rice_(
				bw,
				subframe->residual,
				residual_samples,
				subframe->order,
				subframe->entropy_coding_method.data.partitioned_rice.contents->parameters,
				subframe->entropy_coding_method.data.partitioned_rice.contents->raw_bits,
				subframe->entropy_coding_method.data.partitioned_rice.order,
				/*is_extended=*/subframe->entropy_coding_method.type == FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2
			))
				return false;
			break;
		default:
			FLAC__ASSERT(0);
	}

	return true;
}

FLAC__bool FLAC__subframe_add_verbatim(const FLAC__Subframe_Verbatim *subframe, unsigned samples, unsigned subframe_bps, unsigned wasted_bits, FLAC__BitWriter *bw)
{
	unsigned i;
	const FLAC__int32 *signal = subframe->data;

	if(!FLAC__bitwriter_write_raw_uint32(bw, FLAC__SUBFRAME_TYPE_VERBATIM_BYTE_ALIGNED_MASK | (wasted_bits? 1:0), FLAC__SUBFRAME_ZERO_PAD_LEN + FLAC__SUBFRAME_TYPE_LEN + FLAC__SUBFRAME_WASTED_BITS_FLAG_LEN))
		return false;
	if(wasted_bits)
		if(!FLAC__bitwriter_write_unary_unsigned(bw, wasted_bits-1))
			return false;

	for(i = 0; i < samples; i++)
		if(!FLAC__bitwriter_write_raw_int32(bw, signal[i], subframe_bps))
			return false;

	return true;
}

FLAC__bool add_entropy_coding_method_(FLAC__BitWriter *bw, const FLAC__EntropyCodingMethod *method)
{
	if(!FLAC__bitwriter_write_raw_uint32(bw, method->type, FLAC__ENTROPY_CODING_METHOD_TYPE_LEN))
		return false;
	switch(method->type) {
		case FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE:
		case FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2:
			if(!FLAC__bitwriter_write_raw_uint32(bw, method->data.partitioned_rice.order, FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ORDER_LEN))
				return false;
			break;
		default:
			FLAC__ASSERT(0);
	}
	return true;
}

FLAC__bool add_residual_partitioned_rice_(FLAC__BitWriter *bw, const FLAC__int32 residual[], const unsigned residual_samples, const unsigned predictor_order, const unsigned rice_parameters[], const unsigned raw_bits[], const unsigned partition_order, const FLAC__bool is_extended)
{
	const unsigned plen = is_extended? FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2_PARAMETER_LEN : FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_PARAMETER_LEN;
	const unsigned pesc = is_extended? FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2_ESCAPE_PARAMETER : FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER;

	if(partition_order == 0) {
		unsigned i;

		if(raw_bits[0] == 0) {
			if(!FLAC__bitwriter_write_raw_uint32(bw, rice_parameters[0], plen))
				return false;
			if(!FLAC__bitwriter_write_rice_signed_block(bw, residual, residual_samples, rice_parameters[0]))
				return false;
		}
		else {
			FLAC__ASSERT(rice_parameters[0] == 0);
			if(!FLAC__bitwriter_write_raw_uint32(bw, pesc, plen))
				return false;
			if(!FLAC__bitwriter_write_raw_uint32(bw, raw_bits[0], FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_RAW_LEN))
				return false;
			for(i = 0; i < residual_samples; i++) {
				if(!FLAC__bitwriter_write_raw_int32(bw, residual[i], raw_bits[0]))
					return false;
			}
		}
		return true;
	}
	else {
		unsigned i, j, k = 0, k_last = 0;
		unsigned partition_samples;
		const unsigned default_partition_samples = (residual_samples+predictor_order) >> partition_order;
		for(i = 0; i < (1u<<partition_order); i++) {
			partition_samples = default_partition_samples;
			if(i == 0)
				partition_samples -= predictor_order;
			k += partition_samples;
			if(raw_bits[i] == 0) {
				if(!FLAC__bitwriter_write_raw_uint32(bw, rice_parameters[i], plen))
					return false;
				if(!FLAC__bitwriter_write_rice_signed_block(bw, residual+k_last, k-k_last, rice_parameters[i]))
					return false;
			}
			else {
				if(!FLAC__bitwriter_write_raw_uint32(bw, pesc, plen))
					return false;
				if(!FLAC__bitwriter_write_raw_uint32(bw, raw_bits[i], FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_RAW_LEN))
					return false;
				for(j = k_last; j < k; j++) {
					if(!FLAC__bitwriter_write_raw_int32(bw, residual[j], raw_bits[i]))
						return false;
				}
			}
			k_last = k;
		}
		return true;
	}
}
//++-- stream_encoder_framing.c 

//++ lpc.c 
#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h> /* for qsort() */
#include <string.h> /* for memset() */
#include <math.h>

#if defined DEBUG || defined FLAC__OVERFLOW_DETECT || defined FLAC__OVERFLOW_DETECT_VERBOSE
#include <stdio.h>
#endif

#ifndef FLAC__INTEGER_ONLY_LIBRARY

#ifndef M_LN2
/* math.h in VC++ doesn't seem to have this (how Microsoft is that?) */
#define M_LN2 0.69314718055994530942
#endif

/* OPT: #undef'ing this may improve the speed on some architectures */
#define FLAC__LPC_UNROLLED_FILTER_LOOPS


void FLAC__lpc_window_data(const FLAC__int32 in[], const FLAC__real window[], FLAC__real out[], unsigned data_len)
{
	unsigned i;
	for(i = 0; i < data_len; i++)
		out[i] = in[i] * window[i];
}

void FLAC__lpc_compute_autocorrelation(const FLAC__real data[], unsigned data_len, unsigned lag, FLAC__real autoc[])
{
	/* a readable, but slower, version */
#if 0
	FLAC__real d;
	unsigned i;

	FLAC__ASSERT(lag > 0);
	FLAC__ASSERT(lag <= data_len);

	/*
	 * Technically we should subtract the mean first like so:
	 *   for(i = 0; i < data_len; i++)
	 *     data[i] -= mean;
	 * but it appears not to make enough of a difference to matter, and
	 * most signals are already closely centered around zero
	 */
	while(lag--) {
		for(i = lag, d = 0.0; i < data_len; i++)
			d += data[i] * data[i - lag];
		autoc[lag] = d;
	}
#endif

	/*
	 * this version tends to run faster because of better data locality
	 * ('data_len' is usually much larger than 'lag')
	 */
	FLAC__real d;
	unsigned sample, coeff;
	const unsigned limit = data_len - lag;

	FLAC__ASSERT(lag > 0);
	FLAC__ASSERT(lag <= data_len);

	for(coeff = 0; coeff < lag; coeff++)
		autoc[coeff] = 0.0;
	for(sample = 0; sample <= limit; sample++) {
		d = data[sample];
		for(coeff = 0; coeff < lag; coeff++)
			autoc[coeff] += d * data[sample+coeff];
	}
	for(; sample < data_len; sample++) {
		d = data[sample];
		for(coeff = 0; coeff < data_len - sample; coeff++)
			autoc[coeff] += d * data[sample+coeff];
	}
}

void FLAC__lpc_compute_lp_coefficients(const FLAC__real autoc[], unsigned *max_order, FLAC__real lp_coeff[][FLAC__MAX_LPC_ORDER], FLAC__double error[])
{
	unsigned i, j;
	FLAC__double r, err, ref[FLAC__MAX_LPC_ORDER], lpc[FLAC__MAX_LPC_ORDER];

	FLAC__ASSERT(0 != max_order);
	FLAC__ASSERT(0 < *max_order);
	FLAC__ASSERT(*max_order <= FLAC__MAX_LPC_ORDER);
	FLAC__ASSERT(autoc[0] != 0.0);

	err = autoc[0];

	for(i = 0; i < *max_order; i++) {
		/* Sum up this iteration's reflection coefficient. */
		r = -autoc[i+1];
		for(j = 0; j < i; j++)
			r -= lpc[j] * autoc[i-j];
		ref[i] = (r/=err);

		/* Update LPC coefficients and total error. */
		lpc[i]=r;
		for(j = 0; j < (i>>1); j++) {
			FLAC__double tmp = lpc[j];
			lpc[j] += r * lpc[i-1-j];
			lpc[i-1-j] += r * tmp;
		}
		if(i & 1)
			lpc[j] += lpc[j] * r;

		err *= (1.0 - r * r);

		/* save this order */
		for(j = 0; j <= i; j++)
			lp_coeff[i][j] = (FLAC__real)(-lpc[j]); /* negate FIR filter coeff to get predictor coeff */
		error[i] = err;

		/* see SF bug #1601812 http://sourceforge.net/tracker/index.php?func=detail&aid=1601812&group_id=13478&atid=113478 */
		if(err == 0.0) {
			*max_order = i+1;
			return;
		}
	}
}

int FLAC__lpc_quantize_coefficients(const FLAC__real lp_coeff[], unsigned order, unsigned precision, FLAC__int32 qlp_coeff[], int *shift)
{
	unsigned i;
	FLAC__double cmax;
	FLAC__int32 qmax, qmin;

	FLAC__ASSERT(precision > 0);
	FLAC__ASSERT(precision >= FLAC__MIN_QLP_COEFF_PRECISION);

	/* drop one bit for the sign; from here on out we consider only |lp_coeff[i]| */
	precision--;
	qmax = 1 << precision;
	qmin = -qmax;
	qmax--;

	/* calc cmax = max( |lp_coeff[i]| ) */
	cmax = 0.0;
	for(i = 0; i < order; i++) {
		const FLAC__double d = fabs(lp_coeff[i]);
		if(d > cmax)
			cmax = d;
	}

	if(cmax <= 0.0) {
		/* => coefficients are all 0, which means our constant-detect didn't work */
		return 2;
	}
	else {
		const int max_shiftlimit = (1 << (FLAC__SUBFRAME_LPC_QLP_SHIFT_LEN-1)) - 1;
		const int min_shiftlimit = -max_shiftlimit - 1;
		int log2cmax;

		(void)frexp(cmax, &log2cmax);
		log2cmax--;
		*shift = (int)precision - log2cmax - 1;

		if(*shift > max_shiftlimit)
			*shift = max_shiftlimit;
		else if(*shift < min_shiftlimit)
			return 1;
	}

	if(*shift >= 0) {
		FLAC__double error = 0.0;
		FLAC__int32 q;
		for(i = 0; i < order; i++) {
			error += lp_coeff[i] * (1 << *shift);
#if 1 /* unfortunately lround() is C99 */
			if(error >= 0.0)
				q = (FLAC__int32)(error + 0.5);
			else
				q = (FLAC__int32)(error - 0.5);
#else
			q = lround(error);
#endif
#ifdef FLAC__OVERFLOW_DETECT
			if(q > qmax+1) /* we expect q==qmax+1 occasionally due to rounding */
				fprintf(stderr,"FLAC__lpc_quantize_coefficients: quantizer overflow: q>qmax %d>%d shift=%d cmax=%f precision=%u lpc[%u]=%f\n",q,qmax,*shift,cmax,precision+1,i,lp_coeff[i]);
			else if(q < qmin)
				fprintf(stderr,"FLAC__lpc_quantize_coefficients: quantizer overflow: q<qmin %d<%d shift=%d cmax=%f precision=%u lpc[%u]=%f\n",q,qmin,*shift,cmax,precision+1,i,lp_coeff[i]);
#endif
			if(q > qmax)
				q = qmax;
			else if(q < qmin)
				q = qmin;
			error -= q;
			qlp_coeff[i] = q;
		}
	}
	/* negative shift is very rare but due to design flaw, negative shift is
	 * a NOP in the decoder, so it must be handled specially by scaling down
	 * coeffs
	 */
	else {
		const int nshift = -(*shift);
		FLAC__double error = 0.0;
		FLAC__int32 q;
#ifdef DEBUG
		fprintf(stderr,"FLAC__lpc_quantize_coefficients: negative shift=%d order=%u cmax=%f\n", *shift, order, cmax);
#endif
		for(i = 0; i < order; i++) {
			error += lp_coeff[i] / (1 << nshift);
#if 1 /* unfortunately lround() is C99 */
			if(error >= 0.0)
				q = (FLAC__int32)(error + 0.5);
			else
				q = (FLAC__int32)(error - 0.5);
#else
			q = lround(error);
#endif
#ifdef FLAC__OVERFLOW_DETECT
			if(q > qmax+1) /* we expect q==qmax+1 occasionally due to rounding */
				fprintf(stderr,"FLAC__lpc_quantize_coefficients: quantizer overflow: q>qmax %d>%d shift=%d cmax=%f precision=%u lpc[%u]=%f\n",q,qmax,*shift,cmax,precision+1,i,lp_coeff[i]);
			else if(q < qmin)
				fprintf(stderr,"FLAC__lpc_quantize_coefficients: quantizer overflow: q<qmin %d<%d shift=%d cmax=%f precision=%u lpc[%u]=%f\n",q,qmin,*shift,cmax,precision+1,i,lp_coeff[i]);
#endif
			if(q > qmax)
				q = qmax;
			else if(q < qmin)
				q = qmin;
			error -= q;
			qlp_coeff[i] = q;
		}
		*shift = 0;
	}

	return 0;
}

void FLAC__lpc_compute_residual_from_qlp_coefficients(const FLAC__int32 *data, unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 residual[])
#if defined(FLAC__OVERFLOW_DETECT) || !defined(FLAC__LPC_UNROLLED_FILTER_LOOPS)
{
	FLAC__int64 sumo;
	unsigned i, j;
	FLAC__int32 sum;
	const FLAC__int32 *history;

#ifdef FLAC__OVERFLOW_DETECT_VERBOSE
	fprintf(stderr,"FLAC__lpc_compute_residual_from_qlp_coefficients: data_len=%d, order=%u, lpq=%d",data_len,order,lp_quantization);
	for(i=0;i<order;i++)
		fprintf(stderr,", q[%u]=%d",i,qlp_coeff[i]);
	fprintf(stderr,"\n");
#endif
	FLAC__ASSERT(order > 0);

	for(i = 0; i < data_len; i++) {
		sumo = 0;
		sum = 0;
		history = data;
		for(j = 0; j < order; j++) {
			sum += qlp_coeff[j] * (*(--history));
			sumo += (FLAC__int64)qlp_coeff[j] * (FLAC__int64)(*history);
#if defined _MSC_VER
			if(sumo > 2147483647I64 || sumo < -2147483648I64)
				fprintf(stderr,"FLAC__lpc_compute_residual_from_qlp_coefficients: OVERFLOW, i=%u, j=%u, c=%d, d=%d, sumo=%I64d\n",i,j,qlp_coeff[j],*history,sumo);
#else
			if(sumo > 2147483647ll || sumo < -2147483648ll)
				fprintf(stderr,"FLAC__lpc_compute_residual_from_qlp_coefficients: OVERFLOW, i=%u, j=%u, c=%d, d=%d, sumo=%lld\n",i,j,qlp_coeff[j],*history,(long long)sumo);
#endif
		}
		*(residual++) = *(data++) - (sum >> lp_quantization);
	}

	/* Here's a slower but clearer version:
	for(i = 0; i < data_len; i++) {
		sum = 0;
		for(j = 0; j < order; j++)
			sum += qlp_coeff[j] * data[i-j-1];
		residual[i] = data[i] - (sum >> lp_quantization);
	}
	*/
}
#else /* fully unrolled version for normal use */
{
	int i;
	FLAC__int32 sum;

	FLAC__ASSERT(order > 0);
	FLAC__ASSERT(order <= 32);

	/*
	 * We do unique versions up to 12th order since that's the subset limit.
	 * Also they are roughly ordered to match frequency of occurrence to
	 * minimize branching.
	 */
	if(order <= 12) {
		if(order > 8) {
			if(order > 10) {
				if(order == 12) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[11] * data[i-12];
						sum += qlp_coeff[10] * data[i-11];
						sum += qlp_coeff[9] * data[i-10];
						sum += qlp_coeff[8] * data[i-9];
						sum += qlp_coeff[7] * data[i-8];
						sum += qlp_coeff[6] * data[i-7];
						sum += qlp_coeff[5] * data[i-6];
						sum += qlp_coeff[4] * data[i-5];
						sum += qlp_coeff[3] * data[i-4];
						sum += qlp_coeff[2] * data[i-3];
						sum += qlp_coeff[1] * data[i-2];
						sum += qlp_coeff[0] * data[i-1];
						residual[i] = data[i] - (sum >> lp_quantization);
					}
				}
				else { /* order == 11 */
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[10] * data[i-11];
						sum += qlp_coeff[9] * data[i-10];
						sum += qlp_coeff[8] * data[i-9];
						sum += qlp_coeff[7] * data[i-8];
						sum += qlp_coeff[6] * data[i-7];
						sum += qlp_coeff[5] * data[i-6];
						sum += qlp_coeff[4] * data[i-5];
						sum += qlp_coeff[3] * data[i-4];
						sum += qlp_coeff[2] * data[i-3];
						sum += qlp_coeff[1] * data[i-2];
						sum += qlp_coeff[0] * data[i-1];
						residual[i] = data[i] - (sum >> lp_quantization);
					}
				}
			}
			else {
				if(order == 10) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[9] * data[i-10];
						sum += qlp_coeff[8] * data[i-9];
						sum += qlp_coeff[7] * data[i-8];
						sum += qlp_coeff[6] * data[i-7];
						sum += qlp_coeff[5] * data[i-6];
						sum += qlp_coeff[4] * data[i-5];
						sum += qlp_coeff[3] * data[i-4];
						sum += qlp_coeff[2] * data[i-3];
						sum += qlp_coeff[1] * data[i-2];
						sum += qlp_coeff[0] * data[i-1];
						residual[i] = data[i] - (sum >> lp_quantization);
					}
				}
				else { /* order == 9 */
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[8] * data[i-9];
						sum += qlp_coeff[7] * data[i-8];
						sum += qlp_coeff[6] * data[i-7];
						sum += qlp_coeff[5] * data[i-6];
						sum += qlp_coeff[4] * data[i-5];
						sum += qlp_coeff[3] * data[i-4];
						sum += qlp_coeff[2] * data[i-3];
						sum += qlp_coeff[1] * data[i-2];
						sum += qlp_coeff[0] * data[i-1];
						residual[i] = data[i] - (sum >> lp_quantization);
					}
				}
			}
		}
		else if(order > 4) {
			if(order > 6) {
				if(order == 8) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[7] * data[i-8];
						sum += qlp_coeff[6] * data[i-7];
						sum += qlp_coeff[5] * data[i-6];
						sum += qlp_coeff[4] * data[i-5];
						sum += qlp_coeff[3] * data[i-4];
						sum += qlp_coeff[2] * data[i-3];
						sum += qlp_coeff[1] * data[i-2];
						sum += qlp_coeff[0] * data[i-1];
						residual[i] = data[i] - (sum >> lp_quantization);
					}
				}
				else { /* order == 7 */
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[6] * data[i-7];
						sum += qlp_coeff[5] * data[i-6];
						sum += qlp_coeff[4] * data[i-5];
						sum += qlp_coeff[3] * data[i-4];
						sum += qlp_coeff[2] * data[i-3];
						sum += qlp_coeff[1] * data[i-2];
						sum += qlp_coeff[0] * data[i-1];
						residual[i] = data[i] - (sum >> lp_quantization);
					}
				}
			}
			else {
				if(order == 6) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[5] * data[i-6];
						sum += qlp_coeff[4] * data[i-5];
						sum += qlp_coeff[3] * data[i-4];
						sum += qlp_coeff[2] * data[i-3];
						sum += qlp_coeff[1] * data[i-2];
						sum += qlp_coeff[0] * data[i-1];
						residual[i] = data[i] - (sum >> lp_quantization);
					}
				}
				else { /* order == 5 */
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[4] * data[i-5];
						sum += qlp_coeff[3] * data[i-4];
						sum += qlp_coeff[2] * data[i-3];
						sum += qlp_coeff[1] * data[i-2];
						sum += qlp_coeff[0] * data[i-1];
						residual[i] = data[i] - (sum >> lp_quantization);
					}
				}
			}
		}
		else {
			if(order > 2) {
				if(order == 4) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[3] * data[i-4];
						sum += qlp_coeff[2] * data[i-3];
						sum += qlp_coeff[1] * data[i-2];
						sum += qlp_coeff[0] * data[i-1];
						residual[i] = data[i] - (sum >> lp_quantization);
					}
				}
				else { /* order == 3 */
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[2] * data[i-3];
						sum += qlp_coeff[1] * data[i-2];
						sum += qlp_coeff[0] * data[i-1];
						residual[i] = data[i] - (sum >> lp_quantization);
					}
				}
			}
			else {
				if(order == 2) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[1] * data[i-2];
						sum += qlp_coeff[0] * data[i-1];
						residual[i] = data[i] - (sum >> lp_quantization);
					}
				}
				else { /* order == 1 */
					for(i = 0; i < (int)data_len; i++)
						residual[i] = data[i] - ((qlp_coeff[0] * data[i-1]) >> lp_quantization);
				}
			}
		}
	}
	else { /* order > 12 */
		for(i = 0; i < (int)data_len; i++) {
			sum = 0;
			switch(order) {
				case 32: sum += qlp_coeff[31] * data[i-32];
				case 31: sum += qlp_coeff[30] * data[i-31];
				case 30: sum += qlp_coeff[29] * data[i-30];
				case 29: sum += qlp_coeff[28] * data[i-29];
				case 28: sum += qlp_coeff[27] * data[i-28];
				case 27: sum += qlp_coeff[26] * data[i-27];
				case 26: sum += qlp_coeff[25] * data[i-26];
				case 25: sum += qlp_coeff[24] * data[i-25];
				case 24: sum += qlp_coeff[23] * data[i-24];
				case 23: sum += qlp_coeff[22] * data[i-23];
				case 22: sum += qlp_coeff[21] * data[i-22];
				case 21: sum += qlp_coeff[20] * data[i-21];
				case 20: sum += qlp_coeff[19] * data[i-20];
				case 19: sum += qlp_coeff[18] * data[i-19];
				case 18: sum += qlp_coeff[17] * data[i-18];
				case 17: sum += qlp_coeff[16] * data[i-17];
				case 16: sum += qlp_coeff[15] * data[i-16];
				case 15: sum += qlp_coeff[14] * data[i-15];
				case 14: sum += qlp_coeff[13] * data[i-14];
				case 13: sum += qlp_coeff[12] * data[i-13];
				         sum += qlp_coeff[11] * data[i-12];
				         sum += qlp_coeff[10] * data[i-11];
				         sum += qlp_coeff[ 9] * data[i-10];
				         sum += qlp_coeff[ 8] * data[i- 9];
				         sum += qlp_coeff[ 7] * data[i- 8];
				         sum += qlp_coeff[ 6] * data[i- 7];
				         sum += qlp_coeff[ 5] * data[i- 6];
				         sum += qlp_coeff[ 4] * data[i- 5];
				         sum += qlp_coeff[ 3] * data[i- 4];
				         sum += qlp_coeff[ 2] * data[i- 3];
				         sum += qlp_coeff[ 1] * data[i- 2];
				         sum += qlp_coeff[ 0] * data[i- 1];
			}
			residual[i] = data[i] - (sum >> lp_quantization);
		}
	}
}
#endif

void FLAC__lpc_compute_residual_from_qlp_coefficients_wide(const FLAC__int32 *data, unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 residual[])
#if defined(FLAC__OVERFLOW_DETECT) || !defined(FLAC__LPC_UNROLLED_FILTER_LOOPS)
{
	unsigned i, j;
	FLAC__int64 sum;
	const FLAC__int32 *history;

#ifdef FLAC__OVERFLOW_DETECT_VERBOSE
	fprintf(stderr,"FLAC__lpc_compute_residual_from_qlp_coefficients_wide: data_len=%d, order=%u, lpq=%d",data_len,order,lp_quantization);
	for(i=0;i<order;i++)
		fprintf(stderr,", q[%u]=%d",i,qlp_coeff[i]);
	fprintf(stderr,"\n");
#endif
	FLAC__ASSERT(order > 0);

	for(i = 0; i < data_len; i++) {
		sum = 0;
		history = data;
		for(j = 0; j < order; j++)
			sum += (FLAC__int64)qlp_coeff[j] * (FLAC__int64)(*(--history));
		if(FLAC__bitmath_silog2_wide(sum >> lp_quantization) > 32) {
#if defined _MSC_VER
			fprintf(stderr,"FLAC__lpc_compute_residual_from_qlp_coefficients_wide: OVERFLOW, i=%u, sum=%I64d\n", i, sum >> lp_quantization);
#else
			fprintf(stderr,"FLAC__lpc_compute_residual_from_qlp_coefficients_wide: OVERFLOW, i=%u, sum=%lld\n", i, (long long)(sum >> lp_quantization));
#endif
			break;
		}
		if(FLAC__bitmath_silog2_wide((FLAC__int64)(*data) - (sum >> lp_quantization)) > 32) {
#if defined _MSC_VER
			fprintf(stderr,"FLAC__lpc_compute_residual_from_qlp_coefficients_wide: OVERFLOW, i=%u, data=%d, sum=%I64d, residual=%I64d\n", i, *data, sum >> lp_quantization, (FLAC__int64)(*data) - (sum >> lp_quantization));
#else
			fprintf(stderr,"FLAC__lpc_compute_residual_from_qlp_coefficients_wide: OVERFLOW, i=%u, data=%d, sum=%lld, residual=%lld\n", i, *data, (long long)(sum >> lp_quantization), (long long)((FLAC__int64)(*data) - (sum >> lp_quantization)));
#endif
			break;
		}
		*(residual++) = *(data++) - (FLAC__int32)(sum >> lp_quantization);
	}
}
#else /* fully unrolled version for normal use */
{
	int i;
	FLAC__int64 sum;

	FLAC__ASSERT(order > 0);
	FLAC__ASSERT(order <= 32);

	/*
	 * We do unique versions up to 12th order since that's the subset limit.
	 * Also they are roughly ordered to match frequency of occurrence to
	 * minimize branching.
	 */
	if(order <= 12) {
		if(order > 8) {
			if(order > 10) {
				if(order == 12) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[11] * (FLAC__int64)data[i-12];
						sum += qlp_coeff[10] * (FLAC__int64)data[i-11];
						sum += qlp_coeff[9] * (FLAC__int64)data[i-10];
						sum += qlp_coeff[8] * (FLAC__int64)data[i-9];
						sum += qlp_coeff[7] * (FLAC__int64)data[i-8];
						sum += qlp_coeff[6] * (FLAC__int64)data[i-7];
						sum += qlp_coeff[5] * (FLAC__int64)data[i-6];
						sum += qlp_coeff[4] * (FLAC__int64)data[i-5];
						sum += qlp_coeff[3] * (FLAC__int64)data[i-4];
						sum += qlp_coeff[2] * (FLAC__int64)data[i-3];
						sum += qlp_coeff[1] * (FLAC__int64)data[i-2];
						sum += qlp_coeff[0] * (FLAC__int64)data[i-1];
						residual[i] = data[i] - (FLAC__int32)(sum >> lp_quantization);
					}
				}
				else { /* order == 11 */
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[10] * (FLAC__int64)data[i-11];
						sum += qlp_coeff[9] * (FLAC__int64)data[i-10];
						sum += qlp_coeff[8] * (FLAC__int64)data[i-9];
						sum += qlp_coeff[7] * (FLAC__int64)data[i-8];
						sum += qlp_coeff[6] * (FLAC__int64)data[i-7];
						sum += qlp_coeff[5] * (FLAC__int64)data[i-6];
						sum += qlp_coeff[4] * (FLAC__int64)data[i-5];
						sum += qlp_coeff[3] * (FLAC__int64)data[i-4];
						sum += qlp_coeff[2] * (FLAC__int64)data[i-3];
						sum += qlp_coeff[1] * (FLAC__int64)data[i-2];
						sum += qlp_coeff[0] * (FLAC__int64)data[i-1];
						residual[i] = data[i] - (FLAC__int32)(sum >> lp_quantization);
					}
				}
			}
			else {
				if(order == 10) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[9] * (FLAC__int64)data[i-10];
						sum += qlp_coeff[8] * (FLAC__int64)data[i-9];
						sum += qlp_coeff[7] * (FLAC__int64)data[i-8];
						sum += qlp_coeff[6] * (FLAC__int64)data[i-7];
						sum += qlp_coeff[5] * (FLAC__int64)data[i-6];
						sum += qlp_coeff[4] * (FLAC__int64)data[i-5];
						sum += qlp_coeff[3] * (FLAC__int64)data[i-4];
						sum += qlp_coeff[2] * (FLAC__int64)data[i-3];
						sum += qlp_coeff[1] * (FLAC__int64)data[i-2];
						sum += qlp_coeff[0] * (FLAC__int64)data[i-1];
						residual[i] = data[i] - (FLAC__int32)(sum >> lp_quantization);
					}
				}
				else { /* order == 9 */
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[8] * (FLAC__int64)data[i-9];
						sum += qlp_coeff[7] * (FLAC__int64)data[i-8];
						sum += qlp_coeff[6] * (FLAC__int64)data[i-7];
						sum += qlp_coeff[5] * (FLAC__int64)data[i-6];
						sum += qlp_coeff[4] * (FLAC__int64)data[i-5];
						sum += qlp_coeff[3] * (FLAC__int64)data[i-4];
						sum += qlp_coeff[2] * (FLAC__int64)data[i-3];
						sum += qlp_coeff[1] * (FLAC__int64)data[i-2];
						sum += qlp_coeff[0] * (FLAC__int64)data[i-1];
						residual[i] = data[i] - (FLAC__int32)(sum >> lp_quantization);
					}
				}
			}
		}
		else if(order > 4) {
			if(order > 6) {
				if(order == 8) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[7] * (FLAC__int64)data[i-8];
						sum += qlp_coeff[6] * (FLAC__int64)data[i-7];
						sum += qlp_coeff[5] * (FLAC__int64)data[i-6];
						sum += qlp_coeff[4] * (FLAC__int64)data[i-5];
						sum += qlp_coeff[3] * (FLAC__int64)data[i-4];
						sum += qlp_coeff[2] * (FLAC__int64)data[i-3];
						sum += qlp_coeff[1] * (FLAC__int64)data[i-2];
						sum += qlp_coeff[0] * (FLAC__int64)data[i-1];
						residual[i] = data[i] - (FLAC__int32)(sum >> lp_quantization);
					}
				}
				else { /* order == 7 */
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[6] * (FLAC__int64)data[i-7];
						sum += qlp_coeff[5] * (FLAC__int64)data[i-6];
						sum += qlp_coeff[4] * (FLAC__int64)data[i-5];
						sum += qlp_coeff[3] * (FLAC__int64)data[i-4];
						sum += qlp_coeff[2] * (FLAC__int64)data[i-3];
						sum += qlp_coeff[1] * (FLAC__int64)data[i-2];
						sum += qlp_coeff[0] * (FLAC__int64)data[i-1];
						residual[i] = data[i] - (FLAC__int32)(sum >> lp_quantization);
					}
				}
			}
			else {
				if(order == 6) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[5] * (FLAC__int64)data[i-6];
						sum += qlp_coeff[4] * (FLAC__int64)data[i-5];
						sum += qlp_coeff[3] * (FLAC__int64)data[i-4];
						sum += qlp_coeff[2] * (FLAC__int64)data[i-3];
						sum += qlp_coeff[1] * (FLAC__int64)data[i-2];
						sum += qlp_coeff[0] * (FLAC__int64)data[i-1];
						residual[i] = data[i] - (FLAC__int32)(sum >> lp_quantization);
					}
				}
				else { /* order == 5 */
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[4] * (FLAC__int64)data[i-5];
						sum += qlp_coeff[3] * (FLAC__int64)data[i-4];
						sum += qlp_coeff[2] * (FLAC__int64)data[i-3];
						sum += qlp_coeff[1] * (FLAC__int64)data[i-2];
						sum += qlp_coeff[0] * (FLAC__int64)data[i-1];
						residual[i] = data[i] - (FLAC__int32)(sum >> lp_quantization);
					}
				}
			}
		}
		else {
			if(order > 2) {
				if(order == 4) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[3] * (FLAC__int64)data[i-4];
						sum += qlp_coeff[2] * (FLAC__int64)data[i-3];
						sum += qlp_coeff[1] * (FLAC__int64)data[i-2];
						sum += qlp_coeff[0] * (FLAC__int64)data[i-1];
						residual[i] = data[i] - (FLAC__int32)(sum >> lp_quantization);
					}
				}
				else { /* order == 3 */
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[2] * (FLAC__int64)data[i-3];
						sum += qlp_coeff[1] * (FLAC__int64)data[i-2];
						sum += qlp_coeff[0] * (FLAC__int64)data[i-1];
						residual[i] = data[i] - (FLAC__int32)(sum >> lp_quantization);
					}
				}
			}
			else {
				if(order == 2) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[1] * (FLAC__int64)data[i-2];
						sum += qlp_coeff[0] * (FLAC__int64)data[i-1];
						residual[i] = data[i] - (FLAC__int32)(sum >> lp_quantization);
					}
				}
				else { /* order == 1 */
					for(i = 0; i < (int)data_len; i++)
						residual[i] = data[i] - (FLAC__int32)((qlp_coeff[0] * (FLAC__int64)data[i-1]) >> lp_quantization);
				}
			}
		}
	}
	else { /* order > 12 */
		for(i = 0; i < (int)data_len; i++) {
			sum = 0;
			switch(order) {
				case 32: sum += qlp_coeff[31] * (FLAC__int64)data[i-32];
				case 31: sum += qlp_coeff[30] * (FLAC__int64)data[i-31];
				case 30: sum += qlp_coeff[29] * (FLAC__int64)data[i-30];
				case 29: sum += qlp_coeff[28] * (FLAC__int64)data[i-29];
				case 28: sum += qlp_coeff[27] * (FLAC__int64)data[i-28];
				case 27: sum += qlp_coeff[26] * (FLAC__int64)data[i-27];
				case 26: sum += qlp_coeff[25] * (FLAC__int64)data[i-26];
				case 25: sum += qlp_coeff[24] * (FLAC__int64)data[i-25];
				case 24: sum += qlp_coeff[23] * (FLAC__int64)data[i-24];
				case 23: sum += qlp_coeff[22] * (FLAC__int64)data[i-23];
				case 22: sum += qlp_coeff[21] * (FLAC__int64)data[i-22];
				case 21: sum += qlp_coeff[20] * (FLAC__int64)data[i-21];
				case 20: sum += qlp_coeff[19] * (FLAC__int64)data[i-20];
				case 19: sum += qlp_coeff[18] * (FLAC__int64)data[i-19];
				case 18: sum += qlp_coeff[17] * (FLAC__int64)data[i-18];
				case 17: sum += qlp_coeff[16] * (FLAC__int64)data[i-17];
				case 16: sum += qlp_coeff[15] * (FLAC__int64)data[i-16];
				case 15: sum += qlp_coeff[14] * (FLAC__int64)data[i-15];
				case 14: sum += qlp_coeff[13] * (FLAC__int64)data[i-14];
				case 13: sum += qlp_coeff[12] * (FLAC__int64)data[i-13];
				         sum += qlp_coeff[11] * (FLAC__int64)data[i-12];
				         sum += qlp_coeff[10] * (FLAC__int64)data[i-11];
				         sum += qlp_coeff[ 9] * (FLAC__int64)data[i-10];
				         sum += qlp_coeff[ 8] * (FLAC__int64)data[i- 9];
				         sum += qlp_coeff[ 7] * (FLAC__int64)data[i- 8];
				         sum += qlp_coeff[ 6] * (FLAC__int64)data[i- 7];
				         sum += qlp_coeff[ 5] * (FLAC__int64)data[i- 6];
				         sum += qlp_coeff[ 4] * (FLAC__int64)data[i- 5];
				         sum += qlp_coeff[ 3] * (FLAC__int64)data[i- 4];
				         sum += qlp_coeff[ 2] * (FLAC__int64)data[i- 3];
				         sum += qlp_coeff[ 1] * (FLAC__int64)data[i- 2];
				         sum += qlp_coeff[ 0] * (FLAC__int64)data[i- 1];
			}
			residual[i] = data[i] - (FLAC__int32)(sum >> lp_quantization);
		}
	}
}
#endif

#endif /* !defined FLAC__INTEGER_ONLY_LIBRARY */

void FLAC__lpc_restore_signal(const FLAC__int32 residual[], unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 data[])
#if defined(FLAC__OVERFLOW_DETECT) || !defined(FLAC__LPC_UNROLLED_FILTER_LOOPS)
{
	FLAC__int64 sumo;
	unsigned i, j;
	FLAC__int32 sum;
	const FLAC__int32 *r = residual, *history;

#ifdef FLAC__OVERFLOW_DETECT_VERBOSE
	fprintf(stderr,"FLAC__lpc_restore_signal: data_len=%d, order=%u, lpq=%d",data_len,order,lp_quantization);
	for(i=0;i<order;i++)
		fprintf(stderr,", q[%u]=%d",i,qlp_coeff[i]);
	fprintf(stderr,"\n");
#endif
	FLAC__ASSERT(order > 0);

	for(i = 0; i < data_len; i++) {
		sumo = 0;
		sum = 0;
		history = data;
		for(j = 0; j < order; j++) {
			sum += qlp_coeff[j] * (*(--history));
			sumo += (FLAC__int64)qlp_coeff[j] * (FLAC__int64)(*history);
#if defined _MSC_VER
			if(sumo > 2147483647I64 || sumo < -2147483648I64)
				fprintf(stderr,"FLAC__lpc_restore_signal: OVERFLOW, i=%u, j=%u, c=%d, d=%d, sumo=%I64d\n",i,j,qlp_coeff[j],*history,sumo);
#else
			if(sumo > 2147483647ll || sumo < -2147483648ll)
				fprintf(stderr,"FLAC__lpc_restore_signal: OVERFLOW, i=%u, j=%u, c=%d, d=%d, sumo=%lld\n",i,j,qlp_coeff[j],*history,(long long)sumo);
#endif
		}
		*(data++) = *(r++) + (sum >> lp_quantization);
	}

	/* Here's a slower but clearer version:
	for(i = 0; i < data_len; i++) {
		sum = 0;
		for(j = 0; j < order; j++)
			sum += qlp_coeff[j] * data[i-j-1];
		data[i] = residual[i] + (sum >> lp_quantization);
	}
	*/
}
#else /* fully unrolled version for normal use */
{
	int i;
	FLAC__int32 sum;

	FLAC__ASSERT(order > 0);
	FLAC__ASSERT(order <= 32);

	/*
	 * We do unique versions up to 12th order since that's the subset limit.
	 * Also they are roughly ordered to match frequency of occurrence to
	 * minimize branching.
	 */
	if(order <= 12) {
		if(order > 8) {
			if(order > 10) {
				if(order == 12) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[11] * data[i-12];
						sum += qlp_coeff[10] * data[i-11];
						sum += qlp_coeff[9] * data[i-10];
						sum += qlp_coeff[8] * data[i-9];
						sum += qlp_coeff[7] * data[i-8];
						sum += qlp_coeff[6] * data[i-7];
						sum += qlp_coeff[5] * data[i-6];
						sum += qlp_coeff[4] * data[i-5];
						sum += qlp_coeff[3] * data[i-4];
						sum += qlp_coeff[2] * data[i-3];
						sum += qlp_coeff[1] * data[i-2];
						sum += qlp_coeff[0] * data[i-1];
						data[i] = residual[i] + (sum >> lp_quantization);
					}
				}
				else { /* order == 11 */
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[10] * data[i-11];
						sum += qlp_coeff[9] * data[i-10];
						sum += qlp_coeff[8] * data[i-9];
						sum += qlp_coeff[7] * data[i-8];
						sum += qlp_coeff[6] * data[i-7];
						sum += qlp_coeff[5] * data[i-6];
						sum += qlp_coeff[4] * data[i-5];
						sum += qlp_coeff[3] * data[i-4];
						sum += qlp_coeff[2] * data[i-3];
						sum += qlp_coeff[1] * data[i-2];
						sum += qlp_coeff[0] * data[i-1];
						data[i] = residual[i] + (sum >> lp_quantization);
					}
				}
			}
			else {
				if(order == 10) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[9] * data[i-10];
						sum += qlp_coeff[8] * data[i-9];
						sum += qlp_coeff[7] * data[i-8];
						sum += qlp_coeff[6] * data[i-7];
						sum += qlp_coeff[5] * data[i-6];
						sum += qlp_coeff[4] * data[i-5];
						sum += qlp_coeff[3] * data[i-4];
						sum += qlp_coeff[2] * data[i-3];
						sum += qlp_coeff[1] * data[i-2];
						sum += qlp_coeff[0] * data[i-1];
						data[i] = residual[i] + (sum >> lp_quantization);
					}
				}
				else { /* order == 9 */
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[8] * data[i-9];
						sum += qlp_coeff[7] * data[i-8];
						sum += qlp_coeff[6] * data[i-7];
						sum += qlp_coeff[5] * data[i-6];
						sum += qlp_coeff[4] * data[i-5];
						sum += qlp_coeff[3] * data[i-4];
						sum += qlp_coeff[2] * data[i-3];
						sum += qlp_coeff[1] * data[i-2];
						sum += qlp_coeff[0] * data[i-1];
						data[i] = residual[i] + (sum >> lp_quantization);
					}
				}
			}
		}
		else if(order > 4) {
			if(order > 6) {
				if(order == 8) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[7] * data[i-8];
						sum += qlp_coeff[6] * data[i-7];
						sum += qlp_coeff[5] * data[i-6];
						sum += qlp_coeff[4] * data[i-5];
						sum += qlp_coeff[3] * data[i-4];
						sum += qlp_coeff[2] * data[i-3];
						sum += qlp_coeff[1] * data[i-2];
						sum += qlp_coeff[0] * data[i-1];
						data[i] = residual[i] + (sum >> lp_quantization);
					}
				}
				else { /* order == 7 */
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[6] * data[i-7];
						sum += qlp_coeff[5] * data[i-6];
						sum += qlp_coeff[4] * data[i-5];
						sum += qlp_coeff[3] * data[i-4];
						sum += qlp_coeff[2] * data[i-3];
						sum += qlp_coeff[1] * data[i-2];
						sum += qlp_coeff[0] * data[i-1];
						data[i] = residual[i] + (sum >> lp_quantization);
					}
				}
			}
			else {
				if(order == 6) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[5] * data[i-6];
						sum += qlp_coeff[4] * data[i-5];
						sum += qlp_coeff[3] * data[i-4];
						sum += qlp_coeff[2] * data[i-3];
						sum += qlp_coeff[1] * data[i-2];
						sum += qlp_coeff[0] * data[i-1];
						data[i] = residual[i] + (sum >> lp_quantization);
					}
				}
				else { /* order == 5 */
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[4] * data[i-5];
						sum += qlp_coeff[3] * data[i-4];
						sum += qlp_coeff[2] * data[i-3];
						sum += qlp_coeff[1] * data[i-2];
						sum += qlp_coeff[0] * data[i-1];
						data[i] = residual[i] + (sum >> lp_quantization);
					}
				}
			}
		}
		else {
			if(order > 2) {
				if(order == 4) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[3] * data[i-4];
						sum += qlp_coeff[2] * data[i-3];
						sum += qlp_coeff[1] * data[i-2];
						sum += qlp_coeff[0] * data[i-1];
						data[i] = residual[i] + (sum >> lp_quantization);
					}
				}
				else { /* order == 3 */
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[2] * data[i-3];
						sum += qlp_coeff[1] * data[i-2];
						sum += qlp_coeff[0] * data[i-1];
						data[i] = residual[i] + (sum >> lp_quantization);
					}
				}
			}
			else {
				if(order == 2) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[1] * data[i-2];
						sum += qlp_coeff[0] * data[i-1];
						data[i] = residual[i] + (sum >> lp_quantization);
					}
				}
				else { /* order == 1 */
					for(i = 0; i < (int)data_len; i++)
						data[i] = residual[i] + ((qlp_coeff[0] * data[i-1]) >> lp_quantization);
				}
			}
		}
	}
	else { /* order > 12 */
		for(i = 0; i < (int)data_len; i++) {
			sum = 0;
			switch(order) {
				case 32: sum += qlp_coeff[31] * data[i-32];
				case 31: sum += qlp_coeff[30] * data[i-31];
				case 30: sum += qlp_coeff[29] * data[i-30];
				case 29: sum += qlp_coeff[28] * data[i-29];
				case 28: sum += qlp_coeff[27] * data[i-28];
				case 27: sum += qlp_coeff[26] * data[i-27];
				case 26: sum += qlp_coeff[25] * data[i-26];
				case 25: sum += qlp_coeff[24] * data[i-25];
				case 24: sum += qlp_coeff[23] * data[i-24];
				case 23: sum += qlp_coeff[22] * data[i-23];
				case 22: sum += qlp_coeff[21] * data[i-22];
				case 21: sum += qlp_coeff[20] * data[i-21];
				case 20: sum += qlp_coeff[19] * data[i-20];
				case 19: sum += qlp_coeff[18] * data[i-19];
				case 18: sum += qlp_coeff[17] * data[i-18];
				case 17: sum += qlp_coeff[16] * data[i-17];
				case 16: sum += qlp_coeff[15] * data[i-16];
				case 15: sum += qlp_coeff[14] * data[i-15];
				case 14: sum += qlp_coeff[13] * data[i-14];
				case 13: sum += qlp_coeff[12] * data[i-13];
				         sum += qlp_coeff[11] * data[i-12];
				         sum += qlp_coeff[10] * data[i-11];
				         sum += qlp_coeff[ 9] * data[i-10];
				         sum += qlp_coeff[ 8] * data[i- 9];
				         sum += qlp_coeff[ 7] * data[i- 8];
				         sum += qlp_coeff[ 6] * data[i- 7];
				         sum += qlp_coeff[ 5] * data[i- 6];
				         sum += qlp_coeff[ 4] * data[i- 5];
				         sum += qlp_coeff[ 3] * data[i- 4];
				         sum += qlp_coeff[ 2] * data[i- 3];
				         sum += qlp_coeff[ 1] * data[i- 2];
				         sum += qlp_coeff[ 0] * data[i- 1];
			}
			data[i] = residual[i] + (sum >> lp_quantization);
		}
	}
}
#endif

void FLAC__lpc_restore_signal_wide(const FLAC__int32 residual[], unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 data[])
#if defined(FLAC__OVERFLOW_DETECT) || !defined(FLAC__LPC_UNROLLED_FILTER_LOOPS)
{
	unsigned i, j;
	FLAC__int64 sum;
	const FLAC__int32 *r = residual, *history;

#ifdef FLAC__OVERFLOW_DETECT_VERBOSE
	fprintf(stderr,"FLAC__lpc_restore_signal_wide: data_len=%d, order=%u, lpq=%d",data_len,order,lp_quantization);
	for(i=0;i<order;i++)
		fprintf(stderr,", q[%u]=%d",i,qlp_coeff[i]);
	fprintf(stderr,"\n");
#endif
	FLAC__ASSERT(order > 0);

	for(i = 0; i < data_len; i++) {
		sum = 0;
		history = data;
		for(j = 0; j < order; j++)
			sum += (FLAC__int64)qlp_coeff[j] * (FLAC__int64)(*(--history));
		if(FLAC__bitmath_silog2_wide(sum >> lp_quantization) > 32) {
#ifdef _MSC_VER
			fprintf(stderr,"FLAC__lpc_restore_signal_wide: OVERFLOW, i=%u, sum=%I64d\n", i, sum >> lp_quantization);
#else
			fprintf(stderr,"FLAC__lpc_restore_signal_wide: OVERFLOW, i=%u, sum=%lld\n", i, (long long)(sum >> lp_quantization));
#endif
			break;
		}
		if(FLAC__bitmath_silog2_wide((FLAC__int64)(*r) + (sum >> lp_quantization)) > 32) {
#ifdef _MSC_VER
			fprintf(stderr,"FLAC__lpc_restore_signal_wide: OVERFLOW, i=%u, residual=%d, sum=%I64d, data=%I64d\n", i, *r, sum >> lp_quantization, (FLAC__int64)(*r) + (sum >> lp_quantization));
#else
			fprintf(stderr,"FLAC__lpc_restore_signal_wide: OVERFLOW, i=%u, residual=%d, sum=%lld, data=%lld\n", i, *r, (long long)(sum >> lp_quantization), (long long)((FLAC__int64)(*r) + (sum >> lp_quantization)));
#endif
			break;
		}
		*(data++) = *(r++) + (FLAC__int32)(sum >> lp_quantization);
	}
}
#else /* fully unrolled version for normal use */
{
	int i;
	FLAC__int64 sum;

	FLAC__ASSERT(order > 0);
	FLAC__ASSERT(order <= 32);

	/*
	 * We do unique versions up to 12th order since that's the subset limit.
	 * Also they are roughly ordered to match frequency of occurrence to
	 * minimize branching.
	 */
	if(order <= 12) {
		if(order > 8) {
			if(order > 10) {
				if(order == 12) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[11] * (FLAC__int64)data[i-12];
						sum += qlp_coeff[10] * (FLAC__int64)data[i-11];
						sum += qlp_coeff[9] * (FLAC__int64)data[i-10];
						sum += qlp_coeff[8] * (FLAC__int64)data[i-9];
						sum += qlp_coeff[7] * (FLAC__int64)data[i-8];
						sum += qlp_coeff[6] * (FLAC__int64)data[i-7];
						sum += qlp_coeff[5] * (FLAC__int64)data[i-6];
						sum += qlp_coeff[4] * (FLAC__int64)data[i-5];
						sum += qlp_coeff[3] * (FLAC__int64)data[i-4];
						sum += qlp_coeff[2] * (FLAC__int64)data[i-3];
						sum += qlp_coeff[1] * (FLAC__int64)data[i-2];
						sum += qlp_coeff[0] * (FLAC__int64)data[i-1];
						data[i] = residual[i] + (FLAC__int32)(sum >> lp_quantization);
					}
				}
				else { /* order == 11 */
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[10] * (FLAC__int64)data[i-11];
						sum += qlp_coeff[9] * (FLAC__int64)data[i-10];
						sum += qlp_coeff[8] * (FLAC__int64)data[i-9];
						sum += qlp_coeff[7] * (FLAC__int64)data[i-8];
						sum += qlp_coeff[6] * (FLAC__int64)data[i-7];
						sum += qlp_coeff[5] * (FLAC__int64)data[i-6];
						sum += qlp_coeff[4] * (FLAC__int64)data[i-5];
						sum += qlp_coeff[3] * (FLAC__int64)data[i-4];
						sum += qlp_coeff[2] * (FLAC__int64)data[i-3];
						sum += qlp_coeff[1] * (FLAC__int64)data[i-2];
						sum += qlp_coeff[0] * (FLAC__int64)data[i-1];
						data[i] = residual[i] + (FLAC__int32)(sum >> lp_quantization);
					}
				}
			}
			else {
				if(order == 10) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[9] * (FLAC__int64)data[i-10];
						sum += qlp_coeff[8] * (FLAC__int64)data[i-9];
						sum += qlp_coeff[7] * (FLAC__int64)data[i-8];
						sum += qlp_coeff[6] * (FLAC__int64)data[i-7];
						sum += qlp_coeff[5] * (FLAC__int64)data[i-6];
						sum += qlp_coeff[4] * (FLAC__int64)data[i-5];
						sum += qlp_coeff[3] * (FLAC__int64)data[i-4];
						sum += qlp_coeff[2] * (FLAC__int64)data[i-3];
						sum += qlp_coeff[1] * (FLAC__int64)data[i-2];
						sum += qlp_coeff[0] * (FLAC__int64)data[i-1];
						data[i] = residual[i] + (FLAC__int32)(sum >> lp_quantization);
					}
				}
				else { /* order == 9 */
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[8] * (FLAC__int64)data[i-9];
						sum += qlp_coeff[7] * (FLAC__int64)data[i-8];
						sum += qlp_coeff[6] * (FLAC__int64)data[i-7];
						sum += qlp_coeff[5] * (FLAC__int64)data[i-6];
						sum += qlp_coeff[4] * (FLAC__int64)data[i-5];
						sum += qlp_coeff[3] * (FLAC__int64)data[i-4];
						sum += qlp_coeff[2] * (FLAC__int64)data[i-3];
						sum += qlp_coeff[1] * (FLAC__int64)data[i-2];
						sum += qlp_coeff[0] * (FLAC__int64)data[i-1];
						data[i] = residual[i] + (FLAC__int32)(sum >> lp_quantization);
					}
				}
			}
		}
		else if(order > 4) {
			if(order > 6) {
				if(order == 8) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[7] * (FLAC__int64)data[i-8];
						sum += qlp_coeff[6] * (FLAC__int64)data[i-7];
						sum += qlp_coeff[5] * (FLAC__int64)data[i-6];
						sum += qlp_coeff[4] * (FLAC__int64)data[i-5];
						sum += qlp_coeff[3] * (FLAC__int64)data[i-4];
						sum += qlp_coeff[2] * (FLAC__int64)data[i-3];
						sum += qlp_coeff[1] * (FLAC__int64)data[i-2];
						sum += qlp_coeff[0] * (FLAC__int64)data[i-1];
						data[i] = residual[i] + (FLAC__int32)(sum >> lp_quantization);
					}
				}
				else { /* order == 7 */
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[6] * (FLAC__int64)data[i-7];
						sum += qlp_coeff[5] * (FLAC__int64)data[i-6];
						sum += qlp_coeff[4] * (FLAC__int64)data[i-5];
						sum += qlp_coeff[3] * (FLAC__int64)data[i-4];
						sum += qlp_coeff[2] * (FLAC__int64)data[i-3];
						sum += qlp_coeff[1] * (FLAC__int64)data[i-2];
						sum += qlp_coeff[0] * (FLAC__int64)data[i-1];
						data[i] = residual[i] + (FLAC__int32)(sum >> lp_quantization);
					}
				}
			}
			else {
				if(order == 6) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[5] * (FLAC__int64)data[i-6];
						sum += qlp_coeff[4] * (FLAC__int64)data[i-5];
						sum += qlp_coeff[3] * (FLAC__int64)data[i-4];
						sum += qlp_coeff[2] * (FLAC__int64)data[i-3];
						sum += qlp_coeff[1] * (FLAC__int64)data[i-2];
						sum += qlp_coeff[0] * (FLAC__int64)data[i-1];
						data[i] = residual[i] + (FLAC__int32)(sum >> lp_quantization);
					}
				}
				else { /* order == 5 */
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[4] * (FLAC__int64)data[i-5];
						sum += qlp_coeff[3] * (FLAC__int64)data[i-4];
						sum += qlp_coeff[2] * (FLAC__int64)data[i-3];
						sum += qlp_coeff[1] * (FLAC__int64)data[i-2];
						sum += qlp_coeff[0] * (FLAC__int64)data[i-1];
						data[i] = residual[i] + (FLAC__int32)(sum >> lp_quantization);
					}
				}
			}
		}
		else {
			if(order > 2) {
				if(order == 4) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[3] * (FLAC__int64)data[i-4];
						sum += qlp_coeff[2] * (FLAC__int64)data[i-3];
						sum += qlp_coeff[1] * (FLAC__int64)data[i-2];
						sum += qlp_coeff[0] * (FLAC__int64)data[i-1];
						data[i] = residual[i] + (FLAC__int32)(sum >> lp_quantization);
					}
				}
				else { /* order == 3 */
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[2] * (FLAC__int64)data[i-3];
						sum += qlp_coeff[1] * (FLAC__int64)data[i-2];
						sum += qlp_coeff[0] * (FLAC__int64)data[i-1];
						data[i] = residual[i] + (FLAC__int32)(sum >> lp_quantization);
					}
				}
			}
			else {
				if(order == 2) {
					for(i = 0; i < (int)data_len; i++) {
						sum = 0;
						sum += qlp_coeff[1] * (FLAC__int64)data[i-2];
						sum += qlp_coeff[0] * (FLAC__int64)data[i-1];
						data[i] = residual[i] + (FLAC__int32)(sum >> lp_quantization);
					}
				}
				else { /* order == 1 */
					for(i = 0; i < (int)data_len; i++)
						data[i] = residual[i] + (FLAC__int32)((qlp_coeff[0] * (FLAC__int64)data[i-1]) >> lp_quantization);
				}
			}
		}
	}
	else { /* order > 12 */
		for(i = 0; i < (int)data_len; i++) {
			sum = 0;
			switch(order) {
				case 32: sum += qlp_coeff[31] * (FLAC__int64)data[i-32];
				case 31: sum += qlp_coeff[30] * (FLAC__int64)data[i-31];
				case 30: sum += qlp_coeff[29] * (FLAC__int64)data[i-30];
				case 29: sum += qlp_coeff[28] * (FLAC__int64)data[i-29];
				case 28: sum += qlp_coeff[27] * (FLAC__int64)data[i-28];
				case 27: sum += qlp_coeff[26] * (FLAC__int64)data[i-27];
				case 26: sum += qlp_coeff[25] * (FLAC__int64)data[i-26];
				case 25: sum += qlp_coeff[24] * (FLAC__int64)data[i-25];
				case 24: sum += qlp_coeff[23] * (FLAC__int64)data[i-24];
				case 23: sum += qlp_coeff[22] * (FLAC__int64)data[i-23];
				case 22: sum += qlp_coeff[21] * (FLAC__int64)data[i-22];
				case 21: sum += qlp_coeff[20] * (FLAC__int64)data[i-21];
				case 20: sum += qlp_coeff[19] * (FLAC__int64)data[i-20];
				case 19: sum += qlp_coeff[18] * (FLAC__int64)data[i-19];
				case 18: sum += qlp_coeff[17] * (FLAC__int64)data[i-18];
				case 17: sum += qlp_coeff[16] * (FLAC__int64)data[i-17];
				case 16: sum += qlp_coeff[15] * (FLAC__int64)data[i-16];
				case 15: sum += qlp_coeff[14] * (FLAC__int64)data[i-15];
				case 14: sum += qlp_coeff[13] * (FLAC__int64)data[i-14];
				case 13: sum += qlp_coeff[12] * (FLAC__int64)data[i-13];
				         sum += qlp_coeff[11] * (FLAC__int64)data[i-12];
				         sum += qlp_coeff[10] * (FLAC__int64)data[i-11];
				         sum += qlp_coeff[ 9] * (FLAC__int64)data[i-10];
				         sum += qlp_coeff[ 8] * (FLAC__int64)data[i- 9];
				         sum += qlp_coeff[ 7] * (FLAC__int64)data[i- 8];
				         sum += qlp_coeff[ 6] * (FLAC__int64)data[i- 7];
				         sum += qlp_coeff[ 5] * (FLAC__int64)data[i- 6];
				         sum += qlp_coeff[ 4] * (FLAC__int64)data[i- 5];
				         sum += qlp_coeff[ 3] * (FLAC__int64)data[i- 4];
				         sum += qlp_coeff[ 2] * (FLAC__int64)data[i- 3];
				         sum += qlp_coeff[ 1] * (FLAC__int64)data[i- 2];
				         sum += qlp_coeff[ 0] * (FLAC__int64)data[i- 1];
			}
			data[i] = residual[i] + (FLAC__int32)(sum >> lp_quantization);
		}
	}
}
#endif

#ifndef FLAC__INTEGER_ONLY_LIBRARY

FLAC__double FLAC__lpc_compute_expected_bits_per_residual_sample(FLAC__double lpc_error, unsigned total_samples)
{
	FLAC__double error_scale;

	FLAC__ASSERT(total_samples > 0);

	error_scale = 0.5 * M_LN2 * M_LN2 / (FLAC__double)total_samples;

	return FLAC__lpc_compute_expected_bits_per_residual_sample_with_error_scale(lpc_error, error_scale);
}

FLAC__double FLAC__lpc_compute_expected_bits_per_residual_sample_with_error_scale(FLAC__double lpc_error, FLAC__double error_scale)
{
	if(lpc_error > 0.0) {
		FLAC__double bps = (FLAC__double)0.5 * log(error_scale * lpc_error) / M_LN2;
		if(bps >= 0.0)
			return bps;
		else
			return 0.0;
	}
	else if(lpc_error < 0.0) { /* error should not be negative but can happen due to inadequate floating-point resolution */
		return 1e32;
	}
	else {
		return 0.0;
	}
}

unsigned FLAC__lpc_compute_best_order(const FLAC__double lpc_error[], unsigned max_order, unsigned total_samples, unsigned overhead_bits_per_order)
{
	unsigned order, index, best_index; /* 'index' the index into lpc_error; index==order-1 since lpc_error[0] is for order==1, lpc_error[1] is for order==2, etc */
	FLAC__double bits, best_bits, error_scale;

	FLAC__ASSERT(max_order > 0);
	FLAC__ASSERT(total_samples > 0);

	error_scale = 0.5 * M_LN2 * M_LN2 / (FLAC__double)total_samples;

	best_index = 0;
	best_bits = (unsigned)(-1);

	for(index = 0, order = 1; index < max_order; index++, order++) {
		bits = FLAC__lpc_compute_expected_bits_per_residual_sample_with_error_scale(lpc_error[index], error_scale) * (FLAC__double)(total_samples - order) + (FLAC__double)(order * overhead_bits_per_order);
		if(bits < best_bits) {
			best_index = index;
			best_bits = bits;
		}
	}

	return best_index+1; /* +1 since index of lpc_error[] is order-1 */
}

#endif /* !defined FLAC__INTEGER_ONLY_LIBRARY */
//++-- lpc.c 

//++ float.c  
#if HAVE_CONFIG_H
#  include <config.h>
#endif

#ifdef FLAC__INTEGER_ONLY_LIBRARY

/* adjust for compilers that can't understand using LLU suffix for uint64_t literals */
#ifdef _MSC_VER
#define FLAC__U64L(x) x
#else
#define FLAC__U64L(x) x##LLU
#endif

const FLAC__fixedpoint FLAC__FP_ZERO = 0;
const FLAC__fixedpoint FLAC__FP_ONE_HALF = 0x00008000;
const FLAC__fixedpoint FLAC__FP_ONE = 0x00010000;
const FLAC__fixedpoint FLAC__FP_LN2 = 45426;
const FLAC__fixedpoint FLAC__FP_E = 178145;

/* Lookup tables for Knuth's logarithm algorithm */
#define LOG2_LOOKUP_PRECISION 16
static const FLAC__uint32 log2_lookup[][LOG2_LOOKUP_PRECISION] = {
	{
		/*
		 * 0 fraction bits
		 */
		/* undefined */ 0x00000000,
		/* lg(2/1) = */ 0x00000001,
		/* lg(4/3) = */ 0x00000000,
		/* lg(8/7) = */ 0x00000000,
		/* lg(16/15) = */ 0x00000000,
		/* lg(32/31) = */ 0x00000000,
		/* lg(64/63) = */ 0x00000000,
		/* lg(128/127) = */ 0x00000000,
		/* lg(256/255) = */ 0x00000000,
		/* lg(512/511) = */ 0x00000000,
		/* lg(1024/1023) = */ 0x00000000,
		/* lg(2048/2047) = */ 0x00000000,
		/* lg(4096/4095) = */ 0x00000000,
		/* lg(8192/8191) = */ 0x00000000,
		/* lg(16384/16383) = */ 0x00000000,
		/* lg(32768/32767) = */ 0x00000000
	},
	{
		/*
		 * 4 fraction bits
		 */
		/* undefined */ 0x00000000,
		/* lg(2/1) = */ 0x00000010,
		/* lg(4/3) = */ 0x00000007,
		/* lg(8/7) = */ 0x00000003,
		/* lg(16/15) = */ 0x00000001,
		/* lg(32/31) = */ 0x00000001,
		/* lg(64/63) = */ 0x00000000,
		/* lg(128/127) = */ 0x00000000,
		/* lg(256/255) = */ 0x00000000,
		/* lg(512/511) = */ 0x00000000,
		/* lg(1024/1023) = */ 0x00000000,
		/* lg(2048/2047) = */ 0x00000000,
		/* lg(4096/4095) = */ 0x00000000,
		/* lg(8192/8191) = */ 0x00000000,
		/* lg(16384/16383) = */ 0x00000000,
		/* lg(32768/32767) = */ 0x00000000
	},
	{
		/*
		 * 8 fraction bits
		 */
		/* undefined */ 0x00000000,
		/* lg(2/1) = */ 0x00000100,
		/* lg(4/3) = */ 0x0000006a,
		/* lg(8/7) = */ 0x00000031,
		/* lg(16/15) = */ 0x00000018,
		/* lg(32/31) = */ 0x0000000c,
		/* lg(64/63) = */ 0x00000006,
		/* lg(128/127) = */ 0x00000003,
		/* lg(256/255) = */ 0x00000001,
		/* lg(512/511) = */ 0x00000001,
		/* lg(1024/1023) = */ 0x00000000,
		/* lg(2048/2047) = */ 0x00000000,
		/* lg(4096/4095) = */ 0x00000000,
		/* lg(8192/8191) = */ 0x00000000,
		/* lg(16384/16383) = */ 0x00000000,
		/* lg(32768/32767) = */ 0x00000000
	},
	{
		/*
		 * 12 fraction bits
		 */
		/* undefined */ 0x00000000,
		/* lg(2/1) = */ 0x00001000,
		/* lg(4/3) = */ 0x000006a4,
		/* lg(8/7) = */ 0x00000315,
		/* lg(16/15) = */ 0x0000017d,
		/* lg(32/31) = */ 0x000000bc,
		/* lg(64/63) = */ 0x0000005d,
		/* lg(128/127) = */ 0x0000002e,
		/* lg(256/255) = */ 0x00000017,
		/* lg(512/511) = */ 0x0000000c,
		/* lg(1024/1023) = */ 0x00000006,
		/* lg(2048/2047) = */ 0x00000003,
		/* lg(4096/4095) = */ 0x00000001,
		/* lg(8192/8191) = */ 0x00000001,
		/* lg(16384/16383) = */ 0x00000000,
		/* lg(32768/32767) = */ 0x00000000
	},
	{
		/*
		 * 16 fraction bits
		 */
		/* undefined */ 0x00000000,
		/* lg(2/1) = */ 0x00010000,
		/* lg(4/3) = */ 0x00006a40,
		/* lg(8/7) = */ 0x00003151,
		/* lg(16/15) = */ 0x000017d6,
		/* lg(32/31) = */ 0x00000bba,
		/* lg(64/63) = */ 0x000005d1,
		/* lg(128/127) = */ 0x000002e6,
		/* lg(256/255) = */ 0x00000172,
		/* lg(512/511) = */ 0x000000b9,
		/* lg(1024/1023) = */ 0x0000005c,
		/* lg(2048/2047) = */ 0x0000002e,
		/* lg(4096/4095) = */ 0x00000017,
		/* lg(8192/8191) = */ 0x0000000c,
		/* lg(16384/16383) = */ 0x00000006,
		/* lg(32768/32767) = */ 0x00000003
	},
	{
		/*
		 * 20 fraction bits
		 */
		/* undefined */ 0x00000000,
		/* lg(2/1) = */ 0x00100000,
		/* lg(4/3) = */ 0x0006a3fe,
		/* lg(8/7) = */ 0x00031513,
		/* lg(16/15) = */ 0x00017d60,
		/* lg(32/31) = */ 0x0000bb9d,
		/* lg(64/63) = */ 0x00005d10,
		/* lg(128/127) = */ 0x00002e59,
		/* lg(256/255) = */ 0x00001721,
		/* lg(512/511) = */ 0x00000b8e,
		/* lg(1024/1023) = */ 0x000005c6,
		/* lg(2048/2047) = */ 0x000002e3,
		/* lg(4096/4095) = */ 0x00000171,
		/* lg(8192/8191) = */ 0x000000b9,
		/* lg(16384/16383) = */ 0x0000005c,
		/* lg(32768/32767) = */ 0x0000002e
	},
	{
		/*
		 * 24 fraction bits
		 */
		/* undefined */ 0x00000000,
		/* lg(2/1) = */ 0x01000000,
		/* lg(4/3) = */ 0x006a3fe6,
		/* lg(8/7) = */ 0x00315130,
		/* lg(16/15) = */ 0x0017d605,
		/* lg(32/31) = */ 0x000bb9ca,
		/* lg(64/63) = */ 0x0005d0fc,
		/* lg(128/127) = */ 0x0002e58f,
		/* lg(256/255) = */ 0x0001720e,
		/* lg(512/511) = */ 0x0000b8d8,
		/* lg(1024/1023) = */ 0x00005c61,
		/* lg(2048/2047) = */ 0x00002e2d,
		/* lg(4096/4095) = */ 0x00001716,
		/* lg(8192/8191) = */ 0x00000b8b,
		/* lg(16384/16383) = */ 0x000005c5,
		/* lg(32768/32767) = */ 0x000002e3
	},
	{
		/*
		 * 28 fraction bits
		 */
		/* undefined */ 0x00000000,
		/* lg(2/1) = */ 0x10000000,
		/* lg(4/3) = */ 0x06a3fe5c,
		/* lg(8/7) = */ 0x03151301,
		/* lg(16/15) = */ 0x017d6049,
		/* lg(32/31) = */ 0x00bb9ca6,
		/* lg(64/63) = */ 0x005d0fba,
		/* lg(128/127) = */ 0x002e58f7,
		/* lg(256/255) = */ 0x001720da,
		/* lg(512/511) = */ 0x000b8d87,
		/* lg(1024/1023) = */ 0x0005c60b,
		/* lg(2048/2047) = */ 0x0002e2d7,
		/* lg(4096/4095) = */ 0x00017160,
		/* lg(8192/8191) = */ 0x0000b8ad,
		/* lg(16384/16383) = */ 0x00005c56,
		/* lg(32768/32767) = */ 0x00002e2b
	}
};

#if 0
static const FLAC__uint64 log2_lookup_wide[] = {
	{
		/*
		 * 32 fraction bits
		 */
		/* undefined */ 0x00000000,
		/* lg(2/1) = */ FLAC__U64L(0x100000000),
		/* lg(4/3) = */ FLAC__U64L(0x6a3fe5c6),
		/* lg(8/7) = */ FLAC__U64L(0x31513015),
		/* lg(16/15) = */ FLAC__U64L(0x17d60497),
		/* lg(32/31) = */ FLAC__U64L(0x0bb9ca65),
		/* lg(64/63) = */ FLAC__U64L(0x05d0fba2),
		/* lg(128/127) = */ FLAC__U64L(0x02e58f74),
		/* lg(256/255) = */ FLAC__U64L(0x01720d9c),
		/* lg(512/511) = */ FLAC__U64L(0x00b8d875),
		/* lg(1024/1023) = */ FLAC__U64L(0x005c60aa),
		/* lg(2048/2047) = */ FLAC__U64L(0x002e2d72),
		/* lg(4096/4095) = */ FLAC__U64L(0x00171600),
		/* lg(8192/8191) = */ FLAC__U64L(0x000b8ad2),
		/* lg(16384/16383) = */ FLAC__U64L(0x0005c55d),
		/* lg(32768/32767) = */ FLAC__U64L(0x0002e2ac)
	},
	{
		/*
		 * 48 fraction bits
		 */
		/* undefined */ 0x00000000,
		/* lg(2/1) = */ FLAC__U64L(0x1000000000000),
		/* lg(4/3) = */ FLAC__U64L(0x6a3fe5c60429),
		/* lg(8/7) = */ FLAC__U64L(0x315130157f7a),
		/* lg(16/15) = */ FLAC__U64L(0x17d60496cfbb),
		/* lg(32/31) = */ FLAC__U64L(0xbb9ca64ecac),
		/* lg(64/63) = */ FLAC__U64L(0x5d0fba187cd),
		/* lg(128/127) = */ FLAC__U64L(0x2e58f7441ee),
		/* lg(256/255) = */ FLAC__U64L(0x1720d9c06a8),
		/* lg(512/511) = */ FLAC__U64L(0xb8d8752173),
		/* lg(1024/1023) = */ FLAC__U64L(0x5c60aa252e),
		/* lg(2048/2047) = */ FLAC__U64L(0x2e2d71b0d8),
		/* lg(4096/4095) = */ FLAC__U64L(0x1716001719),
		/* lg(8192/8191) = */ FLAC__U64L(0xb8ad1de1b),
		/* lg(16384/16383) = */ FLAC__U64L(0x5c55d640d),
		/* lg(32768/32767) = */ FLAC__U64L(0x2e2abcf52)
	}
};
#endif

FLAC__uint32 FLAC__fixedpoint_log2(FLAC__uint32 x, unsigned fracbits, unsigned precision)
{
	const FLAC__uint32 ONE = (1u << fracbits);
	const FLAC__uint32 *table = log2_lookup[fracbits >> 2];

	FLAC__ASSERT(fracbits < 32);
	FLAC__ASSERT((fracbits & 0x3) == 0);

	if(x < ONE)
		return 0;
	
	if(precision > LOG2_LOOKUP_PRECISION)
		precision = LOG2_LOOKUP_PRECISION;

	/* Knuth's algorithm for computing logarithms, optimized for base-2 with lookup tables */
	{
		FLAC__uint32 y = 0;
		FLAC__uint32 z = x >> 1, k = 1;
		while (x > ONE && k < precision) {
			if (x - z >= ONE) {
				x -= z;
				z = x >> k;
				y += table[k];
			}
			else {
				z >>= 1;
				k++;
			}
		}
		return y;
	}
}

#endif /* defined FLAC__INTEGER_ONLY_LIBRARY */
//++-- float.c  


//++ fixed.c   

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <math.h>
#include <string.h>

#ifndef M_LN2
/* math.h in VC++ doesn't seem to have this (how Microsoft is that?) */
#define M_LN2 0.69314718055994530942
#endif

#ifdef min
#undef min
#endif
#define min(x,y) ((x) < (y)? (x) : (y))

#ifdef local_abs
#undef local_abs
#endif
#define local_abs(x) ((unsigned)((x)<0? -(x) : (x)))

#ifdef FLAC__INTEGER_ONLY_LIBRARY
/* rbps stands for residual bits per sample
 *
 *             (ln(2) * err)
 * rbps = log  (-----------)
 *           2 (     n     )
 */
static FLAC__fixedpoint local__compute_rbps_integerized(FLAC__uint32 err, FLAC__uint32 n)
{
	FLAC__uint32 rbps;
	unsigned bits; /* the number of bits required to represent a number */
	int fracbits; /* the number of bits of rbps that comprise the fractional part */

	FLAC__ASSERT(sizeof(rbps) == sizeof(FLAC__fixedpoint));
	FLAC__ASSERT(err > 0);
	FLAC__ASSERT(n > 0);

	FLAC__ASSERT(n <= FLAC__MAX_BLOCK_SIZE);
	if(err <= n)
		return 0;
	/*
	 * The above two things tell us 1) n fits in 16 bits; 2) err/n > 1.
	 * These allow us later to know we won't lose too much precision in the
	 * fixed-point division (err<<fracbits)/n.
	 */

	fracbits = (8*sizeof(err)) - (FLAC__bitmath_ilog2(err)+1);

	err <<= fracbits;
	err /= n;
	/* err now holds err/n with fracbits fractional bits */

	/*
	 * Whittle err down to 16 bits max.  16 significant bits is enough for
	 * our purposes.
	 */
	FLAC__ASSERT(err > 0);
	bits = FLAC__bitmath_ilog2(err)+1;
	if(bits > 16) {
		err >>= (bits-16);
		fracbits -= (bits-16);
	}
	rbps = (FLAC__uint32)err;

	/* Multiply by fixed-point version of ln(2), with 16 fractional bits */
	rbps *= FLAC__FP_LN2;
	fracbits += 16;
	FLAC__ASSERT(fracbits >= 0);

	/* FLAC__fixedpoint_log2 requires fracbits%4 to be 0 */
	{
		const int f = fracbits & 3;
		if(f) {
			rbps >>= f;
			fracbits -= f;
		}
	}

	rbps = FLAC__fixedpoint_log2(rbps, fracbits, (unsigned)(-1));

	if(rbps == 0)
		return 0;

	/*
	 * The return value must have 16 fractional bits.  Since the whole part
	 * of the base-2 log of a 32 bit number must fit in 5 bits, and fracbits
	 * must be >= -3, these assertion allows us to be able to shift rbps
	 * left if necessary to get 16 fracbits without losing any bits of the
	 * whole part of rbps.
	 *
	 * There is a slight chance due to accumulated error that the whole part
	 * will require 6 bits, so we use 6 in the assertion.  Really though as
	 * long as it fits in 13 bits (32 - (16 - (-3))) we are fine.
	 */
	FLAC__ASSERT((int)FLAC__bitmath_ilog2(rbps)+1 <= fracbits + 6);
	FLAC__ASSERT(fracbits >= -3);

	/* now shift the decimal point into place */
	if(fracbits < 16)
		return rbps << (16-fracbits);
	else if(fracbits > 16)
		return rbps >> (fracbits-16);
	else
		return rbps;
}

static FLAC__fixedpoint local__compute_rbps_wide_integerized(FLAC__uint64 err, FLAC__uint32 n)
{
	FLAC__uint32 rbps;
	unsigned bits; /* the number of bits required to represent a number */
	int fracbits; /* the number of bits of rbps that comprise the fractional part */

	FLAC__ASSERT(sizeof(rbps) == sizeof(FLAC__fixedpoint));
	FLAC__ASSERT(err > 0);
	FLAC__ASSERT(n > 0);

	FLAC__ASSERT(n <= FLAC__MAX_BLOCK_SIZE);
	if(err <= n)
		return 0;
	/*
	 * The above two things tell us 1) n fits in 16 bits; 2) err/n > 1.
	 * These allow us later to know we won't lose too much precision in the
	 * fixed-point division (err<<fracbits)/n.
	 */

	fracbits = (8*sizeof(err)) - (FLAC__bitmath_ilog2_wide(err)+1);

	err <<= fracbits;
	err /= n;
	/* err now holds err/n with fracbits fractional bits */

	/*
	 * Whittle err down to 16 bits max.  16 significant bits is enough for
	 * our purposes.
	 */
	FLAC__ASSERT(err > 0);
	bits = FLAC__bitmath_ilog2_wide(err)+1;
	if(bits > 16) {
		err >>= (bits-16);
		fracbits -= (bits-16);
	}
	rbps = (FLAC__uint32)err;

	/* Multiply by fixed-point version of ln(2), with 16 fractional bits */
	rbps *= FLAC__FP_LN2;
	fracbits += 16;
	FLAC__ASSERT(fracbits >= 0);

	/* FLAC__fixedpoint_log2 requires fracbits%4 to be 0 */
	{
		const int f = fracbits & 3;
		if(f) {
			rbps >>= f;
			fracbits -= f;
		}
	}

	rbps = FLAC__fixedpoint_log2(rbps, fracbits, (unsigned)(-1));

	if(rbps == 0)
		return 0;

	/*
	 * The return value must have 16 fractional bits.  Since the whole part
	 * of the base-2 log of a 32 bit number must fit in 5 bits, and fracbits
	 * must be >= -3, these assertion allows us to be able to shift rbps
	 * left if necessary to get 16 fracbits without losing any bits of the
	 * whole part of rbps.
	 *
	 * There is a slight chance due to accumulated error that the whole part
	 * will require 6 bits, so we use 6 in the assertion.  Really though as
	 * long as it fits in 13 bits (32 - (16 - (-3))) we are fine.
	 */
	FLAC__ASSERT((int)FLAC__bitmath_ilog2(rbps)+1 <= fracbits + 6);
	FLAC__ASSERT(fracbits >= -3);

	/* now shift the decimal point into place */
	if(fracbits < 16)
		return rbps << (16-fracbits);
	else if(fracbits > 16)
		return rbps >> (fracbits-16);
	else
		return rbps;
}
#endif

#ifndef FLAC__INTEGER_ONLY_LIBRARY
unsigned FLAC__fixed_compute_best_predictor(const FLAC__int32 data[], unsigned data_len, FLAC__float residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1])
#else
unsigned FLAC__fixed_compute_best_predictor(const FLAC__int32 data[], unsigned data_len, FLAC__fixedpoint residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1])
#endif
{
	FLAC__int32 last_error_0 = data[-1];
	FLAC__int32 last_error_1 = data[-1] - data[-2];
	FLAC__int32 last_error_2 = last_error_1 - (data[-2] - data[-3]);
	FLAC__int32 last_error_3 = last_error_2 - (data[-2] - 2*data[-3] + data[-4]);
	FLAC__int32 error, save;
	FLAC__uint32 total_error_0 = 0, total_error_1 = 0, total_error_2 = 0, total_error_3 = 0, total_error_4 = 0;
	unsigned i, order;

	for(i = 0; i < data_len; i++) {
		error  = data[i]     ; total_error_0 += local_abs(error);                      save = error;
		error -= last_error_0; total_error_1 += local_abs(error); last_error_0 = save; save = error;
		error -= last_error_1; total_error_2 += local_abs(error); last_error_1 = save; save = error;
		error -= last_error_2; total_error_3 += local_abs(error); last_error_2 = save; save = error;
		error -= last_error_3; total_error_4 += local_abs(error); last_error_3 = save;
	}

	if(total_error_0 < min(min(min(total_error_1, total_error_2), total_error_3), total_error_4))
		order = 0;
	else if(total_error_1 < min(min(total_error_2, total_error_3), total_error_4))
		order = 1;
	else if(total_error_2 < min(total_error_3, total_error_4))
		order = 2;
	else if(total_error_3 < total_error_4)
		order = 3;
	else
		order = 4;

	/* Estimate the expected number of bits per residual signal sample. */
	/* 'total_error*' is linearly related to the variance of the residual */
	/* signal, so we use it directly to compute E(|x|) */
	FLAC__ASSERT(data_len > 0 || total_error_0 == 0);
	FLAC__ASSERT(data_len > 0 || total_error_1 == 0);
	FLAC__ASSERT(data_len > 0 || total_error_2 == 0);
	FLAC__ASSERT(data_len > 0 || total_error_3 == 0);
	FLAC__ASSERT(data_len > 0 || total_error_4 == 0);
#ifndef FLAC__INTEGER_ONLY_LIBRARY
	residual_bits_per_sample[0] = (FLAC__float)((total_error_0 > 0) ? log(M_LN2 * (FLAC__double)total_error_0 / (FLAC__double)data_len) / M_LN2 : 0.0);
	residual_bits_per_sample[1] = (FLAC__float)((total_error_1 > 0) ? log(M_LN2 * (FLAC__double)total_error_1 / (FLAC__double)data_len) / M_LN2 : 0.0);
	residual_bits_per_sample[2] = (FLAC__float)((total_error_2 > 0) ? log(M_LN2 * (FLAC__double)total_error_2 / (FLAC__double)data_len) / M_LN2 : 0.0);
	residual_bits_per_sample[3] = (FLAC__float)((total_error_3 > 0) ? log(M_LN2 * (FLAC__double)total_error_3 / (FLAC__double)data_len) / M_LN2 : 0.0);
	residual_bits_per_sample[4] = (FLAC__float)((total_error_4 > 0) ? log(M_LN2 * (FLAC__double)total_error_4 / (FLAC__double)data_len) / M_LN2 : 0.0);
#else
	residual_bits_per_sample[0] = (total_error_0 > 0) ? local__compute_rbps_integerized(total_error_0, data_len) : 0;
	residual_bits_per_sample[1] = (total_error_1 > 0) ? local__compute_rbps_integerized(total_error_1, data_len) : 0;
	residual_bits_per_sample[2] = (total_error_2 > 0) ? local__compute_rbps_integerized(total_error_2, data_len) : 0;
	residual_bits_per_sample[3] = (total_error_3 > 0) ? local__compute_rbps_integerized(total_error_3, data_len) : 0;
	residual_bits_per_sample[4] = (total_error_4 > 0) ? local__compute_rbps_integerized(total_error_4, data_len) : 0;
#endif

	return order;
}

#ifndef FLAC__INTEGER_ONLY_LIBRARY
unsigned FLAC__fixed_compute_best_predictor_wide(const FLAC__int32 data[], unsigned data_len, FLAC__float residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1])
#else
unsigned FLAC__fixed_compute_best_predictor_wide(const FLAC__int32 data[], unsigned data_len, FLAC__fixedpoint residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1])
#endif
{
	FLAC__int32 last_error_0 = data[-1];
	FLAC__int32 last_error_1 = data[-1] - data[-2];
	FLAC__int32 last_error_2 = last_error_1 - (data[-2] - data[-3]);
	FLAC__int32 last_error_3 = last_error_2 - (data[-2] - 2*data[-3] + data[-4]);
	FLAC__int32 error, save;
	/* total_error_* are 64-bits to avoid overflow when encoding
	 * erratic signals when the bits-per-sample and blocksize are
	 * large.
	 */
	FLAC__uint64 total_error_0 = 0, total_error_1 = 0, total_error_2 = 0, total_error_3 = 0, total_error_4 = 0;
	unsigned i, order;

	for(i = 0; i < data_len; i++) {
		error  = data[i]     ; total_error_0 += local_abs(error);                      save = error;
		error -= last_error_0; total_error_1 += local_abs(error); last_error_0 = save; save = error;
		error -= last_error_1; total_error_2 += local_abs(error); last_error_1 = save; save = error;
		error -= last_error_2; total_error_3 += local_abs(error); last_error_2 = save; save = error;
		error -= last_error_3; total_error_4 += local_abs(error); last_error_3 = save;
	}

	if(total_error_0 < min(min(min(total_error_1, total_error_2), total_error_3), total_error_4))
		order = 0;
	else if(total_error_1 < min(min(total_error_2, total_error_3), total_error_4))
		order = 1;
	else if(total_error_2 < min(total_error_3, total_error_4))
		order = 2;
	else if(total_error_3 < total_error_4)
		order = 3;
	else
		order = 4;

	/* Estimate the expected number of bits per residual signal sample. */
	/* 'total_error*' is linearly related to the variance of the residual */
	/* signal, so we use it directly to compute E(|x|) */
	FLAC__ASSERT(data_len > 0 || total_error_0 == 0);
	FLAC__ASSERT(data_len > 0 || total_error_1 == 0);
	FLAC__ASSERT(data_len > 0 || total_error_2 == 0);
	FLAC__ASSERT(data_len > 0 || total_error_3 == 0);
	FLAC__ASSERT(data_len > 0 || total_error_4 == 0);
#ifndef FLAC__INTEGER_ONLY_LIBRARY
#if defined _MSC_VER || defined __MINGW32__
	/* with MSVC you have to spoon feed it the casting */
	residual_bits_per_sample[0] = (FLAC__float)((total_error_0 > 0) ? log(M_LN2 * (FLAC__double)(FLAC__int64)total_error_0 / (FLAC__double)data_len) / M_LN2 : 0.0);
	residual_bits_per_sample[1] = (FLAC__float)((total_error_1 > 0) ? log(M_LN2 * (FLAC__double)(FLAC__int64)total_error_1 / (FLAC__double)data_len) / M_LN2 : 0.0);
	residual_bits_per_sample[2] = (FLAC__float)((total_error_2 > 0) ? log(M_LN2 * (FLAC__double)(FLAC__int64)total_error_2 / (FLAC__double)data_len) / M_LN2 : 0.0);
	residual_bits_per_sample[3] = (FLAC__float)((total_error_3 > 0) ? log(M_LN2 * (FLAC__double)(FLAC__int64)total_error_3 / (FLAC__double)data_len) / M_LN2 : 0.0);
	residual_bits_per_sample[4] = (FLAC__float)((total_error_4 > 0) ? log(M_LN2 * (FLAC__double)(FLAC__int64)total_error_4 / (FLAC__double)data_len) / M_LN2 : 0.0);
#else
	residual_bits_per_sample[0] = (FLAC__float)((total_error_0 > 0) ? log(M_LN2 * (FLAC__double)total_error_0 / (FLAC__double)data_len) / M_LN2 : 0.0);
	residual_bits_per_sample[1] = (FLAC__float)((total_error_1 > 0) ? log(M_LN2 * (FLAC__double)total_error_1 / (FLAC__double)data_len) / M_LN2 : 0.0);
	residual_bits_per_sample[2] = (FLAC__float)((total_error_2 > 0) ? log(M_LN2 * (FLAC__double)total_error_2 / (FLAC__double)data_len) / M_LN2 : 0.0);
	residual_bits_per_sample[3] = (FLAC__float)((total_error_3 > 0) ? log(M_LN2 * (FLAC__double)total_error_3 / (FLAC__double)data_len) / M_LN2 : 0.0);
	residual_bits_per_sample[4] = (FLAC__float)((total_error_4 > 0) ? log(M_LN2 * (FLAC__double)total_error_4 / (FLAC__double)data_len) / M_LN2 : 0.0);
#endif
#else
	residual_bits_per_sample[0] = (total_error_0 > 0) ? local__compute_rbps_wide_integerized(total_error_0, data_len) : 0;
	residual_bits_per_sample[1] = (total_error_1 > 0) ? local__compute_rbps_wide_integerized(total_error_1, data_len) : 0;
	residual_bits_per_sample[2] = (total_error_2 > 0) ? local__compute_rbps_wide_integerized(total_error_2, data_len) : 0;
	residual_bits_per_sample[3] = (total_error_3 > 0) ? local__compute_rbps_wide_integerized(total_error_3, data_len) : 0;
	residual_bits_per_sample[4] = (total_error_4 > 0) ? local__compute_rbps_wide_integerized(total_error_4, data_len) : 0;
#endif

	return order;
}

void FLAC__fixed_compute_residual(const FLAC__int32 data[], unsigned data_len, unsigned order, FLAC__int32 residual[])
{
	const int idata_len = (int)data_len;
	int i;

	switch(order) {
		case 0:
			FLAC__ASSERT(sizeof(residual[0]) == sizeof(data[0]));
			memcpy(residual, data, sizeof(residual[0])*data_len);
			break;
		case 1:
			for(i = 0; i < idata_len; i++)
				residual[i] = data[i] - data[i-1];
			break;
		case 2:
			for(i = 0; i < idata_len; i++)
#if 1 /* OPT: may be faster with some compilers on some systems */
				residual[i] = data[i] - (data[i-1] << 1) + data[i-2];
#else
				residual[i] = data[i] - 2*data[i-1] + data[i-2];
#endif
			break;
		case 3:
			for(i = 0; i < idata_len; i++)
#if 1 /* OPT: may be faster with some compilers on some systems */
				residual[i] = data[i] - (((data[i-1]-data[i-2])<<1) + (data[i-1]-data[i-2])) - data[i-3];
#else
				residual[i] = data[i] - 3*data[i-1] + 3*data[i-2] - data[i-3];
#endif
			break;
		case 4:
			for(i = 0; i < idata_len; i++)
#if 1 /* OPT: may be faster with some compilers on some systems */
				residual[i] = data[i] - ((data[i-1]+data[i-3])<<2) + ((data[i-2]<<2) + (data[i-2]<<1)) + data[i-4];
#else
				residual[i] = data[i] - 4*data[i-1] + 6*data[i-2] - 4*data[i-3] + data[i-4];
#endif
			break;
		default:
			FLAC__ASSERT(0);
	}
}

void FLAC__fixed_restore_signal(const FLAC__int32 residual[], unsigned data_len, unsigned order, FLAC__int32 data[])
{
	int i, idata_len = (int)data_len;

	switch(order) {
		case 0:
			FLAC__ASSERT(sizeof(residual[0]) == sizeof(data[0]));
			memcpy(data, residual, sizeof(residual[0])*data_len);
			break;
		case 1:
			for(i = 0; i < idata_len; i++)
				data[i] = residual[i] + data[i-1];
			break;
		case 2:
			for(i = 0; i < idata_len; i++)
#if 1 /* OPT: may be faster with some compilers on some systems */
				data[i] = residual[i] + (data[i-1]<<1) - data[i-2];
#else
				data[i] = residual[i] + 2*data[i-1] - data[i-2];
#endif
			break;
		case 3:
			for(i = 0; i < idata_len; i++)
#if 1 /* OPT: may be faster with some compilers on some systems */
				data[i] = residual[i] + (((data[i-1]-data[i-2])<<1) + (data[i-1]-data[i-2])) + data[i-3];
#else
				data[i] = residual[i] + 3*data[i-1] - 3*data[i-2] + data[i-3];
#endif
			break;
		case 4:
			for(i = 0; i < idata_len; i++)
#if 1 /* OPT: may be faster with some compilers on some systems */
				data[i] = residual[i] + ((data[i-1]+data[i-3])<<2) - ((data[i-2]<<2) + (data[i-2]<<1)) - data[i-4];
#else
				data[i] = residual[i] + 4*data[i-1] - 6*data[i-2] + 4*data[i-3] - data[i-4];
#endif
			break;
		default:
			FLAC__ASSERT(0);
	}
}
//++-- fixed.c   

//++ format.c   

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef FLaC__INLINE
#define FLaC__INLINE
#endif

#ifdef min
#undef min
#endif
#define min(a,b) ((a)<(b)?(a):(b))

/* adjust for compilers that can't understand using LLU suffix for uint64_t literals */
#ifdef _MSC_VER
#define FLAC__U64L(x) x
#else
#define FLAC__U64L(x) x##LLU
#endif

/* VERSION should come from configure */
FLAC_API const char *FLAC__VERSION_STRING = VERSION;

#if defined _MSC_VER || defined __BORLANDC__ || defined __MINW32__
/* yet one more hack because of MSVC6: */
FLAC_API const char *FLAC__VENDOR_STRING = "reference libFLAC 1.2.1 20070917";
#else
FLAC_API const char *FLAC__VENDOR_STRING = "reference libFLAC " VERSION " 20070917";
#endif

FLAC_API const FLAC__byte FLAC__STREAM_SYNC_STRING[4] = { 'f','L','a','C' };
FLAC_API const unsigned FLAC__STREAM_SYNC = 0x664C6143;
FLAC_API const unsigned FLAC__STREAM_SYNC_LEN = 32; /* bits */

FLAC_API const unsigned FLAC__STREAM_METADATA_STREAMINFO_MIN_BLOCK_SIZE_LEN = 16; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_STREAMINFO_MAX_BLOCK_SIZE_LEN = 16; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_STREAMINFO_MIN_FRAME_SIZE_LEN = 24; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_STREAMINFO_MAX_FRAME_SIZE_LEN = 24; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_STREAMINFO_SAMPLE_RATE_LEN = 20; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_STREAMINFO_CHANNELS_LEN = 3; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_STREAMINFO_BITS_PER_SAMPLE_LEN = 5; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_STREAMINFO_TOTAL_SAMPLES_LEN = 36; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_STREAMINFO_MD5SUM_LEN = 128; /* bits */

FLAC_API const unsigned FLAC__STREAM_METADATA_APPLICATION_ID_LEN = 32; /* bits */

FLAC_API const unsigned FLAC__STREAM_METADATA_SEEKPOINT_SAMPLE_NUMBER_LEN = 64; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_SEEKPOINT_STREAM_OFFSET_LEN = 64; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_SEEKPOINT_FRAME_SAMPLES_LEN = 16; /* bits */

FLAC_API const FLAC__uint64 FLAC__STREAM_METADATA_SEEKPOINT_PLACEHOLDER = FLAC__U64L(0xffffffffffffffff);

FLAC_API const unsigned FLAC__STREAM_METADATA_VORBIS_COMMENT_ENTRY_LENGTH_LEN = 32; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_VORBIS_COMMENT_NUM_COMMENTS_LEN = 32; /* bits */

FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_INDEX_OFFSET_LEN = 64; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_INDEX_NUMBER_LEN = 8; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_INDEX_RESERVED_LEN = 3*8; /* bits */

FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_TRACK_OFFSET_LEN = 64; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_TRACK_NUMBER_LEN = 8; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_TRACK_ISRC_LEN = 12*8; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_TRACK_TYPE_LEN = 1; /* bit */
FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_TRACK_PRE_EMPHASIS_LEN = 1; /* bit */
FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_TRACK_RESERVED_LEN = 6+13*8; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_TRACK_NUM_INDICES_LEN = 8; /* bits */

FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_MEDIA_CATALOG_NUMBER_LEN = 128*8; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_LEAD_IN_LEN = 64; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_IS_CD_LEN = 1; /* bit */
FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_RESERVED_LEN = 7+258*8; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_CUESHEET_NUM_TRACKS_LEN = 8; /* bits */

FLAC_API const unsigned FLAC__STREAM_METADATA_PICTURE_TYPE_LEN = 32; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_PICTURE_MIME_TYPE_LENGTH_LEN = 32; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_PICTURE_DESCRIPTION_LENGTH_LEN = 32; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_PICTURE_WIDTH_LEN = 32; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_PICTURE_HEIGHT_LEN = 32; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_PICTURE_DEPTH_LEN = 32; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_PICTURE_COLORS_LEN = 32; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_PICTURE_DATA_LENGTH_LEN = 32; /* bits */

FLAC_API const unsigned FLAC__STREAM_METADATA_IS_LAST_LEN = 1; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_TYPE_LEN = 7; /* bits */
FLAC_API const unsigned FLAC__STREAM_METADATA_LENGTH_LEN = 24; /* bits */

FLAC_API const unsigned FLAC__FRAME_HEADER_SYNC = 0x3ffe;
FLAC_API const unsigned FLAC__FRAME_HEADER_SYNC_LEN = 14; /* bits */
FLAC_API const unsigned FLAC__FRAME_HEADER_RESERVED_LEN = 1; /* bits */
FLAC_API const unsigned FLAC__FRAME_HEADER_BLOCKING_STRATEGY_LEN = 1; /* bits */
FLAC_API const unsigned FLAC__FRAME_HEADER_BLOCK_SIZE_LEN = 4; /* bits */
FLAC_API const unsigned FLAC__FRAME_HEADER_SAMPLE_RATE_LEN = 4; /* bits */
FLAC_API const unsigned FLAC__FRAME_HEADER_CHANNEL_ASSIGNMENT_LEN = 4; /* bits */
FLAC_API const unsigned FLAC__FRAME_HEADER_BITS_PER_SAMPLE_LEN = 3; /* bits */
FLAC_API const unsigned FLAC__FRAME_HEADER_ZERO_PAD_LEN = 1; /* bits */
FLAC_API const unsigned FLAC__FRAME_HEADER_CRC_LEN = 8; /* bits */

FLAC_API const unsigned FLAC__FRAME_FOOTER_CRC_LEN = 16; /* bits */

FLAC_API const unsigned FLAC__ENTROPY_CODING_METHOD_TYPE_LEN = 2; /* bits */
FLAC_API const unsigned FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ORDER_LEN = 4; /* bits */
FLAC_API const unsigned FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_PARAMETER_LEN = 4; /* bits */
FLAC_API const unsigned FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2_PARAMETER_LEN = 5; /* bits */
FLAC_API const unsigned FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_RAW_LEN = 5; /* bits */

FLAC_API const unsigned FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER = 15; /* == (1<<FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_PARAMETER_LEN)-1 */
FLAC_API const unsigned FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2_ESCAPE_PARAMETER = 31; /* == (1<<FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2_PARAMETER_LEN)-1 */

FLAC_API const char * const FLAC__EntropyCodingMethodTypeString[] = {
	"PARTITIONED_RICE",
	"PARTITIONED_RICE2"
};

FLAC_API const unsigned FLAC__SUBFRAME_LPC_QLP_COEFF_PRECISION_LEN = 4; /* bits */
FLAC_API const unsigned FLAC__SUBFRAME_LPC_QLP_SHIFT_LEN = 5; /* bits */

FLAC_API const unsigned FLAC__SUBFRAME_ZERO_PAD_LEN = 1; /* bits */
FLAC_API const unsigned FLAC__SUBFRAME_TYPE_LEN = 6; /* bits */
FLAC_API const unsigned FLAC__SUBFRAME_WASTED_BITS_FLAG_LEN = 1; /* bits */

FLAC_API const unsigned FLAC__SUBFRAME_TYPE_CONSTANT_BYTE_ALIGNED_MASK = 0x00;
FLAC_API const unsigned FLAC__SUBFRAME_TYPE_VERBATIM_BYTE_ALIGNED_MASK = 0x02;
FLAC_API const unsigned FLAC__SUBFRAME_TYPE_FIXED_BYTE_ALIGNED_MASK = 0x10;
FLAC_API const unsigned FLAC__SUBFRAME_TYPE_LPC_BYTE_ALIGNED_MASK = 0x40;

FLAC_API const char * const FLAC__SubframeTypeString[] = {
	"CONSTANT",
	"VERBATIM",
	"FIXED",
	"LPC"
};

FLAC_API const char * const FLAC__ChannelAssignmentString[] = {
	"INDEPENDENT",
	"LEFT_SIDE",
	"RIGHT_SIDE",
	"MID_SIDE"
};

FLAC_API const char * const FLAC__FrameNumberTypeString[] = {
	"FRAME_NUMBER_TYPE_FRAME_NUMBER",
	"FRAME_NUMBER_TYPE_SAMPLE_NUMBER"
};

FLAC_API const char * const FLAC__MetadataTypeString[] = {
	"STREAMINFO",
	"PADDING",
	"APPLICATION",
	"SEEKTABLE",
	"VORBIS_COMMENT",
	"CUESHEET",
	"PICTURE"
};

FLAC_API const char * const FLAC__StreamMetadata_Picture_TypeString[] = {
	"Other",
	"32x32 pixels 'file icon' (PNG only)",
	"Other file icon",
	"Cover (front)",
	"Cover (back)",
	"Leaflet page",
	"Media (e.g. label side of CD)",
	"Lead artist/lead performer/soloist",
	"Artist/performer",
	"Conductor",
	"Band/Orchestra",
	"Composer",
	"Lyricist/text writer",
	"Recording Location",
	"During recording",
	"During performance",
	"Movie/video screen capture",
	"A bright coloured fish",
	"Illustration",
	"Band/artist logotype",
	"Publisher/Studio logotype"
};

FLAC_API FLAC__bool FLAC__format_sample_rate_is_valid(unsigned sample_rate)
{
	if(sample_rate == 0 || sample_rate > FLAC__MAX_SAMPLE_RATE) {
		return false;
	}
	else
		return true;
}

FLAC_API FLAC__bool FLAC__format_sample_rate_is_subset(unsigned sample_rate)
{
	if(
		!FLAC__format_sample_rate_is_valid(sample_rate) ||
		(
			sample_rate >= (1u << 16) &&
			!(sample_rate % 1000 == 0 || sample_rate % 10 == 0)
		)
	) {
		return false;
	}
	else
		return true;
}

/* @@@@ add to unit tests; it is already indirectly tested by the metadata_object tests */
FLAC_API FLAC__bool FLAC__format_seektable_is_legal(const FLAC__StreamMetadata_SeekTable *seek_table)
{
	unsigned i;
	FLAC__uint64 prev_sample_number = 0;
	FLAC__bool got_prev = false;

	FLAC__ASSERT(0 != seek_table);

	for(i = 0; i < seek_table->num_points; i++) {
		if(got_prev) {
			if(
				seek_table->points[i].sample_number != FLAC__STREAM_METADATA_SEEKPOINT_PLACEHOLDER &&
				seek_table->points[i].sample_number <= prev_sample_number
			)
				return false;
		}
		prev_sample_number = seek_table->points[i].sample_number;
		got_prev = true;
	}

	return true;
}

/* used as the sort predicate for qsort() */
static int seekpoint_compare_(const FLAC__StreamMetadata_SeekPoint *l, const FLAC__StreamMetadata_SeekPoint *r)
{
	/* we don't just 'return l->sample_number - r->sample_number' since the result (FLAC__int64) might overflow an 'int' */
	if(l->sample_number == r->sample_number)
		return 0;
	else if(l->sample_number < r->sample_number)
		return -1;
	else
		return 1;
}

/* @@@@ add to unit tests; it is already indirectly tested by the metadata_object tests */
FLAC_API unsigned FLAC__format_seektable_sort(FLAC__StreamMetadata_SeekTable *seek_table)
{
	unsigned i, j;
	FLAC__bool first;

	FLAC__ASSERT(0 != seek_table);

	/* sort the seekpoints */
	qsort(seek_table->points, seek_table->num_points, sizeof(FLAC__StreamMetadata_SeekPoint), (int (*)(const void *, const void *))seekpoint_compare_);

	/* uniquify the seekpoints */
	first = true;
	for(i = j = 0; i < seek_table->num_points; i++) {
		if(seek_table->points[i].sample_number != FLAC__STREAM_METADATA_SEEKPOINT_PLACEHOLDER) {
			if(!first) {
				if(seek_table->points[i].sample_number == seek_table->points[j-1].sample_number)
					continue;
			}
		}
		first = false;
		seek_table->points[j++] = seek_table->points[i];
	}

	for(i = j; i < seek_table->num_points; i++) {
		seek_table->points[i].sample_number = FLAC__STREAM_METADATA_SEEKPOINT_PLACEHOLDER;
		seek_table->points[i].stream_offset = 0;
		seek_table->points[i].frame_samples = 0;
	}

	return j;
}

/*
 * also disallows non-shortest-form encodings, c.f.
 *   http://www.unicode.org/versions/corrigendum1.html
 * and a more clear explanation at the end of this section:
 *   http://www.cl.cam.ac.uk/~mgk25/unicode.html#utf-8
 */
static FLaC__INLINE unsigned utf8len_(const FLAC__byte *utf8)
{
	FLAC__ASSERT(0 != utf8);
	if ((utf8[0] & 0x80) == 0) {
		return 1;
	}
	else if ((utf8[0] & 0xE0) == 0xC0 && (utf8[1] & 0xC0) == 0x80) {
		if ((utf8[0] & 0xFE) == 0xC0) /* overlong sequence check */
			return 0;
		return 2;
	}
	else if ((utf8[0] & 0xF0) == 0xE0 && (utf8[1] & 0xC0) == 0x80 && (utf8[2] & 0xC0) == 0x80) {
		if (utf8[0] == 0xE0 && (utf8[1] & 0xE0) == 0x80) /* overlong sequence check */
			return 0;
		/* illegal surrogates check (U+D800...U+DFFF and U+FFFE...U+FFFF) */
		if (utf8[0] == 0xED && (utf8[1] & 0xE0) == 0xA0) /* D800-DFFF */
			return 0;
		if (utf8[0] == 0xEF && utf8[1] == 0xBF && (utf8[2] & 0xFE) == 0xBE) /* FFFE-FFFF */
			return 0;
		return 3;
	}
	else if ((utf8[0] & 0xF8) == 0xF0 && (utf8[1] & 0xC0) == 0x80 && (utf8[2] & 0xC0) == 0x80 && (utf8[3] & 0xC0) == 0x80) {
		if (utf8[0] == 0xF0 && (utf8[1] & 0xF0) == 0x80) /* overlong sequence check */
			return 0;
		return 4;
	}
	else if ((utf8[0] & 0xFC) == 0xF8 && (utf8[1] & 0xC0) == 0x80 && (utf8[2] & 0xC0) == 0x80 && (utf8[3] & 0xC0) == 0x80 && (utf8[4] & 0xC0) == 0x80) {
		if (utf8[0] == 0xF8 && (utf8[1] & 0xF8) == 0x80) /* overlong sequence check */
			return 0;
		return 5;
	}
	else if ((utf8[0] & 0xFE) == 0xFC && (utf8[1] & 0xC0) == 0x80 && (utf8[2] & 0xC0) == 0x80 && (utf8[3] & 0xC0) == 0x80 && (utf8[4] & 0xC0) == 0x80 && (utf8[5] & 0xC0) == 0x80) {
		if (utf8[0] == 0xFC && (utf8[1] & 0xFC) == 0x80) /* overlong sequence check */
			return 0;
		return 6;
	}
	else {
		return 0;
	}
}

FLAC_API FLAC__bool FLAC__format_vorbiscomment_entry_name_is_legal(const char *name)
{
	char c;
	for(c = *name; c; c = *(++name))
		if(c < 0x20 || c == 0x3d || c > 0x7d)
			return false;
	return true;
}

FLAC_API FLAC__bool FLAC__format_vorbiscomment_entry_value_is_legal(const FLAC__byte *value, unsigned length)
{
	if(length == (unsigned)(-1)) {
		while(*value) {
			unsigned n = utf8len_(value);
			if(n == 0)
				return false;
			value += n;
		}
	}
	else {
		const FLAC__byte *end = value + length;
		while(value < end) {
			unsigned n = utf8len_(value);
			if(n == 0)
				return false;
			value += n;
		}
		if(value != end)
			return false;
	}
	return true;
}

FLAC_API FLAC__bool FLAC__format_vorbiscomment_entry_is_legal(const FLAC__byte *entry, unsigned length)
{
	const FLAC__byte *s, *end;

	for(s = entry, end = s + length; s < end && *s != '='; s++) {
		if(*s < 0x20 || *s > 0x7D)
			return false;
	}
	if(s == end)
		return false;

	s++; /* skip '=' */

	while(s < end) {
		unsigned n = utf8len_(s);
		if(n == 0)
			return false;
		s += n;
	}
	if(s != end)
		return false;

	return true;
}

/* @@@@ add to unit tests; it is already indirectly tested by the metadata_object tests */
FLAC_API FLAC__bool FLAC__format_cuesheet_is_legal(const FLAC__StreamMetadata_CueSheet *cue_sheet, FLAC__bool check_cd_da_subset, const char **violation)
{
	unsigned i, j;

	if(check_cd_da_subset) {
		if(cue_sheet->lead_in < 2 * 44100) {
			if(violation) *violation = "CD-DA cue sheet must have a lead-in length of at least 2 seconds";
			return false;
		}
		if(cue_sheet->lead_in % 588 != 0) {
			if(violation) *violation = "CD-DA cue sheet lead-in length must be evenly divisible by 588 samples";
			return false;
		}
	}

	if(cue_sheet->num_tracks == 0) {
		if(violation) *violation = "cue sheet must have at least one track (the lead-out)";
		return false;
	}

	if(check_cd_da_subset && cue_sheet->tracks[cue_sheet->num_tracks-1].number != 170) {
		if(violation) *violation = "CD-DA cue sheet must have a lead-out track number 170 (0xAA)";
		return false;
	}

	for(i = 0; i < cue_sheet->num_tracks; i++) {
		if(cue_sheet->tracks[i].number == 0) {
			if(violation) *violation = "cue sheet may not have a track number 0";
			return false;
		}

		if(check_cd_da_subset) {
			if(!((cue_sheet->tracks[i].number >= 1 && cue_sheet->tracks[i].number <= 99) || cue_sheet->tracks[i].number == 170)) {
				if(violation) *violation = "CD-DA cue sheet track number must be 1-99 or 170";
				return false;
			}
		}

		if(check_cd_da_subset && cue_sheet->tracks[i].offset % 588 != 0) {
			if(violation) {
				if(i == cue_sheet->num_tracks-1) /* the lead-out track... */
					*violation = "CD-DA cue sheet lead-out offset must be evenly divisible by 588 samples";
				else
					*violation = "CD-DA cue sheet track offset must be evenly divisible by 588 samples";
			}
			return false;
		}

		if(i < cue_sheet->num_tracks - 1) {
			if(cue_sheet->tracks[i].num_indices == 0) {
				if(violation) *violation = "cue sheet track must have at least one index point";
				return false;
			}

			if(cue_sheet->tracks[i].indices[0].number > 1) {
				if(violation) *violation = "cue sheet track's first index number must be 0 or 1";
				return false;
			}
		}

		for(j = 0; j < cue_sheet->tracks[i].num_indices; j++) {
			if(check_cd_da_subset && cue_sheet->tracks[i].indices[j].offset % 588 != 0) {
				if(violation) *violation = "CD-DA cue sheet track index offset must be evenly divisible by 588 samples";
				return false;
			}

			if(j > 0) {
				if(cue_sheet->tracks[i].indices[j].number != cue_sheet->tracks[i].indices[j-1].number + 1) {
					if(violation) *violation = "cue sheet track index numbers must increase by 1";
					return false;
				}
			}
		}
	}

	return true;
}

/* @@@@ add to unit tests; it is already indirectly tested by the metadata_object tests */
FLAC_API FLAC__bool FLAC__format_picture_is_legal(const FLAC__StreamMetadata_Picture *picture, const char **violation)
{
	char *p;
	FLAC__byte *b;

	for(p = picture->mime_type; *p; p++) {
		if(*p < 0x20 || *p > 0x7e) {
			if(violation) *violation = "MIME type string must contain only printable ASCII characters (0x20-0x7e)";
			return false;
		}
	}

	for(b = picture->description; *b; ) {
		unsigned n = utf8len_(b);
		if(n == 0) {
			if(violation) *violation = "description string must be valid UTF-8";
			return false;
		}
		b += n;
	}

	return true;
}

/*
 * These routines are private to libFLAC
 */
unsigned FLAC__format_get_max_rice_partition_order(unsigned blocksize, unsigned predictor_order)
{
	return
		FLAC__format_get_max_rice_partition_order_from_blocksize_limited_max_and_predictor_order(
			FLAC__format_get_max_rice_partition_order_from_blocksize(blocksize),
			blocksize,
			predictor_order
		);
}

unsigned FLAC__format_get_max_rice_partition_order_from_blocksize(unsigned blocksize)
{
	unsigned max_rice_partition_order = 0;
	while(!(blocksize & 1)) {
		max_rice_partition_order++;
		blocksize >>= 1;
	}
	return min(FLAC__MAX_RICE_PARTITION_ORDER, max_rice_partition_order);
}

unsigned FLAC__format_get_max_rice_partition_order_from_blocksize_limited_max_and_predictor_order(unsigned limit, unsigned blocksize, unsigned predictor_order)
{
	unsigned max_rice_partition_order = limit;

	while(max_rice_partition_order > 0 && (blocksize >> max_rice_partition_order) <= predictor_order)
		max_rice_partition_order--;

	FLAC__ASSERT(
		(max_rice_partition_order == 0 && blocksize >= predictor_order) ||
		(max_rice_partition_order > 0 && blocksize >> max_rice_partition_order > predictor_order)
	);

	return max_rice_partition_order;
}

void FLAC__format_entropy_coding_method_partitioned_rice_contents_init(FLAC__EntropyCodingMethod_PartitionedRiceContents *object)
{
	FLAC__ASSERT(0 != object);

	object->parameters = 0;
	object->raw_bits = 0;
	object->capacity_by_order = 0;
}

void FLAC__format_entropy_coding_method_partitioned_rice_contents_clear(FLAC__EntropyCodingMethod_PartitionedRiceContents *object)
{
	FLAC__ASSERT(0 != object);

	if(0 != object->parameters)
		free(object->parameters);
	if(0 != object->raw_bits)
		free(object->raw_bits);
	FLAC__format_entropy_coding_method_partitioned_rice_contents_init(object);
}

FLAC__bool FLAC__format_entropy_coding_method_partitioned_rice_contents_ensure_size(FLAC__EntropyCodingMethod_PartitionedRiceContents *object, unsigned max_partition_order)
{
	FLAC__ASSERT(0 != object);

	FLAC__ASSERT(object->capacity_by_order > 0 || (0 == object->parameters && 0 == object->raw_bits));

	if(object->capacity_by_order < max_partition_order) {
		if(0 == (object->parameters = (unsigned*)realloc(object->parameters, sizeof(unsigned)*(1 << max_partition_order))))
			return false;
		if(0 == (object->raw_bits = (unsigned*)realloc(object->raw_bits, sizeof(unsigned)*(1 << max_partition_order))))
			return false;
		memset(object->raw_bits, 0, sizeof(unsigned)*(1 << max_partition_order));
		object->capacity_by_order = max_partition_order;
	}

	return true;
}
//++-- format.c   

//++ crc.c  

#if HAVE_CONFIG_H
#  include <config.h>
#endif

/* CRC-8, poly = x^8 + x^2 + x^1 + x^0, init = 0 */

FLAC__byte const FLAC__crc8_table[256] = {
	0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
	0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
	0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
	0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
	0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5,
	0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
	0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85,
	0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
	0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
	0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
	0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2,
	0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
	0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32,
	0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
	0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
	0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
	0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C,
	0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
	0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC,
	0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
	0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
	0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
	0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C,
	0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
	0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B,
	0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
	0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
	0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
	0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB,
	0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
	0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB,
	0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3
};

/* CRC-16, poly = x^16 + x^15 + x^2 + x^0, init = 0 */

unsigned FLAC__crc16_table[256] = {
	0x0000,  0x8005,  0x800f,  0x000a,  0x801b,  0x001e,  0x0014,  0x8011,
	0x8033,  0x0036,  0x003c,  0x8039,  0x0028,  0x802d,  0x8027,  0x0022,
	0x8063,  0x0066,  0x006c,  0x8069,  0x0078,  0x807d,  0x8077,  0x0072,
	0x0050,  0x8055,  0x805f,  0x005a,  0x804b,  0x004e,  0x0044,  0x8041,
	0x80c3,  0x00c6,  0x00cc,  0x80c9,  0x00d8,  0x80dd,  0x80d7,  0x00d2,
	0x00f0,  0x80f5,  0x80ff,  0x00fa,  0x80eb,  0x00ee,  0x00e4,  0x80e1,
	0x00a0,  0x80a5,  0x80af,  0x00aa,  0x80bb,  0x00be,  0x00b4,  0x80b1,
	0x8093,  0x0096,  0x009c,  0x8099,  0x0088,  0x808d,  0x8087,  0x0082,
	0x8183,  0x0186,  0x018c,  0x8189,  0x0198,  0x819d,  0x8197,  0x0192,
	0x01b0,  0x81b5,  0x81bf,  0x01ba,  0x81ab,  0x01ae,  0x01a4,  0x81a1,
	0x01e0,  0x81e5,  0x81ef,  0x01ea,  0x81fb,  0x01fe,  0x01f4,  0x81f1,
	0x81d3,  0x01d6,  0x01dc,  0x81d9,  0x01c8,  0x81cd,  0x81c7,  0x01c2,
	0x0140,  0x8145,  0x814f,  0x014a,  0x815b,  0x015e,  0x0154,  0x8151,
	0x8173,  0x0176,  0x017c,  0x8179,  0x0168,  0x816d,  0x8167,  0x0162,
	0x8123,  0x0126,  0x012c,  0x8129,  0x0138,  0x813d,  0x8137,  0x0132,
	0x0110,  0x8115,  0x811f,  0x011a,  0x810b,  0x010e,  0x0104,  0x8101,
	0x8303,  0x0306,  0x030c,  0x8309,  0x0318,  0x831d,  0x8317,  0x0312,
	0x0330,  0x8335,  0x833f,  0x033a,  0x832b,  0x032e,  0x0324,  0x8321,
	0x0360,  0x8365,  0x836f,  0x036a,  0x837b,  0x037e,  0x0374,  0x8371,
	0x8353,  0x0356,  0x035c,  0x8359,  0x0348,  0x834d,  0x8347,  0x0342,
	0x03c0,  0x83c5,  0x83cf,  0x03ca,  0x83db,  0x03de,  0x03d4,  0x83d1,
	0x83f3,  0x03f6,  0x03fc,  0x83f9,  0x03e8,  0x83ed,  0x83e7,  0x03e2,
	0x83a3,  0x03a6,  0x03ac,  0x83a9,  0x03b8,  0x83bd,  0x83b7,  0x03b2,
	0x0390,  0x8395,  0x839f,  0x039a,  0x838b,  0x038e,  0x0384,  0x8381,
	0x0280,  0x8285,  0x828f,  0x028a,  0x829b,  0x029e,  0x0294,  0x8291,
	0x82b3,  0x02b6,  0x02bc,  0x82b9,  0x02a8,  0x82ad,  0x82a7,  0x02a2,
	0x82e3,  0x02e6,  0x02ec,  0x82e9,  0x02f8,  0x82fd,  0x82f7,  0x02f2,
	0x02d0,  0x82d5,  0x82df,  0x02da,  0x82cb,  0x02ce,  0x02c4,  0x82c1,
	0x8243,  0x0246,  0x024c,  0x8249,  0x0258,  0x825d,  0x8257,  0x0252,
	0x0270,  0x8275,  0x827f,  0x027a,  0x826b,  0x026e,  0x0264,  0x8261,
	0x0220,  0x8225,  0x822f,  0x022a,  0x823b,  0x023e,  0x0234,  0x8231,
	0x8213,  0x0216,  0x021c,  0x8219,  0x0208,  0x820d,  0x8207,  0x0202
};


void FLAC__crc8_update(const FLAC__byte data, FLAC__uint8 *crc)
{
	*crc = FLAC__crc8_table[*crc ^ data];
}

void FLAC__crc8_update_block(const FLAC__byte *data, unsigned len, FLAC__uint8 *crc)
{
	while(len--)
		*crc = FLAC__crc8_table[*crc ^ *data++];
}

FLAC__uint8 FLAC__crc8(const FLAC__byte *data, unsigned len)
{
	FLAC__uint8 crc = 0;

	while(len--)
		crc = FLAC__crc8_table[crc ^ *data++];

	return crc;
}

unsigned FLAC__crc16(const FLAC__byte *data, unsigned len)
{
	unsigned crc = 0;

	while(len--)
		crc = ((crc<<8) ^ FLAC__crc16_table[(crc>>8) ^ *data++]) & 0xffff;

	return crc;
}
//++-- crc.c  


//++ cpu.c  

#if defined FLAC__CPU_IA32
# include <signal.h>
#elif defined FLAC__CPU_PPC
# if !defined FLAC__NO_ASM
#  if defined FLAC__SYS_DARWIN
#   include <sys/sysctl.h>
#   include <mach/mach.h>
#   include <mach/mach_host.h>
#   include <mach/host_info.h>
#   include <mach/machine.h>
#   ifndef CPU_SUBTYPE_POWERPC_970
#    define CPU_SUBTYPE_POWERPC_970 ((cpu_subtype_t) 100)
#   endif
#  else /* FLAC__SYS_DARWIN */

#   include <signal.h>
#   include <setjmp.h>

static sigjmp_buf jmpbuf;
static volatile sig_atomic_t canjump = 0;

static void sigill_handler (int sig)
{
	if (!canjump) {
		signal (sig, SIG_DFL);
		raise (sig);
	}
	canjump = 0;
	siglongjmp (jmpbuf, 1);
}
#  endif /* FLAC__SYS_DARWIN */
# endif /* FLAC__NO_ASM */
#endif /* FLAC__CPU_PPC */

#if defined (__NetBSD__) || defined(__OpenBSD__)
#include <sys/param.h>
#include <sys/sysctl.h>
#include <machine/cpu.h>
#endif

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__DragonFly__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#if defined(__APPLE__)
/* how to get sysctlbyname()? */
#endif

/* these are flags in EDX of CPUID AX=00000001 */
static const unsigned FLAC__CPUINFO_IA32_CPUID_CMOV = 0x00008000;
static const unsigned FLAC__CPUINFO_IA32_CPUID_MMX = 0x00800000;
static const unsigned FLAC__CPUINFO_IA32_CPUID_FXSR = 0x01000000;
static const unsigned FLAC__CPUINFO_IA32_CPUID_SSE = 0x02000000;
static const unsigned FLAC__CPUINFO_IA32_CPUID_SSE2 = 0x04000000;
/* these are flags in ECX of CPUID AX=00000001 */
static const unsigned FLAC__CPUINFO_IA32_CPUID_SSE3 = 0x00000001;
static const unsigned FLAC__CPUINFO_IA32_CPUID_SSSE3 = 0x00000200;
/* these are flags in EDX of CPUID AX=80000001 */
static const unsigned FLAC__CPUINFO_IA32_CPUID_EXTENDED_AMD_3DNOW = 0x80000000;
static const unsigned FLAC__CPUINFO_IA32_CPUID_EXTENDED_AMD_EXT3DNOW = 0x40000000;
static const unsigned FLAC__CPUINFO_IA32_CPUID_EXTENDED_AMD_EXTMMX = 0x00400000;


/*
 * Extra stuff needed for detection of OS support for SSE on IA-32
 */
#if defined(FLAC__CPU_IA32) && !defined FLAC__NO_ASM && defined FLAC__HAS_NASM && !defined FLAC__NO_SSE_OS && !defined FLAC__SSE_OS
# if defined(__linux__)
/*
 * If the OS doesn't support SSE, we will get here with a SIGILL.  We
 * modify the return address to jump over the offending SSE instruction
 * and also the operation following it that indicates the instruction
 * executed successfully.  In this way we use no global variables and
 * stay thread-safe.
 *
 * 3 + 3 + 6:
 *   3 bytes for "xorps xmm0,xmm0"
 *   3 bytes for estimate of how long the follwing "inc var" instruction is
 *   6 bytes extra in case our estimate is wrong
 * 12 bytes puts us in the NOP "landing zone"
 */
#  undef USE_OBSOLETE_SIGCONTEXT_FLAVOR /* #define this to use the older signal handler method */
#  ifdef USE_OBSOLETE_SIGCONTEXT_FLAVOR
	static void sigill_handler_sse_os(int signal, struct sigcontext sc)
	{
		(void)signal;
		sc.eip += 3 + 3 + 6;
	}
#  else
#   include <sys/ucontext.h>
	static void sigill_handler_sse_os(int signal, siginfo_t *si, void *uc)
	{
		(void)signal, (void)si;
		((ucontext_t*)uc)->uc_mcontext.gregs[14/*REG_EIP*/] += 3 + 3 + 6;
	}
#  endif
# elif defined(_MSC_VER)
#  include <windows.h>
#  undef USE_TRY_CATCH_FLAVOR /* #define this to use the try/catch method for catching illegal opcode exception */
#  ifdef USE_TRY_CATCH_FLAVOR
#  else
	LONG CALLBACK sigill_handler_sse_os(EXCEPTION_POINTERS *ep)
	{
		if(ep->ExceptionRecord->ExceptionCode == EXCEPTION_ILLEGAL_INSTRUCTION) {
			ep->ContextRecord->Eip += 3 + 3 + 6;
			return EXCEPTION_CONTINUE_EXECUTION;
		}
		return EXCEPTION_CONTINUE_SEARCH;
	}
#  endif
# endif
#endif


void FLAC__cpu_info(FLAC__CPUInfo *info)
{
/*
 * IA32-specific
 */
#ifdef FLAC__CPU_IA32
	info->type = FLAC__CPUINFO_TYPE_IA32;
#if !defined FLAC__NO_ASM && defined FLAC__HAS_NASM
	info->use_asm = true; /* we assume a minimum of 80386 with FLAC__CPU_IA32 */
	info->data.ia32.cpuid = FLAC__cpu_have_cpuid_asm_ia32()? true : false;
	info->data.ia32.bswap = info->data.ia32.cpuid; /* CPUID => BSWAP since it came after */
	info->data.ia32.cmov = false;
	info->data.ia32.mmx = false;
	info->data.ia32.fxsr = false;
	info->data.ia32.sse = false;
	info->data.ia32.sse2 = false;
	info->data.ia32.sse3 = false;
	info->data.ia32.ssse3 = false;
	info->data.ia32._3dnow = false;
	info->data.ia32.ext3dnow = false;
	info->data.ia32.extmmx = false;
	if(info->data.ia32.cpuid) {
		/* http://www.sandpile.org/ia32/cpuid.htm */
		FLAC__uint32 flags_edx, flags_ecx;
		FLAC__cpu_info_asm_ia32(&flags_edx, &flags_ecx);
		info->data.ia32.cmov  = (flags_edx & FLAC__CPUINFO_IA32_CPUID_CMOV )? true : false;
		info->data.ia32.mmx   = (flags_edx & FLAC__CPUINFO_IA32_CPUID_MMX  )? true : false;
		info->data.ia32.fxsr  = (flags_edx & FLAC__CPUINFO_IA32_CPUID_FXSR )? true : false;
		info->data.ia32.sse   = (flags_edx & FLAC__CPUINFO_IA32_CPUID_SSE  )? true : false;
		info->data.ia32.sse2  = (flags_edx & FLAC__CPUINFO_IA32_CPUID_SSE2 )? true : false;
		info->data.ia32.sse3  = (flags_ecx & FLAC__CPUINFO_IA32_CPUID_SSE3 )? true : false;
		info->data.ia32.ssse3 = (flags_ecx & FLAC__CPUINFO_IA32_CPUID_SSSE3)? true : false;

#ifdef FLAC__USE_3DNOW
		flags_edx = FLAC__cpu_info_extended_amd_asm_ia32();
		info->data.ia32._3dnow   = (flags_edx & FLAC__CPUINFO_IA32_CPUID_EXTENDED_AMD_3DNOW   )? true : false;
		info->data.ia32.ext3dnow = (flags_edx & FLAC__CPUINFO_IA32_CPUID_EXTENDED_AMD_EXT3DNOW)? true : false;
		info->data.ia32.extmmx   = (flags_edx & FLAC__CPUINFO_IA32_CPUID_EXTENDED_AMD_EXTMMX  )? true : false;
#else
		info->data.ia32._3dnow = info->data.ia32.ext3dnow = info->data.ia32.extmmx = false;
#endif

#ifdef DEBUG
		fprintf(stderr, "CPU info (IA-32):\n");
		fprintf(stderr, "  CPUID ...... %c\n", info->data.ia32.cpuid   ? 'Y' : 'n');
		fprintf(stderr, "  BSWAP ...... %c\n", info->data.ia32.bswap   ? 'Y' : 'n');
		fprintf(stderr, "  CMOV ....... %c\n", info->data.ia32.cmov    ? 'Y' : 'n');
		fprintf(stderr, "  MMX ........ %c\n", info->data.ia32.mmx     ? 'Y' : 'n');
		fprintf(stderr, "  FXSR ....... %c\n", info->data.ia32.fxsr    ? 'Y' : 'n');
		fprintf(stderr, "  SSE ........ %c\n", info->data.ia32.sse     ? 'Y' : 'n');
		fprintf(stderr, "  SSE2 ....... %c\n", info->data.ia32.sse2    ? 'Y' : 'n');
		fprintf(stderr, "  SSE3 ....... %c\n", info->data.ia32.sse3    ? 'Y' : 'n');
		fprintf(stderr, "  SSSE3 ...... %c\n", info->data.ia32.ssse3   ? 'Y' : 'n');
		fprintf(stderr, "  3DNow! ..... %c\n", info->data.ia32._3dnow  ? 'Y' : 'n');
		fprintf(stderr, "  3DNow!-ext . %c\n", info->data.ia32.ext3dnow? 'Y' : 'n');
		fprintf(stderr, "  3DNow!-MMX . %c\n", info->data.ia32.extmmx  ? 'Y' : 'n');
#endif

		/*
		 * now have to check for OS support of SSE/SSE2
		 */
		if(info->data.ia32.fxsr || info->data.ia32.sse || info->data.ia32.sse2) {
#if defined FLAC__NO_SSE_OS
			/* assume user knows better than us; turn it off */
			info->data.ia32.fxsr = info->data.ia32.sse = info->data.ia32.sse2 = info->data.ia32.sse3 = info->data.ia32.ssse3 = false;
#elif defined FLAC__SSE_OS
			/* assume user knows better than us; leave as detected above */
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__DragonFly__) || defined(__APPLE__)
			int sse = 0;
			size_t len;
			/* at least one of these must work: */
			len = sizeof(sse); sse = sse || (sysctlbyname("hw.instruction_sse", &sse, &len, NULL, 0) == 0 && sse);
			len = sizeof(sse); sse = sse || (sysctlbyname("hw.optional.sse"   , &sse, &len, NULL, 0) == 0 && sse); /* __APPLE__ ? */
			if(!sse)
				info->data.ia32.fxsr = info->data.ia32.sse = info->data.ia32.sse2 = info->data.ia32.sse3 = info->data.ia32.ssse3 = false;
#elif defined(__NetBSD__) || defined (__OpenBSD__)
# if __NetBSD_Version__ >= 105250000 || (defined __OpenBSD__)
			int val = 0, mib[2] = { CTL_MACHDEP, CPU_SSE };
			size_t len = sizeof(val);
			if(sysctl(mib, 2, &val, &len, NULL, 0) < 0 || !val)
				info->data.ia32.fxsr = info->data.ia32.sse = info->data.ia32.sse2 = info->data.ia32.sse3 = info->data.ia32.ssse3 = false;
			else { /* double-check SSE2 */
				mib[1] = CPU_SSE2;
				len = sizeof(val);
				if(sysctl(mib, 2, &val, &len, NULL, 0) < 0 || !val)
					info->data.ia32.sse2 = info->data.ia32.sse3 = info->data.ia32.ssse3 = false;
			}
# else
			info->data.ia32.fxsr = info->data.ia32.sse = info->data.ia32.sse2 = info->data.ia32.sse3 = info->data.ia32.ssse3 = false;
# endif
#elif defined(__linux__)
			int sse = 0;
			struct sigaction sigill_save;
#ifdef USE_OBSOLETE_SIGCONTEXT_FLAVOR
			if(0 == sigaction(SIGILL, NULL, &sigill_save) && signal(SIGILL, (void (*)(int))sigill_handler_sse_os) != SIG_ERR)
#else
			struct sigaction sigill_sse;
			sigill_sse.sa_sigaction = sigill_handler_sse_os;
			__sigemptyset(&sigill_sse.sa_mask);
			sigill_sse.sa_flags = SA_SIGINFO | SA_RESETHAND; /* SA_RESETHAND just in case our SIGILL return jump breaks, so we don't get stuck in a loop */
			if(0 == sigaction(SIGILL, &sigill_sse, &sigill_save))
#endif
			{
				/* http://www.ibiblio.org/gferg/ldp/GCC-Inline-Assembly-HOWTO.html */
				/* see sigill_handler_sse_os() for an explanation of the following: */
				asm volatile (
					"xorl %0,%0\n\t"          /* for some reason, still need to do this to clear 'sse' var */
					"xorps %%xmm0,%%xmm0\n\t" /* will cause SIGILL if unsupported by OS */
					"incl %0\n\t"             /* SIGILL handler will jump over this */
					/* landing zone */
					"nop\n\t" /* SIGILL jump lands here if "inc" is 9 bytes */
					"nop\n\t"
					"nop\n\t"
					"nop\n\t"
					"nop\n\t"
					"nop\n\t"
					"nop\n\t" /* SIGILL jump lands here if "inc" is 3 bytes (expected) */
					"nop\n\t"
					"nop"     /* SIGILL jump lands here if "inc" is 1 byte */
					: "=r"(sse)
					: "r"(sse)
				);

				sigaction(SIGILL, &sigill_save, NULL);
			}

			if(!sse)
				info->data.ia32.fxsr = info->data.ia32.sse = info->data.ia32.sse2 = info->data.ia32.sse3 = info->data.ia32.ssse3 = false;
#elif defined(_MSC_VER)
# ifdef USE_TRY_CATCH_FLAVOR
			_try {
				__asm {
#  if _MSC_VER <= 1200
					/* VC6 assembler doesn't know SSE, have to emit bytecode instead */
					_emit 0x0F
					_emit 0x57
					_emit 0xC0
#  else
					xorps xmm0,xmm0
#  endif
				}
			}
			_except(EXCEPTION_EXECUTE_HANDLER) {
				if (_exception_code() == STATUS_ILLEGAL_INSTRUCTION)
					info->data.ia32.fxsr = info->data.ia32.sse = info->data.ia32.sse2 = info->data.ia32.sse3 = info->data.ia32.ssse3 = false;
			}
# else
			int sse = 0;
			LPTOP_LEVEL_EXCEPTION_FILTER save = SetUnhandledExceptionFilter(sigill_handler_sse_os);
			/* see GCC version above for explanation */
			/*  http://msdn2.microsoft.com/en-us/library/4ks26t93.aspx */
			/*  http://www.codeproject.com/cpp/gccasm.asp */
			/*  http://www.hick.org/~mmiller/msvc_inline_asm.html */
			__asm {
#  if _MSC_VER <= 1200
				/* VC6 assembler doesn't know SSE, have to emit bytecode instead */
				_emit 0x0F
				_emit 0x57
				_emit 0xC0
#  else
				xorps xmm0,xmm0
#  endif
				inc sse
				nop
				nop
				nop
				nop
				nop
				nop
				nop
				nop
				nop
			}
			SetUnhandledExceptionFilter(save);
			if(!sse)
				info->data.ia32.fxsr = info->data.ia32.sse = info->data.ia32.sse2 = info->data.ia32.sse3 = info->data.ia32.ssse3 = false;
# endif
#else
			/* no way to test, disable to be safe */
			info->data.ia32.fxsr = info->data.ia32.sse = info->data.ia32.sse2 = info->data.ia32.sse3 = info->data.ia32.ssse3 = false;
#endif
#ifdef DEBUG
		fprintf(stderr, "  SSE OS sup . %c\n", info->data.ia32.sse     ? 'Y' : 'n');
#endif

		}
	}
#else
	info->use_asm = false;
#endif

/*
 * PPC-specific
 */
#elif defined FLAC__CPU_PPC
	info->type = FLAC__CPUINFO_TYPE_PPC;
# if !defined FLAC__NO_ASM
	info->use_asm = true;
#  ifdef FLAC__USE_ALTIVEC
#   if defined FLAC__SYS_DARWIN
	{
		int val = 0, mib[2] = { CTL_HW, HW_VECTORUNIT };
		size_t len = sizeof(val);
		info->data.ppc.altivec = !(sysctl(mib, 2, &val, &len, NULL, 0) || !val);
	}
	{
		host_basic_info_data_t hostInfo;
		mach_msg_type_number_t infoCount;

		infoCount = HOST_BASIC_INFO_COUNT;
		host_info(mach_host_self(), HOST_BASIC_INFO, (host_info_t)&hostInfo, &infoCount);

		info->data.ppc.ppc64 = (hostInfo.cpu_type == CPU_TYPE_POWERPC) && (hostInfo.cpu_subtype == CPU_SUBTYPE_POWERPC_970);
	}
#   else /* FLAC__USE_ALTIVEC && !FLAC__SYS_DARWIN */
	{
		/* no Darwin, do it the brute-force way */
		/* @@@@@@ this is not thread-safe; replace with SSE OS method above or remove */
		info->data.ppc.altivec = 0;
		info->data.ppc.ppc64 = 0;

		signal (SIGILL, sigill_handler);
		canjump = 0;
		if (!sigsetjmp (jmpbuf, 1)) {
			canjump = 1;

			asm volatile (
				"mtspr 256, %0\n\t"
				"vand %%v0, %%v0, %%v0"
				:
				: "r" (-1)
			);

			info->data.ppc.altivec = 1;
		}
		canjump = 0;
		if (!sigsetjmp (jmpbuf, 1)) {
			int x = 0;
			canjump = 1;

			/* PPC64 hardware implements the cntlzd instruction */
			asm volatile ("cntlzd %0, %1" : "=r" (x) : "r" (x) );

			info->data.ppc.ppc64 = 1;
		}
		signal (SIGILL, SIG_DFL); /*@@@@@@ should save and restore old signal */
	}
#   endif
#  else /* !FLAC__USE_ALTIVEC */
	info->data.ppc.altivec = 0;
	info->data.ppc.ppc64 = 0;
#  endif
# else
	info->use_asm = false;
# endif

/*
 * unknown CPI
 */
#else
	info->type = FLAC__CPUINFO_TYPE_UNKNOWN;
	info->use_asm = false;
#endif
}
//++-- cpu.c  


//++ bitwriter.c 
// 
// #if HAVE_CONFIG_H
// #  include <config.h>
// #endif
// 
// #include <stdlib.h> /* for malloc() */
// #include <string.h> /* for memcpy(), memset() */
#ifdef _MSC_VER
#include <winsock.h> /* for ntohl() */
#elif defined FLAC__SYS_DARWIN
#include <machine/endian.h> /* for ntohl() */
#elif defined __MINGW32__
#include <winsock.h> /* for ntohl() */
#else
#include <netinet/in.h> /* for ntohl() */
#endif

/* Things should be fastest when this matches the machine word size */
/* WATCHOUT: if you change this you must also change the following #defines down to SWAP_BE_WORD_TO_HOST below to match */
/* WATCHOUT: there are a few places where the code will not work unless bwword is >= 32 bits wide */
typedef FLAC__uint32 bwword;
#define FLAC__BYTES_PER_WORD 4
#define FLAC__BITS_PER_WORD 32
#define FLAC__WORD_ALL_ONES ((FLAC__uint32)0xffffffff)
/* SWAP_BE_WORD_TO_HOST swaps bytes in a bwword (which is always big-endian) if necessary to match host byte order */
#if WORDS_BIGENDIAN
#define SWAP_BE_WORD_TO_HOST(x) (x)
#else
#ifdef _MSC_VER
#define SWAP_BE_WORD_TO_HOST(x) local_swap32_(x)
#else
#define SWAP_BE_WORD_TO_HOST(x) ntohl(x)
#endif
#endif

/*
 * The default capacity here doesn't matter too much.  The buffer always grows
 * to hold whatever is written to it.  Usually the encoder will stop adding at
 * a frame or metadata block, then write that out and clear the buffer for the
 * next one.
 */
static const unsigned FLAC__BITWRITER_DEFAULT_CAPACITY = 32768u / sizeof(bwword); /* size in words */
/* When growing, increment 4K at a time */
static const unsigned FLAC__BITWRITER_DEFAULT_INCREMENT = 4096u / sizeof(bwword); /* size in words */

#define FLAC__WORDS_TO_BITS(words) ((words) * FLAC__BITS_PER_WORD)
#define FLAC__TOTAL_BITS(bw) (FLAC__WORDS_TO_BITS((bw)->words) + (bw)->bits)

#ifdef min
#undef min
#endif
#define min(x,y) ((x)<(y)?(x):(y))

/* adjust for compilers that can't understand using LLU suffix for uint64_t literals */
#ifdef _MSC_VER
#define FLAC__U64L(x) x
#else
#define FLAC__U64L(x) x##LLU
#endif

#ifndef FLaC__INLINE
#define FLaC__INLINE
#endif

struct FLAC__BitWriter {
	bwword *buffer;
	bwword accum; /* accumulator; bits are right-justified; when full, accum is appended to buffer */
	unsigned capacity; /* capacity of buffer in words */
	unsigned words; /* # of complete words in buffer */
	unsigned bits; /* # of used bits in accum */
};

#ifdef _MSC_VER
/* OPT: an MSVC built-in would be better */
static _inline FLAC__uint32 local_swap32_(FLAC__uint32 x)
{
	x = ((x<<8)&0xFF00FF00) | ((x>>8)&0x00FF00FF);
	return (x>>16) | (x<<16);
}
#endif

/* * WATCHOUT: The current implementation only grows the buffer. */
static FLAC__bool bitwriter_grow_(FLAC__BitWriter *bw, unsigned bits_to_add)
{
	unsigned new_capacity;
	bwword *new_buffer;

	FLAC__ASSERT(0 != bw);
	FLAC__ASSERT(0 != bw->buffer);

	/* calculate total words needed to store 'bits_to_add' additional bits */
	new_capacity = bw->words + ((bw->bits + bits_to_add + FLAC__BITS_PER_WORD - 1) / FLAC__BITS_PER_WORD);

	/* it's possible (due to pessimism in the growth estimation that
	 * leads to this call) that we don't actually need to grow
	 */
	if(bw->capacity >= new_capacity)
		return true;

	/* round up capacity increase to the nearest FLAC__BITWRITER_DEFAULT_INCREMENT */
	if((new_capacity - bw->capacity) % FLAC__BITWRITER_DEFAULT_INCREMENT)
		new_capacity += FLAC__BITWRITER_DEFAULT_INCREMENT - ((new_capacity - bw->capacity) % FLAC__BITWRITER_DEFAULT_INCREMENT);
	/* make sure we got everything right */
	FLAC__ASSERT(0 == (new_capacity - bw->capacity) % FLAC__BITWRITER_DEFAULT_INCREMENT);
	FLAC__ASSERT(new_capacity > bw->capacity);
	FLAC__ASSERT(new_capacity >= bw->words + ((bw->bits + bits_to_add + FLAC__BITS_PER_WORD - 1) / FLAC__BITS_PER_WORD));

	new_buffer = (bwword*)safe_realloc_mul_2op_(bw->buffer, sizeof(bwword), /*times*/new_capacity);
	if(new_buffer == 0)
		return false;
	bw->buffer = new_buffer;
	bw->capacity = new_capacity;
	return true;
}


/***********************************************************************
 *
 * Class constructor/destructor
 *
 ***********************************************************************/

FLAC__BitWriter *FLAC__bitwriter_new(void)
{
	FLAC__BitWriter *bw = (FLAC__BitWriter*)calloc(1, sizeof(FLAC__BitWriter));
	/* note that calloc() sets all members to 0 for us */
	return bw;
}

void FLAC__bitwriter_delete(FLAC__BitWriter *bw)
{
	FLAC__ASSERT(0 != bw);

	FLAC__bitwriter_free(bw);
	free(bw);
}

/***********************************************************************
 *
 * Public class methods
 *
 ***********************************************************************/

FLAC__bool FLAC__bitwriter_init(FLAC__BitWriter *bw)
{
	FLAC__ASSERT(0 != bw);

	bw->words = bw->bits = 0;
	bw->capacity = FLAC__BITWRITER_DEFAULT_CAPACITY;
	bw->buffer = (bwword*)malloc(sizeof(bwword) * bw->capacity);
	if(bw->buffer == 0)
		return false;

	return true;
}

void FLAC__bitwriter_free(FLAC__BitWriter *bw)
{
	FLAC__ASSERT(0 != bw);

	if(0 != bw->buffer)
		free(bw->buffer);
	bw->buffer = 0;
	bw->capacity = 0;
	bw->words = bw->bits = 0;
}

void FLAC__bitwriter_clear(FLAC__BitWriter *bw)
{
	bw->words = bw->bits = 0;
}

void FLAC__bitwriter_dump(const FLAC__BitWriter *bw, FILE *out)
{
	unsigned i, j;
	if(bw == 0) {
		fprintf(out, "bitwriter is NULL\n");
	}
	else {
		fprintf(out, "bitwriter: capacity=%u words=%u bits=%u total_bits=%u\n", bw->capacity, bw->words, bw->bits, FLAC__TOTAL_BITS(bw));

		for(i = 0; i < bw->words; i++) {
			fprintf(out, "%08X: ", i);
			for(j = 0; j < FLAC__BITS_PER_WORD; j++)
				fprintf(out, "%01u", bw->buffer[i] & (1 << (FLAC__BITS_PER_WORD-j-1)) ? 1:0);
			fprintf(out, "\n");
		}
		if(bw->bits > 0) {
			fprintf(out, "%08X: ", i);
			for(j = 0; j < bw->bits; j++)
				fprintf(out, "%01u", bw->accum & (1 << (bw->bits-j-1)) ? 1:0);
			fprintf(out, "\n");
		}
	}
}

FLAC__bool FLAC__bitwriter_get_write_crc16(FLAC__BitWriter *bw, FLAC__uint16 *crc)
{
	const FLAC__byte *buffer;
	size_t bytes;

	FLAC__ASSERT((bw->bits & 7) == 0); /* assert that we're byte-aligned */

	if(!FLAC__bitwriter_get_buffer(bw, &buffer, &bytes))
		return false;

	*crc = (FLAC__uint16)FLAC__crc16(buffer, bytes);
	FLAC__bitwriter_release_buffer(bw);
	return true;
}

FLAC__bool FLAC__bitwriter_get_write_crc8(FLAC__BitWriter *bw, FLAC__byte *crc)
{
	const FLAC__byte *buffer;
	size_t bytes;

	FLAC__ASSERT((bw->bits & 7) == 0); /* assert that we're byte-aligned */

	if(!FLAC__bitwriter_get_buffer(bw, &buffer, &bytes))
		return false;

	*crc = FLAC__crc8(buffer, bytes);
	FLAC__bitwriter_release_buffer(bw);
	return true;
}

FLAC__bool FLAC__bitwriter_is_byte_aligned(const FLAC__BitWriter *bw)
{
	return ((bw->bits & 7) == 0);
}

unsigned FLAC__bitwriter_get_input_bits_unconsumed(const FLAC__BitWriter *bw)
{
	return FLAC__TOTAL_BITS(bw);
}

FLAC__bool FLAC__bitwriter_get_buffer(FLAC__BitWriter *bw, const FLAC__byte **buffer, size_t *bytes)
{
	FLAC__ASSERT((bw->bits & 7) == 0);
	/* double protection */
	if(bw->bits & 7)
		return false;
	/* if we have bits in the accumulator we have to flush those to the buffer first */
	if(bw->bits) {
		FLAC__ASSERT(bw->words <= bw->capacity);
		if(bw->words == bw->capacity && !bitwriter_grow_(bw, FLAC__BITS_PER_WORD))
			return false;
		/* append bits as complete word to buffer, but don't change bw->accum or bw->bits */
		bw->buffer[bw->words] = SWAP_BE_WORD_TO_HOST(bw->accum << (FLAC__BITS_PER_WORD-bw->bits));
	}
	/* now we can just return what we have */
	*buffer = (FLAC__byte*)bw->buffer;
	*bytes = (FLAC__BYTES_PER_WORD * bw->words) + (bw->bits >> 3);
	return true;
}

void FLAC__bitwriter_release_buffer(FLAC__BitWriter *bw)
{
	/* nothing to do.  in the future, strict checking of a 'writer-is-in-
	 * get-mode' flag could be added everywhere and then cleared here
	 */
	(void)bw;
}

FLaC__INLINE FLAC__bool FLAC__bitwriter_write_zeroes(FLAC__BitWriter *bw, unsigned bits)
{
	unsigned n;

	FLAC__ASSERT(0 != bw);
	FLAC__ASSERT(0 != bw->buffer);

	if(bits == 0)
		return true;
	/* slightly pessimistic size check but faster than "<= bw->words + (bw->bits+bits+FLAC__BITS_PER_WORD-1)/FLAC__BITS_PER_WORD" */
	if(bw->capacity <= bw->words + bits && !bitwriter_grow_(bw, bits))
		return false;
	/* first part gets to word alignment */
	if(bw->bits) {
		n = min(FLAC__BITS_PER_WORD - bw->bits, bits);
		bw->accum <<= n;
		bits -= n;
		bw->bits += n;
		if(bw->bits == FLAC__BITS_PER_WORD) {
			bw->buffer[bw->words++] = SWAP_BE_WORD_TO_HOST(bw->accum);
			bw->bits = 0;
		}
		else
			return true;
	}
	/* do whole words */
	while(bits >= FLAC__BITS_PER_WORD) {
		bw->buffer[bw->words++] = 0;
		bits -= FLAC__BITS_PER_WORD;
	}
	/* do any leftovers */
	if(bits > 0) {
		bw->accum = 0;
		bw->bits = bits;
	}
	return true;
}

FLaC__INLINE FLAC__bool FLAC__bitwriter_write_raw_uint32(FLAC__BitWriter *bw, FLAC__uint32 val, unsigned bits)
{
	register unsigned left;

	/* WATCHOUT: code does not work with <32bit words; we can make things much faster with this assertion */
	FLAC__ASSERT(FLAC__BITS_PER_WORD >= 32);

	FLAC__ASSERT(0 != bw);
	FLAC__ASSERT(0 != bw->buffer);

	FLAC__ASSERT(bits <= 32);
	if(bits == 0)
		return true;

	/* slightly pessimistic size check but faster than "<= bw->words + (bw->bits+bits+FLAC__BITS_PER_WORD-1)/FLAC__BITS_PER_WORD" */
	if(bw->capacity <= bw->words + bits && !bitwriter_grow_(bw, bits))
		return false;

	left = FLAC__BITS_PER_WORD - bw->bits;
	if(bits < left) {
		bw->accum <<= bits;
		bw->accum |= val;
		bw->bits += bits;
	}
	else if(bw->bits) { /* WATCHOUT: if bw->bits == 0, left==FLAC__BITS_PER_WORD and bw->accum<<=left is a NOP instead of setting to 0 */
		bw->accum <<= left;
		bw->accum |= val >> (bw->bits = bits - left);
		bw->buffer[bw->words++] = SWAP_BE_WORD_TO_HOST(bw->accum);
		bw->accum = val;
	}
	else {
		bw->accum = val;
		bw->bits = 0;
		bw->buffer[bw->words++] = SWAP_BE_WORD_TO_HOST(val);
	}

	return true;
}

FLaC__INLINE FLAC__bool FLAC__bitwriter_write_raw_int32(FLAC__BitWriter *bw, FLAC__int32 val, unsigned bits)
{
	/* zero-out unused bits */
	if(bits < 32)
		val &= (~(0xffffffff << bits));

	return FLAC__bitwriter_write_raw_uint32(bw, (FLAC__uint32)val, bits);
}

FLaC__INLINE FLAC__bool FLAC__bitwriter_write_raw_uint64(FLAC__BitWriter *bw, FLAC__uint64 val, unsigned bits)
{
	/* this could be a little faster but it's not used for much */
	if(bits > 32) {
		return
			FLAC__bitwriter_write_raw_uint32(bw, (FLAC__uint32)(val>>32), bits-32) &&
			FLAC__bitwriter_write_raw_uint32(bw, (FLAC__uint32)val, 32);
	}
	else
		return FLAC__bitwriter_write_raw_uint32(bw, (FLAC__uint32)val, bits);
}

FLaC__INLINE FLAC__bool FLAC__bitwriter_write_raw_uint32_little_endian(FLAC__BitWriter *bw, FLAC__uint32 val)
{
	/* this doesn't need to be that fast as currently it is only used for vorbis comments */

	if(!FLAC__bitwriter_write_raw_uint32(bw, val & 0xff, 8))
		return false;
	if(!FLAC__bitwriter_write_raw_uint32(bw, (val>>8) & 0xff, 8))
		return false;
	if(!FLAC__bitwriter_write_raw_uint32(bw, (val>>16) & 0xff, 8))
		return false;
	if(!FLAC__bitwriter_write_raw_uint32(bw, val>>24, 8))
		return false;

	return true;
}

FLaC__INLINE FLAC__bool FLAC__bitwriter_write_byte_block(FLAC__BitWriter *bw, const FLAC__byte vals[], unsigned nvals)
{
	unsigned i;

	/* this could be faster but currently we don't need it to be since it's only used for writing metadata */
	for(i = 0; i < nvals; i++) {
		if(!FLAC__bitwriter_write_raw_uint32(bw, (FLAC__uint32)(vals[i]), 8))
			return false;
	}

	return true;
}

FLAC__bool FLAC__bitwriter_write_unary_unsigned(FLAC__BitWriter *bw, unsigned val)
{
	if(val < 32)
		return FLAC__bitwriter_write_raw_uint32(bw, 1, ++val);
	else
		return
			FLAC__bitwriter_write_zeroes(bw, val) &&
			FLAC__bitwriter_write_raw_uint32(bw, 1, 1);
}

unsigned FLAC__bitwriter_rice_bits(FLAC__int32 val, unsigned parameter)
{
	FLAC__uint32 uval;

	FLAC__ASSERT(parameter < sizeof(unsigned)*8);

	/* fold signed to unsigned; actual formula is: negative(v)? -2v-1 : 2v */
	uval = (val<<1) ^ (val>>31);

	return 1 + parameter + (uval >> parameter);
}

#if 0 /* UNUSED */
unsigned FLAC__bitwriter_golomb_bits_signed(int val, unsigned parameter)
{
	unsigned bits, msbs, uval;
	unsigned k;

	FLAC__ASSERT(parameter > 0);

	/* fold signed to unsigned */
	if(val < 0)
		uval = (unsigned)(((-(++val)) << 1) + 1);
	else
		uval = (unsigned)(val << 1);

	k = FLAC__bitmath_ilog2(parameter);
	if(parameter == 1u<<k) {
		FLAC__ASSERT(k <= 30);

		msbs = uval >> k;
		bits = 1 + k + msbs;
	}
	else {
		unsigned q, r, d;

		d = (1 << (k+1)) - parameter;
		q = uval / parameter;
		r = uval - (q * parameter);

		bits = 1 + q + k;
		if(r >= d)
			bits++;
	}
	return bits;
}

unsigned FLAC__bitwriter_golomb_bits_unsigned(unsigned uval, unsigned parameter)
{
	unsigned bits, msbs;
	unsigned k;

	FLAC__ASSERT(parameter > 0);

	k = FLAC__bitmath_ilog2(parameter);
	if(parameter == 1u<<k) {
		FLAC__ASSERT(k <= 30);

		msbs = uval >> k;
		bits = 1 + k + msbs;
	}
	else {
		unsigned q, r, d;

		d = (1 << (k+1)) - parameter;
		q = uval / parameter;
		r = uval - (q * parameter);

		bits = 1 + q + k;
		if(r >= d)
			bits++;
	}
	return bits;
}
#endif /* UNUSED */

FLAC__bool FLAC__bitwriter_write_rice_signed(FLAC__BitWriter *bw, FLAC__int32 val, unsigned parameter)
{
	unsigned total_bits, interesting_bits, msbs;
	FLAC__uint32 uval, pattern;

	FLAC__ASSERT(0 != bw);
	FLAC__ASSERT(0 != bw->buffer);
	FLAC__ASSERT(parameter < 8*sizeof(uval));

	/* fold signed to unsigned; actual formula is: negative(v)? -2v-1 : 2v */
	uval = (val<<1) ^ (val>>31);

	msbs = uval >> parameter;
	interesting_bits = 1 + parameter;
	total_bits = interesting_bits + msbs;
	pattern = 1 << parameter; /* the unary end bit */
	pattern |= (uval & ((1<<parameter)-1)); /* the binary LSBs */

	if(total_bits <= 32)
		return FLAC__bitwriter_write_raw_uint32(bw, pattern, total_bits);
	else
		return
			FLAC__bitwriter_write_zeroes(bw, msbs) && /* write the unary MSBs */
			FLAC__bitwriter_write_raw_uint32(bw, pattern, interesting_bits); /* write the unary end bit and binary LSBs */
}

FLAC__bool FLAC__bitwriter_write_rice_signed_block(FLAC__BitWriter *bw, const FLAC__int32 *vals, unsigned nvals, unsigned parameter)
{
	const FLAC__uint32 mask1 = FLAC__WORD_ALL_ONES << parameter; /* we val|=mask1 to set the stop bit above it... */
	const FLAC__uint32 mask2 = FLAC__WORD_ALL_ONES >> (31-parameter); /* ...then mask off the bits above the stop bit with val&=mask2*/
	FLAC__uint32 uval;
	unsigned left;
	const unsigned lsbits = 1 + parameter;
	unsigned msbits;

	FLAC__ASSERT(0 != bw);
	FLAC__ASSERT(0 != bw->buffer);
	FLAC__ASSERT(parameter < 8*sizeof(bwword)-1);
	/* WATCHOUT: code does not work with <32bit words; we can make things much faster with this assertion */
	FLAC__ASSERT(FLAC__BITS_PER_WORD >= 32);

	while(nvals) {
		/* fold signed to unsigned; actual formula is: negative(v)? -2v-1 : 2v */
		uval = (*vals<<1) ^ (*vals>>31);

		msbits = uval >> parameter;

#if 0 /* OPT: can remove this special case if it doesn't make up for the extra compare (doesn't make a statistically significant difference with msvc or gcc/x86) */
		if(bw->bits && bw->bits + msbits + lsbits <= FLAC__BITS_PER_WORD) { /* i.e. if the whole thing fits in the current bwword */
			/* ^^^ if bw->bits is 0 then we may have filled the buffer and have no free bwword to work in */
			bw->bits = bw->bits + msbits + lsbits;
			uval |= mask1; /* set stop bit */
			uval &= mask2; /* mask off unused top bits */
			/* NOT: bw->accum <<= msbits + lsbits because msbits+lsbits could be 32, then the shift would be a NOP */
			bw->accum <<= msbits;
			bw->accum <<= lsbits;
			bw->accum |= uval;
			if(bw->bits == FLAC__BITS_PER_WORD) {
				bw->buffer[bw->words++] = SWAP_BE_WORD_TO_HOST(bw->accum);
				bw->bits = 0;
				/* burying the capacity check down here means we have to grow the buffer a little if there are more vals to do */
				if(bw->capacity <= bw->words && nvals > 1 && !bitwriter_grow_(bw, 1)) {
					FLAC__ASSERT(bw->capacity == bw->words);
					return false;
				}
			}
		}
		else {
#elif 1 /*@@@@@@ OPT: try this version with MSVC6 to see if better, not much difference for gcc-4 */
		if(bw->bits && bw->bits + msbits + lsbits < FLAC__BITS_PER_WORD) { /* i.e. if the whole thing fits in the current bwword */
			/* ^^^ if bw->bits is 0 then we may have filled the buffer and have no free bwword to work in */
			bw->bits = bw->bits + msbits + lsbits;
			uval |= mask1; /* set stop bit */
			uval &= mask2; /* mask off unused top bits */
			bw->accum <<= msbits + lsbits;
			bw->accum |= uval;
		}
		else {
#endif
			/* slightly pessimistic size check but faster than "<= bw->words + (bw->bits+msbits+lsbits+FLAC__BITS_PER_WORD-1)/FLAC__BITS_PER_WORD" */
			/* OPT: pessimism may cause flurry of false calls to grow_ which eat up all savings before it */
			if(bw->capacity <= bw->words + bw->bits + msbits + 1/*lsbits always fit in 1 bwword*/ && !bitwriter_grow_(bw, msbits+lsbits))
				return false;

			if(msbits) {
				/* first part gets to word alignment */
				if(bw->bits) {
					left = FLAC__BITS_PER_WORD - bw->bits;
					if(msbits < left) {
						bw->accum <<= msbits;
						bw->bits += msbits;
						goto break1;
					}
					else {
						bw->accum <<= left;
						msbits -= left;
						bw->buffer[bw->words++] = SWAP_BE_WORD_TO_HOST(bw->accum);
						bw->bits = 0;
					}
				}
				/* do whole words */
				while(msbits >= FLAC__BITS_PER_WORD) {
					bw->buffer[bw->words++] = 0;
					msbits -= FLAC__BITS_PER_WORD;
				}
				/* do any leftovers */
				if(msbits > 0) {
					bw->accum = 0;
					bw->bits = msbits;
				}
			}
break1:
			uval |= mask1; /* set stop bit */
			uval &= mask2; /* mask off unused top bits */

			left = FLAC__BITS_PER_WORD - bw->bits;
			if(lsbits < left) {
				bw->accum <<= lsbits;
				bw->accum |= uval;
				bw->bits += lsbits;
			}
			else {
				/* if bw->bits == 0, left==FLAC__BITS_PER_WORD which will always
				 * be > lsbits (because of previous assertions) so it would have
				 * triggered the (lsbits<left) case above.
				 */
				FLAC__ASSERT(bw->bits);
				FLAC__ASSERT(left < FLAC__BITS_PER_WORD);
				bw->accum <<= left;
				bw->accum |= uval >> (bw->bits = lsbits - left);
				bw->buffer[bw->words++] = SWAP_BE_WORD_TO_HOST(bw->accum);
				bw->accum = uval;
			}
#if 1
		}
#endif
		vals++;
		nvals--;
	}
	return true;
}

#if 0 /* UNUSED */
FLAC__bool FLAC__bitwriter_write_golomb_signed(FLAC__BitWriter *bw, int val, unsigned parameter)
{
	unsigned total_bits, msbs, uval;
	unsigned k;

	FLAC__ASSERT(0 != bw);
	FLAC__ASSERT(0 != bw->buffer);
	FLAC__ASSERT(parameter > 0);

	/* fold signed to unsigned */
	if(val < 0)
		uval = (unsigned)(((-(++val)) << 1) + 1);
	else
		uval = (unsigned)(val << 1);

	k = FLAC__bitmath_ilog2(parameter);
	if(parameter == 1u<<k) {
		unsigned pattern;

		FLAC__ASSERT(k <= 30);

		msbs = uval >> k;
		total_bits = 1 + k + msbs;
		pattern = 1 << k; /* the unary end bit */
		pattern |= (uval & ((1u<<k)-1)); /* the binary LSBs */

		if(total_bits <= 32) {
			if(!FLAC__bitwriter_write_raw_uint32(bw, pattern, total_bits))
				return false;
		}
		else {
			/* write the unary MSBs */
			if(!FLAC__bitwriter_write_zeroes(bw, msbs))
				return false;
			/* write the unary end bit and binary LSBs */
			if(!FLAC__bitwriter_write_raw_uint32(bw, pattern, k+1))
				return false;
		}
	}
	else {
		unsigned q, r, d;

		d = (1 << (k+1)) - parameter;
		q = uval / parameter;
		r = uval - (q * parameter);
		/* write the unary MSBs */
		if(!FLAC__bitwriter_write_zeroes(bw, q))
			return false;
		/* write the unary end bit */
		if(!FLAC__bitwriter_write_raw_uint32(bw, 1, 1))
			return false;
		/* write the binary LSBs */
		if(r >= d) {
			if(!FLAC__bitwriter_write_raw_uint32(bw, r+d, k+1))
				return false;
		}
		else {
			if(!FLAC__bitwriter_write_raw_uint32(bw, r, k))
				return false;
		}
	}
	return true;
}

FLAC__bool FLAC__bitwriter_write_golomb_unsigned(FLAC__BitWriter *bw, unsigned uval, unsigned parameter)
{
	unsigned total_bits, msbs;
	unsigned k;

	FLAC__ASSERT(0 != bw);
	FLAC__ASSERT(0 != bw->buffer);
	FLAC__ASSERT(parameter > 0);

	k = FLAC__bitmath_ilog2(parameter);
	if(parameter == 1u<<k) {
		unsigned pattern;

		FLAC__ASSERT(k <= 30);

		msbs = uval >> k;
		total_bits = 1 + k + msbs;
		pattern = 1 << k; /* the unary end bit */
		pattern |= (uval & ((1u<<k)-1)); /* the binary LSBs */

		if(total_bits <= 32) {
			if(!FLAC__bitwriter_write_raw_uint32(bw, pattern, total_bits))
				return false;
		}
		else {
			/* write the unary MSBs */
			if(!FLAC__bitwriter_write_zeroes(bw, msbs))
				return false;
			/* write the unary end bit and binary LSBs */
			if(!FLAC__bitwriter_write_raw_uint32(bw, pattern, k+1))
				return false;
		}
	}
	else {
		unsigned q, r, d;

		d = (1 << (k+1)) - parameter;
		q = uval / parameter;
		r = uval - (q * parameter);
		/* write the unary MSBs */
		if(!FLAC__bitwriter_write_zeroes(bw, q))
			return false;
		/* write the unary end bit */
		if(!FLAC__bitwriter_write_raw_uint32(bw, 1, 1))
			return false;
		/* write the binary LSBs */
		if(r >= d) {
			if(!FLAC__bitwriter_write_raw_uint32(bw, r+d, k+1))
				return false;
		}
		else {
			if(!FLAC__bitwriter_write_raw_uint32(bw, r, k))
				return false;
		}
	}
	return true;
}
#endif /* UNUSED */

FLAC__bool FLAC__bitwriter_write_utf8_uint32(FLAC__BitWriter *bw, FLAC__uint32 val)
{
	FLAC__bool ok = 1;

	FLAC__ASSERT(0 != bw);
	FLAC__ASSERT(0 != bw->buffer);

	FLAC__ASSERT(!(val & 0x80000000)); /* this version only handles 31 bits */

	if(val < 0x80) {
		return FLAC__bitwriter_write_raw_uint32(bw, val, 8);
	}
	else if(val < 0x800) {
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0xC0 | (val>>6), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (val&0x3F), 8);
	}
	else if(val < 0x10000) {
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0xE0 | (val>>12), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | ((val>>6)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (val&0x3F), 8);
	}
	else if(val < 0x200000) {
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0xF0 | (val>>18), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | ((val>>12)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | ((val>>6)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (val&0x3F), 8);
	}
	else if(val < 0x4000000) {
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0xF8 | (val>>24), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | ((val>>18)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | ((val>>12)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | ((val>>6)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (val&0x3F), 8);
	}
	else {
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0xFC | (val>>30), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | ((val>>24)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | ((val>>18)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | ((val>>12)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | ((val>>6)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (val&0x3F), 8);
	}

	return ok;
}

FLAC__bool FLAC__bitwriter_write_utf8_uint64(FLAC__BitWriter *bw, FLAC__uint64 val)
{
	FLAC__bool ok = 1;

	FLAC__ASSERT(0 != bw);
	FLAC__ASSERT(0 != bw->buffer);

	FLAC__ASSERT(!(val & FLAC__U64L(0xFFFFFFF000000000))); /* this version only handles 36 bits */

	if(val < 0x80) {
		return FLAC__bitwriter_write_raw_uint32(bw, (FLAC__uint32)val, 8);
	}
	else if(val < 0x800) {
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0xC0 | (FLAC__uint32)(val>>6), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (FLAC__uint32)(val&0x3F), 8);
	}
	else if(val < 0x10000) {
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0xE0 | (FLAC__uint32)(val>>12), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (FLAC__uint32)((val>>6)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (FLAC__uint32)(val&0x3F), 8);
	}
	else if(val < 0x200000) {
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0xF0 | (FLAC__uint32)(val>>18), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (FLAC__uint32)((val>>12)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (FLAC__uint32)((val>>6)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (FLAC__uint32)(val&0x3F), 8);
	}
	else if(val < 0x4000000) {
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0xF8 | (FLAC__uint32)(val>>24), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (FLAC__uint32)((val>>18)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (FLAC__uint32)((val>>12)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (FLAC__uint32)((val>>6)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (FLAC__uint32)(val&0x3F), 8);
	}
	else if(val < 0x80000000) {
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0xFC | (FLAC__uint32)(val>>30), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (FLAC__uint32)((val>>24)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (FLAC__uint32)((val>>18)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (FLAC__uint32)((val>>12)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (FLAC__uint32)((val>>6)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (FLAC__uint32)(val&0x3F), 8);
	}
	else {
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0xFE, 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (FLAC__uint32)((val>>30)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (FLAC__uint32)((val>>24)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (FLAC__uint32)((val>>18)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (FLAC__uint32)((val>>12)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (FLAC__uint32)((val>>6)&0x3F), 8);
		ok &= FLAC__bitwriter_write_raw_uint32(bw, 0x80 | (FLAC__uint32)(val&0x3F), 8);
	}

	return ok;
}

FLAC__bool FLAC__bitwriter_zero_pad_to_byte_boundary(FLAC__BitWriter *bw)
{
	/* 0-pad to byte boundary */
	if(bw->bits & 7u)
		return FLAC__bitwriter_write_zeroes(bw, 8 - (bw->bits & 7u));
	else
		return true;
}

//++-- bitwriter.c 

//++ bitmath.c  

/* An example of what FLAC__bitmath_ilog2() computes:
 *
 * ilog2( 0) = assertion failure
 * ilog2( 1) = 0
 * ilog2( 2) = 1
 * ilog2( 3) = 1
 * ilog2( 4) = 2
 * ilog2( 5) = 2
 * ilog2( 6) = 2
 * ilog2( 7) = 2
 * ilog2( 8) = 3
 * ilog2( 9) = 3
 * ilog2(10) = 3
 * ilog2(11) = 3
 * ilog2(12) = 3
 * ilog2(13) = 3
 * ilog2(14) = 3
 * ilog2(15) = 3
 * ilog2(16) = 4
 * ilog2(17) = 4
 * ilog2(18) = 4
 */
unsigned FLAC__bitmath_ilog2(FLAC__uint32 v)
{
	unsigned l = 0;
	FLAC__ASSERT(v > 0);
	while(v >>= 1)
		l++;
	return l;
}

unsigned FLAC__bitmath_ilog2_wide(FLAC__uint64 v)
{
	unsigned l = 0;
	FLAC__ASSERT(v > 0);
	while(v >>= 1)
		l++;
	return l;
}

/* An example of what FLAC__bitmath_silog2() computes:
 *
 * silog2(-10) = 5
 * silog2(- 9) = 5
 * silog2(- 8) = 4
 * silog2(- 7) = 4
 * silog2(- 6) = 4
 * silog2(- 5) = 4
 * silog2(- 4) = 3
 * silog2(- 3) = 3
 * silog2(- 2) = 2
 * silog2(- 1) = 2
 * silog2(  0) = 0
 * silog2(  1) = 2
 * silog2(  2) = 3
 * silog2(  3) = 3
 * silog2(  4) = 4
 * silog2(  5) = 4
 * silog2(  6) = 4
 * silog2(  7) = 4
 * silog2(  8) = 5
 * silog2(  9) = 5
 * silog2( 10) = 5
 */
unsigned FLAC__bitmath_silog2(int v)
{
	while(1) {
		if(v == 0) {
			return 0;
		}
		else if(v > 0) {
			unsigned l = 0;
			while(v) {
				l++;
				v >>= 1;
			}
			return l+1;
		}
		else if(v == -1) {
			return 2;
		}
		else {
			v++;
			v = -v;
		}
	}
}

unsigned FLAC__bitmath_silog2_wide(FLAC__int64 v)
{
	while(1) {
		if(v == 0) {
			return 0;
		}
		else if(v > 0) {
			unsigned l = 0;
			while(v) {
				l++;
				v >>= 1;
			}
			return l+1;
		}
		else if(v == -1) {
			return 2;
		}
		else {
			v++;
			v = -v;
		}
	}
}
//++-- bitmath.c  

//++ md5.c 

#ifndef FLaC__INLINE
#define FLaC__INLINE
#endif

/*
 * This code implements the MD5 message-digest algorithm.
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
 * Changed so as no longer to depend on Colin Plumb's `usual.h' header
 * definitions; now uses stuff from dpkg's config.h.
 *  - Ian Jackson <ijackson@nyx.cs.du.edu>.
 * Still in the public domain.
 *
 * Josh Coalson: made some changes to integrate with libFLAC.
 * Still in the public domain.
 */

/* The four core functions - F1 is optimized somewhat */

/* #define F1(x, y, z) (x & y | ~x & z) */
#define F1(x, y, z) (z ^ (x & (y ^ z)))
#define F2(x, y, z) F1(z, x, y)
#define F3(x, y, z) (x ^ y ^ z)
#define F4(x, y, z) (y ^ (x | ~z))

/* This is the central step in the MD5 algorithm. */
#define MD5STEP(f,w,x,y,z,in,s) \
	 (w += f(x,y,z) + in, w = (w<<s | w>>(32-s)) + x)

/*
 * The core of the MD5 algorithm, this alters an existing MD5 hash to
 * reflect the addition of 16 longwords of new data.  MD5Update blocks
 * the data and converts bytes into longwords for this routine.
 */
static void FLAC__MD5Transform(FLAC__uint32 buf[4], FLAC__uint32 const in[16])
{
	register FLAC__uint32 a, b, c, d;

	a = buf[0];
	b = buf[1];
	c = buf[2];
	d = buf[3];

	MD5STEP(F1, a, b, c, d, in[0] + 0xd76aa478, 7);
	MD5STEP(F1, d, a, b, c, in[1] + 0xe8c7b756, 12);
	MD5STEP(F1, c, d, a, b, in[2] + 0x242070db, 17);
	MD5STEP(F1, b, c, d, a, in[3] + 0xc1bdceee, 22);
	MD5STEP(F1, a, b, c, d, in[4] + 0xf57c0faf, 7);
	MD5STEP(F1, d, a, b, c, in[5] + 0x4787c62a, 12);
	MD5STEP(F1, c, d, a, b, in[6] + 0xa8304613, 17);
	MD5STEP(F1, b, c, d, a, in[7] + 0xfd469501, 22);
	MD5STEP(F1, a, b, c, d, in[8] + 0x698098d8, 7);
	MD5STEP(F1, d, a, b, c, in[9] + 0x8b44f7af, 12);
	MD5STEP(F1, c, d, a, b, in[10] + 0xffff5bb1, 17);
	MD5STEP(F1, b, c, d, a, in[11] + 0x895cd7be, 22);
	MD5STEP(F1, a, b, c, d, in[12] + 0x6b901122, 7);
	MD5STEP(F1, d, a, b, c, in[13] + 0xfd987193, 12);
	MD5STEP(F1, c, d, a, b, in[14] + 0xa679438e, 17);
	MD5STEP(F1, b, c, d, a, in[15] + 0x49b40821, 22);

	MD5STEP(F2, a, b, c, d, in[1] + 0xf61e2562, 5);
	MD5STEP(F2, d, a, b, c, in[6] + 0xc040b340, 9);
	MD5STEP(F2, c, d, a, b, in[11] + 0x265e5a51, 14);
	MD5STEP(F2, b, c, d, a, in[0] + 0xe9b6c7aa, 20);
	MD5STEP(F2, a, b, c, d, in[5] + 0xd62f105d, 5);
	MD5STEP(F2, d, a, b, c, in[10] + 0x02441453, 9);
	MD5STEP(F2, c, d, a, b, in[15] + 0xd8a1e681, 14);
	MD5STEP(F2, b, c, d, a, in[4] + 0xe7d3fbc8, 20);
	MD5STEP(F2, a, b, c, d, in[9] + 0x21e1cde6, 5);
	MD5STEP(F2, d, a, b, c, in[14] + 0xc33707d6, 9);
	MD5STEP(F2, c, d, a, b, in[3] + 0xf4d50d87, 14);
	MD5STEP(F2, b, c, d, a, in[8] + 0x455a14ed, 20);
	MD5STEP(F2, a, b, c, d, in[13] + 0xa9e3e905, 5);
	MD5STEP(F2, d, a, b, c, in[2] + 0xfcefa3f8, 9);
	MD5STEP(F2, c, d, a, b, in[7] + 0x676f02d9, 14);
	MD5STEP(F2, b, c, d, a, in[12] + 0x8d2a4c8a, 20);

	MD5STEP(F3, a, b, c, d, in[5] + 0xfffa3942, 4);
	MD5STEP(F3, d, a, b, c, in[8] + 0x8771f681, 11);
	MD5STEP(F3, c, d, a, b, in[11] + 0x6d9d6122, 16);
	MD5STEP(F3, b, c, d, a, in[14] + 0xfde5380c, 23);
	MD5STEP(F3, a, b, c, d, in[1] + 0xa4beea44, 4);
	MD5STEP(F3, d, a, b, c, in[4] + 0x4bdecfa9, 11);
	MD5STEP(F3, c, d, a, b, in[7] + 0xf6bb4b60, 16);
	MD5STEP(F3, b, c, d, a, in[10] + 0xbebfbc70, 23);
	MD5STEP(F3, a, b, c, d, in[13] + 0x289b7ec6, 4);
	MD5STEP(F3, d, a, b, c, in[0] + 0xeaa127fa, 11);
	MD5STEP(F3, c, d, a, b, in[3] + 0xd4ef3085, 16);
	MD5STEP(F3, b, c, d, a, in[6] + 0x04881d05, 23);
	MD5STEP(F3, a, b, c, d, in[9] + 0xd9d4d039, 4);
	MD5STEP(F3, d, a, b, c, in[12] + 0xe6db99e5, 11);
	MD5STEP(F3, c, d, a, b, in[15] + 0x1fa27cf8, 16);
	MD5STEP(F3, b, c, d, a, in[2] + 0xc4ac5665, 23);

	MD5STEP(F4, a, b, c, d, in[0] + 0xf4292244, 6);
	MD5STEP(F4, d, a, b, c, in[7] + 0x432aff97, 10);
	MD5STEP(F4, c, d, a, b, in[14] + 0xab9423a7, 15);
	MD5STEP(F4, b, c, d, a, in[5] + 0xfc93a039, 21);
	MD5STEP(F4, a, b, c, d, in[12] + 0x655b59c3, 6);
	MD5STEP(F4, d, a, b, c, in[3] + 0x8f0ccc92, 10);
	MD5STEP(F4, c, d, a, b, in[10] + 0xffeff47d, 15);
	MD5STEP(F4, b, c, d, a, in[1] + 0x85845dd1, 21);
	MD5STEP(F4, a, b, c, d, in[8] + 0x6fa87e4f, 6);
	MD5STEP(F4, d, a, b, c, in[15] + 0xfe2ce6e0, 10);
	MD5STEP(F4, c, d, a, b, in[6] + 0xa3014314, 15);
	MD5STEP(F4, b, c, d, a, in[13] + 0x4e0811a1, 21);
	MD5STEP(F4, a, b, c, d, in[4] + 0xf7537e82, 6);
	MD5STEP(F4, d, a, b, c, in[11] + 0xbd3af235, 10);
	MD5STEP(F4, c, d, a, b, in[2] + 0x2ad7d2bb, 15);
	MD5STEP(F4, b, c, d, a, in[9] + 0xeb86d391, 21);

	buf[0] += a;
	buf[1] += b;
	buf[2] += c;
	buf[3] += d;
}

#if WORDS_BIGENDIAN
//@@@@@@ OPT: use bswap/intrinsics
static void byteSwap(FLAC__uint32 *buf, unsigned words)
{
	register FLAC__uint32 x;
	do {
		x = *buf; 
		x = ((x << 8) & 0xff00ff00) | ((x >> 8) & 0x00ff00ff);
		*buf++ = (x >> 16) | (x << 16);
	} while (--words);
}
static void byteSwapX16(FLAC__uint32 *buf)
{
	register FLAC__uint32 x;

	x = *buf; x = ((x << 8) & 0xff00ff00) | ((x >> 8) & 0x00ff00ff); *buf++ = (x >> 16) | (x << 16);
	x = *buf; x = ((x << 8) & 0xff00ff00) | ((x >> 8) & 0x00ff00ff); *buf++ = (x >> 16) | (x << 16);
	x = *buf; x = ((x << 8) & 0xff00ff00) | ((x >> 8) & 0x00ff00ff); *buf++ = (x >> 16) | (x << 16);
	x = *buf; x = ((x << 8) & 0xff00ff00) | ((x >> 8) & 0x00ff00ff); *buf++ = (x >> 16) | (x << 16);
	x = *buf; x = ((x << 8) & 0xff00ff00) | ((x >> 8) & 0x00ff00ff); *buf++ = (x >> 16) | (x << 16);
	x = *buf; x = ((x << 8) & 0xff00ff00) | ((x >> 8) & 0x00ff00ff); *buf++ = (x >> 16) | (x << 16);
	x = *buf; x = ((x << 8) & 0xff00ff00) | ((x >> 8) & 0x00ff00ff); *buf++ = (x >> 16) | (x << 16);
	x = *buf; x = ((x << 8) & 0xff00ff00) | ((x >> 8) & 0x00ff00ff); *buf++ = (x >> 16) | (x << 16);
	x = *buf; x = ((x << 8) & 0xff00ff00) | ((x >> 8) & 0x00ff00ff); *buf++ = (x >> 16) | (x << 16);
	x = *buf; x = ((x << 8) & 0xff00ff00) | ((x >> 8) & 0x00ff00ff); *buf++ = (x >> 16) | (x << 16);
	x = *buf; x = ((x << 8) & 0xff00ff00) | ((x >> 8) & 0x00ff00ff); *buf++ = (x >> 16) | (x << 16);
	x = *buf; x = ((x << 8) & 0xff00ff00) | ((x >> 8) & 0x00ff00ff); *buf++ = (x >> 16) | (x << 16);
	x = *buf; x = ((x << 8) & 0xff00ff00) | ((x >> 8) & 0x00ff00ff); *buf++ = (x >> 16) | (x << 16);
	x = *buf; x = ((x << 8) & 0xff00ff00) | ((x >> 8) & 0x00ff00ff); *buf++ = (x >> 16) | (x << 16);
	x = *buf; x = ((x << 8) & 0xff00ff00) | ((x >> 8) & 0x00ff00ff); *buf++ = (x >> 16) | (x << 16);
	x = *buf; x = ((x << 8) & 0xff00ff00) | ((x >> 8) & 0x00ff00ff); *buf   = (x >> 16) | (x << 16);
}
#else
#define byteSwap(buf, words)
#define byteSwapX16(buf)
#endif

/*
 * Update context to reflect the concatenation of another buffer full
 * of bytes.
 */
static void FLAC__MD5Update(FLAC__MD5Context *ctx, FLAC__byte const *buf, unsigned len)
{
	FLAC__uint32 t;

	/* Update byte count */

	t = ctx->bytes[0];
	if ((ctx->bytes[0] = t + len) < t)
		ctx->bytes[1]++;	/* Carry from low to high */

	t = 64 - (t & 0x3f);	/* Space available in ctx->in (at least 1) */
	if (t > len) {
		memcpy((FLAC__byte *)ctx->in + 64 - t, buf, len);
		return;
	}
	/* First chunk is an odd size */
	memcpy((FLAC__byte *)ctx->in + 64 - t, buf, t);
	byteSwapX16(ctx->in);
	FLAC__MD5Transform(ctx->buf, ctx->in);
	buf += t;
	len -= t;

	/* Process data in 64-byte chunks */
	while (len >= 64) {
		memcpy(ctx->in, buf, 64);
		byteSwapX16(ctx->in);
		FLAC__MD5Transform(ctx->buf, ctx->in);
		buf += 64;
		len -= 64;
	}

	/* Handle any remaining bytes of data. */
	memcpy(ctx->in, buf, len);
}

/*
 * Start MD5 accumulation.  Set bit count to 0 and buffer to mysterious
 * initialization constants.
 */
void FLAC__MD5Init(FLAC__MD5Context *ctx)
{
	ctx->buf[0] = 0x67452301;
	ctx->buf[1] = 0xefcdab89;
	ctx->buf[2] = 0x98badcfe;
	ctx->buf[3] = 0x10325476;

	ctx->bytes[0] = 0;
	ctx->bytes[1] = 0;

	ctx->internal_buf = 0;
	ctx->capacity = 0;
}

/*
 * Final wrapup - pad to 64-byte boundary with the bit pattern
 * 1 0* (64-bit count of bits processed, MSB-first)
 */
void FLAC__MD5Final(FLAC__byte digest[16], FLAC__MD5Context *ctx)
{
	int count = ctx->bytes[0] & 0x3f;	/* Number of bytes in ctx->in */
	FLAC__byte *p = (FLAC__byte *)ctx->in + count;

	/* Set the first char of padding to 0x80.  There is always room. */
	*p++ = 0x80;

	/* Bytes of padding needed to make 56 bytes (-8..55) */
	count = 56 - 1 - count;

	if (count < 0) {	/* Padding forces an extra block */
		memset(p, 0, count + 8);
		byteSwapX16(ctx->in);
		FLAC__MD5Transform(ctx->buf, ctx->in);
		p = (FLAC__byte *)ctx->in;
		count = 56;
	}
	memset(p, 0, count);
	byteSwap(ctx->in, 14);

	/* Append length in bits and transform */
	ctx->in[14] = ctx->bytes[0] << 3;
	ctx->in[15] = ctx->bytes[1] << 3 | ctx->bytes[0] >> 29;
	FLAC__MD5Transform(ctx->buf, ctx->in);

	byteSwap(ctx->buf, 4);
	memcpy(digest, ctx->buf, 16);
	memset(ctx, 0, sizeof(ctx));	/* In case it's sensitive */
	if(0 != ctx->internal_buf) {
		free(ctx->internal_buf);
		ctx->internal_buf = 0;
		ctx->capacity = 0;
	}
}

/*
 * Convert the incoming audio signal to a byte stream
 */
static void format_input_(FLAC__byte *buf, const FLAC__int32 * const signal[], unsigned channels, unsigned samples, unsigned bytes_per_sample)
{
	unsigned channel, sample;
	register FLAC__int32 a_word;
	register FLAC__byte *buf_ = buf;

#if WORDS_BIGENDIAN
#else
	if(channels == 2 && bytes_per_sample == 2) {
		FLAC__int16 *buf1_ = ((FLAC__int16*)buf_) + 1;
		memcpy(buf_, signal[0], sizeof(FLAC__int32) * samples);
		for(sample = 0; sample < samples; sample++, buf1_+=2)
			*buf1_ = (FLAC__int16)signal[1][sample];
	}
	else if(channels == 1 && bytes_per_sample == 2) {
		FLAC__int16 *buf1_ = (FLAC__int16*)buf_;
		for(sample = 0; sample < samples; sample++)
			*buf1_++ = (FLAC__int16)signal[0][sample];
	}
	else
#endif
	if(bytes_per_sample == 2) {
		if(channels == 2) {
			for(sample = 0; sample < samples; sample++) {
				a_word = signal[0][sample];
				*buf_++ = (FLAC__byte)a_word; a_word >>= 8;
				*buf_++ = (FLAC__byte)a_word;
				a_word = signal[1][sample];
				*buf_++ = (FLAC__byte)a_word; a_word >>= 8;
				*buf_++ = (FLAC__byte)a_word;
			}
		}
		else if(channels == 1) {
			for(sample = 0; sample < samples; sample++) {
				a_word = signal[0][sample];
				*buf_++ = (FLAC__byte)a_word; a_word >>= 8;
				*buf_++ = (FLAC__byte)a_word;
			}
		}
		else {
			for(sample = 0; sample < samples; sample++) {
				for(channel = 0; channel < channels; channel++) {
					a_word = signal[channel][sample];
					*buf_++ = (FLAC__byte)a_word; a_word >>= 8;
					*buf_++ = (FLAC__byte)a_word;
				}
			}
		}
	}
	else if(bytes_per_sample == 3) {
		if(channels == 2) {
			for(sample = 0; sample < samples; sample++) {
				a_word = signal[0][sample];
				*buf_++ = (FLAC__byte)a_word; a_word >>= 8;
				*buf_++ = (FLAC__byte)a_word; a_word >>= 8;
				*buf_++ = (FLAC__byte)a_word;
				a_word = signal[1][sample];
				*buf_++ = (FLAC__byte)a_word; a_word >>= 8;
				*buf_++ = (FLAC__byte)a_word; a_word >>= 8;
				*buf_++ = (FLAC__byte)a_word;
			}
		}
		else if(channels == 1) {
			for(sample = 0; sample < samples; sample++) {
				a_word = signal[0][sample];
				*buf_++ = (FLAC__byte)a_word; a_word >>= 8;
				*buf_++ = (FLAC__byte)a_word; a_word >>= 8;
				*buf_++ = (FLAC__byte)a_word;
			}
		}
		else {
			for(sample = 0; sample < samples; sample++) {
				for(channel = 0; channel < channels; channel++) {
					a_word = signal[channel][sample];
					*buf_++ = (FLAC__byte)a_word; a_word >>= 8;
					*buf_++ = (FLAC__byte)a_word; a_word >>= 8;
					*buf_++ = (FLAC__byte)a_word;
				}
			}
		}
	}
	else if(bytes_per_sample == 1) {
		if(channels == 2) {
			for(sample = 0; sample < samples; sample++) {
				a_word = signal[0][sample];
				*buf_++ = (FLAC__byte)a_word;
				a_word = signal[1][sample];
				*buf_++ = (FLAC__byte)a_word;
			}
		}
		else if(channels == 1) {
			for(sample = 0; sample < samples; sample++) {
				a_word = signal[0][sample];
				*buf_++ = (FLAC__byte)a_word;
			}
		}
		else {
			for(sample = 0; sample < samples; sample++) {
				for(channel = 0; channel < channels; channel++) {
					a_word = signal[channel][sample];
					*buf_++ = (FLAC__byte)a_word;
				}
			}
		}
	}
	else { /* bytes_per_sample == 4, maybe optimize more later */
		for(sample = 0; sample < samples; sample++) {
			for(channel = 0; channel < channels; channel++) {
				a_word = signal[channel][sample];
				*buf_++ = (FLAC__byte)a_word; a_word >>= 8;
				*buf_++ = (FLAC__byte)a_word; a_word >>= 8;
				*buf_++ = (FLAC__byte)a_word; a_word >>= 8;
				*buf_++ = (FLAC__byte)a_word;
			}
		}
	}
}

/*
 * Convert the incoming audio signal to a byte stream and FLAC__MD5Update it.
 */
FLAC__bool FLAC__MD5Accumulate(FLAC__MD5Context *ctx, const FLAC__int32 * const signal[], unsigned channels, unsigned samples, unsigned bytes_per_sample)
{
	const size_t bytes_needed = (size_t)channels * (size_t)samples * (size_t)bytes_per_sample;

	/* overflow check */
	if((size_t)channels > SIZE_MAX / (size_t)bytes_per_sample)
		return false;
	if((size_t)channels * (size_t)bytes_per_sample > SIZE_MAX / (size_t)samples)
		return false;

	if(ctx->capacity < bytes_needed) {
		FLAC__byte *tmp = (FLAC__byte*)realloc(ctx->internal_buf, bytes_needed);
		if(0 == tmp) {
			free(ctx->internal_buf);
			if(0 == (ctx->internal_buf = (FLAC__byte*)safe_malloc_(bytes_needed)))
				return false;
		}
		ctx->internal_buf = tmp;
		ctx->capacity = bytes_needed;
	}

	format_input_(ctx->internal_buf, signal, channels, samples, bytes_per_sample);

	FLAC__MD5Update(ctx, ctx->internal_buf, bytes_needed);

	return true;
}
//++-- md5.c 

//++ memory.c 

void *FLAC__memory_alloc_aligned(size_t bytes, void **aligned_address)
{
	void *x;

	FLAC__ASSERT(0 != aligned_address);

#ifdef FLAC__ALIGN_MALLOC_DATA
	/* align on 32-byte (256-bit) boundary */
	x = safe_malloc_add_2op_(bytes, /*+*/31);
#ifdef SIZEOF_VOIDP
#if SIZEOF_VOIDP == 4
	/* could do  *aligned_address = x + ((unsigned) (32 - (((unsigned)x) & 31))) & 31; */
	*aligned_address = (void*)(((unsigned)x + 31) & -32);
#elif SIZEOF_VOIDP == 8
	*aligned_address = (void*)(((FLAC__uint64)x + 31) & (FLAC__uint64)(-((FLAC__int64)32)));
#else
# error  Unsupported sizeof(void*)
#endif
#else
	/* there's got to be a better way to do this right for all archs */
	if(sizeof(void*) == sizeof(unsigned))
		*aligned_address = (void*)(((unsigned)x + 31) & -32);
	else if(sizeof(void*) == sizeof(FLAC__uint64))
		*aligned_address = (void*)(((FLAC__uint64)x + 31) & (FLAC__uint64)(-((FLAC__int64)32)));
	else
		return 0;
#endif
#else
	x = safe_malloc_(bytes);
	*aligned_address = x;
#endif
	return x;
}

FLAC__bool FLAC__memory_alloc_aligned_int32_array(unsigned elements, FLAC__int32 **unaligned_pointer, FLAC__int32 **aligned_pointer)
{
	FLAC__int32 *pu; /* unaligned pointer */
	union { /* union needed to comply with C99 pointer aliasing rules */
		FLAC__int32 *pa; /* aligned pointer */
		void        *pv; /* aligned pointer alias */
	} u;

	FLAC__ASSERT(elements > 0);
	FLAC__ASSERT(0 != unaligned_pointer);
	FLAC__ASSERT(0 != aligned_pointer);
	FLAC__ASSERT(unaligned_pointer != aligned_pointer);

	if((size_t)elements > SIZE_MAX / sizeof(*pu)) /* overflow check */
		return false;

	pu = (FLAC__int32*)FLAC__memory_alloc_aligned(sizeof(*pu) * (size_t)elements, &u.pv);
	if(0 == pu) {
		return false;
	}
	else {
		if(*unaligned_pointer != 0)
			free(*unaligned_pointer);
		*unaligned_pointer = pu;
		*aligned_pointer = u.pa;
		return true;
	}
}

FLAC__bool FLAC__memory_alloc_aligned_uint32_array(unsigned elements, FLAC__uint32 **unaligned_pointer, FLAC__uint32 **aligned_pointer)
{
	FLAC__uint32 *pu; /* unaligned pointer */
	union { /* union needed to comply with C99 pointer aliasing rules */
		FLAC__uint32 *pa; /* aligned pointer */
		void         *pv; /* aligned pointer alias */
	} u;

	FLAC__ASSERT(elements > 0);
	FLAC__ASSERT(0 != unaligned_pointer);
	FLAC__ASSERT(0 != aligned_pointer);
	FLAC__ASSERT(unaligned_pointer != aligned_pointer);

	if((size_t)elements > SIZE_MAX / sizeof(*pu)) /* overflow check */
		return false;

	pu = (FLAC__uint32*)FLAC__memory_alloc_aligned(sizeof(*pu) * elements, &u.pv);
	if(0 == pu) {
		return false;
	}
	else {
		if(*unaligned_pointer != 0)
			free(*unaligned_pointer);
		*unaligned_pointer = pu;
		*aligned_pointer = u.pa;
		return true;
	}
}

FLAC__bool FLAC__memory_alloc_aligned_uint64_array(unsigned elements, FLAC__uint64 **unaligned_pointer, FLAC__uint64 **aligned_pointer)
{
	FLAC__uint64 *pu; /* unaligned pointer */
	union { /* union needed to comply with C99 pointer aliasing rules */
		FLAC__uint64 *pa; /* aligned pointer */
		void         *pv; /* aligned pointer alias */
	} u;

	FLAC__ASSERT(elements > 0);
	FLAC__ASSERT(0 != unaligned_pointer);
	FLAC__ASSERT(0 != aligned_pointer);
	FLAC__ASSERT(unaligned_pointer != aligned_pointer);

	if((size_t)elements > SIZE_MAX / sizeof(*pu)) /* overflow check */
		return false;

	pu = (FLAC__uint64*)FLAC__memory_alloc_aligned(sizeof(*pu) * elements, &u.pv);
	if(0 == pu) {
		return false;
	}
	else {
		if(*unaligned_pointer != 0)
			free(*unaligned_pointer);
		*unaligned_pointer = pu;
		*aligned_pointer = u.pa;
		return true;
	}
}

FLAC__bool FLAC__memory_alloc_aligned_unsigned_array(unsigned elements, unsigned **unaligned_pointer, unsigned **aligned_pointer)
{
	unsigned *pu; /* unaligned pointer */
	union { /* union needed to comply with C99 pointer aliasing rules */
		unsigned *pa; /* aligned pointer */
		void     *pv; /* aligned pointer alias */
	} u;

	FLAC__ASSERT(elements > 0);
	FLAC__ASSERT(0 != unaligned_pointer);
	FLAC__ASSERT(0 != aligned_pointer);
	FLAC__ASSERT(unaligned_pointer != aligned_pointer);

	if((size_t)elements > SIZE_MAX / sizeof(*pu)) /* overflow check */
		return false;

	pu = (unsigned*)FLAC__memory_alloc_aligned(sizeof(*pu) * elements, &u.pv);
	if(0 == pu) {
		return false;
	}
	else {
		if(*unaligned_pointer != 0)
			free(*unaligned_pointer);
		*unaligned_pointer = pu;
		*aligned_pointer = u.pa;
		return true;
	}
}

#ifndef FLAC__INTEGER_ONLY_LIBRARY

FLAC__bool FLAC__memory_alloc_aligned_real_array(unsigned elements, FLAC__real **unaligned_pointer, FLAC__real **aligned_pointer)
{
	FLAC__real *pu; /* unaligned pointer */
	union { /* union needed to comply with C99 pointer aliasing rules */
		FLAC__real *pa; /* aligned pointer */
		void       *pv; /* aligned pointer alias */
	} u;

	FLAC__ASSERT(elements > 0);
	FLAC__ASSERT(0 != unaligned_pointer);
	FLAC__ASSERT(0 != aligned_pointer);
	FLAC__ASSERT(unaligned_pointer != aligned_pointer);

	if((size_t)elements > SIZE_MAX / sizeof(*pu)) /* overflow check */
		return false;

	pu = (FLAC__real*)FLAC__memory_alloc_aligned(sizeof(*pu) * elements, &u.pv);
	if(0 == pu) {
		return false;
	}
	else {
		if(*unaligned_pointer != 0)
			free(*unaligned_pointer);
		*unaligned_pointer = pu;
		*aligned_pointer = u.pa;
		return true;
	}
}

#endif
//++-- memory.c 


//++ window.c  

#include <math.h>

#ifndef FLAC__INTEGER_ONLY_LIBRARY

#ifndef M_PI
/* math.h in VC++ doesn't seem to have this (how Microsoft is that?) */
#define M_PI 3.14159265358979323846
#endif


void FLAC__window_bartlett(FLAC__real *window, const FLAC__int32 L)
{
	const FLAC__int32 N = L - 1;
	FLAC__int32 n;

	if (L & 1) {
		for (n = 0; n <= N/2; n++)
			window[n] = 2.0f * n / (float)N;
		for (; n <= N; n++)
			window[n] = 2.0f - 2.0f * n / (float)N;
	}
	else {
		for (n = 0; n <= L/2-1; n++)
			window[n] = 2.0f * n / (float)N;
		for (; n <= N; n++)
			window[n] = 2.0f - 2.0f * (N-n) / (float)N;
	}
}

void FLAC__window_bartlett_hann(FLAC__real *window, const FLAC__int32 L)
{
	const FLAC__int32 N = L - 1;
	FLAC__int32 n;

	for (n = 0; n < L; n++)
		window[n] = (FLAC__real)(0.62f - 0.48f * fabs((float)n/(float)N+0.5f) + 0.38f * cos(2.0f * M_PI * ((float)n/(float)N+0.5f)));
}

void FLAC__window_blackman(FLAC__real *window, const FLAC__int32 L)
{
	const FLAC__int32 N = L - 1;
	FLAC__int32 n;

	for (n = 0; n < L; n++)
		window[n] = (FLAC__real)(0.42f - 0.5f * cos(2.0f * M_PI * n / N) + 0.08f * cos(4.0f * M_PI * n / N));
}

/* 4-term -92dB side-lobe */
void FLAC__window_blackman_harris_4term_92db_sidelobe(FLAC__real *window, const FLAC__int32 L)
{
	const FLAC__int32 N = L - 1;
	FLAC__int32 n;

	for (n = 0; n <= N; n++)
		window[n] = (FLAC__real)(0.35875f - 0.48829f * cos(2.0f * M_PI * n / N) + 0.14128f * cos(4.0f * M_PI * n / N) - 0.01168f * cos(6.0f * M_PI * n / N));
}

void FLAC__window_connes(FLAC__real *window, const FLAC__int32 L)
{
	const FLAC__int32 N = L - 1;
	const double N2 = (double)N / 2.;
	FLAC__int32 n;

	for (n = 0; n <= N; n++) {
		double k = ((double)n - N2) / N2;
		k = 1.0f - k * k;
		window[n] = (FLAC__real)(k * k);
	}
}

void FLAC__window_flattop(FLAC__real *window, const FLAC__int32 L)
{
	const FLAC__int32 N = L - 1;
	FLAC__int32 n;

	for (n = 0; n < L; n++)
		window[n] = (FLAC__real)(1.0f - 1.93f * cos(2.0f * M_PI * n / N) + 1.29f * cos(4.0f * M_PI * n / N) - 0.388f * cos(6.0f * M_PI * n / N) + 0.0322f * cos(8.0f * M_PI * n / N));
}

void FLAC__window_gauss(FLAC__real *window, const FLAC__int32 L, const FLAC__real stddev)
{
	const FLAC__int32 N = L - 1;
	const double N2 = (double)N / 2.;
	FLAC__int32 n;

	for (n = 0; n <= N; n++) {
		const double k = ((double)n - N2) / (stddev * N2);
		window[n] = (FLAC__real)exp(-0.5f * k * k);
	}
}

void FLAC__window_hamming(FLAC__real *window, const FLAC__int32 L)
{
	const FLAC__int32 N = L - 1;
	FLAC__int32 n;

	for (n = 0; n < L; n++)
		window[n] = (FLAC__real)(0.54f - 0.46f * cos(2.0f * M_PI * n / N));
}

void FLAC__window_hann(FLAC__real *window, const FLAC__int32 L)
{
	const FLAC__int32 N = L - 1;
	FLAC__int32 n;

	for (n = 0; n < L; n++)
		window[n] = (FLAC__real)(0.5f - 0.5f * cos(2.0f * M_PI * n / N));
}

void FLAC__window_kaiser_bessel(FLAC__real *window, const FLAC__int32 L)
{
	const FLAC__int32 N = L - 1;
	FLAC__int32 n;

	for (n = 0; n < L; n++)
		window[n] = (FLAC__real)(0.402f - 0.498f * cos(2.0f * M_PI * n / N) + 0.098f * cos(4.0f * M_PI * n / N) - 0.001f * cos(6.0f * M_PI * n / N));
}

void FLAC__window_nuttall(FLAC__real *window, const FLAC__int32 L)
{
	const FLAC__int32 N = L - 1;
	FLAC__int32 n;

	for (n = 0; n < L; n++)
		window[n] = (FLAC__real)(0.3635819f - 0.4891775f*cos(2.0f*M_PI*n/N) + 0.1365995f*cos(4.0f*M_PI*n/N) - 0.0106411f*cos(6.0f*M_PI*n/N));
}

void FLAC__window_rectangle(FLAC__real *window, const FLAC__int32 L)
{
	FLAC__int32 n;

	for (n = 0; n < L; n++)
		window[n] = 1.0f;
}

void FLAC__window_triangle(FLAC__real *window, const FLAC__int32 L)
{
	FLAC__int32 n;

	if (L & 1) {
		for (n = 1; n <= L+1/2; n++)
			window[n-1] = 2.0f * n / ((float)L + 1.0f);
		for (; n <= L; n++)
			window[n-1] = - (float)(2 * (L - n + 1)) / ((float)L + 1.0f);
	}
	else {
		for (n = 1; n <= L/2; n++)
			window[n-1] = 2.0f * n / (float)L;
		for (; n <= L; n++)
			window[n-1] = ((float)(2 * (L - n)) + 1.0f) / (float)L;
	}
}

void FLAC__window_tukey(FLAC__real *window, const FLAC__int32 L, const FLAC__real p)
{
	if (p <= 0.0)
		FLAC__window_rectangle(window, L);
	else if (p >= 1.0)
		FLAC__window_hann(window, L);
	else {
		const FLAC__int32 Np = (FLAC__int32)(p / 2.0f * L) - 1;
		FLAC__int32 n;
		/* start with rectangle... */
		FLAC__window_rectangle(window, L);
		/* ...replace ends with hann */
		if (Np > 0) {
			for (n = 0; n <= Np; n++) {
				window[n] = (FLAC__real)(0.5f - 0.5f * cos(M_PI * n / Np));
				window[L-Np-1+n] = (FLAC__real)(0.5f - 0.5f * cos(M_PI * (n+Np) / Np));
			}
		}
	}
}

void FLAC__window_welch(FLAC__real *window, const FLAC__int32 L)
{
	const FLAC__int32 N = L - 1;
	const double N2 = (double)N / 2.;
	FLAC__int32 n;

	for (n = 0; n <= N; n++) {
		const double k = ((double)n - N2) / N2;
		window[n] = (FLAC__real)(1.0f - k * k);
	}
}

#endif /* !defined FLAC__INTEGER_ONLY_LIBRARY */

//++-- window.c  


//++ ogg_helper.c 

static FLAC__bool full_read_(FLAC__StreamEncoder *encoder, FLAC__byte *buffer, size_t bytes, FLAC__StreamEncoderReadCallback read_callback, void *client_data)
{
	while(bytes > 0) {
		size_t bytes_read = bytes;
		switch(read_callback(encoder, buffer, &bytes_read, client_data)) {
			case FLAC__STREAM_ENCODER_READ_STATUS_CONTINUE:
				bytes -= bytes_read;
				buffer += bytes_read;
				break;
			case FLAC__STREAM_ENCODER_READ_STATUS_END_OF_STREAM:
				if(bytes_read == 0) {
					encoder->protected_->state = FLAC__STREAM_ENCODER_OGG_ERROR;
					return false;
				}
				bytes -= bytes_read;
				buffer += bytes_read;
				break;
			case FLAC__STREAM_ENCODER_READ_STATUS_ABORT:
				encoder->protected_->state = FLAC__STREAM_ENCODER_CLIENT_ERROR;
				return false;
			case FLAC__STREAM_ENCODER_READ_STATUS_UNSUPPORTED:
				return false;
			default:
				/* double protection: */
				FLAC__ASSERT(0);
				encoder->protected_->state = FLAC__STREAM_ENCODER_CLIENT_ERROR;
				return false;
		}
	}

	return true;
}

void simple_ogg_page__init(ogg_page *page)
{
	page->header = 0;
	page->header_len = 0;
	page->body = 0;
	page->body_len = 0;
}

void simple_ogg_page__clear(ogg_page *page)
{
	if(page->header)
		free(page->header);
	if(page->body)
		free(page->body);
	simple_ogg_page__init(page);
}

FLAC__bool simple_ogg_page__get_at(FLAC__StreamEncoder *encoder, FLAC__uint64 position, ogg_page *page, FLAC__StreamEncoderSeekCallback seek_callback, FLAC__StreamEncoderReadCallback read_callback, void *client_data)
{
	static const unsigned OGG_HEADER_FIXED_PORTION_LEN = 27;
	static const unsigned OGG_MAX_HEADER_LEN = 27/*OGG_HEADER_FIXED_PORTION_LEN*/ + 255;
	FLAC__byte crc[4];
	FLAC__StreamEncoderSeekStatus seek_status;

	FLAC__ASSERT(page->header == 0);
	FLAC__ASSERT(page->header_len == 0);
	FLAC__ASSERT(page->body == 0);
	FLAC__ASSERT(page->body_len == 0);

	/* move the stream pointer to the supposed beginning of the page */
	if(0 == seek_callback)
		return false;
	if((seek_status = seek_callback((FLAC__StreamEncoder*)encoder, position, client_data)) != FLAC__STREAM_ENCODER_SEEK_STATUS_OK) {
		if(seek_status == FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR)
			encoder->protected_->state = FLAC__STREAM_ENCODER_CLIENT_ERROR;
		return false;
	}

	/* allocate space for the page header */
	if(0 == (page->header = (unsigned char *)safe_malloc_(OGG_MAX_HEADER_LEN))) {
		encoder->protected_->state = FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}

	/* read in the fixed part of the page header (up to but not including
	 * the segment table */
	if(!full_read_(encoder, page->header, OGG_HEADER_FIXED_PORTION_LEN, read_callback, client_data))
		return false;

	page->header_len = OGG_HEADER_FIXED_PORTION_LEN + page->header[26];

	/* check to see if it's a correct, "simple" page (one packet only) */
	if(
		memcmp(page->header, "OggS", 4) ||               /* doesn't start with OggS */
		(page->header[5] & 0x01) ||                      /* continued packet */
		memcmp(page->header+6, "\0\0\0\0\0\0\0\0", 8) || /* granulepos is non-zero */
		page->header[26] == 0                            /* packet is 0-size */
	) {
		encoder->protected_->state = FLAC__STREAM_ENCODER_OGG_ERROR;
		return false;
	}

	/* read in the segment table */
	if(!full_read_(encoder, page->header + OGG_HEADER_FIXED_PORTION_LEN, page->header[26], read_callback, client_data))
		return false;

	{
		unsigned i;

		/* check to see that it specifies a single packet */
		for(i = 0; i < (unsigned)page->header[26] - 1; i++) {
			if(page->header[i + OGG_HEADER_FIXED_PORTION_LEN] != 255) {
				encoder->protected_->state = FLAC__STREAM_ENCODER_OGG_ERROR;
				return false;
			}
		}

		page->body_len = 255 * i + page->header[i + OGG_HEADER_FIXED_PORTION_LEN];
	}

	/* allocate space for the page body */
	if(0 == (page->body = (unsigned char *)safe_malloc_(page->body_len))) {
		encoder->protected_->state = FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}

	/* read in the page body */
	if(!full_read_(encoder, page->body, page->body_len, read_callback, client_data))
		return false;

	/* check the CRC */
	memcpy(crc, page->header+22, 4);
	ogg_page_checksum_set(page);
	if(memcmp(crc, page->header+22, 4)) {
		encoder->protected_->state = FLAC__STREAM_ENCODER_OGG_ERROR;
		return false;
	}

	return true;
}

FLAC__bool simple_ogg_page__set_at(FLAC__StreamEncoder *encoder, FLAC__uint64 position, ogg_page *page, FLAC__StreamEncoderSeekCallback seek_callback, FLAC__StreamEncoderWriteCallback write_callback, void *client_data)
{
	FLAC__StreamEncoderSeekStatus seek_status;

	FLAC__ASSERT(page->header != 0);
	FLAC__ASSERT(page->header_len != 0);
	FLAC__ASSERT(page->body != 0);
	FLAC__ASSERT(page->body_len != 0);

	/* move the stream pointer to the supposed beginning of the page */
	if(0 == seek_callback)
		return false;
	if((seek_status = seek_callback((FLAC__StreamEncoder*)encoder, position, client_data)) != FLAC__STREAM_ENCODER_SEEK_STATUS_OK) {
		if(seek_status == FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR)
			encoder->protected_->state = FLAC__STREAM_ENCODER_CLIENT_ERROR;
		return false;
	}

	ogg_page_checksum_set(page);

	/* re-write the page */
	if(write_callback((FLAC__StreamEncoder*)encoder, page->header, page->header_len, 0, 0, client_data) != FLAC__STREAM_ENCODER_WRITE_STATUS_OK) {
		encoder->protected_->state = FLAC__STREAM_ENCODER_CLIENT_ERROR;
		return false;
	}
	if(write_callback((FLAC__StreamEncoder*)encoder, page->body, page->body_len, 0, 0, client_data) != FLAC__STREAM_ENCODER_WRITE_STATUS_OK) {
		encoder->protected_->state = FLAC__STREAM_ENCODER_CLIENT_ERROR;
		return false;
	}

	return true;
}
//++-- ogg_helper.c 


//++ ogg_mapping.c  

const unsigned FLAC__OGG_MAPPING_PACKET_TYPE_LEN = 8; /* bits */

const FLAC__byte FLAC__OGG_MAPPING_FIRST_HEADER_PACKET_TYPE = 0x7f;

const FLAC__byte * const FLAC__OGG_MAPPING_MAGIC = (const FLAC__byte * const)"FLAC";

const unsigned FLAC__OGG_MAPPING_VERSION_MAJOR_LEN = 8; /* bits */
const unsigned FLAC__OGG_MAPPING_VERSION_MINOR_LEN = 8; /* bits */

const unsigned FLAC__OGG_MAPPING_NUM_HEADERS_LEN = 16; /* bits */
//++-- ogg_mapping.c  

//++ ogg_encoder_aspect.c 

static const FLAC__byte FLAC__OGG_MAPPING_VERSION_MAJOR = 1;
static const FLAC__byte FLAC__OGG_MAPPING_VERSION_MINOR = 0;

/***********************************************************************
 *
 * Public class methods
 *
 ***********************************************************************/

FLAC__bool FLAC__ogg_encoder_aspect_init(FLAC__OggEncoderAspect *aspect)
{
	/* we will determine the serial number later if necessary */
	if(ogg_stream_init(&aspect->stream_state, aspect->serial_number) != 0)
		return false;

	aspect->seen_magic = false;
	aspect->is_first_packet = true;
	aspect->samples_written = 0;

	return true;
}

void FLAC__ogg_encoder_aspect_finish(FLAC__OggEncoderAspect *aspect)
{
	(void)ogg_stream_clear(&aspect->stream_state);
	/*@@@ what about the page? */
}

void FLAC__ogg_encoder_aspect_set_serial_number(FLAC__OggEncoderAspect *aspect, long value)
{
	aspect->serial_number = value;
}

FLAC__bool FLAC__ogg_encoder_aspect_set_num_metadata(FLAC__OggEncoderAspect *aspect, unsigned value)
{
	if(value < (1u << FLAC__OGG_MAPPING_NUM_HEADERS_LEN)) {
		aspect->num_metadata = value;
		return true;
	}
	else
		return false;
}

void FLAC__ogg_encoder_aspect_set_defaults(FLAC__OggEncoderAspect *aspect)
{
	aspect->serial_number = 0;
	aspect->num_metadata = 0;
}

/*
 * The basic FLAC -> Ogg mapping goes like this:
 *
 * - 'fLaC' magic and STREAMINFO block get combined into the first
 *   packet.  The packet is prefixed with
 *   + the one-byte packet type 0x7F
 *   + 'FLAC' magic
 *   + the 2 byte Ogg FLAC mapping version number
 *   + tne 2 byte big-endian # of header packets
 * - The first packet is flushed to the first page.
 * - Each subsequent metadata block goes into its own packet.
 * - Each metadata packet is flushed to page (this is not required,
 *   the mapping only requires that a flush must occur after all
 *   metadata is written).
 * - Each subsequent FLAC audio frame goes into its own packet.
 *
 * WATCHOUT:
 * This depends on the behavior of FLAC__StreamEncoder that we get a
 * separate write callback for the fLaC magic, and then separate write
 * callbacks for each metadata block and audio frame.
 */
FLAC__StreamEncoderWriteStatus FLAC__ogg_encoder_aspect_write_callback_wrapper(FLAC__OggEncoderAspect *aspect, const FLAC__byte buffer[], size_t bytes, unsigned samples, unsigned current_frame, FLAC__bool is_last_block, FLAC__OggEncoderAspectWriteCallbackProxy write_callback, void *encoder, void *client_data)
{
	/* WATCHOUT:
	 * This depends on the behavior of FLAC__StreamEncoder that 'samples'
	 * will be 0 for metadata writes.
	 */
	const FLAC__bool is_metadata = (samples == 0);

	/*
	 * Treat fLaC magic packet specially.  We will note when we see it, then
	 * wait until we get the STREAMINFO and prepend it in that packet
	 */
	if(aspect->seen_magic) {
		ogg_packet packet;
		FLAC__byte synthetic_first_packet_body[
			FLAC__OGG_MAPPING_PACKET_TYPE_LENGTH +
			FLAC__OGG_MAPPING_MAGIC_LENGTH +
			FLAC__OGG_MAPPING_VERSION_MAJOR_LENGTH +
			FLAC__OGG_MAPPING_VERSION_MINOR_LENGTH +
			FLAC__OGG_MAPPING_NUM_HEADERS_LENGTH +
			FLAC__STREAM_SYNC_LENGTH +
			FLAC__STREAM_METADATA_HEADER_LENGTH +
			FLAC__STREAM_METADATA_STREAMINFO_LENGTH
		];

		memset(&packet, 0, sizeof(packet));
		packet.granulepos = aspect->samples_written + samples;

		if(aspect->is_first_packet) {
			FLAC__byte *b = synthetic_first_packet_body;
			if(bytes != FLAC__STREAM_METADATA_HEADER_LENGTH + FLAC__STREAM_METADATA_STREAMINFO_LENGTH) {
				/*
				 * If we get here, our assumption about the way write callbacks happen
				 * (explained above) is wrong
				 */
				FLAC__ASSERT(0);
				return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
			}
			/* add first header packet type */
			*b = FLAC__OGG_MAPPING_FIRST_HEADER_PACKET_TYPE;
			b += FLAC__OGG_MAPPING_PACKET_TYPE_LENGTH;
			/* add 'FLAC' mapping magic */
			memcpy(b, FLAC__OGG_MAPPING_MAGIC, FLAC__OGG_MAPPING_MAGIC_LENGTH);
			b += FLAC__OGG_MAPPING_MAGIC_LENGTH;
			/* add Ogg FLAC mapping major version number */
			memcpy(b, &FLAC__OGG_MAPPING_VERSION_MAJOR, FLAC__OGG_MAPPING_VERSION_MAJOR_LENGTH);
			b += FLAC__OGG_MAPPING_VERSION_MAJOR_LENGTH;
			/* add Ogg FLAC mapping minor version number */
			memcpy(b, &FLAC__OGG_MAPPING_VERSION_MINOR, FLAC__OGG_MAPPING_VERSION_MINOR_LENGTH);
			b += FLAC__OGG_MAPPING_VERSION_MINOR_LENGTH;
			/* add number of header packets */
			*b = (FLAC__byte)(aspect->num_metadata >> 8);
			b++;
			*b = (FLAC__byte)(aspect->num_metadata);
			b++;
			/* add native FLAC 'fLaC' magic */
			memcpy(b, FLAC__STREAM_SYNC_STRING, FLAC__STREAM_SYNC_LENGTH);
			b += FLAC__STREAM_SYNC_LENGTH;
			/* add STREAMINFO */
			memcpy(b, buffer, bytes);
			FLAC__ASSERT(b + bytes - synthetic_first_packet_body == sizeof(synthetic_first_packet_body));
			packet.packet = (unsigned char *)synthetic_first_packet_body;
			packet.bytes = sizeof(synthetic_first_packet_body);

			packet.b_o_s = 1;
			aspect->is_first_packet = false;
		}
		else {
			packet.packet = (unsigned char *)buffer;
			packet.bytes = bytes;
		}

		if(is_last_block) {
			/* we used to check:
			 * FLAC__ASSERT(total_samples_estimate == 0 || total_samples_estimate == aspect->samples_written + samples);
			 * but it's really not useful since total_samples_estimate is an estimate and can be inexact
			 */
			packet.e_o_s = 1;
		}

		if(ogg_stream_packetin(&aspect->stream_state, &packet) != 0)
			return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;

		/*@@@ can't figure out a way to pass a useful number for 'samples' to the write_callback, so we'll just pass 0 */
		if(is_metadata) {
			while(ogg_stream_flush(&aspect->stream_state, &aspect->page) != 0) {
				if(write_callback(encoder, aspect->page.header, aspect->page.header_len, 0, current_frame, client_data) != FLAC__STREAM_ENCODER_WRITE_STATUS_OK)
					return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
				if(write_callback(encoder, aspect->page.body, aspect->page.body_len, 0, current_frame, client_data) != FLAC__STREAM_ENCODER_WRITE_STATUS_OK)
					return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
			}
		}
		else {
			while(ogg_stream_pageout(&aspect->stream_state, &aspect->page) != 0) {
				if(write_callback(encoder, aspect->page.header, aspect->page.header_len, 0, current_frame, client_data) != FLAC__STREAM_ENCODER_WRITE_STATUS_OK)
					return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
				if(write_callback(encoder, aspect->page.body, aspect->page.body_len, 0, current_frame, client_data) != FLAC__STREAM_ENCODER_WRITE_STATUS_OK)
					return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
			}
		}
	}
	else if(is_metadata && current_frame == 0 && samples == 0 && bytes == 4 && 0 == memcmp(buffer, FLAC__STREAM_SYNC_STRING, sizeof(FLAC__STREAM_SYNC_STRING))) {
		aspect->seen_magic = true;
	}
	else {
		/*
		 * If we get here, our assumption about the way write callbacks happen
		 * explained above is wrong
		 */
		FLAC__ASSERT(0);
		return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
	}

	aspect->samples_written += samples;

	return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}
//++-- ogg_encoder_aspect.c 

//++ ogg_decoder_aspect.c 

/***********************************************************************
 *
 * Public class methods
 *
 ***********************************************************************/

FLAC__bool FLAC__ogg_decoder_aspect_init(FLAC__OggDecoderAspect *aspect)
{
	/* we will determine the serial number later if necessary */
	if(ogg_stream_init(&aspect->stream_state, aspect->serial_number) != 0)
		return false;

	if(ogg_sync_init(&aspect->sync_state) != 0)
		return false;

	aspect->version_major = ~(0u);
	aspect->version_minor = ~(0u);

	aspect->need_serial_number = aspect->use_first_serial_number;

	aspect->end_of_stream = false;
	aspect->have_working_page = false;

	return true;
}

void FLAC__ogg_decoder_aspect_finish(FLAC__OggDecoderAspect *aspect)
{
	(void)ogg_sync_clear(&aspect->sync_state);
	(void)ogg_stream_clear(&aspect->stream_state);
}

void FLAC__ogg_decoder_aspect_set_serial_number(FLAC__OggDecoderAspect *aspect, long value)
{
	aspect->use_first_serial_number = false;
	aspect->serial_number = value;
}

void FLAC__ogg_decoder_aspect_set_defaults(FLAC__OggDecoderAspect *aspect)
{
	aspect->use_first_serial_number = true;
}

void FLAC__ogg_decoder_aspect_flush(FLAC__OggDecoderAspect *aspect)
{
	(void)ogg_stream_reset(&aspect->stream_state);
	(void)ogg_sync_reset(&aspect->sync_state);
	aspect->end_of_stream = false;
	aspect->have_working_page = false;
}

void FLAC__ogg_decoder_aspect_reset(FLAC__OggDecoderAspect *aspect)
{
	FLAC__ogg_decoder_aspect_flush(aspect);

	if(aspect->use_first_serial_number)
		aspect->need_serial_number = true;
}

FLAC__OggDecoderAspectReadStatus FLAC__ogg_decoder_aspect_read_callback_wrapper(FLAC__OggDecoderAspect *aspect, FLAC__byte buffer[], size_t *bytes, FLAC__OggDecoderAspectReadCallbackProxy read_callback, const FLAC__StreamDecoder *decoder, void *client_data)
{
	static const size_t OGG_BYTES_CHUNK = 8192;
	const size_t bytes_requested = *bytes;

	/*
	 * The FLAC decoding API uses pull-based reads, whereas Ogg decoding
	 * is push-based.  In libFLAC, when you ask to decode a frame, the
	 * decoder will eventually call the read callback to supply some data,
	 * but how much it asks for depends on how much free space it has in
	 * its internal buffer.  It does not try to grow its internal buffer
	 * to accomodate a whole frame because then the internal buffer size
	 * could not be limited, which is necessary in embedded applications.
	 *
	 * Ogg however grows its internal buffer until a whole page is present;
	 * only then can you get decoded data out.  So we can't just ask for
	 * the same number of bytes from Ogg, then pass what's decoded down to
	 * libFLAC.  If what libFLAC is asking for will not contain a whole
	 * page, then we will get no data from ogg_sync_pageout(), and at the
	 * same time cannot just read more data from the client for the purpose
	 * of getting a whole decoded page because the decoded size might be
	 * larger than libFLAC's internal buffer.
	 *
	 * Instead, whenever this read callback wrapper is called, we will
	 * continually request data from the client until we have at least one
	 * page, and manage pages internally so that we can send pieces of
	 * pages down to libFLAC in such a way that we obey its size
	 * requirement.  To limit the amount of callbacks, we will always try
	 * to read in enough pages to return the full number of bytes
	 * requested.
	 */
	*bytes = 0;
	while (*bytes < bytes_requested && !aspect->end_of_stream) {
		if (aspect->have_working_page) {
			if (aspect->have_working_packet) {
				size_t n = bytes_requested - *bytes;
				if ((size_t)aspect->working_packet.bytes <= n) {
					/* the rest of the packet will fit in the buffer */
					n = aspect->working_packet.bytes;
					memcpy(buffer, aspect->working_packet.packet, n);
					*bytes += n;
					buffer += n;
					aspect->have_working_packet = false;
				}
				else {
					/* only n bytes of the packet will fit in the buffer */
					memcpy(buffer, aspect->working_packet.packet, n);
					*bytes += n;
					buffer += n;
					aspect->working_packet.packet += n;
					aspect->working_packet.bytes -= n;
				}
			}
			else {
				/* try and get another packet */
				const int ret = ogg_stream_packetout(&aspect->stream_state, &aspect->working_packet);
				if (ret > 0) {
					aspect->have_working_packet = true;
					/* if it is the first header packet, check for magic and a supported Ogg FLAC mapping version */
					if (aspect->working_packet.bytes > 0 && aspect->working_packet.packet[0] == FLAC__OGG_MAPPING_FIRST_HEADER_PACKET_TYPE) {
						const FLAC__byte *b = aspect->working_packet.packet;
						const unsigned header_length =
							FLAC__OGG_MAPPING_PACKET_TYPE_LENGTH +
							FLAC__OGG_MAPPING_MAGIC_LENGTH +
							FLAC__OGG_MAPPING_VERSION_MAJOR_LENGTH +
							FLAC__OGG_MAPPING_VERSION_MINOR_LENGTH +
							FLAC__OGG_MAPPING_NUM_HEADERS_LENGTH;
						if (aspect->working_packet.bytes < (long)header_length)
							return FLAC__OGG_DECODER_ASPECT_READ_STATUS_NOT_FLAC;
						b += FLAC__OGG_MAPPING_PACKET_TYPE_LENGTH;
						if (memcmp(b, FLAC__OGG_MAPPING_MAGIC, FLAC__OGG_MAPPING_MAGIC_LENGTH))
							return FLAC__OGG_DECODER_ASPECT_READ_STATUS_NOT_FLAC;
						b += FLAC__OGG_MAPPING_MAGIC_LENGTH;
						aspect->version_major = (unsigned)(*b);
						b += FLAC__OGG_MAPPING_VERSION_MAJOR_LENGTH;
						aspect->version_minor = (unsigned)(*b);
						if (aspect->version_major != 1)
							return FLAC__OGG_DECODER_ASPECT_READ_STATUS_UNSUPPORTED_MAPPING_VERSION;
						aspect->working_packet.packet += header_length;
						aspect->working_packet.bytes -= header_length;
					}
				}
				else if (ret == 0) {
					aspect->have_working_page = false;
				}
				else { /* ret < 0 */
					/* lost sync, we'll leave the working page for the next call */
					return FLAC__OGG_DECODER_ASPECT_READ_STATUS_LOST_SYNC;
				}
			}
		}
		else {
			/* try and get another page */
			const int ret = ogg_sync_pageout(&aspect->sync_state, &aspect->working_page);
			if (ret > 0) {
				/* got a page, grab the serial number if necessary */
				if(aspect->need_serial_number) {
					aspect->stream_state.serialno = aspect->serial_number = ogg_page_serialno(&aspect->working_page);
					aspect->need_serial_number = false;
				}
				if(ogg_stream_pagein(&aspect->stream_state, &aspect->working_page) == 0) {
					aspect->have_working_page = true;
					aspect->have_working_packet = false;
				}
				/* else do nothing, could be a page from another stream */
			}
			else if (ret == 0) {
				/* need more data */
				const size_t ogg_bytes_to_read = max(bytes_requested - *bytes, OGG_BYTES_CHUNK);
				char *oggbuf = ogg_sync_buffer(&aspect->sync_state, ogg_bytes_to_read);

				if(0 == oggbuf) {
					return FLAC__OGG_DECODER_ASPECT_READ_STATUS_MEMORY_ALLOCATION_ERROR;
				}
				else {
					size_t ogg_bytes_read = ogg_bytes_to_read;

					switch(read_callback(decoder, (FLAC__byte*)oggbuf, &ogg_bytes_read, client_data)) {
						case FLAC__OGG_DECODER_ASPECT_READ_STATUS_OK:
							break;
						case FLAC__OGG_DECODER_ASPECT_READ_STATUS_END_OF_STREAM:
							aspect->end_of_stream = true;
							break;
						case FLAC__OGG_DECODER_ASPECT_READ_STATUS_ABORT:
							return FLAC__OGG_DECODER_ASPECT_READ_STATUS_ABORT;
						default:
							FLAC__ASSERT(0);
					}

					if(ogg_sync_wrote(&aspect->sync_state, ogg_bytes_read) < 0) {
						/* double protection; this will happen if the read callback returns more bytes than the max requested, which would overflow Ogg's internal buffer */
						FLAC__ASSERT(0);
						return FLAC__OGG_DECODER_ASPECT_READ_STATUS_ERROR;
					}
				}
			}
			else { /* ret < 0 */
				/* lost sync, bail out */
				return FLAC__OGG_DECODER_ASPECT_READ_STATUS_LOST_SYNC;
			}
		}
	}

	if (aspect->end_of_stream && *bytes == 0) {
		return FLAC__OGG_DECODER_ASPECT_READ_STATUS_END_OF_STREAM;
	}

	return FLAC__OGG_DECODER_ASPECT_READ_STATUS_OK;
}
//++-- ogg_decoder_aspect.c 

#if 1 //++ bitreader.c 

#ifdef _MSC_VER
#include <winsock.h> /* for ntohl() */
#elif defined FLAC__SYS_DARWIN
#include <machine/endian.h> /* for ntohl() */
#elif defined __MINGW32__
#include <winsock.h> /* for ntohl() */
#else
#include <netinet/in.h> /* for ntohl() */
#endif

/* Things should be fastest when this matches the machine word size */
/* WATCHOUT: if you change this you must also change the following #defines down to COUNT_ZERO_MSBS below to match */
/* WATCHOUT: there are a few places where the code will not work unless brword is >= 32 bits wide */
/*           also, some sections currently only have fast versions for 4 or 8 bytes per word */
typedef FLAC__uint32 brword;
#define FLAC__BYTES_PER_WORD 4
#define FLAC__BITS_PER_WORD 32
#define FLAC__WORD_ALL_ONES ((FLAC__uint32)0xffffffff)
/* SWAP_BE_WORD_TO_HOST swaps bytes in a brword (which is always big-endian) if necessary to match host byte order */
#if WORDS_BIGENDIAN
#define SWAP_BE_WORD_TO_HOST(x) (x)
#else
#ifdef _MSC_VER
#define SWAP_BE_WORD_TO_HOST(x) local_swap32_(x)
#else
#define SWAP_BE_WORD_TO_HOST(x) ntohl(x)
#endif
#endif
/* counts the # of zero MSBs in a word */
#define COUNT_ZERO_MSBS(word) ( \
	(word) <= 0xffff ? \
		( (word) <= 0xff? byte_to_unary_table[word] + 24 : byte_to_unary_table[(word) >> 8] + 16 ) : \
		( (word) <= 0xffffff? byte_to_unary_table[word >> 16] + 8 : byte_to_unary_table[(word) >> 24] ) \
)
/* this alternate might be slightly faster on some systems/compilers: */
#define COUNT_ZERO_MSBS2(word) ( (word) <= 0xff ? byte_to_unary_table[word] + 24 : ((word) <= 0xffff ? byte_to_unary_table[(word) >> 8] + 16 : ((word) <= 0xffffff ? byte_to_unary_table[(word) >> 16] + 8 : byte_to_unary_table[(word) >> 24])) )


/*
 * This should be at least twice as large as the largest number of words
 * required to represent any 'number' (in any encoding) you are going to
 * read.  With FLAC this is on the order of maybe a few hundred bits.
 * If the buffer is smaller than that, the decoder won't be able to read
 * in a whole number that is in a variable length encoding (e.g. Rice).
 * But to be practical it should be at least 1K bytes.
 *
 * Increase this number to decrease the number of read callbacks, at the
 * expense of using more memory.  Or decrease for the reverse effect,
 * keeping in mind the limit from the first paragraph.  The optimal size
 * also depends on the CPU cache size and other factors; some twiddling
 * may be necessary to squeeze out the best performance.
 */
static const unsigned FLAC__BITREADER_DEFAULT_CAPACITY = 65536u / FLAC__BITS_PER_WORD; /* in words */

static const unsigned char byte_to_unary_table[] = {
	8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4,
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* adjust for compilers that can't understand using LLU suffix for uint64_t literals */
#ifdef _MSC_VER
#define FLAC__U64L(x) x
#else
#define FLAC__U64L(x) x##LLU
#endif

#ifndef FLaC__INLINE
#define FLaC__INLINE
#endif

/* WATCHOUT: assembly routines rely on the order in which these fields are declared */
struct FLAC__BitReader {
	/* any partially-consumed word at the head will stay right-justified as bits are consumed from the left */
	/* any incomplete word at the tail will be left-justified, and bytes from the read callback are added on the right */
	brword *buffer;
	unsigned capacity; /* in words */
	unsigned words; /* # of completed words in buffer */
	unsigned bytes; /* # of bytes in incomplete word at buffer[words] */
	unsigned consumed_words; /* #words ... */
	unsigned consumed_bits; /* ... + (#bits of head word) already consumed from the front of buffer */
	unsigned read_crc16; /* the running frame CRC */
	unsigned crc16_align; /* the number of bits in the current consumed word that should not be CRC'd */
	FLAC__BitReaderReadCallback read_callback;
	void *client_data;
	FLAC__CPUInfo cpu_info;
};

#ifdef _MSC_VER
/* OPT: an MSVC built-in would be better */
//-= static _inline FLAC__uint32 local_swap32_(FLAC__uint32 x)
// {
// 	x = ((x<<8)&0xFF00FF00) | ((x>>8)&0x00FF00FF);
// 	return (x>>16) | (x<<16);
// }
static void local_swap32_block_(FLAC__uint32 *start, FLAC__uint32 len)
{
	__asm {
		mov edx, start
		mov ecx, len
		test ecx, ecx
loop1:
		jz done1
		mov eax, [edx]
		bswap eax
		mov [edx], eax
		add edx, 4
		dec ecx
		jmp short loop1
done1:
	}
}
#endif

static FLaC__INLINE void crc16_update_word_(FLAC__BitReader *br, brword word)
{
	register unsigned crc = br->read_crc16;
#if FLAC__BYTES_PER_WORD == 4
	switch(br->crc16_align) {
		case  0: crc = FLAC__CRC16_UPDATE((unsigned)(word >> 24), crc);
		case  8: crc = FLAC__CRC16_UPDATE((unsigned)((word >> 16) & 0xff), crc);
		case 16: crc = FLAC__CRC16_UPDATE((unsigned)((word >> 8) & 0xff), crc);
		case 24: br->read_crc16 = FLAC__CRC16_UPDATE((unsigned)(word & 0xff), crc);
	}
#elif FLAC__BYTES_PER_WORD == 8
	switch(br->crc16_align) {
		case  0: crc = FLAC__CRC16_UPDATE((unsigned)(word >> 56), crc);
		case  8: crc = FLAC__CRC16_UPDATE((unsigned)((word >> 48) & 0xff), crc);
		case 16: crc = FLAC__CRC16_UPDATE((unsigned)((word >> 40) & 0xff), crc);
		case 24: crc = FLAC__CRC16_UPDATE((unsigned)((word >> 32) & 0xff), crc);
		case 32: crc = FLAC__CRC16_UPDATE((unsigned)((word >> 24) & 0xff), crc);
		case 40: crc = FLAC__CRC16_UPDATE((unsigned)((word >> 16) & 0xff), crc);
		case 48: crc = FLAC__CRC16_UPDATE((unsigned)((word >> 8) & 0xff), crc);
		case 56: br->read_crc16 = FLAC__CRC16_UPDATE((unsigned)(word & 0xff), crc);
	}
#else
	for( ; br->crc16_align < FLAC__BITS_PER_WORD; br->crc16_align += 8)
		crc = FLAC__CRC16_UPDATE((unsigned)((word >> (FLAC__BITS_PER_WORD-8-br->crc16_align)) & 0xff), crc);
	br->read_crc16 = crc;
#endif
	br->crc16_align = 0;
}

/* would be static except it needs to be called by asm routines */
FLAC__bool bitreader_read_from_client_(FLAC__BitReader *br)
{
	unsigned start, end;
	size_t bytes;
	FLAC__byte *target;

	/* first shift the unconsumed buffer data toward the front as much as possible */
	if(br->consumed_words > 0) {
		start = br->consumed_words;
		end = br->words + (br->bytes? 1:0);
		memmove(br->buffer, br->buffer+start, FLAC__BYTES_PER_WORD * (end - start));

		br->words -= start;
		br->consumed_words = 0;
	}

	/*
	 * set the target for reading, taking into account word alignment and endianness
	 */
	bytes = (br->capacity - br->words) * FLAC__BYTES_PER_WORD - br->bytes;
	if(bytes == 0)
		return false; /* no space left, buffer is too small; see note for FLAC__BITREADER_DEFAULT_CAPACITY  */
	target = ((FLAC__byte*)(br->buffer+br->words)) + br->bytes;

	/* before reading, if the existing reader looks like this (say brword is 32 bits wide)
	 *   bitstream :  11 22 33 44 55            br->words=1 br->bytes=1 (partial tail word is left-justified)
	 *   buffer[BE]:  11 22 33 44 55 ?? ?? ??   (shown layed out as bytes sequentially in memory)
	 *   buffer[LE]:  44 33 22 11 ?? ?? ?? 55   (?? being don't-care)
	 *                               ^^-------target, bytes=3
	 * on LE machines, have to byteswap the odd tail word so nothing is
	 * overwritten:
	 */
#if WORDS_BIGENDIAN
#else
	if(br->bytes)
		br->buffer[br->words] = SWAP_BE_WORD_TO_HOST(br->buffer[br->words]);
#endif

	/* now it looks like:
	 *   bitstream :  11 22 33 44 55            br->words=1 br->bytes=1
	 *   buffer[BE]:  11 22 33 44 55 ?? ?? ??
	 *   buffer[LE]:  44 33 22 11 55 ?? ?? ??
	 *                               ^^-------target, bytes=3
	 */

	/* read in the data; note that the callback may return a smaller number of bytes */
	if(!br->read_callback(target, &bytes, br->client_data))
		return false;

	/* after reading bytes 66 77 88 99 AA BB CC DD EE FF from the client:
	 *   bitstream :  11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF
	 *   buffer[BE]:  11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF ??
	 *   buffer[LE]:  44 33 22 11 55 66 77 88 99 AA BB CC DD EE FF ??
	 * now have to byteswap on LE machines:
	 */
#if WORDS_BIGENDIAN
#else
	end = (br->words*FLAC__BYTES_PER_WORD + br->bytes + bytes + (FLAC__BYTES_PER_WORD-1)) / FLAC__BYTES_PER_WORD;
# if defined(_MSC_VER) && (FLAC__BYTES_PER_WORD == 4)
	if(br->cpu_info.type == FLAC__CPUINFO_TYPE_IA32 && br->cpu_info.data.ia32.bswap) {
		start = br->words;
		local_swap32_block_(br->buffer + start, end - start);
	}
	else
# endif
	for(start = br->words; start < end; start++)
		br->buffer[start] = SWAP_BE_WORD_TO_HOST(br->buffer[start]);
#endif

	/* now it looks like:
	 *   bitstream :  11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF
	 *   buffer[BE]:  11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF ??
	 *   buffer[LE]:  44 33 22 11 88 77 66 55 CC BB AA 99 ?? FF EE DD
	 * finally we'll update the reader values:
	 */
	end = br->words*FLAC__BYTES_PER_WORD + br->bytes + bytes;
	br->words = end / FLAC__BYTES_PER_WORD;
	br->bytes = end % FLAC__BYTES_PER_WORD;

	return true;
}

/***********************************************************************
 *
 * Class constructor/destructor
 *
 ***********************************************************************/

FLAC__BitReader *FLAC__bitreader_new(void)
{
	FLAC__BitReader *br = (FLAC__BitReader*)calloc(1, sizeof(FLAC__BitReader));

	/* calloc() implies:
		memset(br, 0, sizeof(FLAC__BitReader));
		br->buffer = 0;
		br->capacity = 0;
		br->words = br->bytes = 0;
		br->consumed_words = br->consumed_bits = 0;
		br->read_callback = 0;
		br->client_data = 0;
	*/
	return br;
}

void FLAC__bitreader_delete(FLAC__BitReader *br)
{
	FLAC__ASSERT(0 != br);

	FLAC__bitreader_free(br);
	free(br);
}

/***********************************************************************
 *
 * Public class methods
 *
 ***********************************************************************/

FLAC__bool FLAC__bitreader_init(FLAC__BitReader *br, FLAC__CPUInfo cpu, FLAC__BitReaderReadCallback rcb, void *cd)
{
	FLAC__ASSERT(0 != br);

	br->words = br->bytes = 0;
	br->consumed_words = br->consumed_bits = 0;
	br->capacity = FLAC__BITREADER_DEFAULT_CAPACITY;
	br->buffer = (brword*)malloc(sizeof(brword) * br->capacity);
	if(br->buffer == 0)
		return false;
	br->read_callback = rcb;
	br->client_data = cd;
	br->cpu_info = cpu;

	return true;
}

void FLAC__bitreader_free(FLAC__BitReader *br)
{
	FLAC__ASSERT(0 != br);

	if(0 != br->buffer)
		free(br->buffer);
	br->buffer = 0;
	br->capacity = 0;
	br->words = br->bytes = 0;
	br->consumed_words = br->consumed_bits = 0;
	br->read_callback = 0;
	br->client_data = 0;
}

FLAC__bool FLAC__bitreader_clear(FLAC__BitReader *br)
{
	br->words = br->bytes = 0;
	br->consumed_words = br->consumed_bits = 0;
	return true;
}

void FLAC__bitreader_dump(const FLAC__BitReader *br, FILE *out)
{
	unsigned i, j;
	if(br == 0) {
		fprintf(out, "bitreader is NULL\n");
	}
	else {
		fprintf(out, "bitreader: capacity=%u words=%u bytes=%u consumed: words=%u, bits=%u\n", br->capacity, br->words, br->bytes, br->consumed_words, br->consumed_bits);

		for(i = 0; i < br->words; i++) {
			fprintf(out, "%08X: ", i);
			for(j = 0; j < FLAC__BITS_PER_WORD; j++)
				if(i < br->consumed_words || (i == br->consumed_words && j < br->consumed_bits))
					fprintf(out, ".");
				else
					fprintf(out, "%01u", br->buffer[i] & (1 << (FLAC__BITS_PER_WORD-j-1)) ? 1:0);
			fprintf(out, "\n");
		}
		if(br->bytes > 0) {
			fprintf(out, "%08X: ", i);
			for(j = 0; j < br->bytes*8; j++)
				if(i < br->consumed_words || (i == br->consumed_words && j < br->consumed_bits))
					fprintf(out, ".");
				else
					fprintf(out, "%01u", br->buffer[i] & (1 << (br->bytes*8-j-1)) ? 1:0);
			fprintf(out, "\n");
		}
	}
}

void FLAC__bitreader_reset_read_crc16(FLAC__BitReader *br, FLAC__uint16 seed)
{
	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);
	FLAC__ASSERT((br->consumed_bits & 7) == 0);

	br->read_crc16 = (unsigned)seed;
	br->crc16_align = br->consumed_bits;
}

FLAC__uint16 FLAC__bitreader_get_read_crc16(FLAC__BitReader *br)
{
	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);
	FLAC__ASSERT((br->consumed_bits & 7) == 0);
	FLAC__ASSERT(br->crc16_align <= br->consumed_bits);

	/* CRC any tail bytes in a partially-consumed word */
	if(br->consumed_bits) {
		const brword tail = br->buffer[br->consumed_words];
		for( ; br->crc16_align < br->consumed_bits; br->crc16_align += 8)
			br->read_crc16 = FLAC__CRC16_UPDATE((unsigned)((tail >> (FLAC__BITS_PER_WORD-8-br->crc16_align)) & 0xff), br->read_crc16);
	}
	return br->read_crc16;
}

FLaC__INLINE FLAC__bool FLAC__bitreader_is_consumed_byte_aligned(const FLAC__BitReader *br)
{
	return ((br->consumed_bits & 7) == 0);
}

FLaC__INLINE unsigned FLAC__bitreader_bits_left_for_byte_alignment(const FLAC__BitReader *br)
{
	return 8 - (br->consumed_bits & 7);
}

FLaC__INLINE unsigned FLAC__bitreader_get_input_bits_unconsumed(const FLAC__BitReader *br)
{
	return (br->words-br->consumed_words)*FLAC__BITS_PER_WORD + br->bytes*8 - br->consumed_bits;
}

FLaC__INLINE FLAC__bool FLAC__bitreader_read_raw_uint32(FLAC__BitReader *br, FLAC__uint32 *val, unsigned bits)
{
	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);

	FLAC__ASSERT(bits <= 32);
	FLAC__ASSERT((br->capacity*FLAC__BITS_PER_WORD) * 2 >= bits);
	FLAC__ASSERT(br->consumed_words <= br->words);

	/* WATCHOUT: code does not work with <32bit words; we can make things much faster with this assertion */
	FLAC__ASSERT(FLAC__BITS_PER_WORD >= 32);

	if(bits == 0) { /* OPT: investigate if this can ever happen, maybe change to assertion */
		*val = 0;
		return true;
	}

	while((br->words-br->consumed_words)*FLAC__BITS_PER_WORD + br->bytes*8 - br->consumed_bits < bits) {
		if(!bitreader_read_from_client_(br))
			return false;
	}
	if(br->consumed_words < br->words) { /* if we've not consumed up to a partial tail word... */
		/* OPT: taking out the consumed_bits==0 "else" case below might make things faster if less code allows the compiler to inline this function */
		if(br->consumed_bits) {
			/* this also works when consumed_bits==0, it's just a little slower than necessary for that case */
			const unsigned n = FLAC__BITS_PER_WORD - br->consumed_bits;
			const brword word = br->buffer[br->consumed_words];
			if(bits < n) {
				*val = (word & (FLAC__WORD_ALL_ONES >> br->consumed_bits)) >> (n-bits);
				br->consumed_bits += bits;
				return true;
			}
			*val = word & (FLAC__WORD_ALL_ONES >> br->consumed_bits);
			bits -= n;
			crc16_update_word_(br, word);
			br->consumed_words++;
			br->consumed_bits = 0;
			if(bits) { /* if there are still bits left to read, there have to be less than 32 so they will all be in the next word */
				*val <<= bits;
				*val |= (br->buffer[br->consumed_words] >> (FLAC__BITS_PER_WORD-bits));
				br->consumed_bits = bits;
			}
			return true;
		}
		else {
			const brword word = br->buffer[br->consumed_words];
			if(bits < FLAC__BITS_PER_WORD) {
				*val = word >> (FLAC__BITS_PER_WORD-bits);
				br->consumed_bits = bits;
				return true;
			}
			/* at this point 'bits' must be == FLAC__BITS_PER_WORD; because of previous assertions, it can't be larger */
			*val = word;
			crc16_update_word_(br, word);
			br->consumed_words++;
			return true;
		}
	}
	else {
		/* in this case we're starting our read at a partial tail word;
		 * the reader has guaranteed that we have at least 'bits' bits
		 * available to read, which makes this case simpler.
		 */
		/* OPT: taking out the consumed_bits==0 "else" case below might make things faster if less code allows the compiler to inline this function */
		if(br->consumed_bits) {
			/* this also works when consumed_bits==0, it's just a little slower than necessary for that case */
			FLAC__ASSERT(br->consumed_bits + bits <= br->bytes*8);
			*val = (br->buffer[br->consumed_words] & (FLAC__WORD_ALL_ONES >> br->consumed_bits)) >> (FLAC__BITS_PER_WORD-br->consumed_bits-bits);
			br->consumed_bits += bits;
			return true;
		}
		else {
			*val = br->buffer[br->consumed_words] >> (FLAC__BITS_PER_WORD-bits);
			br->consumed_bits += bits;
			return true;
		}
	}
}

FLAC__bool FLAC__bitreader_read_raw_int32(FLAC__BitReader *br, FLAC__int32 *val, unsigned bits)
{
	/* OPT: inline raw uint32 code here, or make into a macro if possible in the .h file */
	if(!FLAC__bitreader_read_raw_uint32(br, (FLAC__uint32*)val, bits))
		return false;
	/* sign-extend: */
	*val <<= (32-bits);
	*val >>= (32-bits);
	return true;
}

FLAC__bool FLAC__bitreader_read_raw_uint64(FLAC__BitReader *br, FLAC__uint64 *val, unsigned bits)
{
	FLAC__uint32 hi, lo;

	if(bits > 32) {
		if(!FLAC__bitreader_read_raw_uint32(br, &hi, bits-32))
			return false;
		if(!FLAC__bitreader_read_raw_uint32(br, &lo, 32))
			return false;
		*val = hi;
		*val <<= 32;
		*val |= lo;
	}
	else {
		if(!FLAC__bitreader_read_raw_uint32(br, &lo, bits))
			return false;
		*val = lo;
	}
	return true;
}

FLaC__INLINE FLAC__bool FLAC__bitreader_read_uint32_little_endian(FLAC__BitReader *br, FLAC__uint32 *val)
{
	FLAC__uint32 x8, x32 = 0;

	/* this doesn't need to be that fast as currently it is only used for vorbis comments */

	if(!FLAC__bitreader_read_raw_uint32(br, &x32, 8))
		return false;

	if(!FLAC__bitreader_read_raw_uint32(br, &x8, 8))
		return false;
	x32 |= (x8 << 8);

	if(!FLAC__bitreader_read_raw_uint32(br, &x8, 8))
		return false;
	x32 |= (x8 << 16);

	if(!FLAC__bitreader_read_raw_uint32(br, &x8, 8))
		return false;
	x32 |= (x8 << 24);

	*val = x32;
	return true;
}

FLAC__bool FLAC__bitreader_skip_bits_no_crc(FLAC__BitReader *br, unsigned bits)
{
	/*
	 * OPT: a faster implementation is possible but probably not that useful
	 * since this is only called a couple of times in the metadata readers.
	 */
	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);

	if(bits > 0) {
		const unsigned n = br->consumed_bits & 7;
		unsigned m;
		FLAC__uint32 x;

		if(n != 0) {
			m = min(8-n, bits);
			if(!FLAC__bitreader_read_raw_uint32(br, &x, m))
				return false;
			bits -= m;
		}
		m = bits / 8;
		if(m > 0) {
			if(!FLAC__bitreader_skip_byte_block_aligned_no_crc(br, m))
				return false;
			bits %= 8;
		}
		if(bits > 0) {
			if(!FLAC__bitreader_read_raw_uint32(br, &x, bits))
				return false;
		}
	}

	return true;
}

FLAC__bool FLAC__bitreader_skip_byte_block_aligned_no_crc(FLAC__BitReader *br, unsigned nvals)
{
	FLAC__uint32 x;

	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);
	FLAC__ASSERT(FLAC__bitreader_is_consumed_byte_aligned(br));

	/* step 1: skip over partial head word to get word aligned */
	while(nvals && br->consumed_bits) { /* i.e. run until we read 'nvals' bytes or we hit the end of the head word */
		if(!FLAC__bitreader_read_raw_uint32(br, &x, 8))
			return false;
		nvals--;
	}
	if(0 == nvals)
		return true;
	/* step 2: skip whole words in chunks */
	while(nvals >= FLAC__BYTES_PER_WORD) {
		if(br->consumed_words < br->words) {
			br->consumed_words++;
			nvals -= FLAC__BYTES_PER_WORD;
		}
		else if(!bitreader_read_from_client_(br))
			return false;
	}
	/* step 3: skip any remainder from partial tail bytes */
	while(nvals) {
		if(!FLAC__bitreader_read_raw_uint32(br, &x, 8))
			return false;
		nvals--;
	}

	return true;
}

FLAC__bool FLAC__bitreader_read_byte_block_aligned_no_crc(FLAC__BitReader *br, FLAC__byte *val, unsigned nvals)
{
	FLAC__uint32 x;

	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);
	FLAC__ASSERT(FLAC__bitreader_is_consumed_byte_aligned(br));

	/* step 1: read from partial head word to get word aligned */
	while(nvals && br->consumed_bits) { /* i.e. run until we read 'nvals' bytes or we hit the end of the head word */
		if(!FLAC__bitreader_read_raw_uint32(br, &x, 8))
			return false;
		*val++ = (FLAC__byte)x;
		nvals--;
	}
	if(0 == nvals)
		return true;
	/* step 2: read whole words in chunks */
	while(nvals >= FLAC__BYTES_PER_WORD) {
		if(br->consumed_words < br->words) {
			const brword word = br->buffer[br->consumed_words++];
#if FLAC__BYTES_PER_WORD == 4
			val[0] = (FLAC__byte)(word >> 24);
			val[1] = (FLAC__byte)(word >> 16);
			val[2] = (FLAC__byte)(word >> 8);
			val[3] = (FLAC__byte)word;
#elif FLAC__BYTES_PER_WORD == 8
			val[0] = (FLAC__byte)(word >> 56);
			val[1] = (FLAC__byte)(word >> 48);
			val[2] = (FLAC__byte)(word >> 40);
			val[3] = (FLAC__byte)(word >> 32);
			val[4] = (FLAC__byte)(word >> 24);
			val[5] = (FLAC__byte)(word >> 16);
			val[6] = (FLAC__byte)(word >> 8);
			val[7] = (FLAC__byte)word;
#else
			for(x = 0; x < FLAC__BYTES_PER_WORD; x++)
				val[x] = (FLAC__byte)(word >> (8*(FLAC__BYTES_PER_WORD-x-1)));
#endif
			val += FLAC__BYTES_PER_WORD;
			nvals -= FLAC__BYTES_PER_WORD;
		}
		else if(!bitreader_read_from_client_(br))
			return false;
	}
	/* step 3: read any remainder from partial tail bytes */
	while(nvals) {
		if(!FLAC__bitreader_read_raw_uint32(br, &x, 8))
			return false;
		*val++ = (FLAC__byte)x;
		nvals--;
	}

	return true;
}

FLaC__INLINE FLAC__bool FLAC__bitreader_read_unary_unsigned(FLAC__BitReader *br, unsigned *val)
#if 0 /* slow but readable version */
{
	unsigned bit;

	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);

	*val = 0;
	while(1) {
		if(!FLAC__bitreader_read_bit(br, &bit))
			return false;
		if(bit)
			break;
		else
			*val++;
	}
	return true;
}
#else
{
	unsigned i;

	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);

	*val = 0;
	while(1) {
		while(br->consumed_words < br->words) { /* if we've not consumed up to a partial tail word... */
			brword b = br->buffer[br->consumed_words] << br->consumed_bits;
			if(b) {
				i = COUNT_ZERO_MSBS(b);
				*val += i;
				i++;
				br->consumed_bits += i;
				if(br->consumed_bits >= FLAC__BITS_PER_WORD) { /* faster way of testing if(br->consumed_bits == FLAC__BITS_PER_WORD) */
					crc16_update_word_(br, br->buffer[br->consumed_words]);
					br->consumed_words++;
					br->consumed_bits = 0;
				}
				return true;
			}
			else {
				*val += FLAC__BITS_PER_WORD - br->consumed_bits;
				crc16_update_word_(br, br->buffer[br->consumed_words]);
				br->consumed_words++;
				br->consumed_bits = 0;
				/* didn't find stop bit yet, have to keep going... */
			}
		}
		/* at this point we've eaten up all the whole words; have to try
		 * reading through any tail bytes before calling the read callback.
		 * this is a repeat of the above logic adjusted for the fact we
		 * don't have a whole word.  note though if the client is feeding
		 * us data a byte at a time (unlikely), br->consumed_bits may not
		 * be zero.
		 */
		if(br->bytes) {
			const unsigned end = br->bytes * 8;
			brword b = (br->buffer[br->consumed_words] & (FLAC__WORD_ALL_ONES << (FLAC__BITS_PER_WORD-end))) << br->consumed_bits;
			if(b) {
				i = COUNT_ZERO_MSBS(b);
				*val += i;
				i++;
				br->consumed_bits += i;
				FLAC__ASSERT(br->consumed_bits < FLAC__BITS_PER_WORD);
				return true;
			}
			else {
				*val += end - br->consumed_bits;
				br->consumed_bits += end;
				FLAC__ASSERT(br->consumed_bits < FLAC__BITS_PER_WORD);
				/* didn't find stop bit yet, have to keep going... */
			}
		}
		if(!bitreader_read_from_client_(br))
			return false;
	}
}
#endif

FLAC__bool FLAC__bitreader_read_rice_signed(FLAC__BitReader *br, int *val, unsigned parameter)
{
	FLAC__uint32 lsbs = 0, msbs = 0;
	unsigned uval;

	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);
	FLAC__ASSERT(parameter <= 31);

	/* read the unary MSBs and end bit */
	if(!FLAC__bitreader_read_unary_unsigned(br, &msbs))
		return false;

	/* read the binary LSBs */
	if(!FLAC__bitreader_read_raw_uint32(br, &lsbs, parameter))
		return false;

	/* compose the value */
	uval = (msbs << parameter) | lsbs;
	if(uval & 1)
		*val = -((int)(uval >> 1)) - 1;
	else
		*val = (int)(uval >> 1);

	return true;
}

/* this is by far the most heavily used reader call.  it ain't pretty but it's fast */
/* a lot of the logic is copied, then adapted, from FLAC__bitreader_read_unary_unsigned() and FLAC__bitreader_read_raw_uint32() */
FLAC__bool FLAC__bitreader_read_rice_signed_block(FLAC__BitReader *br, int vals[], unsigned nvals, unsigned parameter)
/* OPT: possibly faster version for use with MSVC */
#ifdef _MSC_VER
{
	unsigned i;
	unsigned uval = 0;
	unsigned bits; /* the # of binary LSBs left to read to finish a rice codeword */

	/* try and get br->consumed_words and br->consumed_bits into register;
	 * must remember to flush them back to *br before calling other
	 * bitwriter functions that use them, and before returning */
	register unsigned cwords;
	register unsigned cbits;

	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);
	/* WATCHOUT: code does not work with <32bit words; we can make things much faster with this assertion */
	FLAC__ASSERT(FLAC__BITS_PER_WORD >= 32);
	FLAC__ASSERT(parameter < 32);
	/* the above two asserts also guarantee that the binary part never straddles more that 2 words, so we don't have to loop to read it */

	if(nvals == 0)
		return true;

	cbits = br->consumed_bits;
	cwords = br->consumed_words;

	while(1) {

		/* read unary part */
		while(1) {
			while(cwords < br->words) { /* if we've not consumed up to a partial tail word... */
				brword b = br->buffer[cwords] << cbits;
				if(b) {
#if 0 /* slower, probably due to bad register allocation... */ && defined FLAC__CPU_IA32 && !defined FLAC__NO_ASM && FLAC__BITS_PER_WORD == 32
					__asm {
						bsr eax, b
						not eax
						and eax, 31
						mov i, eax
					}
#else
					i = COUNT_ZERO_MSBS(b);
#endif
					uval += i;
					bits = parameter;
					i++;
					cbits += i;
					if(cbits == FLAC__BITS_PER_WORD) {
						crc16_update_word_(br, br->buffer[cwords]);
						cwords++;
						cbits = 0;
					}
					goto break1;
				}
				else {
					uval += FLAC__BITS_PER_WORD - cbits;
					crc16_update_word_(br, br->buffer[cwords]);
					cwords++;
					cbits = 0;
					/* didn't find stop bit yet, have to keep going... */
				}
			}
			/* at this point we've eaten up all the whole words; have to try
			 * reading through any tail bytes before calling the read callback.
			 * this is a repeat of the above logic adjusted for the fact we
			 * don't have a whole word.  note though if the client is feeding
			 * us data a byte at a time (unlikely), br->consumed_bits may not
			 * be zero.
			 */
			if(br->bytes) {
				const unsigned end = br->bytes * 8;
				brword b = (br->buffer[cwords] & (FLAC__WORD_ALL_ONES << (FLAC__BITS_PER_WORD-end))) << cbits;
				if(b) {
					i = COUNT_ZERO_MSBS(b);
					uval += i;
					bits = parameter;
					i++;
					cbits += i;
					FLAC__ASSERT(cbits < FLAC__BITS_PER_WORD);
					goto break1;
				}
				else {
					uval += end - cbits;
					cbits += end;
					FLAC__ASSERT(cbits < FLAC__BITS_PER_WORD);
					/* didn't find stop bit yet, have to keep going... */
				}
			}
			/* flush registers and read; bitreader_read_from_client_() does
			 * not touch br->consumed_bits at all but we still need to set
			 * it in case it fails and we have to return false.
			 */
			br->consumed_bits = cbits;
			br->consumed_words = cwords;
			if(!bitreader_read_from_client_(br))
				return false;
			cwords = br->consumed_words;
		}
break1:
		/* read binary part */
		FLAC__ASSERT(cwords <= br->words);

		if(bits) {
			while((br->words-cwords)*FLAC__BITS_PER_WORD + br->bytes*8 - cbits < bits) {
				/* flush registers and read; bitreader_read_from_client_() does
				 * not touch br->consumed_bits at all but we still need to set
				 * it in case it fails and we have to return false.
				 */
				br->consumed_bits = cbits;
				br->consumed_words = cwords;
				if(!bitreader_read_from_client_(br))
					return false;
				cwords = br->consumed_words;
			}
			if(cwords < br->words) { /* if we've not consumed up to a partial tail word... */
				if(cbits) {
					/* this also works when consumed_bits==0, it's just a little slower than necessary for that case */
					const unsigned n = FLAC__BITS_PER_WORD - cbits;
					const brword word = br->buffer[cwords];
					if(bits < n) {
						uval <<= bits;
						uval |= (word & (FLAC__WORD_ALL_ONES >> cbits)) >> (n-bits);
						cbits += bits;
						goto break2;
					}
					uval <<= n;
					uval |= word & (FLAC__WORD_ALL_ONES >> cbits);
					bits -= n;
					crc16_update_word_(br, word);
					cwords++;
					cbits = 0;
					if(bits) { /* if there are still bits left to read, there have to be less than 32 so they will all be in the next word */
						uval <<= bits;
						uval |= (br->buffer[cwords] >> (FLAC__BITS_PER_WORD-bits));
						cbits = bits;
					}
					goto break2;
				}
				else {
					FLAC__ASSERT(bits < FLAC__BITS_PER_WORD);
					uval <<= bits;
					uval |= br->buffer[cwords] >> (FLAC__BITS_PER_WORD-bits);
					cbits = bits;
					goto break2;
				}
			}
			else {
				/* in this case we're starting our read at a partial tail word;
				 * the reader has guaranteed that we have at least 'bits' bits
				 * available to read, which makes this case simpler.
				 */
				uval <<= bits;
				if(cbits) {
					/* this also works when consumed_bits==0, it's just a little slower than necessary for that case */
					FLAC__ASSERT(cbits + bits <= br->bytes*8);
					uval |= (br->buffer[cwords] & (FLAC__WORD_ALL_ONES >> cbits)) >> (FLAC__BITS_PER_WORD-cbits-bits);
					cbits += bits;
					goto break2;
				}
				else {
					uval |= br->buffer[cwords] >> (FLAC__BITS_PER_WORD-bits);
					cbits += bits;
					goto break2;
				}
			}
		}
break2:
		/* compose the value */
		*vals = (int)(uval >> 1 ^ -(int)(uval & 1));

		/* are we done? */
		--nvals;
		if(nvals == 0) {
			br->consumed_bits = cbits;
			br->consumed_words = cwords;
			return true;
		}

		uval = 0;
		++vals;

	}
}
#else
{
	unsigned i;
	unsigned uval = 0;

	/* try and get br->consumed_words and br->consumed_bits into register;
	 * must remember to flush them back to *br before calling other
	 * bitwriter functions that use them, and before returning */
	register unsigned cwords;
	register unsigned cbits;
	unsigned ucbits; /* keep track of the number of unconsumed bits in the buffer */

	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);
	/* WATCHOUT: code does not work with <32bit words; we can make things much faster with this assertion */
	FLAC__ASSERT(FLAC__BITS_PER_WORD >= 32);
	FLAC__ASSERT(parameter < 32);
	/* the above two asserts also guarantee that the binary part never straddles more than 2 words, so we don't have to loop to read it */

	if(nvals == 0)
		return true;

	cbits = br->consumed_bits;
	cwords = br->consumed_words;
	ucbits = (br->words-cwords)*FLAC__BITS_PER_WORD + br->bytes*8 - cbits;

	while(1) {

		/* read unary part */
		while(1) {
			while(cwords < br->words) { /* if we've not consumed up to a partial tail word... */
				brword b = br->buffer[cwords] << cbits;
				if(b) {
#if 0 /* is not discernably faster... */ && defined FLAC__CPU_IA32 && !defined FLAC__NO_ASM && FLAC__BITS_PER_WORD == 32 && defined __GNUC__
					asm volatile (
						"bsrl %1, %0;"
						"notl %0;"
						"andl $31, %0;"
						: "=r"(i)
						: "r"(b)
					);
#else
					i = COUNT_ZERO_MSBS(b);
#endif
					uval += i;
					cbits += i;
					cbits++; /* skip over stop bit */
					if(cbits >= FLAC__BITS_PER_WORD) { /* faster way of testing if(cbits == FLAC__BITS_PER_WORD) */
						crc16_update_word_(br, br->buffer[cwords]);
						cwords++;
						cbits = 0;
					}
					goto break1;
				}
				else {
					uval += FLAC__BITS_PER_WORD - cbits;
					crc16_update_word_(br, br->buffer[cwords]);
					cwords++;
					cbits = 0;
					/* didn't find stop bit yet, have to keep going... */
				}
			}
			/* at this point we've eaten up all the whole words; have to try
			 * reading through any tail bytes before calling the read callback.
			 * this is a repeat of the above logic adjusted for the fact we
			 * don't have a whole word.  note though if the client is feeding
			 * us data a byte at a time (unlikely), br->consumed_bits may not
			 * be zero.
			 */
			if(br->bytes) {
				const unsigned end = br->bytes * 8;
				brword b = (br->buffer[cwords] & ~(FLAC__WORD_ALL_ONES >> end)) << cbits;
				if(b) {
					i = COUNT_ZERO_MSBS(b);
					uval += i;
					cbits += i;
					cbits++; /* skip over stop bit */
					FLAC__ASSERT(cbits < FLAC__BITS_PER_WORD);
					goto break1;
				}
				else {
					uval += end - cbits;
					cbits += end;
					FLAC__ASSERT(cbits < FLAC__BITS_PER_WORD);
					/* didn't find stop bit yet, have to keep going... */
				}
			}
			/* flush registers and read; bitreader_read_from_client_() does
			 * not touch br->consumed_bits at all but we still need to set
			 * it in case it fails and we have to return false.
			 */
			br->consumed_bits = cbits;
			br->consumed_words = cwords;
			if(!bitreader_read_from_client_(br))
				return false;
			cwords = br->consumed_words;
			ucbits = (br->words-cwords)*FLAC__BITS_PER_WORD + br->bytes*8 - cbits + uval;
			/* + uval to offset our count by the # of unary bits already
			 * consumed before the read, because we will add these back
			 * in all at once at break1
			 */
		}
break1:
		ucbits -= uval;
		ucbits--; /* account for stop bit */

		/* read binary part */
		FLAC__ASSERT(cwords <= br->words);

		if(parameter) {
			while(ucbits < parameter) {
				/* flush registers and read; bitreader_read_from_client_() does
				 * not touch br->consumed_bits at all but we still need to set
				 * it in case it fails and we have to return false.
				 */
				br->consumed_bits = cbits;
				br->consumed_words = cwords;
				if(!bitreader_read_from_client_(br))
					return false;
				cwords = br->consumed_words;
				ucbits = (br->words-cwords)*FLAC__BITS_PER_WORD + br->bytes*8 - cbits;
			}
			if(cwords < br->words) { /* if we've not consumed up to a partial tail word... */
				if(cbits) {
					/* this also works when consumed_bits==0, it's just slower than necessary for that case */
					const unsigned n = FLAC__BITS_PER_WORD - cbits;
					const brword word = br->buffer[cwords];
					if(parameter < n) {
						uval <<= parameter;
						uval |= (word & (FLAC__WORD_ALL_ONES >> cbits)) >> (n-parameter);
						cbits += parameter;
					}
					else {
						uval <<= n;
						uval |= word & (FLAC__WORD_ALL_ONES >> cbits);
						crc16_update_word_(br, word);
						cwords++;
						cbits = parameter - n;
						if(cbits) { /* parameter > n, i.e. if there are still bits left to read, there have to be less than 32 so they will all be in the next word */
							uval <<= cbits;
							uval |= (br->buffer[cwords] >> (FLAC__BITS_PER_WORD-cbits));
						}
					}
				}
				else {
					cbits = parameter;
					uval <<= parameter;
					uval |= br->buffer[cwords] >> (FLAC__BITS_PER_WORD-cbits);
				}
			}
			else {
				/* in this case we're starting our read at a partial tail word;
				 * the reader has guaranteed that we have at least 'parameter'
				 * bits available to read, which makes this case simpler.
				 */
				uval <<= parameter;
				if(cbits) {
					/* this also works when consumed_bits==0, it's just a little slower than necessary for that case */
					FLAC__ASSERT(cbits + parameter <= br->bytes*8);
					uval |= (br->buffer[cwords] & (FLAC__WORD_ALL_ONES >> cbits)) >> (FLAC__BITS_PER_WORD-cbits-parameter);
					cbits += parameter;
				}
				else {
					cbits = parameter;
					uval |= br->buffer[cwords] >> (FLAC__BITS_PER_WORD-cbits);
				}
			}
		}

		ucbits -= parameter;

		/* compose the value */
		*vals = (int)(uval >> 1 ^ -(int)(uval & 1));

		/* are we done? */
		--nvals;
		if(nvals == 0) {
			br->consumed_bits = cbits;
			br->consumed_words = cwords;
			return true;
		}

		uval = 0;
		++vals;

	}
}
#endif

#if 0 /* UNUSED */
FLAC__bool FLAC__bitreader_read_golomb_signed(FLAC__BitReader *br, int *val, unsigned parameter)
{
	FLAC__uint32 lsbs = 0, msbs = 0;
	unsigned bit, uval, k;

	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);

	k = FLAC__bitmath_ilog2(parameter);

	/* read the unary MSBs and end bit */
	if(!FLAC__bitreader_read_unary_unsigned(br, &msbs))
		return false;

	/* read the binary LSBs */
	if(!FLAC__bitreader_read_raw_uint32(br, &lsbs, k))
		return false;

	if(parameter == 1u<<k) {
		/* compose the value */
		uval = (msbs << k) | lsbs;
	}
	else {
		unsigned d = (1 << (k+1)) - parameter;
		if(lsbs >= d) {
			if(!FLAC__bitreader_read_bit(br, &bit))
				return false;
			lsbs <<= 1;
			lsbs |= bit;
			lsbs -= d;
		}
		/* compose the value */
		uval = msbs * parameter + lsbs;
	}

	/* unfold unsigned to signed */
	if(uval & 1)
		*val = -((int)(uval >> 1)) - 1;
	else
		*val = (int)(uval >> 1);

	return true;
}

FLAC__bool FLAC__bitreader_read_golomb_unsigned(FLAC__BitReader *br, unsigned *val, unsigned parameter)
{
	FLAC__uint32 lsbs, msbs = 0;
	unsigned bit, k;

	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);

	k = FLAC__bitmath_ilog2(parameter);

	/* read the unary MSBs and end bit */
	if(!FLAC__bitreader_read_unary_unsigned(br, &msbs))
		return false;

	/* read the binary LSBs */
	if(!FLAC__bitreader_read_raw_uint32(br, &lsbs, k))
		return false;

	if(parameter == 1u<<k) {
		/* compose the value */
		*val = (msbs << k) | lsbs;
	}
	else {
		unsigned d = (1 << (k+1)) - parameter;
		if(lsbs >= d) {
			if(!FLAC__bitreader_read_bit(br, &bit))
				return false;
			lsbs <<= 1;
			lsbs |= bit;
			lsbs -= d;
		}
		/* compose the value */
		*val = msbs * parameter + lsbs;
	}

	return true;
}
#endif /* UNUSED */

/* on return, if *val == 0xffffffff then the utf-8 sequence was invalid, but the return value will be true */
FLAC__bool FLAC__bitreader_read_utf8_uint32(FLAC__BitReader *br, FLAC__uint32 *val, FLAC__byte *raw, unsigned *rawlen)
{
	FLAC__uint32 v = 0;
	FLAC__uint32 x;
	unsigned i;

	if(!FLAC__bitreader_read_raw_uint32(br, &x, 8))
		return false;
	if(raw)
		raw[(*rawlen)++] = (FLAC__byte)x;
	if(!(x & 0x80)) { /* 0xxxxxxx */
		v = x;
		i = 0;
	}
	else if(x & 0xC0 && !(x & 0x20)) { /* 110xxxxx */
		v = x & 0x1F;
		i = 1;
	}
	else if(x & 0xE0 && !(x & 0x10)) { /* 1110xxxx */
		v = x & 0x0F;
		i = 2;
	}
	else if(x & 0xF0 && !(x & 0x08)) { /* 11110xxx */
		v = x & 0x07;
		i = 3;
	}
	else if(x & 0xF8 && !(x & 0x04)) { /* 111110xx */
		v = x & 0x03;
		i = 4;
	}
	else if(x & 0xFC && !(x & 0x02)) { /* 1111110x */
		v = x & 0x01;
		i = 5;
	}
	else {
		*val = 0xffffffff;
		return true;
	}
	for( ; i; i--) {
		if(!FLAC__bitreader_read_raw_uint32(br, &x, 8))
			return false;
		if(raw)
			raw[(*rawlen)++] = (FLAC__byte)x;
		if(!(x & 0x80) || (x & 0x40)) { /* 10xxxxxx */
			*val = 0xffffffff;
			return true;
		}
		v <<= 6;
		v |= (x & 0x3F);
	}
	*val = v;
	return true;
}

/* on return, if *val == 0xffffffffffffffff then the utf-8 sequence was invalid, but the return value will be true */
FLAC__bool FLAC__bitreader_read_utf8_uint64(FLAC__BitReader *br, FLAC__uint64 *val, FLAC__byte *raw, unsigned *rawlen)
{
	FLAC__uint64 v = 0;
	FLAC__uint32 x;
	unsigned i;

	if(!FLAC__bitreader_read_raw_uint32(br, &x, 8))
		return false;
	if(raw)
		raw[(*rawlen)++] = (FLAC__byte)x;
	if(!(x & 0x80)) { /* 0xxxxxxx */
		v = x;
		i = 0;
	}
	else if(x & 0xC0 && !(x & 0x20)) { /* 110xxxxx */
		v = x & 0x1F;
		i = 1;
	}
	else if(x & 0xE0 && !(x & 0x10)) { /* 1110xxxx */
		v = x & 0x0F;
		i = 2;
	}
	else if(x & 0xF0 && !(x & 0x08)) { /* 11110xxx */
		v = x & 0x07;
		i = 3;
	}
	else if(x & 0xF8 && !(x & 0x04)) { /* 111110xx */
		v = x & 0x03;
		i = 4;
	}
	else if(x & 0xFC && !(x & 0x02)) { /* 1111110x */
		v = x & 0x01;
		i = 5;
	}
	else if(x & 0xFE && !(x & 0x01)) { /* 11111110 */
		v = 0;
		i = 6;
	}
	else {
		*val = FLAC__U64L(0xffffffffffffffff);
		return true;
	}
	for( ; i; i--) {
		if(!FLAC__bitreader_read_raw_uint32(br, &x, 8))
			return false;
		if(raw)
			raw[(*rawlen)++] = (FLAC__byte)x;
		if(!(x & 0x80) || (x & 0x40)) { /* 10xxxxxx */
			*val = FLAC__U64L(0xffffffffffffffff);
			return true;
		}
		v <<= 6;
		v |= (x & 0x3F);
	}
	*val = v;
	return true;
}
#endif //++-- bitreader.c 


//++ metadata_iterators.c 
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

/****************************************************************************
 *
 * Local function declarations
 *
 ***************************************************************************/

static void pack_uint32_(FLAC__uint32 val, FLAC__byte *b, unsigned bytes);
static void pack_uint32_little_endian_(FLAC__uint32 val, FLAC__byte *b, unsigned bytes);
static void pack_uint64_(FLAC__uint64 val, FLAC__byte *b, unsigned bytes);
static FLAC__uint32 unpack_uint32_(FLAC__byte *b, unsigned bytes);
static FLAC__uint32 unpack_uint32_little_endian_(FLAC__byte *b, unsigned bytes);
static FLAC__uint64 unpack_uint64_(FLAC__byte *b, unsigned bytes);

static FLAC__bool read_metadata_block_header_(FLAC__Metadata_SimpleIterator *iterator);
static FLAC__bool read_metadata_block_data_(FLAC__Metadata_SimpleIterator *iterator, FLAC__StreamMetadata *block);
static FLAC__bool read_metadata_block_header_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__bool *is_last, FLAC__MetadataType *type, unsigned *length);
static FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__IOCallback_Seek seek_cb, FLAC__StreamMetadata *block);
static FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_streaminfo_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__StreamMetadata_StreamInfo *block);
static FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_padding_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Seek seek_cb, FLAC__StreamMetadata_Padding *block, unsigned block_length);
static FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_application_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__StreamMetadata_Application *block, unsigned block_length);
static FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_seektable_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__StreamMetadata_SeekTable *block, unsigned block_length);
static FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_vorbis_comment_entry_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__StreamMetadata_VorbisComment_Entry *entry);
static FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_vorbis_comment_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__StreamMetadata_VorbisComment *block);
static FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_cuesheet_track_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__StreamMetadata_CueSheet_Track *track);
static FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_cuesheet_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__StreamMetadata_CueSheet *block);
static FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_picture_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__StreamMetadata_Picture *block);
static FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_unknown_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__StreamMetadata_Unknown *block, unsigned block_length);

static FLAC__bool write_metadata_block_header_(FILE *file, FLAC__Metadata_SimpleIteratorStatus *status, const FLAC__StreamMetadata *block);
static FLAC__bool write_metadata_block_data_(FILE *file, FLAC__Metadata_SimpleIteratorStatus *status, const FLAC__StreamMetadata *block);
static FLAC__bool write_metadata_block_header_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Write write_cb, const FLAC__StreamMetadata *block);
static FLAC__bool write_metadata_block_data_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Write write_cb, const FLAC__StreamMetadata *block);
static FLAC__bool write_metadata_block_data_streaminfo_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Write write_cb, const FLAC__StreamMetadata_StreamInfo *block);
static FLAC__bool write_metadata_block_data_padding_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Write write_cb, const FLAC__StreamMetadata_Padding *block, unsigned block_length);
static FLAC__bool write_metadata_block_data_application_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Write write_cb, const FLAC__StreamMetadata_Application *block, unsigned block_length);
static FLAC__bool write_metadata_block_data_seektable_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Write write_cb, const FLAC__StreamMetadata_SeekTable *block);
static FLAC__bool write_metadata_block_data_vorbis_comment_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Write write_cb, const FLAC__StreamMetadata_VorbisComment *block);
static FLAC__bool write_metadata_block_data_cuesheet_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Write write_cb, const FLAC__StreamMetadata_CueSheet *block);
static FLAC__bool write_metadata_block_data_picture_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Write write_cb, const FLAC__StreamMetadata_Picture *block);
static FLAC__bool write_metadata_block_data_unknown_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Write write_cb, const FLAC__StreamMetadata_Unknown *block, unsigned block_length);

static FLAC__bool write_metadata_block_stationary_(FLAC__Metadata_SimpleIterator *iterator, const FLAC__StreamMetadata *block);
static FLAC__bool write_metadata_block_stationary_with_padding_(FLAC__Metadata_SimpleIterator *iterator, FLAC__StreamMetadata *block, unsigned padding_length, FLAC__bool padding_is_last);
static FLAC__bool rewrite_whole_file_(FLAC__Metadata_SimpleIterator *iterator, FLAC__StreamMetadata *block, FLAC__bool append);

static void simple_iterator_push_(FLAC__Metadata_SimpleIterator *iterator);
static FLAC__bool simple_iterator_pop_(FLAC__Metadata_SimpleIterator *iterator);

static unsigned seek_to_first_metadata_block_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__IOCallback_Seek seek_cb);
static unsigned seek_to_first_metadata_block_(FILE *f);

static FLAC__bool simple_iterator_copy_file_prefix_(FLAC__Metadata_SimpleIterator *iterator, FILE **tempfile, char **tempfilename, FLAC__bool append);
static FLAC__bool simple_iterator_copy_file_postfix_(FLAC__Metadata_SimpleIterator *iterator, FILE **tempfile, char **tempfilename, int fixup_is_last_code, off_t fixup_is_last_flag_offset, FLAC__bool backup);

static FLAC__bool copy_n_bytes_from_file_(FILE *file, FILE *tempfile, off_t bytes, FLAC__Metadata_SimpleIteratorStatus *status);
static FLAC__bool copy_n_bytes_from_file_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__IOHandle temp_handle, FLAC__IOCallback_Write temp_write_cb, off_t bytes, FLAC__Metadata_SimpleIteratorStatus *status);
static FLAC__bool copy_remaining_bytes_from_file_(FILE *file, FILE *tempfile, FLAC__Metadata_SimpleIteratorStatus *status);
static FLAC__bool copy_remaining_bytes_from_file_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__IOCallback_Eof eof_cb, FLAC__IOHandle temp_handle, FLAC__IOCallback_Write temp_write_cb, FLAC__Metadata_SimpleIteratorStatus *status);

static FLAC__bool open_tempfile_(const char *filename, const char *tempfile_path_prefix, FILE **tempfile, char **tempfilename, FLAC__Metadata_SimpleIteratorStatus *status);
static FLAC__bool transport_tempfile_(const char *filename, FILE **tempfile, char **tempfilename, FLAC__Metadata_SimpleIteratorStatus *status);
static void cleanup_tempfile_(FILE **tempfile, char **tempfilename);

static FLAC__bool get_file_stats_(const char *filename, struct stat *stats);
static void set_file_stats_(const char *filename, struct stat *stats);

static int fseek_wrapper_(FLAC__IOHandle handle, FLAC__int64 offset, int whence);
static FLAC__int64 ftell_wrapper_(FLAC__IOHandle handle);

static FLAC__Metadata_ChainStatus get_equivalent_status_(FLAC__Metadata_SimpleIteratorStatus status);


#ifdef FLAC__VALGRIND_TESTING
static size_t local__fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	size_t ret = fwrite(ptr, size, nmemb, stream);
	if(!ferror(stream))
		fflush(stream);
	return ret;
}
#else
#define local__fwrite fwrite
#endif

/****************************************************************************
 *
 * Level 0 implementation
 *
 ***************************************************************************/

static FLAC__StreamDecoderWriteStatus write_callback_(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[], void *client_data);
static void metadata_callback_(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data);
static void error_callback_(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data);

typedef struct {
	FLAC__bool got_error;
	FLAC__StreamMetadata *object;
} level0_client_data;

static FLAC__StreamMetadata *get_one_metadata_block_(const char *filename, FLAC__MetadataType type)
{
	level0_client_data cd;
	FLAC__StreamDecoder *decoder;

	FLAC__ASSERT(0 != filename);

	cd.got_error = false;
	cd.object = 0;

	decoder = FLAC__stream_decoder_new();

	if(0 == decoder)
		return 0;

	FLAC__stream_decoder_set_md5_checking(decoder, false);
	FLAC__stream_decoder_set_metadata_ignore_all(decoder);
	FLAC__stream_decoder_set_metadata_respond(decoder, type);

	if(FLAC__stream_decoder_init_file(decoder, filename, write_callback_, metadata_callback_, error_callback_, &cd) != FLAC__STREAM_DECODER_INIT_STATUS_OK || cd.got_error) {
		(void)FLAC__stream_decoder_finish(decoder);
		FLAC__stream_decoder_delete(decoder);
		return 0;
	}

	if(!FLAC__stream_decoder_process_until_end_of_metadata(decoder) || cd.got_error) {
		(void)FLAC__stream_decoder_finish(decoder);
		FLAC__stream_decoder_delete(decoder);
		if(0 != cd.object)
			FLAC__metadata_object_delete(cd.object);
		return 0;
	}

	(void)FLAC__stream_decoder_finish(decoder);
	FLAC__stream_decoder_delete(decoder);

	return cd.object;
}

FLAC_API FLAC__bool FLAC__metadata_get_streaminfo(const char *filename, FLAC__StreamMetadata *streaminfo)
{
	FLAC__StreamMetadata *object;

	FLAC__ASSERT(0 != filename);
	FLAC__ASSERT(0 != streaminfo);

	object = get_one_metadata_block_(filename, FLAC__METADATA_TYPE_STREAMINFO);

	if (object) {
		/* can just copy the contents since STREAMINFO has no internal structure */
		*streaminfo = *object;
		FLAC__metadata_object_delete(object);
		return true;
	}
	else {
		return false;
	}
}

FLAC_API FLAC__bool FLAC__metadata_get_tags(const char *filename, FLAC__StreamMetadata **tags)
{
	FLAC__ASSERT(0 != filename);
	FLAC__ASSERT(0 != tags);

	*tags = get_one_metadata_block_(filename, FLAC__METADATA_TYPE_VORBIS_COMMENT);

	return 0 != *tags;
}

FLAC_API FLAC__bool FLAC__metadata_get_cuesheet(const char *filename, FLAC__StreamMetadata **cuesheet)
{
	FLAC__ASSERT(0 != filename);
	FLAC__ASSERT(0 != cuesheet);

	*cuesheet = get_one_metadata_block_(filename, FLAC__METADATA_TYPE_CUESHEET);

	return 0 != *cuesheet;
}

FLAC__StreamDecoderWriteStatus write_callback_(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[], void *client_data)
{
	(void)decoder, (void)frame, (void)buffer, (void)client_data;

	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void metadata_callback_(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data)
{
	level0_client_data *cd = (level0_client_data *)client_data;
	(void)decoder;

	/*
	 * we assume we only get here when the one metadata block we were
	 * looking for was passed to us
	 */
	if(!cd->got_error && 0 == cd->object) {
		if(0 == (cd->object = FLAC__metadata_object_clone(metadata)))
			cd->got_error = true;
	}
}

void error_callback_(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data)
{
	level0_client_data *cd = (level0_client_data *)client_data;
	(void)decoder;

	if(status != FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC)
		cd->got_error = true;
}

FLAC_API FLAC__bool FLAC__metadata_get_picture(const char *filename, FLAC__StreamMetadata **picture, FLAC__StreamMetadata_Picture_Type type, const char *mime_type, const FLAC__byte *description, unsigned max_width, unsigned max_height, unsigned max_depth, unsigned max_colors)
{
	FLAC__Metadata_SimpleIterator *it;
	FLAC__uint64 max_area_seen = 0;
	FLAC__uint64 max_depth_seen = 0;

	FLAC__ASSERT(0 != filename);
	FLAC__ASSERT(0 != picture);

	*picture = 0;

	it = FLAC__metadata_simple_iterator_new();
	if(0 == it)
		return false;
	if(!FLAC__metadata_simple_iterator_init(it, filename, /*read_only=*/true, /*preserve_file_stats=*/true)) {
		FLAC__metadata_simple_iterator_delete(it);
		return false;
	}
	do {
		if(FLAC__metadata_simple_iterator_get_block_type(it) == FLAC__METADATA_TYPE_PICTURE) {
			FLAC__StreamMetadata *obj = FLAC__metadata_simple_iterator_get_block(it);
			FLAC__uint64 area = (FLAC__uint64)obj->data.picture.width * (FLAC__uint64)obj->data.picture.height;
			/* check constraints */
			if(
				(type == (FLAC__StreamMetadata_Picture_Type)(-1) || type == obj->data.picture.type) &&
				(mime_type == 0 || !strcmp(mime_type, obj->data.picture.mime_type)) &&
				(description == 0 || !strcmp((const char *)description, (const char *)obj->data.picture.description)) &&
				obj->data.picture.width <= max_width &&
				obj->data.picture.height <= max_height &&
				obj->data.picture.depth <= max_depth &&
				obj->data.picture.colors <= max_colors &&
				(area > max_area_seen || (area == max_area_seen && obj->data.picture.depth > max_depth_seen))
			) {
				if(*picture)
					FLAC__metadata_object_delete(*picture);
				*picture = obj;
				max_area_seen = area;
				max_depth_seen = obj->data.picture.depth;
			}
			else {
				FLAC__metadata_object_delete(obj);
			}
		}
	} while(FLAC__metadata_simple_iterator_next(it));

	FLAC__metadata_simple_iterator_delete(it);

	return (0 != *picture);
}


/****************************************************************************
 *
 * Level 1 implementation
 *
 ***************************************************************************/

#define SIMPLE_ITERATOR_MAX_PUSH_DEPTH (1+4)
/* 1 for initial offset, +4 for our own personal use */

struct FLAC__Metadata_SimpleIterator {
	FILE *file;
	char *filename, *tempfile_path_prefix;
	struct stat stats;
	FLAC__bool has_stats;
	FLAC__bool is_writable;
	FLAC__Metadata_SimpleIteratorStatus status;
	off_t offset[SIMPLE_ITERATOR_MAX_PUSH_DEPTH];
	off_t first_offset; /* this is the offset to the STREAMINFO block */
	unsigned depth;
	/* this is the metadata block header of the current block we are pointing to: */
	FLAC__bool is_last;
	FLAC__MetadataType type;
	unsigned length;
};

FLAC_API const char * const FLAC__Metadata_SimpleIteratorStatusString[] = {
	"FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK",
	"FLAC__METADATA_SIMPLE_ITERATOR_STATUS_ILLEGAL_INPUT",
	"FLAC__METADATA_SIMPLE_ITERATOR_STATUS_ERROR_OPENING_FILE",
	"FLAC__METADATA_SIMPLE_ITERATOR_STATUS_NOT_A_FLAC_FILE",
	"FLAC__METADATA_SIMPLE_ITERATOR_STATUS_NOT_WRITABLE",
	"FLAC__METADATA_SIMPLE_ITERATOR_STATUS_BAD_METADATA",
	"FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR",
	"FLAC__METADATA_SIMPLE_ITERATOR_STATUS_SEEK_ERROR",
	"FLAC__METADATA_SIMPLE_ITERATOR_STATUS_WRITE_ERROR",
	"FLAC__METADATA_SIMPLE_ITERATOR_STATUS_RENAME_ERROR",
	"FLAC__METADATA_SIMPLE_ITERATOR_STATUS_UNLINK_ERROR",
	"FLAC__METADATA_SIMPLE_ITERATOR_STATUS_MEMORY_ALLOCATION_ERROR",
	"FLAC__METADATA_SIMPLE_ITERATOR_STATUS_INTERNAL_ERROR"
};


FLAC_API FLAC__Metadata_SimpleIterator *FLAC__metadata_simple_iterator_new(void)
{
	FLAC__Metadata_SimpleIterator *iterator = (FLAC__Metadata_SimpleIterator*)calloc(1, sizeof(FLAC__Metadata_SimpleIterator));

	if(0 != iterator) {
		iterator->file = 0;
		iterator->filename = 0;
		iterator->tempfile_path_prefix = 0;
		iterator->has_stats = false;
		iterator->is_writable = false;
		iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK;
		iterator->first_offset = iterator->offset[0] = -1;
		iterator->depth = 0;
	}

	return iterator;
}

static void simple_iterator_free_guts_(FLAC__Metadata_SimpleIterator *iterator)
{
	FLAC__ASSERT(0 != iterator);

	if(0 != iterator->file) {
		fclose(iterator->file);
		iterator->file = 0;
		if(iterator->has_stats)
			set_file_stats_(iterator->filename, &iterator->stats);
	}
	if(0 != iterator->filename) {
		free(iterator->filename);
		iterator->filename = 0;
	}
	if(0 != iterator->tempfile_path_prefix) {
		free(iterator->tempfile_path_prefix);
		iterator->tempfile_path_prefix = 0;
	}
}

FLAC_API void FLAC__metadata_simple_iterator_delete(FLAC__Metadata_SimpleIterator *iterator)
{
	FLAC__ASSERT(0 != iterator);

	simple_iterator_free_guts_(iterator);
	free(iterator);
}

FLAC_API FLAC__Metadata_SimpleIteratorStatus FLAC__metadata_simple_iterator_status(FLAC__Metadata_SimpleIterator *iterator)
{
	FLAC__Metadata_SimpleIteratorStatus status;

	FLAC__ASSERT(0 != iterator);

	status = iterator->status;
	iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK;
	return status;
}

static FLAC__bool simple_iterator_prime_input_(FLAC__Metadata_SimpleIterator *iterator, FLAC__bool read_only)
{
	unsigned ret;

	FLAC__ASSERT(0 != iterator);

	if(read_only || 0 == (iterator->file = fopen(iterator->filename, "r+b"))) {
		iterator->is_writable = false;
		if(read_only || errno == EACCES) {
			if(0 == (iterator->file = fopen(iterator->filename, "rb"))) {
				iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_ERROR_OPENING_FILE;
				return false;
			}
		}
		else {
			iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_ERROR_OPENING_FILE;
			return false;
		}
	}
	else {
		iterator->is_writable = true;
	}

	ret = seek_to_first_metadata_block_(iterator->file);
	switch(ret) {
		case 0:
			iterator->depth = 0;
			iterator->first_offset = iterator->offset[iterator->depth] = ftello(iterator->file);
			return read_metadata_block_header_(iterator);
		case 1:
			iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
			return false;
		case 2:
			iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_SEEK_ERROR;
			return false;
		case 3:
			iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_NOT_A_FLAC_FILE;
			return false;
		default:
			FLAC__ASSERT(0);
			return false;
	}
}

#if 0
@@@ If we decide to finish implementing this, put this comment back in metadata.h
/*
 * The 'tempfile_path_prefix' allows you to specify a directory where
 * tempfiles should go.  Remember that if your metadata edits cause the
 * FLAC file to grow, the entire file will have to be rewritten.  If
 * 'tempfile_path_prefix' is NULL, the temp file will be written in the
 * same directory as the original FLAC file.  This makes replacing the
 * original with the tempfile fast but requires extra space in the same
 * partition for the tempfile.  If space is a problem, you can pass a
 * directory name belonging to a different partition in
 * 'tempfile_path_prefix'.  Note that you should use the forward slash
 * '/' as the directory separator.  A trailing slash is not needed; it
 * will be added automatically.
 */
FLAC__bool FLAC__metadata_simple_iterator_init(FLAC__Metadata_SimpleIterator *iterator, const char *filename, FLAC__bool preserve_file_stats, const char *tempfile_path_prefix);
#endif

FLAC_API FLAC__bool FLAC__metadata_simple_iterator_init(FLAC__Metadata_SimpleIterator *iterator, const char *filename, FLAC__bool read_only, FLAC__bool preserve_file_stats)
{
	const char *tempfile_path_prefix = 0; /*@@@ search for comments near 'rename(...)' for what it will take to finish implementing this */

	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != filename);

	simple_iterator_free_guts_(iterator);

	if(!read_only && preserve_file_stats)
		iterator->has_stats = get_file_stats_(filename, &iterator->stats);

	if(0 == (iterator->filename = strdup(filename))) {
		iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_MEMORY_ALLOCATION_ERROR;
		return false;
	}
	if(0 != tempfile_path_prefix && 0 == (iterator->tempfile_path_prefix = strdup(tempfile_path_prefix))) {
		iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_MEMORY_ALLOCATION_ERROR;
		return false;
	}

	return simple_iterator_prime_input_(iterator, read_only);
}

FLAC_API FLAC__bool FLAC__metadata_simple_iterator_is_writable(const FLAC__Metadata_SimpleIterator *iterator)
{
	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != iterator->file);

	return iterator->is_writable;
}

FLAC_API FLAC__bool FLAC__metadata_simple_iterator_next(FLAC__Metadata_SimpleIterator *iterator)
{
	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != iterator->file);

	if(iterator->is_last)
		return false;

	if(0 != fseeko(iterator->file, iterator->length, SEEK_CUR)) {
		iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_SEEK_ERROR;
		return false;
	}

	iterator->offset[iterator->depth] = ftello(iterator->file);

	return read_metadata_block_header_(iterator);
}

FLAC_API FLAC__bool FLAC__metadata_simple_iterator_prev(FLAC__Metadata_SimpleIterator *iterator)
{
	off_t this_offset;

	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != iterator->file);

	if(iterator->offset[iterator->depth] == iterator->first_offset)
		return false;

	if(0 != fseeko(iterator->file, iterator->first_offset, SEEK_SET)) {
		iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_SEEK_ERROR;
		return false;
	}
	this_offset = iterator->first_offset;
	if(!read_metadata_block_header_(iterator))
		return false;

	/* we ignore any error from ftello() and catch it in fseeko() */
	while(ftello(iterator->file) + (off_t)iterator->length < iterator->offset[iterator->depth]) {
		if(0 != fseeko(iterator->file, iterator->length, SEEK_CUR)) {
			iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_SEEK_ERROR;
			return false;
		}
		this_offset = ftello(iterator->file);
		if(!read_metadata_block_header_(iterator))
			return false;
	}

	iterator->offset[iterator->depth] = this_offset;

	return true;
}

/*@@@@add to tests*/
FLAC_API FLAC__bool FLAC__metadata_simple_iterator_is_last(const FLAC__Metadata_SimpleIterator *iterator)
{
	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != iterator->file);

	return iterator->is_last;
}

/*@@@@add to tests*/
FLAC_API off_t FLAC__metadata_simple_iterator_get_block_offset(const FLAC__Metadata_SimpleIterator *iterator)
{
	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != iterator->file);

	return iterator->offset[iterator->depth];
}

FLAC_API FLAC__MetadataType FLAC__metadata_simple_iterator_get_block_type(const FLAC__Metadata_SimpleIterator *iterator)
{
	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != iterator->file);

	return iterator->type;
}

/*@@@@add to tests*/
FLAC_API unsigned FLAC__metadata_simple_iterator_get_block_length(const FLAC__Metadata_SimpleIterator *iterator)
{
	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != iterator->file);

	return iterator->length;
}

/*@@@@add to tests*/
FLAC_API FLAC__bool FLAC__metadata_simple_iterator_get_application_id(FLAC__Metadata_SimpleIterator *iterator, FLAC__byte *id)
{
	const unsigned id_bytes = FLAC__STREAM_METADATA_APPLICATION_ID_LEN / 8;

	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != iterator->file);
	FLAC__ASSERT(0 != id);

	if(iterator->type != FLAC__METADATA_TYPE_APPLICATION) {
		iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_ILLEGAL_INPUT;
		return false;
	}

	if(fread(id, 1, id_bytes, iterator->file) != id_bytes) {
		iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
		return false;
	}

	/* back up */
	if(0 != fseeko(iterator->file, -((int)id_bytes), SEEK_CUR)) {
		iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_SEEK_ERROR;
		return false;
	}

	return true;
}

FLAC_API FLAC__StreamMetadata *FLAC__metadata_simple_iterator_get_block(FLAC__Metadata_SimpleIterator *iterator)
{
	FLAC__StreamMetadata *block = FLAC__metadata_object_new(iterator->type);

	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != iterator->file);

	if(0 != block) {
		block->is_last = iterator->is_last;
		block->length = iterator->length;

		if(!read_metadata_block_data_(iterator, block)) {
			FLAC__metadata_object_delete(block);
			return 0;
		}

		/* back up to the beginning of the block data to stay consistent */
		if(0 != fseeko(iterator->file, iterator->offset[iterator->depth] + FLAC__STREAM_METADATA_HEADER_LENGTH, SEEK_SET)) {
			iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_SEEK_ERROR;
			FLAC__metadata_object_delete(block);
			return 0;
		}
	}
	else
		iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_MEMORY_ALLOCATION_ERROR;

	return block;
}

FLAC_API FLAC__bool FLAC__metadata_simple_iterator_set_block(FLAC__Metadata_SimpleIterator *iterator, FLAC__StreamMetadata *block, FLAC__bool use_padding)
{
	FLAC__ASSERT_DECLARATION(off_t debug_target_offset = iterator->offset[iterator->depth];)
	FLAC__bool ret;

	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != iterator->file);
	FLAC__ASSERT(0 != block);

	if(!iterator->is_writable) {
		iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_NOT_WRITABLE;
		return false;
	}

	if(iterator->type == FLAC__METADATA_TYPE_STREAMINFO || block->type == FLAC__METADATA_TYPE_STREAMINFO) {
		if(iterator->type != block->type) {
			iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_ILLEGAL_INPUT;
			return false;
		}
	}

	block->is_last = iterator->is_last;

	if(iterator->length == block->length)
		return write_metadata_block_stationary_(iterator, block);
	else if(iterator->length > block->length) {
		if(use_padding && iterator->length >= FLAC__STREAM_METADATA_HEADER_LENGTH + block->length) {
			ret = write_metadata_block_stationary_with_padding_(iterator, block, iterator->length - FLAC__STREAM_METADATA_HEADER_LENGTH - block->length, block->is_last);
			FLAC__ASSERT(!ret || iterator->offset[iterator->depth] == debug_target_offset);
			FLAC__ASSERT(!ret || ftello(iterator->file) == debug_target_offset + (off_t)FLAC__STREAM_METADATA_HEADER_LENGTH);
			return ret;
		}
		else {
			ret = rewrite_whole_file_(iterator, block, /*append=*/false);
			FLAC__ASSERT(!ret || iterator->offset[iterator->depth] == debug_target_offset);
			FLAC__ASSERT(!ret || ftello(iterator->file) == debug_target_offset + (off_t)FLAC__STREAM_METADATA_HEADER_LENGTH);
			return ret;
		}
	}
	else /* iterator->length < block->length */ {
		unsigned padding_leftover = 0;
		FLAC__bool padding_is_last = false;
		if(use_padding) {
			/* first see if we can even use padding */
			if(iterator->is_last) {
				use_padding = false;
			}
			else {
				const unsigned extra_padding_bytes_required = block->length - iterator->length;
				simple_iterator_push_(iterator);
				if(!FLAC__metadata_simple_iterator_next(iterator)) {
					(void)simple_iterator_pop_(iterator);
					return false;
				}
				if(iterator->type != FLAC__METADATA_TYPE_PADDING) {
					use_padding = false;
				}
				else {
					if(FLAC__STREAM_METADATA_HEADER_LENGTH + iterator->length == extra_padding_bytes_required) {
						padding_leftover = 0;
						block->is_last = iterator->is_last;
					}
					else if(iterator->length < extra_padding_bytes_required)
						use_padding = false;
					else {
						padding_leftover = FLAC__STREAM_METADATA_HEADER_LENGTH + iterator->length - extra_padding_bytes_required;
						padding_is_last = iterator->is_last;
						block->is_last = false;
					}
				}
				if(!simple_iterator_pop_(iterator))
					return false;
			}
		}
		if(use_padding) {
			if(padding_leftover == 0) {
				ret = write_metadata_block_stationary_(iterator, block);
				FLAC__ASSERT(!ret || iterator->offset[iterator->depth] == debug_target_offset);
				FLAC__ASSERT(!ret || ftello(iterator->file) == debug_target_offset + (off_t)FLAC__STREAM_METADATA_HEADER_LENGTH);
				return ret;
			}
			else {
				FLAC__ASSERT(padding_leftover >= FLAC__STREAM_METADATA_HEADER_LENGTH);
				ret = write_metadata_block_stationary_with_padding_(iterator, block, padding_leftover - FLAC__STREAM_METADATA_HEADER_LENGTH, padding_is_last);
				FLAC__ASSERT(!ret || iterator->offset[iterator->depth] == debug_target_offset);
				FLAC__ASSERT(!ret || ftello(iterator->file) == debug_target_offset + (off_t)FLAC__STREAM_METADATA_HEADER_LENGTH);
				return ret;
			}
		}
		else {
			ret = rewrite_whole_file_(iterator, block, /*append=*/false);
			FLAC__ASSERT(!ret || iterator->offset[iterator->depth] == debug_target_offset);
			FLAC__ASSERT(!ret || ftello(iterator->file) == debug_target_offset + (off_t)FLAC__STREAM_METADATA_HEADER_LENGTH);
			return ret;
		}
	}
}

FLAC_API FLAC__bool FLAC__metadata_simple_iterator_insert_block_after(FLAC__Metadata_SimpleIterator *iterator, FLAC__StreamMetadata *block, FLAC__bool use_padding)
{
	unsigned padding_leftover = 0;
	FLAC__bool padding_is_last = false;

	FLAC__ASSERT_DECLARATION(off_t debug_target_offset = iterator->offset[iterator->depth] + FLAC__STREAM_METADATA_HEADER_LENGTH + iterator->length;)
	FLAC__bool ret;

	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != iterator->file);
	FLAC__ASSERT(0 != block);

	if(!iterator->is_writable)
		return false;

	if(block->type == FLAC__METADATA_TYPE_STREAMINFO) {
		iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_ILLEGAL_INPUT;
		return false;
	}

	block->is_last = iterator->is_last;

	if(use_padding) {
		/* first see if we can even use padding */
		if(iterator->is_last) {
			use_padding = false;
		}
		else {
			simple_iterator_push_(iterator);
			if(!FLAC__metadata_simple_iterator_next(iterator)) {
				(void)simple_iterator_pop_(iterator);
				return false;
			}
			if(iterator->type != FLAC__METADATA_TYPE_PADDING) {
				use_padding = false;
			}
			else {
				if(iterator->length == block->length) {
					padding_leftover = 0;
					block->is_last = iterator->is_last;
				}
				else if(iterator->length < FLAC__STREAM_METADATA_HEADER_LENGTH + block->length)
					use_padding = false;
				else {
					padding_leftover = iterator->length - block->length;
					padding_is_last = iterator->is_last;
					block->is_last = false;
				}
			}
			if(!simple_iterator_pop_(iterator))
				return false;
		}
	}
	if(use_padding) {
		/* move to the next block, which is suitable padding */
		if(!FLAC__metadata_simple_iterator_next(iterator))
			return false;
		if(padding_leftover == 0) {
			ret = write_metadata_block_stationary_(iterator, block);
			FLAC__ASSERT(iterator->offset[iterator->depth] == debug_target_offset);
			FLAC__ASSERT(ftello(iterator->file) == debug_target_offset + (off_t)FLAC__STREAM_METADATA_HEADER_LENGTH);
			return ret;
		}
		else {
			FLAC__ASSERT(padding_leftover >= FLAC__STREAM_METADATA_HEADER_LENGTH);
			ret = write_metadata_block_stationary_with_padding_(iterator, block, padding_leftover - FLAC__STREAM_METADATA_HEADER_LENGTH, padding_is_last);
			FLAC__ASSERT(iterator->offset[iterator->depth] == debug_target_offset);
			FLAC__ASSERT(ftello(iterator->file) == debug_target_offset + (off_t)FLAC__STREAM_METADATA_HEADER_LENGTH);
			return ret;
		}
	}
	else {
		ret = rewrite_whole_file_(iterator, block, /*append=*/true);
		FLAC__ASSERT(iterator->offset[iterator->depth] == debug_target_offset);
		FLAC__ASSERT(ftello(iterator->file) == debug_target_offset + (off_t)FLAC__STREAM_METADATA_HEADER_LENGTH);
		return ret;
	}
}

FLAC_API FLAC__bool FLAC__metadata_simple_iterator_delete_block(FLAC__Metadata_SimpleIterator *iterator, FLAC__bool use_padding)
{
	FLAC__ASSERT_DECLARATION(off_t debug_target_offset = iterator->offset[iterator->depth];)
	FLAC__bool ret;

	if(iterator->type == FLAC__METADATA_TYPE_STREAMINFO) {
		iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_ILLEGAL_INPUT;
		return false;
	}

	if(use_padding) {
		FLAC__StreamMetadata *padding = FLAC__metadata_object_new(FLAC__METADATA_TYPE_PADDING);
		if(0 == padding) {
			iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_MEMORY_ALLOCATION_ERROR;
			return false;
		}
		padding->length = iterator->length;
		if(!FLAC__metadata_simple_iterator_set_block(iterator, padding, false)) {
			FLAC__metadata_object_delete(padding);
			return false;
		}
		FLAC__metadata_object_delete(padding);
		if(!FLAC__metadata_simple_iterator_prev(iterator))
			return false;
		FLAC__ASSERT(iterator->offset[iterator->depth] + (off_t)FLAC__STREAM_METADATA_HEADER_LENGTH + (off_t)iterator->length == debug_target_offset);
		FLAC__ASSERT(ftello(iterator->file) + (off_t)iterator->length == debug_target_offset);
		return true;
	}
	else {
		ret = rewrite_whole_file_(iterator, 0, /*append=*/false);
		FLAC__ASSERT(iterator->offset[iterator->depth] + (off_t)FLAC__STREAM_METADATA_HEADER_LENGTH + (off_t)iterator->length == debug_target_offset);
		FLAC__ASSERT(ftello(iterator->file) + (off_t)iterator->length == debug_target_offset);
		return ret;
	}
}



/****************************************************************************
 *
 * Level 2 implementation
 *
 ***************************************************************************/


typedef struct FLAC__Metadata_Node {
	FLAC__StreamMetadata *data;
	struct FLAC__Metadata_Node *prev, *next;
} FLAC__Metadata_Node;

struct FLAC__Metadata_Chain {
	char *filename; /* will be NULL if using callbacks */
	FLAC__bool is_ogg;
	FLAC__Metadata_Node *head;
	FLAC__Metadata_Node *tail;
	unsigned nodes;
	FLAC__Metadata_ChainStatus status;
	off_t first_offset, last_offset;
	/*
	 * This is the length of the chain initially read from the FLAC file.
	 * it is used to compare against the current length to decide whether
	 * or not the whole file has to be rewritten.
	 */
	off_t initial_length;
	/* @@@ hacky, these are currently only needed by ogg reader */
	FLAC__IOHandle handle;
	FLAC__IOCallback_Read read_cb;
};

struct FLAC__Metadata_Iterator {
	FLAC__Metadata_Chain *chain;
	FLAC__Metadata_Node *current;
};

FLAC_API const char * const FLAC__Metadata_ChainStatusString[] = {
	"FLAC__METADATA_CHAIN_STATUS_OK",
	"FLAC__METADATA_CHAIN_STATUS_ILLEGAL_INPUT",
	"FLAC__METADATA_CHAIN_STATUS_ERROR_OPENING_FILE",
	"FLAC__METADATA_CHAIN_STATUS_NOT_A_FLAC_FILE",
	"FLAC__METADATA_CHAIN_STATUS_NOT_WRITABLE",
	"FLAC__METADATA_CHAIN_STATUS_BAD_METADATA",
	"FLAC__METADATA_CHAIN_STATUS_READ_ERROR",
	"FLAC__METADATA_CHAIN_STATUS_SEEK_ERROR",
	"FLAC__METADATA_CHAIN_STATUS_WRITE_ERROR",
	"FLAC__METADATA_CHAIN_STATUS_RENAME_ERROR",
	"FLAC__METADATA_CHAIN_STATUS_UNLINK_ERROR",
	"FLAC__METADATA_CHAIN_STATUS_MEMORY_ALLOCATION_ERROR",
	"FLAC__METADATA_CHAIN_STATUS_INTERNAL_ERROR",
	"FLAC__METADATA_CHAIN_STATUS_INVALID_CALLBACKS",
	"FLAC__METADATA_CHAIN_STATUS_READ_WRITE_MISMATCH",
	"FLAC__METADATA_CHAIN_STATUS_WRONG_WRITE_CALL"
};


static FLAC__Metadata_Node *node_new_(void)
{
	return (FLAC__Metadata_Node*)calloc(1, sizeof(FLAC__Metadata_Node));
}

static void node_delete_(FLAC__Metadata_Node *node)
{
	FLAC__ASSERT(0 != node);
	if(0 != node->data)
		FLAC__metadata_object_delete(node->data);
	free(node);
}

static void chain_init_(FLAC__Metadata_Chain *chain)
{
	FLAC__ASSERT(0 != chain);

	chain->filename = 0;
	chain->is_ogg = false;
	chain->head = chain->tail = 0;
	chain->nodes = 0;
	chain->status = FLAC__METADATA_CHAIN_STATUS_OK;
	chain->initial_length = 0;
	chain->read_cb = 0;
}

static void chain_clear_(FLAC__Metadata_Chain *chain)
{
	FLAC__Metadata_Node *node, *next;

	FLAC__ASSERT(0 != chain);

	for(node = chain->head; node; ) {
		next = node->next;
		node_delete_(node);
		node = next;
	}

	if(0 != chain->filename)
		free(chain->filename);

	chain_init_(chain);
}

static void chain_append_node_(FLAC__Metadata_Chain *chain, FLAC__Metadata_Node *node)
{
	FLAC__ASSERT(0 != chain);
	FLAC__ASSERT(0 != node);
	FLAC__ASSERT(0 != node->data);

	node->next = node->prev = 0;
	node->data->is_last = true;
	if(0 != chain->tail)
		chain->tail->data->is_last = false;

	if(0 == chain->head)
		chain->head = node;
	else {
		FLAC__ASSERT(0 != chain->tail);
		chain->tail->next = node;
		node->prev = chain->tail;
	}
	chain->tail = node;
	chain->nodes++;
}

static void chain_remove_node_(FLAC__Metadata_Chain *chain, FLAC__Metadata_Node *node)
{
	FLAC__ASSERT(0 != chain);
	FLAC__ASSERT(0 != node);

	if(node == chain->head)
		chain->head = node->next;
	else
		node->prev->next = node->next;

	if(node == chain->tail)
		chain->tail = node->prev;
	else
		node->next->prev = node->prev;

	if(0 != chain->tail)
		chain->tail->data->is_last = true;

	chain->nodes--;
}

static void chain_delete_node_(FLAC__Metadata_Chain *chain, FLAC__Metadata_Node *node)
{
	chain_remove_node_(chain, node);
	node_delete_(node);
}

static off_t chain_calculate_length_(FLAC__Metadata_Chain *chain)
{
	const FLAC__Metadata_Node *node;
	off_t length = 0;
	for(node = chain->head; node; node = node->next)
		length += (FLAC__STREAM_METADATA_HEADER_LENGTH + node->data->length);
	return length;
}

static void iterator_insert_node_(FLAC__Metadata_Iterator *iterator, FLAC__Metadata_Node *node)
{
	FLAC__ASSERT(0 != node);
	FLAC__ASSERT(0 != node->data);
	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != iterator->current);
	FLAC__ASSERT(0 != iterator->chain);
	FLAC__ASSERT(0 != iterator->chain->head);
	FLAC__ASSERT(0 != iterator->chain->tail);

	node->data->is_last = false;

	node->prev = iterator->current->prev;
	node->next = iterator->current;

	if(0 == node->prev)
		iterator->chain->head = node;
	else
		node->prev->next = node;

	iterator->current->prev = node;

	iterator->chain->nodes++;
}

static void iterator_insert_node_after_(FLAC__Metadata_Iterator *iterator, FLAC__Metadata_Node *node)
{
	FLAC__ASSERT(0 != node);
	FLAC__ASSERT(0 != node->data);
	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != iterator->current);
	FLAC__ASSERT(0 != iterator->chain);
	FLAC__ASSERT(0 != iterator->chain->head);
	FLAC__ASSERT(0 != iterator->chain->tail);

	iterator->current->data->is_last = false;

	node->prev = iterator->current;
	node->next = iterator->current->next;

	if(0 == node->next)
		iterator->chain->tail = node;
	else
		node->next->prev = node;

	node->prev->next = node;

	iterator->chain->tail->data->is_last = true;

	iterator->chain->nodes++;
}

/* return true iff node and node->next are both padding */
static FLAC__bool chain_merge_adjacent_padding_(FLAC__Metadata_Chain *chain, FLAC__Metadata_Node *node)
{
	if(node->data->type == FLAC__METADATA_TYPE_PADDING && 0 != node->next && node->next->data->type == FLAC__METADATA_TYPE_PADDING) {
		const unsigned growth = FLAC__STREAM_METADATA_HEADER_LENGTH + node->next->data->length;
		node->data->length += growth;

		chain_delete_node_(chain, node->next);
		return true;
	}
	else
		return false;
}

/* Returns the new length of the chain, or 0 if there was an error. */
/* WATCHOUT: This can get called multiple times before a write, so
 * it should still work when this happens.
 */
/* WATCHOUT: Make sure to also update the logic in
 * FLAC__metadata_chain_check_if_tempfile_needed() if the logic here changes.
 */
static off_t chain_prepare_for_write_(FLAC__Metadata_Chain *chain, FLAC__bool use_padding)
{
	off_t current_length = chain_calculate_length_(chain);

	if(use_padding) {
		/* if the metadata shrank and the last block is padding, we just extend the last padding block */
		if(current_length < chain->initial_length && chain->tail->data->type == FLAC__METADATA_TYPE_PADDING) {
			const off_t delta = chain->initial_length - current_length;
			chain->tail->data->length += delta;
			current_length += delta;
			FLAC__ASSERT(current_length == chain->initial_length);
		}
		/* if the metadata shrank more than 4 bytes then there's room to add another padding block */
		else if(current_length + (off_t)FLAC__STREAM_METADATA_HEADER_LENGTH <= chain->initial_length) {
			FLAC__StreamMetadata *padding;
			FLAC__Metadata_Node *node;
			if(0 == (padding = FLAC__metadata_object_new(FLAC__METADATA_TYPE_PADDING))) {
				chain->status = FLAC__METADATA_CHAIN_STATUS_MEMORY_ALLOCATION_ERROR;
				return 0;
			}
			padding->length = chain->initial_length - (FLAC__STREAM_METADATA_HEADER_LENGTH + current_length);
			if(0 == (node = node_new_())) {
				FLAC__metadata_object_delete(padding);
				chain->status = FLAC__METADATA_CHAIN_STATUS_MEMORY_ALLOCATION_ERROR;
				return 0;
			}
			node->data = padding;
			chain_append_node_(chain, node);
			current_length = chain_calculate_length_(chain);
			FLAC__ASSERT(current_length == chain->initial_length);
		}
		/* if the metadata grew but the last block is padding, try cutting the padding to restore the original length so we don't have to rewrite the whole file */
		else if(current_length > chain->initial_length) {
			const off_t delta = current_length - chain->initial_length;
			if(chain->tail->data->type == FLAC__METADATA_TYPE_PADDING) {
				/* if the delta is exactly the size of the last padding block, remove the padding block */
				if((off_t)chain->tail->data->length + (off_t)FLAC__STREAM_METADATA_HEADER_LENGTH == delta) {
					chain_delete_node_(chain, chain->tail);
					current_length = chain_calculate_length_(chain);
					FLAC__ASSERT(current_length == chain->initial_length);
				}
				/* if there is at least 'delta' bytes of padding, trim the padding down */
				else if((off_t)chain->tail->data->length >= delta) {
					chain->tail->data->length -= delta;
					current_length -= delta;
					FLAC__ASSERT(current_length == chain->initial_length);
				}
			}
		}
	}

	return current_length;
}

static FLAC__bool chain_read_cb_(FLAC__Metadata_Chain *chain, FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__IOCallback_Seek seek_cb, FLAC__IOCallback_Tell tell_cb)
{
	FLAC__Metadata_Node *node;

	FLAC__ASSERT(0 != chain);

	/* we assume we're already at the beginning of the file */

	switch(seek_to_first_metadata_block_cb_(handle, read_cb, seek_cb)) {
		case 0:
			break;
		case 1:
			chain->status = FLAC__METADATA_CHAIN_STATUS_READ_ERROR;
			return false;
		case 2:
			chain->status = FLAC__METADATA_CHAIN_STATUS_SEEK_ERROR;
			return false;
		case 3:
			chain->status = FLAC__METADATA_CHAIN_STATUS_NOT_A_FLAC_FILE;
			return false;
		default:
			FLAC__ASSERT(0);
			return false;
	}

	{
		FLAC__int64 pos = tell_cb(handle);
		if(pos < 0) {
			chain->status = FLAC__METADATA_CHAIN_STATUS_READ_ERROR;
			return false;
		}
		chain->first_offset = (off_t)pos;
	}

	{
		FLAC__bool is_last;
		FLAC__MetadataType type;
		unsigned length;

		do {
			node = node_new_();
			if(0 == node) {
				chain->status = FLAC__METADATA_CHAIN_STATUS_MEMORY_ALLOCATION_ERROR;
				return false;
			}

			if(!read_metadata_block_header_cb_(handle, read_cb, &is_last, &type, &length)) {
				chain->status = FLAC__METADATA_CHAIN_STATUS_READ_ERROR;
				return false;
			}

			node->data = FLAC__metadata_object_new(type);
			if(0 == node->data) {
				node_delete_(node);
				chain->status = FLAC__METADATA_CHAIN_STATUS_MEMORY_ALLOCATION_ERROR;
				return false;
			}

			node->data->is_last = is_last;
			node->data->length = length;

			chain->status = get_equivalent_status_(read_metadata_block_data_cb_(handle, read_cb, seek_cb, node->data));
			if(chain->status != FLAC__METADATA_CHAIN_STATUS_OK) {
				node_delete_(node);
				return false;
			}
			chain_append_node_(chain, node);
		} while(!is_last);
	}

	{
		FLAC__int64 pos = tell_cb(handle);
		if(pos < 0) {
			chain->status = FLAC__METADATA_CHAIN_STATUS_READ_ERROR;
			return false;
		}
		chain->last_offset = (off_t)pos;
	}

	chain->initial_length = chain_calculate_length_(chain);

	return true;
}

static FLAC__StreamDecoderReadStatus chain_read_ogg_read_cb_(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data)
{
	FLAC__Metadata_Chain *chain = (FLAC__Metadata_Chain*)client_data;
	(void)decoder;
	if(*bytes > 0 && chain->status == FLAC__METADATA_CHAIN_STATUS_OK) {
		*bytes = chain->read_cb(buffer, sizeof(FLAC__byte), *bytes, chain->handle);
		if(*bytes == 0)
			return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
		else
			return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
	}
	else
		return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
}

static FLAC__StreamDecoderWriteStatus chain_read_ogg_write_cb_(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[], void *client_data)
{
	(void)decoder, (void)frame, (void)buffer, (void)client_data;
	return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
}

static void chain_read_ogg_metadata_cb_(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data)
{
	FLAC__Metadata_Chain *chain = (FLAC__Metadata_Chain*)client_data;
	FLAC__Metadata_Node *node;

	(void)decoder;

	node = node_new_();
	if(0 == node) {
		chain->status = FLAC__METADATA_CHAIN_STATUS_MEMORY_ALLOCATION_ERROR;
		return;
	}

	node->data = FLAC__metadata_object_clone(metadata);
	if(0 == node->data) {
		node_delete_(node);
		chain->status = FLAC__METADATA_CHAIN_STATUS_MEMORY_ALLOCATION_ERROR;
		return;
	}

	chain_append_node_(chain, node);
}

static void chain_read_ogg_error_cb_(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data)
{
	FLAC__Metadata_Chain *chain = (FLAC__Metadata_Chain*)client_data;
	(void)decoder, (void)status;
	chain->status = FLAC__METADATA_CHAIN_STATUS_INTERNAL_ERROR; /*@@@ maybe needs better error code */
}

static FLAC__bool chain_read_ogg_cb_(FLAC__Metadata_Chain *chain, FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb)
{
	FLAC__StreamDecoder *decoder;

	FLAC__ASSERT(0 != chain);

	/* we assume we're already at the beginning of the file */

	chain->handle = handle;
	chain->read_cb = read_cb;
	if(0 == (decoder = FLAC__stream_decoder_new())) {
		chain->status = FLAC__METADATA_CHAIN_STATUS_MEMORY_ALLOCATION_ERROR;
		return false;
	}
	FLAC__stream_decoder_set_metadata_respond_all(decoder);
	if(FLAC__stream_decoder_init_ogg_stream(decoder, chain_read_ogg_read_cb_, /*seek_callback=*/0, /*tell_callback=*/0, /*length_callback=*/0, /*eof_callback=*/0, chain_read_ogg_write_cb_, chain_read_ogg_metadata_cb_, chain_read_ogg_error_cb_, chain) != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
		FLAC__stream_decoder_delete(decoder);
		chain->status = FLAC__METADATA_CHAIN_STATUS_INTERNAL_ERROR; /*@@@ maybe needs better error code */
		return false;
	}

	chain->first_offset = 0; /*@@@ wrong; will need to be set correctly to implement metadata writing for Ogg FLAC */

	if(!FLAC__stream_decoder_process_until_end_of_metadata(decoder))
		chain->status = FLAC__METADATA_CHAIN_STATUS_INTERNAL_ERROR; /*@@@ maybe needs better error code */
	if(chain->status != FLAC__METADATA_CHAIN_STATUS_OK) {
		FLAC__stream_decoder_delete(decoder);
		return false;
	}

	FLAC__stream_decoder_delete(decoder);

	chain->last_offset = 0; /*@@@ wrong; will need to be set correctly to implement metadata writing for Ogg FLAC */

	chain->initial_length = chain_calculate_length_(chain);

	return true;
}

static FLAC__bool chain_rewrite_metadata_in_place_cb_(FLAC__Metadata_Chain *chain, FLAC__IOHandle handle, FLAC__IOCallback_Write write_cb, FLAC__IOCallback_Seek seek_cb)
{
	FLAC__Metadata_Node *node;

	FLAC__ASSERT(0 != chain);
	FLAC__ASSERT(0 != chain->head);

	if(0 != seek_cb(handle, chain->first_offset, SEEK_SET)) {
		chain->status = FLAC__METADATA_CHAIN_STATUS_SEEK_ERROR;
		return false;
	}

	for(node = chain->head; node; node = node->next) {
		if(!write_metadata_block_header_cb_(handle, write_cb, node->data)) {
			chain->status = FLAC__METADATA_CHAIN_STATUS_WRITE_ERROR;
			return false;
		}
		if(!write_metadata_block_data_cb_(handle, write_cb, node->data)) {
			chain->status = FLAC__METADATA_CHAIN_STATUS_WRITE_ERROR;
			return false;
		}
	}

	/*FLAC__ASSERT(fflush(), ftello() == chain->last_offset);*/

	chain->status = FLAC__METADATA_CHAIN_STATUS_OK;
	return true;
}

static FLAC__bool chain_rewrite_metadata_in_place_(FLAC__Metadata_Chain *chain)
{
	FILE *file;
	FLAC__bool ret;

	FLAC__ASSERT(0 != chain->filename);

	if(0 == (file = fopen(chain->filename, "r+b"))) {
		chain->status = FLAC__METADATA_CHAIN_STATUS_ERROR_OPENING_FILE;
		return false;
	}

	/* chain_rewrite_metadata_in_place_cb_() sets chain->status for us */
	ret = chain_rewrite_metadata_in_place_cb_(chain, (FLAC__IOHandle)file, (FLAC__IOCallback_Write)fwrite, fseek_wrapper_);

	fclose(file);

	return ret;
}

static FLAC__bool chain_rewrite_file_(FLAC__Metadata_Chain *chain, const char *tempfile_path_prefix)
{
	FILE *f, *tempfile;
	char *tempfilename;
	FLAC__Metadata_SimpleIteratorStatus status;
	const FLAC__Metadata_Node *node;

	FLAC__ASSERT(0 != chain);
	FLAC__ASSERT(0 != chain->filename);
	FLAC__ASSERT(0 != chain->head);

	/* copy the file prefix (data up to first metadata block */
	if(0 == (f = fopen(chain->filename, "rb"))) {
		chain->status = FLAC__METADATA_CHAIN_STATUS_ERROR_OPENING_FILE;
		return false;
	}
	if(!open_tempfile_(chain->filename, tempfile_path_prefix, &tempfile, &tempfilename, &status)) {
		chain->status = get_equivalent_status_(status);
		cleanup_tempfile_(&tempfile, &tempfilename);
		return false;
	}
	if(!copy_n_bytes_from_file_(f, tempfile, chain->first_offset, &status)) {
		chain->status = get_equivalent_status_(status);
		cleanup_tempfile_(&tempfile, &tempfilename);
		return false;
	}

	/* write the metadata */
	for(node = chain->head; node; node = node->next) {
		if(!write_metadata_block_header_(tempfile, &status, node->data)) {
			chain->status = get_equivalent_status_(status);
			return false;
		}
		if(!write_metadata_block_data_(tempfile, &status, node->data)) {
			chain->status = get_equivalent_status_(status);
			return false;
		}
	}
	/*FLAC__ASSERT(fflush(), ftello() == chain->last_offset);*/

	/* copy the file postfix (everything after the metadata) */
	if(0 != fseeko(f, chain->last_offset, SEEK_SET)) {
		cleanup_tempfile_(&tempfile, &tempfilename);
		chain->status = FLAC__METADATA_CHAIN_STATUS_SEEK_ERROR;
		return false;
	}
	if(!copy_remaining_bytes_from_file_(f, tempfile, &status)) {
		cleanup_tempfile_(&tempfile, &tempfilename);
		chain->status = get_equivalent_status_(status);
		return false;
	}

	/* move the tempfile on top of the original */
	(void)fclose(f);
	if(!transport_tempfile_(chain->filename, &tempfile, &tempfilename, &status))
		return false;

	return true;
}

/* assumes 'handle' is already at beginning of file */
static FLAC__bool chain_rewrite_file_cb_(FLAC__Metadata_Chain *chain, FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__IOCallback_Seek seek_cb, FLAC__IOCallback_Eof eof_cb, FLAC__IOHandle temp_handle, FLAC__IOCallback_Write temp_write_cb)
{
	FLAC__Metadata_SimpleIteratorStatus status;
	const FLAC__Metadata_Node *node;

	FLAC__ASSERT(0 != chain);
	FLAC__ASSERT(0 == chain->filename);
	FLAC__ASSERT(0 != chain->head);

	/* copy the file prefix (data up to first metadata block */
	if(!copy_n_bytes_from_file_cb_(handle, read_cb, temp_handle, temp_write_cb, chain->first_offset, &status)) {
		chain->status = get_equivalent_status_(status);
		return false;
	}

	/* write the metadata */
	for(node = chain->head; node; node = node->next) {
		if(!write_metadata_block_header_cb_(temp_handle, temp_write_cb, node->data)) {
			chain->status = FLAC__METADATA_CHAIN_STATUS_WRITE_ERROR;
			return false;
		}
		if(!write_metadata_block_data_cb_(temp_handle, temp_write_cb, node->data)) {
			chain->status = FLAC__METADATA_CHAIN_STATUS_WRITE_ERROR;
			return false;
		}
	}
	/*FLAC__ASSERT(fflush(), ftello() == chain->last_offset);*/

	/* copy the file postfix (everything after the metadata) */
	if(0 != seek_cb(handle, chain->last_offset, SEEK_SET)) {
		chain->status = FLAC__METADATA_CHAIN_STATUS_SEEK_ERROR;
		return false;
	}
	if(!copy_remaining_bytes_from_file_cb_(handle, read_cb, eof_cb, temp_handle, temp_write_cb, &status)) {
		chain->status = get_equivalent_status_(status);
		return false;
	}

	return true;
}

FLAC_API FLAC__Metadata_Chain *FLAC__metadata_chain_new(void)
{
	FLAC__Metadata_Chain *chain = (FLAC__Metadata_Chain*)calloc(1, sizeof(FLAC__Metadata_Chain));

	if(0 != chain)
		chain_init_(chain);

	return chain;
}

FLAC_API void FLAC__metadata_chain_delete(FLAC__Metadata_Chain *chain)
{
	FLAC__ASSERT(0 != chain);

	chain_clear_(chain);

	free(chain);
}

FLAC_API FLAC__Metadata_ChainStatus FLAC__metadata_chain_status(FLAC__Metadata_Chain *chain)
{
	FLAC__Metadata_ChainStatus status;

	FLAC__ASSERT(0 != chain);

	status = chain->status;
	chain->status = FLAC__METADATA_CHAIN_STATUS_OK;
	return status;
}

static FLAC__bool chain_read_(FLAC__Metadata_Chain *chain, const char *filename, FLAC__bool is_ogg)
{
	FILE *file;
	FLAC__bool ret;

	FLAC__ASSERT(0 != chain);
	FLAC__ASSERT(0 != filename);

	chain_clear_(chain);

	if(0 == (chain->filename = strdup(filename))) {
		chain->status = FLAC__METADATA_CHAIN_STATUS_MEMORY_ALLOCATION_ERROR;
		return false;
	}

	chain->is_ogg = is_ogg;

	if(0 == (file = fopen(filename, "rb"))) {
		chain->status = FLAC__METADATA_CHAIN_STATUS_ERROR_OPENING_FILE;
		return false;
	}

	/* the function also sets chain->status for us */
	ret = is_ogg?
		chain_read_ogg_cb_(chain, file, (FLAC__IOCallback_Read)fread) :
		chain_read_cb_(chain, file, (FLAC__IOCallback_Read)fread, fseek_wrapper_, ftell_wrapper_)
	;

	fclose(file);

	return ret;
}

FLAC_API FLAC__bool FLAC__metadata_chain_read(FLAC__Metadata_Chain *chain, const char *filename)
{
	return chain_read_(chain, filename, /*is_ogg=*/false);
}

/*@@@@add to tests*/
FLAC_API FLAC__bool FLAC__metadata_chain_read_ogg(FLAC__Metadata_Chain *chain, const char *filename)
{
	return chain_read_(chain, filename, /*is_ogg=*/true);
}

static FLAC__bool chain_read_with_callbacks_(FLAC__Metadata_Chain *chain, FLAC__IOHandle handle, FLAC__IOCallbacks callbacks, FLAC__bool is_ogg)
{
	FLAC__bool ret;

	FLAC__ASSERT(0 != chain);

	chain_clear_(chain);

	if (0 == callbacks.read || 0 == callbacks.seek || 0 == callbacks.tell) {
		chain->status = FLAC__METADATA_CHAIN_STATUS_INVALID_CALLBACKS;
		return false;
	}

	chain->is_ogg = is_ogg;

	/* rewind */
	if(0 != callbacks.seek(handle, 0, SEEK_SET)) {
		chain->status = FLAC__METADATA_CHAIN_STATUS_SEEK_ERROR;
		return false;
	}

	/* the function also sets chain->status for us */
	ret = is_ogg?
		chain_read_ogg_cb_(chain, handle, callbacks.read) :
		chain_read_cb_(chain, handle, callbacks.read, callbacks.seek, callbacks.tell)
	;

	return ret;
}

FLAC_API FLAC__bool FLAC__metadata_chain_read_with_callbacks(FLAC__Metadata_Chain *chain, FLAC__IOHandle handle, FLAC__IOCallbacks callbacks)
{
	return chain_read_with_callbacks_(chain, handle, callbacks, /*is_ogg=*/false);
}

/*@@@@add to tests*/
FLAC_API FLAC__bool FLAC__metadata_chain_read_ogg_with_callbacks(FLAC__Metadata_Chain *chain, FLAC__IOHandle handle, FLAC__IOCallbacks callbacks)
{
	return chain_read_with_callbacks_(chain, handle, callbacks, /*is_ogg=*/true);
}

FLAC_API FLAC__bool FLAC__metadata_chain_check_if_tempfile_needed(FLAC__Metadata_Chain *chain, FLAC__bool use_padding)
{
	/* This does all the same checks that are in chain_prepare_for_write_()
	 * but doesn't actually alter the chain.  Make sure to update the logic
	 * here if chain_prepare_for_write_() changes.
	 */
	const off_t current_length = chain_calculate_length_(chain);

	FLAC__ASSERT(0 != chain);

	if(use_padding) {
		/* if the metadata shrank and the last block is padding, we just extend the last padding block */
		if(current_length < chain->initial_length && chain->tail->data->type == FLAC__METADATA_TYPE_PADDING)
			return false;
		/* if the metadata shrank more than 4 bytes then there's room to add another padding block */
		else if(current_length + (off_t)FLAC__STREAM_METADATA_HEADER_LENGTH <= chain->initial_length)
			return false;
		/* if the metadata grew but the last block is padding, try cutting the padding to restore the original length so we don't have to rewrite the whole file */
		else if(current_length > chain->initial_length) {
			const off_t delta = current_length - chain->initial_length;
			if(chain->tail->data->type == FLAC__METADATA_TYPE_PADDING) {
				/* if the delta is exactly the size of the last padding block, remove the padding block */
				if((off_t)chain->tail->data->length + (off_t)FLAC__STREAM_METADATA_HEADER_LENGTH == delta)
					return false;
				/* if there is at least 'delta' bytes of padding, trim the padding down */
				else if((off_t)chain->tail->data->length >= delta)
					return false;
			}
		}
	}

	return (current_length != chain->initial_length);
}

FLAC_API FLAC__bool FLAC__metadata_chain_write(FLAC__Metadata_Chain *chain, FLAC__bool use_padding, FLAC__bool preserve_file_stats)
{
	struct stat stats;
	const char *tempfile_path_prefix = 0;
	off_t current_length;

	FLAC__ASSERT(0 != chain);

	if (chain->is_ogg) { /* cannot write back to Ogg FLAC yet */
		chain->status = FLAC__METADATA_CHAIN_STATUS_INTERNAL_ERROR;
		return false;
	}

	if (0 == chain->filename) {
		chain->status = FLAC__METADATA_CHAIN_STATUS_READ_WRITE_MISMATCH;
		return false;
	}

	current_length = chain_prepare_for_write_(chain, use_padding);

	/* a return value of 0 means there was an error; chain->status is already set */
	if (0 == current_length)
		return false;

	if(preserve_file_stats)
		get_file_stats_(chain->filename, &stats);

	if(current_length == chain->initial_length) {
		if(!chain_rewrite_metadata_in_place_(chain))
			return false;
	}
	else {
		if(!chain_rewrite_file_(chain, tempfile_path_prefix))
			return false;

		/* recompute lengths and offsets */
		{
			const FLAC__Metadata_Node *node;
			chain->initial_length = current_length;
			chain->last_offset = chain->first_offset;
			for(node = chain->head; node; node = node->next)
				chain->last_offset += (FLAC__STREAM_METADATA_HEADER_LENGTH + node->data->length);
		}
	}

	if(preserve_file_stats)
		set_file_stats_(chain->filename, &stats);

	return true;
}

FLAC_API FLAC__bool FLAC__metadata_chain_write_with_callbacks(FLAC__Metadata_Chain *chain, FLAC__bool use_padding, FLAC__IOHandle handle, FLAC__IOCallbacks callbacks)
{
	off_t current_length;

	FLAC__ASSERT(0 != chain);

	if (chain->is_ogg) { /* cannot write back to Ogg FLAC yet */
		chain->status = FLAC__METADATA_CHAIN_STATUS_INTERNAL_ERROR;
		return false;
	}

	if (0 != chain->filename) {
		chain->status = FLAC__METADATA_CHAIN_STATUS_READ_WRITE_MISMATCH;
		return false;
	}

	if (0 == callbacks.write || 0 == callbacks.seek) {
		chain->status = FLAC__METADATA_CHAIN_STATUS_INVALID_CALLBACKS;
		return false;
	}

	if (FLAC__metadata_chain_check_if_tempfile_needed(chain, use_padding)) {
		chain->status = FLAC__METADATA_CHAIN_STATUS_WRONG_WRITE_CALL;
		return false;
	}

	current_length = chain_prepare_for_write_(chain, use_padding);

	/* a return value of 0 means there was an error; chain->status is already set */
	if (0 == current_length)
		return false;

	FLAC__ASSERT(current_length == chain->initial_length);

	return chain_rewrite_metadata_in_place_cb_(chain, handle, callbacks.write, callbacks.seek);
}

FLAC_API FLAC__bool FLAC__metadata_chain_write_with_callbacks_and_tempfile(FLAC__Metadata_Chain *chain, FLAC__bool use_padding, FLAC__IOHandle handle, FLAC__IOCallbacks callbacks, FLAC__IOHandle temp_handle, FLAC__IOCallbacks temp_callbacks)
{
	off_t current_length;

	FLAC__ASSERT(0 != chain);

	if (chain->is_ogg) { /* cannot write back to Ogg FLAC yet */
		chain->status = FLAC__METADATA_CHAIN_STATUS_INTERNAL_ERROR;
		return false;
	}

	if (0 != chain->filename) {
		chain->status = FLAC__METADATA_CHAIN_STATUS_READ_WRITE_MISMATCH;
		return false;
	}

	if (0 == callbacks.read || 0 == callbacks.seek || 0 == callbacks.eof) {
		chain->status = FLAC__METADATA_CHAIN_STATUS_INVALID_CALLBACKS;
		return false;
	}
	if (0 == temp_callbacks.write) {
		chain->status = FLAC__METADATA_CHAIN_STATUS_INVALID_CALLBACKS;
		return false;
	}

	if (!FLAC__metadata_chain_check_if_tempfile_needed(chain, use_padding)) {
		chain->status = FLAC__METADATA_CHAIN_STATUS_WRONG_WRITE_CALL;
		return false;
	}

	current_length = chain_prepare_for_write_(chain, use_padding);

	/* a return value of 0 means there was an error; chain->status is already set */
	if (0 == current_length)
		return false;

	FLAC__ASSERT(current_length != chain->initial_length);

	/* rewind */
	if(0 != callbacks.seek(handle, 0, SEEK_SET)) {
		chain->status = FLAC__METADATA_CHAIN_STATUS_SEEK_ERROR;
		return false;
	}

	if(!chain_rewrite_file_cb_(chain, handle, callbacks.read, callbacks.seek, callbacks.eof, temp_handle, temp_callbacks.write))
		return false;

	/* recompute lengths and offsets */
	{
		const FLAC__Metadata_Node *node;
		chain->initial_length = current_length;
		chain->last_offset = chain->first_offset;
		for(node = chain->head; node; node = node->next)
			chain->last_offset += (FLAC__STREAM_METADATA_HEADER_LENGTH + node->data->length);
	}

	return true;
}

FLAC_API void FLAC__metadata_chain_merge_padding(FLAC__Metadata_Chain *chain)
{
	FLAC__Metadata_Node *node;

	FLAC__ASSERT(0 != chain);

	for(node = chain->head; node; ) {
		if(!chain_merge_adjacent_padding_(chain, node))
			node = node->next;
	}
}

FLAC_API void FLAC__metadata_chain_sort_padding(FLAC__Metadata_Chain *chain)
{
	FLAC__Metadata_Node *node, *save;
	unsigned i;

	FLAC__ASSERT(0 != chain);

	/*
	 * Don't try and be too smart... this simple algo is good enough for
	 * the small number of nodes that we deal with.
	 */
	for(i = 0, node = chain->head; i < chain->nodes; i++) {
		if(node->data->type == FLAC__METADATA_TYPE_PADDING) {
			save = node->next;
			chain_remove_node_(chain, node);
			chain_append_node_(chain, node);
			node = save;
		}
		else {
			node = node->next;
		}
	}

	FLAC__metadata_chain_merge_padding(chain);
}


FLAC_API FLAC__Metadata_Iterator *FLAC__metadata_iterator_new(void)
{
	FLAC__Metadata_Iterator *iterator = (FLAC__Metadata_Iterator*)calloc(1, sizeof(FLAC__Metadata_Iterator));

	/* calloc() implies:
		iterator->current = 0;
		iterator->chain = 0;
	*/

	return iterator;
}

FLAC_API void FLAC__metadata_iterator_delete(FLAC__Metadata_Iterator *iterator)
{
	FLAC__ASSERT(0 != iterator);

	free(iterator);
}

FLAC_API void FLAC__metadata_iterator_init(FLAC__Metadata_Iterator *iterator, FLAC__Metadata_Chain *chain)
{
	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != chain);
	FLAC__ASSERT(0 != chain->head);

	iterator->chain = chain;
	iterator->current = chain->head;
}

FLAC_API FLAC__bool FLAC__metadata_iterator_next(FLAC__Metadata_Iterator *iterator)
{
	FLAC__ASSERT(0 != iterator);

	if(0 == iterator->current || 0 == iterator->current->next)
		return false;

	iterator->current = iterator->current->next;
	return true;
}

FLAC_API FLAC__bool FLAC__metadata_iterator_prev(FLAC__Metadata_Iterator *iterator)
{
	FLAC__ASSERT(0 != iterator);

	if(0 == iterator->current || 0 == iterator->current->prev)
		return false;

	iterator->current = iterator->current->prev;
	return true;
}

FLAC_API FLAC__MetadataType FLAC__metadata_iterator_get_block_type(const FLAC__Metadata_Iterator *iterator)
{
	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != iterator->current);
	FLAC__ASSERT(0 != iterator->current->data);

	return iterator->current->data->type;
}

FLAC_API FLAC__StreamMetadata *FLAC__metadata_iterator_get_block(FLAC__Metadata_Iterator *iterator)
{
	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != iterator->current);

	return iterator->current->data;
}

FLAC_API FLAC__bool FLAC__metadata_iterator_set_block(FLAC__Metadata_Iterator *iterator, FLAC__StreamMetadata *block)
{
	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != block);
	return FLAC__metadata_iterator_delete_block(iterator, false) && FLAC__metadata_iterator_insert_block_after(iterator, block);
}

FLAC_API FLAC__bool FLAC__metadata_iterator_delete_block(FLAC__Metadata_Iterator *iterator, FLAC__bool replace_with_padding)
{
	FLAC__Metadata_Node *save;

	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != iterator->current);

	if(0 == iterator->current->prev) {
		FLAC__ASSERT(iterator->current->data->type == FLAC__METADATA_TYPE_STREAMINFO);
		return false;
	}

	save = iterator->current->prev;

	if(replace_with_padding) {
		FLAC__metadata_object_delete_data(iterator->current->data);
		iterator->current->data->type = FLAC__METADATA_TYPE_PADDING;
	}
	else {
		chain_delete_node_(iterator->chain, iterator->current);
	}

	iterator->current = save;
	return true;
}

FLAC_API FLAC__bool FLAC__metadata_iterator_insert_block_before(FLAC__Metadata_Iterator *iterator, FLAC__StreamMetadata *block)
{
	FLAC__Metadata_Node *node;

	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != iterator->current);
	FLAC__ASSERT(0 != block);

	if(block->type == FLAC__METADATA_TYPE_STREAMINFO)
		return false;

	if(0 == iterator->current->prev) {
		FLAC__ASSERT(iterator->current->data->type == FLAC__METADATA_TYPE_STREAMINFO);
		return false;
	}

	if(0 == (node = node_new_()))
		return false;

	node->data = block;
	iterator_insert_node_(iterator, node);
	iterator->current = node;
	return true;
}

FLAC_API FLAC__bool FLAC__metadata_iterator_insert_block_after(FLAC__Metadata_Iterator *iterator, FLAC__StreamMetadata *block)
{
	FLAC__Metadata_Node *node;

	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != iterator->current);
	FLAC__ASSERT(0 != block);

	if(block->type == FLAC__METADATA_TYPE_STREAMINFO)
		return false;

	if(0 == (node = node_new_()))
		return false;

	node->data = block;
	iterator_insert_node_after_(iterator, node);
	iterator->current = node;
	return true;
}


/****************************************************************************
 *
 * Local function definitions
 *
 ***************************************************************************/

void pack_uint32_(FLAC__uint32 val, FLAC__byte *b, unsigned bytes)
{
	unsigned i;

	b += bytes;

	for(i = 0; i < bytes; i++) {
		*(--b) = (FLAC__byte)(val & 0xff);
		val >>= 8;
	}
}

void pack_uint32_little_endian_(FLAC__uint32 val, FLAC__byte *b, unsigned bytes)
{
	unsigned i;

	for(i = 0; i < bytes; i++) {
		*(b++) = (FLAC__byte)(val & 0xff);
		val >>= 8;
	}
}

void pack_uint64_(FLAC__uint64 val, FLAC__byte *b, unsigned bytes)
{
	unsigned i;

	b += bytes;

	for(i = 0; i < bytes; i++) {
		*(--b) = (FLAC__byte)(val & 0xff);
		val >>= 8;
	}
}

FLAC__uint32 unpack_uint32_(FLAC__byte *b, unsigned bytes)
{
	FLAC__uint32 ret = 0;
	unsigned i;

	for(i = 0; i < bytes; i++)
		ret = (ret << 8) | (FLAC__uint32)(*b++);

	return ret;
}

FLAC__uint32 unpack_uint32_little_endian_(FLAC__byte *b, unsigned bytes)
{
	FLAC__uint32 ret = 0;
	unsigned i;

	b += bytes;

	for(i = 0; i < bytes; i++)
		ret = (ret << 8) | (FLAC__uint32)(*--b);

	return ret;
}

FLAC__uint64 unpack_uint64_(FLAC__byte *b, unsigned bytes)
{
	FLAC__uint64 ret = 0;
	unsigned i;

	for(i = 0; i < bytes; i++)
		ret = (ret << 8) | (FLAC__uint64)(*b++);

	return ret;
}

FLAC__bool read_metadata_block_header_(FLAC__Metadata_SimpleIterator *iterator)
{
	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != iterator->file);

	if(!read_metadata_block_header_cb_((FLAC__IOHandle)iterator->file, (FLAC__IOCallback_Read)fread, &iterator->is_last, &iterator->type, &iterator->length)) {
		iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
		return false;
	}

	return true;
}

FLAC__bool read_metadata_block_data_(FLAC__Metadata_SimpleIterator *iterator, FLAC__StreamMetadata *block)
{
	FLAC__ASSERT(0 != iterator);
	FLAC__ASSERT(0 != iterator->file);

	iterator->status = read_metadata_block_data_cb_((FLAC__IOHandle)iterator->file, (FLAC__IOCallback_Read)fread, fseek_wrapper_, block);

	return (iterator->status == FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK);
}

FLAC__bool read_metadata_block_header_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__bool *is_last, FLAC__MetadataType *type, unsigned *length)
{
	FLAC__byte raw_header[FLAC__STREAM_METADATA_HEADER_LENGTH];

	if(read_cb(raw_header, 1, FLAC__STREAM_METADATA_HEADER_LENGTH, handle) != FLAC__STREAM_METADATA_HEADER_LENGTH)
		return false;

	*is_last = raw_header[0] & 0x80? true : false;
	*type = (FLAC__MetadataType)(raw_header[0] & 0x7f);
	*length = unpack_uint32_(raw_header + 1, 3);

	/* Note that we don't check:
	 *    if(iterator->type >= FLAC__METADATA_TYPE_UNDEFINED)
	 * we just will read in an opaque block
	 */

	return true;
}

FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__IOCallback_Seek seek_cb, FLAC__StreamMetadata *block)
{
	switch(block->type) {
		case FLAC__METADATA_TYPE_STREAMINFO:
			return read_metadata_block_data_streaminfo_cb_(handle, read_cb, &block->data.stream_info);
		case FLAC__METADATA_TYPE_PADDING:
			return read_metadata_block_data_padding_cb_(handle, seek_cb, &block->data.padding, block->length);
		case FLAC__METADATA_TYPE_APPLICATION:
			return read_metadata_block_data_application_cb_(handle, read_cb, &block->data.application, block->length);
		case FLAC__METADATA_TYPE_SEEKTABLE:
			return read_metadata_block_data_seektable_cb_(handle, read_cb, &block->data.seek_table, block->length);
		case FLAC__METADATA_TYPE_VORBIS_COMMENT:
			return read_metadata_block_data_vorbis_comment_cb_(handle, read_cb, &block->data.vorbis_comment);
		case FLAC__METADATA_TYPE_CUESHEET:
			return read_metadata_block_data_cuesheet_cb_(handle, read_cb, &block->data.cue_sheet);
		case FLAC__METADATA_TYPE_PICTURE:
			return read_metadata_block_data_picture_cb_(handle, read_cb, &block->data.picture);
		default:
			return read_metadata_block_data_unknown_cb_(handle, read_cb, &block->data.unknown, block->length);
	}
}

FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_streaminfo_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__StreamMetadata_StreamInfo *block)
{
	FLAC__byte buffer[FLAC__STREAM_METADATA_STREAMINFO_LENGTH], *b;

	if(read_cb(buffer, 1, FLAC__STREAM_METADATA_STREAMINFO_LENGTH, handle) != FLAC__STREAM_METADATA_STREAMINFO_LENGTH)
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;

	b = buffer;

	/* we are using hardcoded numbers for simplicity but we should
	 * probably eventually write a bit-level unpacker and use the
	 * _STREAMINFO_ constants.
	 */
	block->min_blocksize = unpack_uint32_(b, 2); b += 2;
	block->max_blocksize = unpack_uint32_(b, 2); b += 2;
	block->min_framesize = unpack_uint32_(b, 3); b += 3;
	block->max_framesize = unpack_uint32_(b, 3); b += 3;
	block->sample_rate = (unpack_uint32_(b, 2) << 4) | ((unsigned)(b[2] & 0xf0) >> 4);
	block->channels = (unsigned)((b[2] & 0x0e) >> 1) + 1;
	block->bits_per_sample = ((((unsigned)(b[2] & 0x01)) << 4) | (((unsigned)(b[3] & 0xf0)) >> 4)) + 1;
	block->total_samples = (((FLAC__uint64)(b[3] & 0x0f)) << 32) | unpack_uint64_(b+4, 4);
	memcpy(block->md5sum, b+8, 16);

	return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK;
}

FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_padding_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Seek seek_cb, FLAC__StreamMetadata_Padding *block, unsigned block_length)
{
	(void)block; /* nothing to do; we don't care about reading the padding bytes */

	if(0 != seek_cb(handle, block_length, SEEK_CUR))
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_SEEK_ERROR;

	return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK;
}

FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_application_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__StreamMetadata_Application *block, unsigned block_length)
{
	const unsigned id_bytes = FLAC__STREAM_METADATA_APPLICATION_ID_LEN / 8;

	if(read_cb(block->id, 1, id_bytes, handle) != id_bytes)
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;

	if(block_length < id_bytes)
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;

	block_length -= id_bytes;

	if(block_length == 0) {
		block->data = 0;
	}
	else {
		if(0 == (block->data = (FLAC__byte*)malloc(block_length)))
			return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_MEMORY_ALLOCATION_ERROR;

		if(read_cb(block->data, 1, block_length, handle) != block_length)
			return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
	}

	return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK;
}

FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_seektable_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__StreamMetadata_SeekTable *block, unsigned block_length)
{
	unsigned i;
	FLAC__byte buffer[FLAC__STREAM_METADATA_SEEKPOINT_LENGTH];

	FLAC__ASSERT(block_length % FLAC__STREAM_METADATA_SEEKPOINT_LENGTH == 0);

	block->num_points = block_length / FLAC__STREAM_METADATA_SEEKPOINT_LENGTH;

	if(block->num_points == 0)
		block->points = 0;
	else if(0 == (block->points = (FLAC__StreamMetadata_SeekPoint*)safe_malloc_mul_2op_(block->num_points, /*times*/sizeof(FLAC__StreamMetadata_SeekPoint))))
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_MEMORY_ALLOCATION_ERROR;

	for(i = 0; i < block->num_points; i++) {
		if(read_cb(buffer, 1, FLAC__STREAM_METADATA_SEEKPOINT_LENGTH, handle) != FLAC__STREAM_METADATA_SEEKPOINT_LENGTH)
			return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
		/* some MAGIC NUMBERs here */
		block->points[i].sample_number = unpack_uint64_(buffer, 8);
		block->points[i].stream_offset = unpack_uint64_(buffer+8, 8);
		block->points[i].frame_samples = unpack_uint32_(buffer+16, 2);
	}

	return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK;
}

FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_vorbis_comment_entry_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__StreamMetadata_VorbisComment_Entry *entry)
{
	const unsigned entry_length_len = FLAC__STREAM_METADATA_VORBIS_COMMENT_ENTRY_LENGTH_LEN / 8;
	FLAC__byte buffer[4]; /* magic number is asserted below */

	FLAC__ASSERT(FLAC__STREAM_METADATA_VORBIS_COMMENT_ENTRY_LENGTH_LEN / 8 == sizeof(buffer));

	if(read_cb(buffer, 1, entry_length_len, handle) != entry_length_len)
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
	entry->length = unpack_uint32_little_endian_(buffer, entry_length_len);

	if(0 != entry->entry)
		free(entry->entry);

	if(entry->length == 0) {
		entry->entry = 0;
	}
	else {
		if(0 == (entry->entry = (FLAC__byte*)safe_malloc_add_2op_(entry->length, /*+*/1)))
			return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_MEMORY_ALLOCATION_ERROR;

		if(read_cb(entry->entry, 1, entry->length, handle) != entry->length)
			return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;

		entry->entry[entry->length] = '\0';
	}

	return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK;
}

FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_vorbis_comment_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__StreamMetadata_VorbisComment *block)
{
	unsigned i;
	FLAC__Metadata_SimpleIteratorStatus status;
	const unsigned num_comments_len = FLAC__STREAM_METADATA_VORBIS_COMMENT_NUM_COMMENTS_LEN / 8;
	FLAC__byte buffer[4]; /* magic number is asserted below */

	FLAC__ASSERT(FLAC__STREAM_METADATA_VORBIS_COMMENT_NUM_COMMENTS_LEN / 8 == sizeof(buffer));

	if(FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK != (status = read_metadata_block_data_vorbis_comment_entry_cb_(handle, read_cb, &(block->vendor_string))))
		return status;

	if(read_cb(buffer, 1, num_comments_len, handle) != num_comments_len)
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
	block->num_comments = unpack_uint32_little_endian_(buffer, num_comments_len);

	if(block->num_comments == 0) {
		block->comments = 0;
	}
	else if(0 == (block->comments = (FLAC__StreamMetadata_VorbisComment_Entry*)calloc(block->num_comments, sizeof(FLAC__StreamMetadata_VorbisComment_Entry))))
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_MEMORY_ALLOCATION_ERROR;

	for(i = 0; i < block->num_comments; i++) {
		if(FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK != (status = read_metadata_block_data_vorbis_comment_entry_cb_(handle, read_cb, block->comments + i)))
			return status;
	}

	return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK;
}

FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_cuesheet_track_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__StreamMetadata_CueSheet_Track *track)
{
	unsigned i, len;
	FLAC__byte buffer[32]; /* asserted below that this is big enough */

	FLAC__ASSERT(sizeof(buffer) >= sizeof(FLAC__uint64));
	FLAC__ASSERT(sizeof(buffer) >= FLAC__STREAM_METADATA_CUESHEET_INDEX_RESERVED_LEN/8);
	FLAC__ASSERT(sizeof(buffer) >= (FLAC__STREAM_METADATA_CUESHEET_TRACK_TYPE_LEN + FLAC__STREAM_METADATA_CUESHEET_TRACK_PRE_EMPHASIS_LEN + FLAC__STREAM_METADATA_CUESHEET_TRACK_RESERVED_LEN) / 8);

	FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_TRACK_OFFSET_LEN % 8 == 0);
	len = FLAC__STREAM_METADATA_CUESHEET_TRACK_OFFSET_LEN / 8;
	if(read_cb(buffer, 1, len, handle) != len)
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
	track->offset = unpack_uint64_(buffer, len);

	FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_TRACK_NUMBER_LEN % 8 == 0);
	len = FLAC__STREAM_METADATA_CUESHEET_TRACK_NUMBER_LEN / 8;
	if(read_cb(buffer, 1, len, handle) != len)
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
	track->number = (FLAC__byte)unpack_uint32_(buffer, len);

	FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_TRACK_ISRC_LEN % 8 == 0);
	len = FLAC__STREAM_METADATA_CUESHEET_TRACK_ISRC_LEN / 8;
	if(read_cb(track->isrc, 1, len, handle) != len)
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;

	FLAC__ASSERT((FLAC__STREAM_METADATA_CUESHEET_TRACK_TYPE_LEN + FLAC__STREAM_METADATA_CUESHEET_TRACK_PRE_EMPHASIS_LEN + FLAC__STREAM_METADATA_CUESHEET_TRACK_RESERVED_LEN) % 8 == 0);
	len = (FLAC__STREAM_METADATA_CUESHEET_TRACK_TYPE_LEN + FLAC__STREAM_METADATA_CUESHEET_TRACK_PRE_EMPHASIS_LEN + FLAC__STREAM_METADATA_CUESHEET_TRACK_RESERVED_LEN) / 8;
	if(read_cb(buffer, 1, len, handle) != len)
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
	FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_TRACK_TYPE_LEN == 1);
	FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_TRACK_PRE_EMPHASIS_LEN == 1);
	track->type = buffer[0] >> 7;
	track->pre_emphasis = (buffer[0] >> 6) & 1;

	FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_TRACK_NUM_INDICES_LEN % 8 == 0);
	len = FLAC__STREAM_METADATA_CUESHEET_TRACK_NUM_INDICES_LEN / 8;
	if(read_cb(buffer, 1, len, handle) != len)
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
	track->num_indices = (FLAC__byte)unpack_uint32_(buffer, len);

	if(track->num_indices == 0) {
		track->indices = 0;
	}
	else if(0 == (track->indices = (FLAC__StreamMetadata_CueSheet_Index*)calloc(track->num_indices, sizeof(FLAC__StreamMetadata_CueSheet_Index))))
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_MEMORY_ALLOCATION_ERROR;

	for(i = 0; i < track->num_indices; i++) {
		FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_INDEX_OFFSET_LEN % 8 == 0);
		len = FLAC__STREAM_METADATA_CUESHEET_INDEX_OFFSET_LEN / 8;
		if(read_cb(buffer, 1, len, handle) != len)
			return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
		track->indices[i].offset = unpack_uint64_(buffer, len);

		FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_INDEX_NUMBER_LEN % 8 == 0);
		len = FLAC__STREAM_METADATA_CUESHEET_INDEX_NUMBER_LEN / 8;
		if(read_cb(buffer, 1, len, handle) != len)
			return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
		track->indices[i].number = (FLAC__byte)unpack_uint32_(buffer, len);

		FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_INDEX_RESERVED_LEN % 8 == 0);
		len = FLAC__STREAM_METADATA_CUESHEET_INDEX_RESERVED_LEN / 8;
		if(read_cb(buffer, 1, len, handle) != len)
			return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
	}

	return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK;
}

FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_cuesheet_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__StreamMetadata_CueSheet *block)
{
	unsigned i, len;
	FLAC__Metadata_SimpleIteratorStatus status;
	FLAC__byte buffer[1024]; /* MSVC needs a constant expression so we put a magic number and assert */

	FLAC__ASSERT((FLAC__STREAM_METADATA_CUESHEET_IS_CD_LEN + FLAC__STREAM_METADATA_CUESHEET_RESERVED_LEN)/8 <= sizeof(buffer));
	FLAC__ASSERT(sizeof(FLAC__uint64) <= sizeof(buffer));

	FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_MEDIA_CATALOG_NUMBER_LEN % 8 == 0);
	len = FLAC__STREAM_METADATA_CUESHEET_MEDIA_CATALOG_NUMBER_LEN / 8;
	if(read_cb(block->media_catalog_number, 1, len, handle) != len)
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;

	FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_LEAD_IN_LEN % 8 == 0);
	len = FLAC__STREAM_METADATA_CUESHEET_LEAD_IN_LEN / 8;
	if(read_cb(buffer, 1, len, handle) != len)
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
	block->lead_in = unpack_uint64_(buffer, len);

	FLAC__ASSERT((FLAC__STREAM_METADATA_CUESHEET_IS_CD_LEN + FLAC__STREAM_METADATA_CUESHEET_RESERVED_LEN) % 8 == 0);
	len = (FLAC__STREAM_METADATA_CUESHEET_IS_CD_LEN + FLAC__STREAM_METADATA_CUESHEET_RESERVED_LEN) / 8;
	if(read_cb(buffer, 1, len, handle) != len)
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
	block->is_cd = buffer[0]&0x80? true : false;

	FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_NUM_TRACKS_LEN % 8 == 0);
	len = FLAC__STREAM_METADATA_CUESHEET_NUM_TRACKS_LEN / 8;
	if(read_cb(buffer, 1, len, handle) != len)
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
	block->num_tracks = unpack_uint32_(buffer, len);

	if(block->num_tracks == 0) {
		block->tracks = 0;
	}
	else if(0 == (block->tracks = (FLAC__StreamMetadata_CueSheet_Track*)calloc(block->num_tracks, sizeof(FLAC__StreamMetadata_CueSheet_Track))))
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_MEMORY_ALLOCATION_ERROR;

	for(i = 0; i < block->num_tracks; i++) {
		if(FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK != (status = read_metadata_block_data_cuesheet_track_cb_(handle, read_cb, block->tracks + i)))
			return status;
	}

	return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK;
}

static FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_picture_cstring_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__byte **data, FLAC__uint32 *length, FLAC__uint32 length_len)
{
	FLAC__byte buffer[sizeof(FLAC__uint32)];

	FLAC__ASSERT(0 != data);
	FLAC__ASSERT(length_len%8 == 0);

	length_len /= 8; /* convert to bytes */

	FLAC__ASSERT(sizeof(buffer) >= length_len);

	if(read_cb(buffer, 1, length_len, handle) != length_len)
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
	*length = unpack_uint32_(buffer, length_len);

	if(0 != *data)
		free(*data);

	if(0 == (*data = (FLAC__byte*)safe_malloc_add_2op_(*length, /*+*/1)))
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_MEMORY_ALLOCATION_ERROR;

	if(*length > 0) {
		if(read_cb(*data, 1, *length, handle) != *length)
			return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
	}

	(*data)[*length] = '\0';

	return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK;
}

FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_picture_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__StreamMetadata_Picture *block)
{
	FLAC__Metadata_SimpleIteratorStatus status;
	FLAC__byte buffer[4]; /* asserted below that this is big enough */
	FLAC__uint32 len;

	FLAC__ASSERT(sizeof(buffer) >= FLAC__STREAM_METADATA_PICTURE_TYPE_LEN/8);
	FLAC__ASSERT(sizeof(buffer) >= FLAC__STREAM_METADATA_PICTURE_WIDTH_LEN/8);
	FLAC__ASSERT(sizeof(buffer) >= FLAC__STREAM_METADATA_PICTURE_HEIGHT_LEN/8);
	FLAC__ASSERT(sizeof(buffer) >= FLAC__STREAM_METADATA_PICTURE_DEPTH_LEN/8);
	FLAC__ASSERT(sizeof(buffer) >= FLAC__STREAM_METADATA_PICTURE_COLORS_LEN/8);

	FLAC__ASSERT(FLAC__STREAM_METADATA_PICTURE_TYPE_LEN % 8 == 0);
	len = FLAC__STREAM_METADATA_PICTURE_TYPE_LEN / 8;
	if(read_cb(buffer, 1, len, handle) != len)
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
	block->type = (FLAC__StreamMetadata_Picture_Type)unpack_uint32_(buffer, len);

	if((status = read_metadata_block_data_picture_cstring_cb_(handle, read_cb, (FLAC__byte**)(&(block->mime_type)), &len, FLAC__STREAM_METADATA_PICTURE_MIME_TYPE_LENGTH_LEN)) != FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK)
		return status;

	if((status = read_metadata_block_data_picture_cstring_cb_(handle, read_cb, &(block->description), &len, FLAC__STREAM_METADATA_PICTURE_DESCRIPTION_LENGTH_LEN)) != FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK)
		return status;

	FLAC__ASSERT(FLAC__STREAM_METADATA_PICTURE_WIDTH_LEN % 8 == 0);
	len = FLAC__STREAM_METADATA_PICTURE_WIDTH_LEN / 8;
	if(read_cb(buffer, 1, len, handle) != len)
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
	block->width = unpack_uint32_(buffer, len);

	FLAC__ASSERT(FLAC__STREAM_METADATA_PICTURE_HEIGHT_LEN % 8 == 0);
	len = FLAC__STREAM_METADATA_PICTURE_HEIGHT_LEN / 8;
	if(read_cb(buffer, 1, len, handle) != len)
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
	block->height = unpack_uint32_(buffer, len);

	FLAC__ASSERT(FLAC__STREAM_METADATA_PICTURE_DEPTH_LEN % 8 == 0);
	len = FLAC__STREAM_METADATA_PICTURE_DEPTH_LEN / 8;
	if(read_cb(buffer, 1, len, handle) != len)
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
	block->depth = unpack_uint32_(buffer, len);

	FLAC__ASSERT(FLAC__STREAM_METADATA_PICTURE_COLORS_LEN % 8 == 0);
	len = FLAC__STREAM_METADATA_PICTURE_COLORS_LEN / 8;
	if(read_cb(buffer, 1, len, handle) != len)
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
	block->colors = unpack_uint32_(buffer, len);

	/* for convenience we use read_metadata_block_data_picture_cstring_cb_() even though it adds an extra terminating NUL we don't use */
	if((status = read_metadata_block_data_picture_cstring_cb_(handle, read_cb, &(block->data), &(block->data_length), FLAC__STREAM_METADATA_PICTURE_DATA_LENGTH_LEN)) != FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK)
		return status;

	return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK;
}

FLAC__Metadata_SimpleIteratorStatus read_metadata_block_data_unknown_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__StreamMetadata_Unknown *block, unsigned block_length)
{
	if(block_length == 0) {
		block->data = 0;
	}
	else {
		if(0 == (block->data = (FLAC__byte*)malloc(block_length)))
			return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_MEMORY_ALLOCATION_ERROR;

		if(read_cb(block->data, 1, block_length, handle) != block_length)
			return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
	}

	return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK;
}

FLAC__bool write_metadata_block_header_(FILE *file, FLAC__Metadata_SimpleIteratorStatus *status, const FLAC__StreamMetadata *block)
{
	FLAC__ASSERT(0 != file);
	FLAC__ASSERT(0 != status);

	if(!write_metadata_block_header_cb_((FLAC__IOHandle)file, (FLAC__IOCallback_Write)fwrite, block)) {
		*status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_WRITE_ERROR;
		return false;
	}

	return true;
}

FLAC__bool write_metadata_block_data_(FILE *file, FLAC__Metadata_SimpleIteratorStatus *status, const FLAC__StreamMetadata *block)
{
	FLAC__ASSERT(0 != file);
	FLAC__ASSERT(0 != status);

	if (write_metadata_block_data_cb_((FLAC__IOHandle)file, (FLAC__IOCallback_Write)fwrite, block)) {
		*status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK;
		return true;
	}
	else {
		*status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_WRITE_ERROR;
		return false;
	}
}

FLAC__bool write_metadata_block_header_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Write write_cb, const FLAC__StreamMetadata *block)
{
	FLAC__byte buffer[FLAC__STREAM_METADATA_HEADER_LENGTH];

	FLAC__ASSERT(block->length < (1u << FLAC__STREAM_METADATA_LENGTH_LEN));

	buffer[0] = (block->is_last? 0x80 : 0) | (FLAC__byte)block->type;
	pack_uint32_(block->length, buffer + 1, 3);

	if(write_cb(buffer, 1, FLAC__STREAM_METADATA_HEADER_LENGTH, handle) != FLAC__STREAM_METADATA_HEADER_LENGTH)
		return false;

	return true;
}

FLAC__bool write_metadata_block_data_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Write write_cb, const FLAC__StreamMetadata *block)
{
	FLAC__ASSERT(0 != block);

	switch(block->type) {
		case FLAC__METADATA_TYPE_STREAMINFO:
			return write_metadata_block_data_streaminfo_cb_(handle, write_cb, &block->data.stream_info);
		case FLAC__METADATA_TYPE_PADDING:
			return write_metadata_block_data_padding_cb_(handle, write_cb, &block->data.padding, block->length);
		case FLAC__METADATA_TYPE_APPLICATION:
			return write_metadata_block_data_application_cb_(handle, write_cb, &block->data.application, block->length);
		case FLAC__METADATA_TYPE_SEEKTABLE:
			return write_metadata_block_data_seektable_cb_(handle, write_cb, &block->data.seek_table);
		case FLAC__METADATA_TYPE_VORBIS_COMMENT:
			return write_metadata_block_data_vorbis_comment_cb_(handle, write_cb, &block->data.vorbis_comment);
		case FLAC__METADATA_TYPE_CUESHEET:
			return write_metadata_block_data_cuesheet_cb_(handle, write_cb, &block->data.cue_sheet);
		case FLAC__METADATA_TYPE_PICTURE:
			return write_metadata_block_data_picture_cb_(handle, write_cb, &block->data.picture);
		default:
			return write_metadata_block_data_unknown_cb_(handle, write_cb, &block->data.unknown, block->length);
	}
}

FLAC__bool write_metadata_block_data_streaminfo_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Write write_cb, const FLAC__StreamMetadata_StreamInfo *block)
{
	FLAC__byte buffer[FLAC__STREAM_METADATA_STREAMINFO_LENGTH];
	const unsigned channels1 = block->channels - 1;
	const unsigned bps1 = block->bits_per_sample - 1;

	/* we are using hardcoded numbers for simplicity but we should
	 * probably eventually write a bit-level packer and use the
	 * _STREAMINFO_ constants.
	 */
	pack_uint32_(block->min_blocksize, buffer, 2);
	pack_uint32_(block->max_blocksize, buffer+2, 2);
	pack_uint32_(block->min_framesize, buffer+4, 3);
	pack_uint32_(block->max_framesize, buffer+7, 3);
	buffer[10] = (block->sample_rate >> 12) & 0xff;
	buffer[11] = (block->sample_rate >> 4) & 0xff;
	buffer[12] = ((block->sample_rate & 0x0f) << 4) | (channels1 << 1) | (bps1 >> 4);
	buffer[13] = (FLAC__byte)(((bps1 & 0x0f) << 4) | ((block->total_samples >> 32) & 0x0f));
	pack_uint32_((FLAC__uint32)block->total_samples, buffer+14, 4);
	memcpy(buffer+18, block->md5sum, 16);

	if(write_cb(buffer, 1, FLAC__STREAM_METADATA_STREAMINFO_LENGTH, handle) != FLAC__STREAM_METADATA_STREAMINFO_LENGTH)
		return false;

	return true;
}

FLAC__bool write_metadata_block_data_padding_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Write write_cb, const FLAC__StreamMetadata_Padding *block, unsigned block_length)
{
	unsigned i, n = block_length;
	FLAC__byte buffer[1024];

	(void)block;

	memset(buffer, 0, 1024);

	for(i = 0; i < n/1024; i++)
		if(write_cb(buffer, 1, 1024, handle) != 1024)
			return false;

	n %= 1024;

	if(write_cb(buffer, 1, n, handle) != n)
		return false;

	return true;
}

FLAC__bool write_metadata_block_data_application_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Write write_cb, const FLAC__StreamMetadata_Application *block, unsigned block_length)
{
	const unsigned id_bytes = FLAC__STREAM_METADATA_APPLICATION_ID_LEN / 8;

	if(write_cb(block->id, 1, id_bytes, handle) != id_bytes)
		return false;

	block_length -= id_bytes;

	if(write_cb(block->data, 1, block_length, handle) != block_length)
		return false;

	return true;
}

FLAC__bool write_metadata_block_data_seektable_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Write write_cb, const FLAC__StreamMetadata_SeekTable *block)
{
	unsigned i;
	FLAC__byte buffer[FLAC__STREAM_METADATA_SEEKPOINT_LENGTH];

	for(i = 0; i < block->num_points; i++) {
		/* some MAGIC NUMBERs here */
		pack_uint64_(block->points[i].sample_number, buffer, 8);
		pack_uint64_(block->points[i].stream_offset, buffer+8, 8);
		pack_uint32_(block->points[i].frame_samples, buffer+16, 2);
		if(write_cb(buffer, 1, FLAC__STREAM_METADATA_SEEKPOINT_LENGTH, handle) != FLAC__STREAM_METADATA_SEEKPOINT_LENGTH)
			return false;
	}

	return true;
}

FLAC__bool write_metadata_block_data_vorbis_comment_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Write write_cb, const FLAC__StreamMetadata_VorbisComment *block)
{
	unsigned i;
	const unsigned entry_length_len = FLAC__STREAM_METADATA_VORBIS_COMMENT_ENTRY_LENGTH_LEN / 8;
	const unsigned num_comments_len = FLAC__STREAM_METADATA_VORBIS_COMMENT_NUM_COMMENTS_LEN / 8;
	FLAC__byte buffer[4]; /* magic number is asserted below */

	FLAC__ASSERT(max(FLAC__STREAM_METADATA_VORBIS_COMMENT_ENTRY_LENGTH_LEN, FLAC__STREAM_METADATA_VORBIS_COMMENT_NUM_COMMENTS_LEN) / 8 == sizeof(buffer));

	pack_uint32_little_endian_(block->vendor_string.length, buffer, entry_length_len);
	if(write_cb(buffer, 1, entry_length_len, handle) != entry_length_len)
		return false;
	if(write_cb(block->vendor_string.entry, 1, block->vendor_string.length, handle) != block->vendor_string.length)
		return false;

	pack_uint32_little_endian_(block->num_comments, buffer, num_comments_len);
	if(write_cb(buffer, 1, num_comments_len, handle) != num_comments_len)
		return false;

	for(i = 0; i < block->num_comments; i++) {
		pack_uint32_little_endian_(block->comments[i].length, buffer, entry_length_len);
		if(write_cb(buffer, 1, entry_length_len, handle) != entry_length_len)
			return false;
		if(write_cb(block->comments[i].entry, 1, block->comments[i].length, handle) != block->comments[i].length)
			return false;
	}

	return true;
}

FLAC__bool write_metadata_block_data_cuesheet_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Write write_cb, const FLAC__StreamMetadata_CueSheet *block)
{
	unsigned i, j, len;
	FLAC__byte buffer[1024]; /* asserted below that this is big enough */

	FLAC__ASSERT(sizeof(buffer) >= sizeof(FLAC__uint64));
	FLAC__ASSERT(sizeof(buffer) >= FLAC__STREAM_METADATA_CUESHEET_RESERVED_LEN/8);
	FLAC__ASSERT(sizeof(buffer) >= (FLAC__STREAM_METADATA_CUESHEET_TRACK_TYPE_LEN + FLAC__STREAM_METADATA_CUESHEET_TRACK_PRE_EMPHASIS_LEN + FLAC__STREAM_METADATA_CUESHEET_IS_CD_LEN + FLAC__STREAM_METADATA_CUESHEET_TRACK_RESERVED_LEN)/8);
	FLAC__ASSERT(sizeof(buffer) >= FLAC__STREAM_METADATA_CUESHEET_INDEX_RESERVED_LEN/8);

	FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_MEDIA_CATALOG_NUMBER_LEN % 8 == 0);
	len = FLAC__STREAM_METADATA_CUESHEET_MEDIA_CATALOG_NUMBER_LEN / 8;
	if(write_cb(block->media_catalog_number, 1, len, handle) != len)
		return false;

	FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_LEAD_IN_LEN % 8 == 0);
	len = FLAC__STREAM_METADATA_CUESHEET_LEAD_IN_LEN / 8;
	pack_uint64_(block->lead_in, buffer, len);
	if(write_cb(buffer, 1, len, handle) != len)
		return false;

	FLAC__ASSERT((FLAC__STREAM_METADATA_CUESHEET_IS_CD_LEN + FLAC__STREAM_METADATA_CUESHEET_RESERVED_LEN) % 8 == 0);
	len = (FLAC__STREAM_METADATA_CUESHEET_IS_CD_LEN + FLAC__STREAM_METADATA_CUESHEET_RESERVED_LEN) / 8;
	memset(buffer, 0, len);
	if(block->is_cd)
		buffer[0] |= 0x80;
	if(write_cb(buffer, 1, len, handle) != len)
		return false;

	FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_NUM_TRACKS_LEN % 8 == 0);
	len = FLAC__STREAM_METADATA_CUESHEET_NUM_TRACKS_LEN / 8;
	pack_uint32_(block->num_tracks, buffer, len);
	if(write_cb(buffer, 1, len, handle) != len)
		return false;

	for(i = 0; i < block->num_tracks; i++) {
		FLAC__StreamMetadata_CueSheet_Track *track = block->tracks + i;

		FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_TRACK_OFFSET_LEN % 8 == 0);
		len = FLAC__STREAM_METADATA_CUESHEET_TRACK_OFFSET_LEN / 8;
		pack_uint64_(track->offset, buffer, len);
		if(write_cb(buffer, 1, len, handle) != len)
			return false;

		FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_TRACK_NUMBER_LEN % 8 == 0);
		len = FLAC__STREAM_METADATA_CUESHEET_TRACK_NUMBER_LEN / 8;
		pack_uint32_(track->number, buffer, len);
		if(write_cb(buffer, 1, len, handle) != len)
			return false;

		FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_TRACK_ISRC_LEN % 8 == 0);
		len = FLAC__STREAM_METADATA_CUESHEET_TRACK_ISRC_LEN / 8;
		if(write_cb(track->isrc, 1, len, handle) != len)
			return false;

		FLAC__ASSERT((FLAC__STREAM_METADATA_CUESHEET_TRACK_TYPE_LEN + FLAC__STREAM_METADATA_CUESHEET_TRACK_PRE_EMPHASIS_LEN + FLAC__STREAM_METADATA_CUESHEET_TRACK_RESERVED_LEN) % 8 == 0);
		len = (FLAC__STREAM_METADATA_CUESHEET_TRACK_TYPE_LEN + FLAC__STREAM_METADATA_CUESHEET_TRACK_PRE_EMPHASIS_LEN + FLAC__STREAM_METADATA_CUESHEET_TRACK_RESERVED_LEN) / 8;
		memset(buffer, 0, len);
		buffer[0] = (track->type << 7) | (track->pre_emphasis << 6);
		if(write_cb(buffer, 1, len, handle) != len)
			return false;

		FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_TRACK_NUM_INDICES_LEN % 8 == 0);
		len = FLAC__STREAM_METADATA_CUESHEET_TRACK_NUM_INDICES_LEN / 8;
		pack_uint32_(track->num_indices, buffer, len);
		if(write_cb(buffer, 1, len, handle) != len)
			return false;

		for(j = 0; j < track->num_indices; j++) {
			FLAC__StreamMetadata_CueSheet_Index *index = track->indices + j;

			FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_INDEX_OFFSET_LEN % 8 == 0);
			len = FLAC__STREAM_METADATA_CUESHEET_INDEX_OFFSET_LEN / 8;
			pack_uint64_(index->offset, buffer, len);
			if(write_cb(buffer, 1, len, handle) != len)
				return false;

			FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_INDEX_NUMBER_LEN % 8 == 0);
			len = FLAC__STREAM_METADATA_CUESHEET_INDEX_NUMBER_LEN / 8;
			pack_uint32_(index->number, buffer, len);
			if(write_cb(buffer, 1, len, handle) != len)
				return false;

			FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_INDEX_RESERVED_LEN % 8 == 0);
			len = FLAC__STREAM_METADATA_CUESHEET_INDEX_RESERVED_LEN / 8;
			memset(buffer, 0, len);
			if(write_cb(buffer, 1, len, handle) != len)
				return false;
		}
	}

	return true;
}

FLAC__bool write_metadata_block_data_picture_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Write write_cb, const FLAC__StreamMetadata_Picture *block)
{
	unsigned len;
	size_t slen;
	FLAC__byte buffer[4]; /* magic number is asserted below */

	FLAC__ASSERT(0 == FLAC__STREAM_METADATA_PICTURE_TYPE_LEN%8);
	FLAC__ASSERT(0 == FLAC__STREAM_METADATA_PICTURE_MIME_TYPE_LENGTH_LEN%8);
	FLAC__ASSERT(0 == FLAC__STREAM_METADATA_PICTURE_DESCRIPTION_LENGTH_LEN%8);
	FLAC__ASSERT(0 == FLAC__STREAM_METADATA_PICTURE_WIDTH_LEN%8);
	FLAC__ASSERT(0 == FLAC__STREAM_METADATA_PICTURE_HEIGHT_LEN%8);
	FLAC__ASSERT(0 == FLAC__STREAM_METADATA_PICTURE_DEPTH_LEN%8);
	FLAC__ASSERT(0 == FLAC__STREAM_METADATA_PICTURE_COLORS_LEN%8);
	FLAC__ASSERT(0 == FLAC__STREAM_METADATA_PICTURE_DATA_LENGTH_LEN%8);
	FLAC__ASSERT(sizeof(buffer) >= FLAC__STREAM_METADATA_PICTURE_TYPE_LEN/8);
	FLAC__ASSERT(sizeof(buffer) >= FLAC__STREAM_METADATA_PICTURE_MIME_TYPE_LENGTH_LEN/8);
	FLAC__ASSERT(sizeof(buffer) >= FLAC__STREAM_METADATA_PICTURE_DESCRIPTION_LENGTH_LEN/8);
	FLAC__ASSERT(sizeof(buffer) >= FLAC__STREAM_METADATA_PICTURE_WIDTH_LEN/8);
	FLAC__ASSERT(sizeof(buffer) >= FLAC__STREAM_METADATA_PICTURE_HEIGHT_LEN/8);
	FLAC__ASSERT(sizeof(buffer) >= FLAC__STREAM_METADATA_PICTURE_DEPTH_LEN/8);
	FLAC__ASSERT(sizeof(buffer) >= FLAC__STREAM_METADATA_PICTURE_COLORS_LEN/8);
	FLAC__ASSERT(sizeof(buffer) >= FLAC__STREAM_METADATA_PICTURE_DATA_LENGTH_LEN/8);

	len = FLAC__STREAM_METADATA_PICTURE_TYPE_LEN/8;
	pack_uint32_(block->type, buffer, len);
	if(write_cb(buffer, 1, len, handle) != len)
		return false;

	len = FLAC__STREAM_METADATA_PICTURE_MIME_TYPE_LENGTH_LEN/8;
	slen = strlen(block->mime_type);
	pack_uint32_(slen, buffer, len);
	if(write_cb(buffer, 1, len, handle) != len)
		return false;
	if(write_cb(block->mime_type, 1, slen, handle) != slen)
		return false;

	len = FLAC__STREAM_METADATA_PICTURE_DESCRIPTION_LENGTH_LEN/8;
	slen = strlen((const char *)block->description);
	pack_uint32_(slen, buffer, len);
	if(write_cb(buffer, 1, len, handle) != len)
		return false;
	if(write_cb(block->description, 1, slen, handle) != slen)
		return false;

	len = FLAC__STREAM_METADATA_PICTURE_WIDTH_LEN/8;
	pack_uint32_(block->width, buffer, len);
	if(write_cb(buffer, 1, len, handle) != len)
		return false;

	len = FLAC__STREAM_METADATA_PICTURE_HEIGHT_LEN/8;
	pack_uint32_(block->height, buffer, len);
	if(write_cb(buffer, 1, len, handle) != len)
		return false;

	len = FLAC__STREAM_METADATA_PICTURE_DEPTH_LEN/8;
	pack_uint32_(block->depth, buffer, len);
	if(write_cb(buffer, 1, len, handle) != len)
		return false;

	len = FLAC__STREAM_METADATA_PICTURE_COLORS_LEN/8;
	pack_uint32_(block->colors, buffer, len);
	if(write_cb(buffer, 1, len, handle) != len)
		return false;

	len = FLAC__STREAM_METADATA_PICTURE_DATA_LENGTH_LEN/8;
	pack_uint32_(block->data_length, buffer, len);
	if(write_cb(buffer, 1, len, handle) != len)
		return false;
	if(write_cb(block->data, 1, block->data_length, handle) != block->data_length)
		return false;

	return true;
}

FLAC__bool write_metadata_block_data_unknown_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Write write_cb, const FLAC__StreamMetadata_Unknown *block, unsigned block_length)
{
	if(write_cb(block->data, 1, block_length, handle) != block_length)
		return false;

	return true;
}

FLAC__bool write_metadata_block_stationary_(FLAC__Metadata_SimpleIterator *iterator, const FLAC__StreamMetadata *block)
{
	if(0 != fseeko(iterator->file, iterator->offset[iterator->depth], SEEK_SET)) {
		iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_SEEK_ERROR;
		return false;
	}

	if(!write_metadata_block_header_(iterator->file, &iterator->status, block))
		return false;

	if(!write_metadata_block_data_(iterator->file, &iterator->status, block))
		return false;

	if(0 != fseeko(iterator->file, iterator->offset[iterator->depth], SEEK_SET)) {
		iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_SEEK_ERROR;
		return false;
	}

	return read_metadata_block_header_(iterator);
}

FLAC__bool write_metadata_block_stationary_with_padding_(FLAC__Metadata_SimpleIterator *iterator, FLAC__StreamMetadata *block, unsigned padding_length, FLAC__bool padding_is_last)
{
	FLAC__StreamMetadata *padding;

	if(0 != fseeko(iterator->file, iterator->offset[iterator->depth], SEEK_SET)) {
		iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_SEEK_ERROR;
		return false;
	}

	block->is_last = false;

	if(!write_metadata_block_header_(iterator->file, &iterator->status, block))
		return false;

	if(!write_metadata_block_data_(iterator->file, &iterator->status, block))
		return false;

	if(0 == (padding = FLAC__metadata_object_new(FLAC__METADATA_TYPE_PADDING)))
		return FLAC__METADATA_SIMPLE_ITERATOR_STATUS_MEMORY_ALLOCATION_ERROR;

	padding->is_last = padding_is_last;
	padding->length = padding_length;

	if(!write_metadata_block_header_(iterator->file, &iterator->status, padding)) {
		FLAC__metadata_object_delete(padding);
		return false;
	}

	if(!write_metadata_block_data_(iterator->file, &iterator->status, padding)) {
		FLAC__metadata_object_delete(padding);
		return false;
	}

	FLAC__metadata_object_delete(padding);

	if(0 != fseeko(iterator->file, iterator->offset[iterator->depth], SEEK_SET)) {
		iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_SEEK_ERROR;
		return false;
	}

	return read_metadata_block_header_(iterator);
}

FLAC__bool rewrite_whole_file_(FLAC__Metadata_SimpleIterator *iterator, FLAC__StreamMetadata *block, FLAC__bool append)
{
	FILE *tempfile;
	char *tempfilename;
	int fixup_is_last_code = 0; /* 0 => no need to change any is_last flags */
	off_t fixup_is_last_flag_offset = -1;

	FLAC__ASSERT(0 != block || append == false);

	if(iterator->is_last) {
		if(append) {
			fixup_is_last_code = 1; /* 1 => clear the is_last flag at the following offset */
			fixup_is_last_flag_offset = iterator->offset[iterator->depth];
		}
		else if(0 == block) {
			simple_iterator_push_(iterator);
			if(!FLAC__metadata_simple_iterator_prev(iterator)) {
				(void)simple_iterator_pop_(iterator);
				return false;
			}
			fixup_is_last_code = -1; /* -1 => set the is_last the flag at the following offset */
			fixup_is_last_flag_offset = iterator->offset[iterator->depth];
			if(!simple_iterator_pop_(iterator))
				return false;
		}
	}

	if(!simple_iterator_copy_file_prefix_(iterator, &tempfile, &tempfilename, append))
		return false;

	if(0 != block) {
		if(!write_metadata_block_header_(tempfile, &iterator->status, block)) {
			cleanup_tempfile_(&tempfile, &tempfilename);
			return false;
		}

		if(!write_metadata_block_data_(tempfile, &iterator->status, block)) {
			cleanup_tempfile_(&tempfile, &tempfilename);
			return false;
		}
	}

	if(!simple_iterator_copy_file_postfix_(iterator, &tempfile, &tempfilename, fixup_is_last_code, fixup_is_last_flag_offset, block==0))
		return false;

	if(append)
		return FLAC__metadata_simple_iterator_next(iterator);

	return true;
}

void simple_iterator_push_(FLAC__Metadata_SimpleIterator *iterator)
{
	FLAC__ASSERT(iterator->depth+1 < SIMPLE_ITERATOR_MAX_PUSH_DEPTH);
	iterator->offset[iterator->depth+1] = iterator->offset[iterator->depth];
	iterator->depth++;
}

FLAC__bool simple_iterator_pop_(FLAC__Metadata_SimpleIterator *iterator)
{
	FLAC__ASSERT(iterator->depth > 0);
	iterator->depth--;
	if(0 != fseeko(iterator->file, iterator->offset[iterator->depth], SEEK_SET)) {
		iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_SEEK_ERROR;
		return false;
	}

	return read_metadata_block_header_(iterator);
}

/* return meanings:
 * 0: ok
 * 1: read error
 * 2: seek error
 * 3: not a FLAC file
 */
unsigned seek_to_first_metadata_block_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__IOCallback_Seek seek_cb)
{
	FLAC__byte buffer[4];
	size_t n;
	unsigned i;

	FLAC__ASSERT(FLAC__STREAM_SYNC_LENGTH == sizeof(buffer));

	/* skip any id3v2 tag */
	errno = 0;
	n = read_cb(buffer, 1, 4, handle);
	if(errno)
		return 1;
	else if(n != 4)
		return 3;
	else if(0 == memcmp(buffer, "ID3", 3)) {
		unsigned tag_length = 0;

		/* skip to the tag length */
		if(seek_cb(handle, 2, SEEK_CUR) < 0)
			return 2;

		/* read the length */
		for(i = 0; i < 4; i++) {
			if(read_cb(buffer, 1, 1, handle) < 1 || buffer[0] & 0x80)
				return 1;
			tag_length <<= 7;
			tag_length |= (buffer[0] & 0x7f);
		}

		/* skip the rest of the tag */
		if(seek_cb(handle, tag_length, SEEK_CUR) < 0)
			return 2;

		/* read the stream sync code */
		errno = 0;
		n = read_cb(buffer, 1, 4, handle);
		if(errno)
			return 1;
		else if(n != 4)
			return 3;
	}

	/* check for the fLaC signature */
	if(0 == memcmp(FLAC__STREAM_SYNC_STRING, buffer, FLAC__STREAM_SYNC_LENGTH))
		return 0;
	else
		return 3;
}

unsigned seek_to_first_metadata_block_(FILE *f)
{
	return seek_to_first_metadata_block_cb_((FLAC__IOHandle)f, (FLAC__IOCallback_Read)fread, fseek_wrapper_);
}

FLAC__bool simple_iterator_copy_file_prefix_(FLAC__Metadata_SimpleIterator *iterator, FILE **tempfile, char **tempfilename, FLAC__bool append)
{
	const off_t offset_end = append? iterator->offset[iterator->depth] + (off_t)FLAC__STREAM_METADATA_HEADER_LENGTH + (off_t)iterator->length : iterator->offset[iterator->depth];

	if(0 != fseeko(iterator->file, 0, SEEK_SET)) {
		iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_SEEK_ERROR;
		return false;
	}
	if(!open_tempfile_(iterator->filename, iterator->tempfile_path_prefix, tempfile, tempfilename, &iterator->status)) {
		cleanup_tempfile_(tempfile, tempfilename);
		return false;
	}
	if(!copy_n_bytes_from_file_(iterator->file, *tempfile, offset_end, &iterator->status)) {
		cleanup_tempfile_(tempfile, tempfilename);
		return false;
	}

	return true;
}

FLAC__bool simple_iterator_copy_file_postfix_(FLAC__Metadata_SimpleIterator *iterator, FILE **tempfile, char **tempfilename, int fixup_is_last_code, off_t fixup_is_last_flag_offset, FLAC__bool backup)
{
	off_t save_offset = iterator->offset[iterator->depth];
	FLAC__ASSERT(0 != *tempfile);

	if(0 != fseeko(iterator->file, save_offset + (off_t)FLAC__STREAM_METADATA_HEADER_LENGTH + (off_t)iterator->length, SEEK_SET)) {
		cleanup_tempfile_(tempfile, tempfilename);
		iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_SEEK_ERROR;
		return false;
	}
	if(!copy_remaining_bytes_from_file_(iterator->file, *tempfile, &iterator->status)) {
		cleanup_tempfile_(tempfile, tempfilename);
		return false;
	}

	if(fixup_is_last_code != 0) {
		/*
		 * if code == 1, it means a block was appended to the end so
		 *   we have to clear the is_last flag of the previous block
		 * if code == -1, it means the last block was deleted so
		 *   we have to set the is_last flag of the previous block
		 */
		/* MAGIC NUMBERs here; we know the is_last flag is the high bit of the byte at this location */
		FLAC__byte x;
		if(0 != fseeko(*tempfile, fixup_is_last_flag_offset, SEEK_SET)) {
			cleanup_tempfile_(tempfile, tempfilename);
			iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_SEEK_ERROR;
			return false;
		}
		if(fread(&x, 1, 1, *tempfile) != 1) {
			cleanup_tempfile_(tempfile, tempfilename);
			iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
			return false;
		}
		if(fixup_is_last_code > 0) {
			FLAC__ASSERT(x & 0x80);
			x &= 0x7f;
		}
		else {
			FLAC__ASSERT(!(x & 0x80));
			x |= 0x80;
		}
		if(0 != fseeko(*tempfile, fixup_is_last_flag_offset, SEEK_SET)) {
			cleanup_tempfile_(tempfile, tempfilename);
			iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_SEEK_ERROR;
			return false;
		}
		if(local__fwrite(&x, 1, 1, *tempfile) != 1) {
			cleanup_tempfile_(tempfile, tempfilename);
			iterator->status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_WRITE_ERROR;
			return false;
		}
	}

	(void)fclose(iterator->file);

	if(!transport_tempfile_(iterator->filename, tempfile, tempfilename, &iterator->status))
		return false;

	if(iterator->has_stats)
		set_file_stats_(iterator->filename, &iterator->stats);

	if(!simple_iterator_prime_input_(iterator, !iterator->is_writable))
		return false;
	if(backup) {
		while(iterator->offset[iterator->depth] + (off_t)FLAC__STREAM_METADATA_HEADER_LENGTH + (off_t)iterator->length < save_offset)
			if(!FLAC__metadata_simple_iterator_next(iterator))
				return false;
		return true;
	}
	else {
		/* move the iterator to it's original block faster by faking a push, then doing a pop_ */
		FLAC__ASSERT(iterator->depth == 0);
		iterator->offset[0] = save_offset;
		iterator->depth++;
		return simple_iterator_pop_(iterator);
	}
}

FLAC__bool copy_n_bytes_from_file_(FILE *file, FILE *tempfile, off_t bytes, FLAC__Metadata_SimpleIteratorStatus *status)
{
	FLAC__byte buffer[8192];
	size_t n;

	FLAC__ASSERT(bytes >= 0);
	while(bytes > 0) {
		n = min(sizeof(buffer), (size_t)bytes);
		if(fread(buffer, 1, n, file) != n) {
			*status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
			return false;
		}
		if(local__fwrite(buffer, 1, n, tempfile) != n) {
			*status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_WRITE_ERROR;
			return false;
		}
		bytes -= n;
	}

	return true;
}

FLAC__bool copy_n_bytes_from_file_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__IOHandle temp_handle, FLAC__IOCallback_Write temp_write_cb, off_t bytes, FLAC__Metadata_SimpleIteratorStatus *status)
{
	FLAC__byte buffer[8192];
	size_t n;

	FLAC__ASSERT(bytes >= 0);
	while(bytes > 0) {
		n = min(sizeof(buffer), (size_t)bytes);
		if(read_cb(buffer, 1, n, handle) != n) {
			*status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
			return false;
		}
		if(temp_write_cb(buffer, 1, n, temp_handle) != n) {
			*status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_WRITE_ERROR;
			return false;
		}
		bytes -= n;
	}

	return true;
}

FLAC__bool copy_remaining_bytes_from_file_(FILE *file, FILE *tempfile, FLAC__Metadata_SimpleIteratorStatus *status)
{
	FLAC__byte buffer[8192];
	size_t n;

	while(!feof(file)) {
		n = fread(buffer, 1, sizeof(buffer), file);
		if(n == 0 && !feof(file)) {
			*status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
			return false;
		}
		if(n > 0 && local__fwrite(buffer, 1, n, tempfile) != n) {
			*status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_WRITE_ERROR;
			return false;
		}
	}

	return true;
}

FLAC__bool copy_remaining_bytes_from_file_cb_(FLAC__IOHandle handle, FLAC__IOCallback_Read read_cb, FLAC__IOCallback_Eof eof_cb, FLAC__IOHandle temp_handle, FLAC__IOCallback_Write temp_write_cb, FLAC__Metadata_SimpleIteratorStatus *status)
{
	FLAC__byte buffer[8192];
	size_t n;

	while(!eof_cb(handle)) {
		n = read_cb(buffer, 1, sizeof(buffer), handle);
		if(n == 0 && !eof_cb(handle)) {
			*status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR;
			return false;
		}
		if(n > 0 && temp_write_cb(buffer, 1, n, temp_handle) != n) {
			*status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_WRITE_ERROR;
			return false;
		}
	}

	return true;
}

FLAC__bool open_tempfile_(const char *filename, const char *tempfile_path_prefix, FILE **tempfile, char **tempfilename, FLAC__Metadata_SimpleIteratorStatus *status)
{
	static const char *tempfile_suffix = ".metadata_edit";
	if(0 == tempfile_path_prefix) {
		if(0 == (*tempfilename = (char*)safe_malloc_add_3op_(strlen(filename), /*+*/strlen(tempfile_suffix), /*+*/1))) {
			*status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_MEMORY_ALLOCATION_ERROR;
			return false;
		}
		strcpy(*tempfilename, filename);
		strcat(*tempfilename, tempfile_suffix);
	}
	else {
		const char *p = strrchr(filename, '/');
		if(0 == p)
			p = filename;
		else
			p++;

		if(0 == (*tempfilename = (char*)safe_malloc_add_4op_(strlen(tempfile_path_prefix), /*+*/strlen(p), /*+*/strlen(tempfile_suffix), /*+*/2))) {
			*status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_MEMORY_ALLOCATION_ERROR;
			return false;
		}
		strcpy(*tempfilename, tempfile_path_prefix);
		strcat(*tempfilename, "/");
		strcat(*tempfilename, p);
		strcat(*tempfilename, tempfile_suffix);
	}

	if(0 == (*tempfile = fopen(*tempfilename, "w+b"))) {
		*status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_ERROR_OPENING_FILE;
		return false;
	}

	return true;
}

FLAC__bool transport_tempfile_(const char *filename, FILE **tempfile, char **tempfilename, FLAC__Metadata_SimpleIteratorStatus *status)
{
	FLAC__ASSERT(0 != filename);
	FLAC__ASSERT(0 != tempfile);
	FLAC__ASSERT(0 != *tempfile);
	FLAC__ASSERT(0 != tempfilename);
	FLAC__ASSERT(0 != *tempfilename);
	FLAC__ASSERT(0 != status);

	(void)fclose(*tempfile);
	*tempfile = 0;

#if defined _MSC_VER || defined __BORLANDC__ || defined __MINGW32__ || defined __EMX__
	/* on some flavors of windows, rename() will fail if the destination already exists */
	if(unlink(filename) < 0) {
		cleanup_tempfile_(tempfile, tempfilename);
		*status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_UNLINK_ERROR;
		return false;
	}
#endif

	/*@@@ to fully support the tempfile_path_prefix we need to update this piece to actually copy across filesystems instead of just rename(): */
	if(0 != rename(*tempfilename, filename)) {
		cleanup_tempfile_(tempfile, tempfilename);
		*status = FLAC__METADATA_SIMPLE_ITERATOR_STATUS_RENAME_ERROR;
		return false;
	}

	cleanup_tempfile_(tempfile, tempfilename);

	return true;
}

void cleanup_tempfile_(FILE **tempfile, char **tempfilename)
{
	if(0 != *tempfile) {
		(void)fclose(*tempfile);
		*tempfile = 0;
	}

	if(0 != *tempfilename) {
		(void)unlink(*tempfilename);
		free(*tempfilename);
		*tempfilename = 0;
	}
}

FLAC__bool get_file_stats_(const char *filename, struct stat *stats)
{
	FLAC__ASSERT(0 != filename);
	FLAC__ASSERT(0 != stats);
	return (0 == stat(filename, stats));
}

void set_file_stats_(const char *filename, struct stat *stats)
{
	struct utimbuf srctime;

	FLAC__ASSERT(0 != filename);
	FLAC__ASSERT(0 != stats);

	srctime.actime = stats->st_atime;
	srctime.modtime = stats->st_mtime;
	(void)chmod(filename, stats->st_mode);
	(void)utime(filename, &srctime);
#if !defined _MSC_VER && !defined __BORLANDC__ && !defined __MINGW32__ && !defined __EMX__
	(void)chown(filename, stats->st_uid, -1);
	(void)chown(filename, -1, stats->st_gid);
#endif
}

int fseek_wrapper_(FLAC__IOHandle handle, FLAC__int64 offset, int whence)
{
	return fseeko((FILE*)handle, (off_t)offset, whence);
}

FLAC__int64 ftell_wrapper_(FLAC__IOHandle handle)
{
	return ftello((FILE*)handle);
}

FLAC__Metadata_ChainStatus get_equivalent_status_(FLAC__Metadata_SimpleIteratorStatus status)
{
	switch(status) {
		case FLAC__METADATA_SIMPLE_ITERATOR_STATUS_OK:
			return FLAC__METADATA_CHAIN_STATUS_OK;
		case FLAC__METADATA_SIMPLE_ITERATOR_STATUS_ILLEGAL_INPUT:
			return FLAC__METADATA_CHAIN_STATUS_ILLEGAL_INPUT;
		case FLAC__METADATA_SIMPLE_ITERATOR_STATUS_ERROR_OPENING_FILE:
			return FLAC__METADATA_CHAIN_STATUS_ERROR_OPENING_FILE;
		case FLAC__METADATA_SIMPLE_ITERATOR_STATUS_NOT_A_FLAC_FILE:
			return FLAC__METADATA_CHAIN_STATUS_NOT_A_FLAC_FILE;
		case FLAC__METADATA_SIMPLE_ITERATOR_STATUS_NOT_WRITABLE:
			return FLAC__METADATA_CHAIN_STATUS_NOT_WRITABLE;
		case FLAC__METADATA_SIMPLE_ITERATOR_STATUS_BAD_METADATA:
			return FLAC__METADATA_CHAIN_STATUS_BAD_METADATA;
		case FLAC__METADATA_SIMPLE_ITERATOR_STATUS_READ_ERROR:
			return FLAC__METADATA_CHAIN_STATUS_READ_ERROR;
		case FLAC__METADATA_SIMPLE_ITERATOR_STATUS_SEEK_ERROR:
			return FLAC__METADATA_CHAIN_STATUS_SEEK_ERROR;
		case FLAC__METADATA_SIMPLE_ITERATOR_STATUS_WRITE_ERROR:
			return FLAC__METADATA_CHAIN_STATUS_WRITE_ERROR;
		case FLAC__METADATA_SIMPLE_ITERATOR_STATUS_RENAME_ERROR:
			return FLAC__METADATA_CHAIN_STATUS_RENAME_ERROR;
		case FLAC__METADATA_SIMPLE_ITERATOR_STATUS_UNLINK_ERROR:
			return FLAC__METADATA_CHAIN_STATUS_UNLINK_ERROR;
		case FLAC__METADATA_SIMPLE_ITERATOR_STATUS_MEMORY_ALLOCATION_ERROR:
			return FLAC__METADATA_CHAIN_STATUS_MEMORY_ALLOCATION_ERROR;
		case FLAC__METADATA_SIMPLE_ITERATOR_STATUS_INTERNAL_ERROR:
		default:
			return FLAC__METADATA_CHAIN_STATUS_INTERNAL_ERROR;
	}
}
//++-- metadata_iterators.c 

//++ metadata_object.c 
#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

/****************************************************************************
 *
 * Local routines
 *
 ***************************************************************************/

/* copy bytes:
 *  from = NULL  && bytes = 0
 *       to <- NULL
 *  from != NULL && bytes > 0
 *       to <- copy of from
 *  else ASSERT
 * malloc error leaves 'to' unchanged
 */
static FLAC__bool copy_bytes_(FLAC__byte **to, const FLAC__byte *from, unsigned bytes)
{
	FLAC__ASSERT(0 != to);
	if(bytes > 0 && 0 != from) {
		FLAC__byte *x;
		if(0 == (x = (FLAC__byte*)safe_malloc_(bytes)))
			return false;
		memcpy(x, from, bytes);
		*to = x;
	}
	else {
		FLAC__ASSERT(0 == from);
		FLAC__ASSERT(bytes == 0);
		*to = 0;
	}
	return true;
}

#if 0 /* UNUSED */
/* like copy_bytes_(), but free()s the original '*to' if the copy succeeds and the original '*to' is non-NULL */
static FLAC__bool free_copy_bytes_(FLAC__byte **to, const FLAC__byte *from, unsigned bytes)
{
	FLAC__byte *copy;
	FLAC__ASSERT(0 != to);
	if(copy_bytes_(&copy, from, bytes)) {
		if(*to)
			free(*to);
		*to = copy;
		return true;
	}
	else
		return false;
}
#endif

/* reallocate entry to 1 byte larger and add a terminating NUL */
/* realloc() failure leaves entry unchanged */
static FLAC__bool ensure_null_terminated_(FLAC__byte **entry, unsigned length)
{
	FLAC__byte *x = (FLAC__byte*)safe_realloc_add_2op_(*entry, length, /*+*/1);
	if(0 != x) {
		x[length] = '\0';
		*entry = x;
		return true;
	}
	else
		return false;
}

/* copies the NUL-terminated C-string 'from' to '*to', leaving '*to'
 * unchanged if malloc fails, free()ing the original '*to' if it
 * succeeds and the original '*to' was not NULL
 */
static FLAC__bool copy_cstring_(char **to, const char *from)
{
	char *copy = strdup(from);
	FLAC__ASSERT(to);
	if(copy) {
		if(*to)
			free(*to);
		*to = copy;
		return true;
	}
	else
		return false;
}

static FLAC__bool copy_vcentry_(FLAC__StreamMetadata_VorbisComment_Entry *to, const FLAC__StreamMetadata_VorbisComment_Entry *from)
{
	to->length = from->length;
	if(0 == from->entry) {
		FLAC__ASSERT(from->length == 0);
		to->entry = 0;
	}
	else {
		FLAC__byte *x;
		FLAC__ASSERT(from->length > 0);
		if(0 == (x = (FLAC__byte*)safe_malloc_add_2op_(from->length, /*+*/1)))
			return false;
		memcpy(x, from->entry, from->length);
		x[from->length] = '\0';
		to->entry = x;
	}
	return true;
}

static FLAC__bool copy_track_(FLAC__StreamMetadata_CueSheet_Track *to, const FLAC__StreamMetadata_CueSheet_Track *from)
{
	memcpy(to, from, sizeof(FLAC__StreamMetadata_CueSheet_Track));
	if(0 == from->indices) {
		FLAC__ASSERT(from->num_indices == 0);
	}
	else {
		FLAC__StreamMetadata_CueSheet_Index *x;
		FLAC__ASSERT(from->num_indices > 0);
		if(0 == (x = (FLAC__StreamMetadata_CueSheet_Index*)safe_malloc_mul_2op_(from->num_indices, /*times*/sizeof(FLAC__StreamMetadata_CueSheet_Index))))
			return false;
		memcpy(x, from->indices, from->num_indices * sizeof(FLAC__StreamMetadata_CueSheet_Index));
		to->indices = x;
	}
	return true;
}

static void seektable_calculate_length_(FLAC__StreamMetadata *object)
{
	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_SEEKTABLE);

	object->length = object->data.seek_table.num_points * FLAC__STREAM_METADATA_SEEKPOINT_LENGTH;
}

static FLAC__StreamMetadata_SeekPoint *seekpoint_array_new_(unsigned num_points)
{
	FLAC__StreamMetadata_SeekPoint *object_array;

	FLAC__ASSERT(num_points > 0);

	object_array = (FLAC__StreamMetadata_SeekPoint*)safe_malloc_mul_2op_(num_points, /*times*/sizeof(FLAC__StreamMetadata_SeekPoint));

	if(0 != object_array) {
		unsigned i;
		for(i = 0; i < num_points; i++) {
			object_array[i].sample_number = FLAC__STREAM_METADATA_SEEKPOINT_PLACEHOLDER;
			object_array[i].stream_offset = 0;
			object_array[i].frame_samples = 0;
		}
	}

	return object_array;
}

static void vorbiscomment_calculate_length_(FLAC__StreamMetadata *object)
{
	unsigned i;

	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_VORBIS_COMMENT);

	object->length = (FLAC__STREAM_METADATA_VORBIS_COMMENT_ENTRY_LENGTH_LEN) / 8;
	object->length += object->data.vorbis_comment.vendor_string.length;
	object->length += (FLAC__STREAM_METADATA_VORBIS_COMMENT_NUM_COMMENTS_LEN) / 8;
	for(i = 0; i < object->data.vorbis_comment.num_comments; i++) {
		object->length += (FLAC__STREAM_METADATA_VORBIS_COMMENT_ENTRY_LENGTH_LEN / 8);
		object->length += object->data.vorbis_comment.comments[i].length;
	}
}

static FLAC__StreamMetadata_VorbisComment_Entry *vorbiscomment_entry_array_new_(unsigned num_comments)
{
	FLAC__ASSERT(num_comments > 0);

	return (FLAC__StreamMetadata_VorbisComment_Entry*)safe_calloc_(num_comments, sizeof(FLAC__StreamMetadata_VorbisComment_Entry));
}

static void vorbiscomment_entry_array_delete_(FLAC__StreamMetadata_VorbisComment_Entry *object_array, unsigned num_comments)
{
	unsigned i;

	FLAC__ASSERT(0 != object_array && num_comments > 0);

	for(i = 0; i < num_comments; i++)
		if(0 != object_array[i].entry)
			free(object_array[i].entry);

	if(0 != object_array)
		free(object_array);
}

static FLAC__StreamMetadata_VorbisComment_Entry *vorbiscomment_entry_array_copy_(const FLAC__StreamMetadata_VorbisComment_Entry *object_array, unsigned num_comments)
{
	FLAC__StreamMetadata_VorbisComment_Entry *return_array;

	FLAC__ASSERT(0 != object_array);
	FLAC__ASSERT(num_comments > 0);

	return_array = vorbiscomment_entry_array_new_(num_comments);

	if(0 != return_array) {
		unsigned i;

		for(i = 0; i < num_comments; i++) {
			if(!copy_vcentry_(return_array+i, object_array+i)) {
				vorbiscomment_entry_array_delete_(return_array, num_comments);
				return 0;
			}
		}
	}

	return return_array;
}

static FLAC__bool vorbiscomment_set_entry_(FLAC__StreamMetadata *object, FLAC__StreamMetadata_VorbisComment_Entry *dest, const FLAC__StreamMetadata_VorbisComment_Entry *src, FLAC__bool copy)
{
	FLAC__byte *save;

	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(0 != dest);
	FLAC__ASSERT(0 != src);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_VORBIS_COMMENT);
	FLAC__ASSERT((0 != src->entry && src->length > 0) || (0 == src->entry && src->length == 0));

	save = dest->entry;

	if(0 != src->entry && src->length > 0) {
		if(copy) {
			/* do the copy first so that if we fail we leave the dest object untouched */
			if(!copy_vcentry_(dest, src))
				return false;
		}
		else {
			/* we have to make sure that the string we're taking over is null-terminated */

			/*
			 * Stripping the const from src->entry is OK since we're taking
			 * ownership of the pointer.  This is a hack around a deficiency
			 * in the API where the same function is used for 'copy' and
			 * 'own', but the source entry is a const pointer.  If we were
			 * precise, the 'own' flavor would be a separate function with a
			 * non-const source pointer.  But it's not, so we hack away.
			 */
			if(!ensure_null_terminated_((FLAC__byte**)(&src->entry), src->length))
				return false;
			*dest = *src;
		}
	}
	else {
		/* the src is null */
		*dest = *src;
	}

	if(0 != save)
		free(save);

	vorbiscomment_calculate_length_(object);
	return true;
}

static int vorbiscomment_find_entry_from_(const FLAC__StreamMetadata *object, unsigned offset, const char *field_name, unsigned field_name_length)
{
	unsigned i;

	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_VORBIS_COMMENT);
	FLAC__ASSERT(0 != field_name);

	for(i = offset; i < object->data.vorbis_comment.num_comments; i++) {
		if(FLAC__metadata_object_vorbiscomment_entry_matches(object->data.vorbis_comment.comments[i], field_name, field_name_length))
			return (int)i;
	}

	return -1;
}

static void cuesheet_calculate_length_(FLAC__StreamMetadata *object)
{
	unsigned i;

	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_CUESHEET);

	object->length = (
		FLAC__STREAM_METADATA_CUESHEET_MEDIA_CATALOG_NUMBER_LEN +
		FLAC__STREAM_METADATA_CUESHEET_LEAD_IN_LEN +
		FLAC__STREAM_METADATA_CUESHEET_IS_CD_LEN +
		FLAC__STREAM_METADATA_CUESHEET_RESERVED_LEN +
		FLAC__STREAM_METADATA_CUESHEET_NUM_TRACKS_LEN
	) / 8;

	object->length += object->data.cue_sheet.num_tracks * (
		FLAC__STREAM_METADATA_CUESHEET_TRACK_OFFSET_LEN +
		FLAC__STREAM_METADATA_CUESHEET_TRACK_NUMBER_LEN +
		FLAC__STREAM_METADATA_CUESHEET_TRACK_ISRC_LEN +
		FLAC__STREAM_METADATA_CUESHEET_TRACK_TYPE_LEN +
		FLAC__STREAM_METADATA_CUESHEET_TRACK_PRE_EMPHASIS_LEN +
		FLAC__STREAM_METADATA_CUESHEET_TRACK_RESERVED_LEN +
		FLAC__STREAM_METADATA_CUESHEET_TRACK_NUM_INDICES_LEN
	) / 8;

	for(i = 0; i < object->data.cue_sheet.num_tracks; i++) {
		object->length += object->data.cue_sheet.tracks[i].num_indices * (
			FLAC__STREAM_METADATA_CUESHEET_INDEX_OFFSET_LEN +
			FLAC__STREAM_METADATA_CUESHEET_INDEX_NUMBER_LEN +
			FLAC__STREAM_METADATA_CUESHEET_INDEX_RESERVED_LEN
		) / 8;
	}
}

static FLAC__StreamMetadata_CueSheet_Index *cuesheet_track_index_array_new_(unsigned num_indices)
{
	FLAC__ASSERT(num_indices > 0);

	return (FLAC__StreamMetadata_CueSheet_Index*)safe_calloc_(num_indices, sizeof(FLAC__StreamMetadata_CueSheet_Index));
}

static FLAC__StreamMetadata_CueSheet_Track *cuesheet_track_array_new_(unsigned num_tracks)
{
	FLAC__ASSERT(num_tracks > 0);

	return (FLAC__StreamMetadata_CueSheet_Track*)safe_calloc_(num_tracks, sizeof(FLAC__StreamMetadata_CueSheet_Track));
}

static void cuesheet_track_array_delete_(FLAC__StreamMetadata_CueSheet_Track *object_array, unsigned num_tracks)
{
	unsigned i;

	FLAC__ASSERT(0 != object_array && num_tracks > 0);

	for(i = 0; i < num_tracks; i++) {
		if(0 != object_array[i].indices) {
			FLAC__ASSERT(object_array[i].num_indices > 0);
			free(object_array[i].indices);
		}
	}

	if(0 != object_array)
		free(object_array);
}

static FLAC__StreamMetadata_CueSheet_Track *cuesheet_track_array_copy_(const FLAC__StreamMetadata_CueSheet_Track *object_array, unsigned num_tracks)
{
	FLAC__StreamMetadata_CueSheet_Track *return_array;

	FLAC__ASSERT(0 != object_array);
	FLAC__ASSERT(num_tracks > 0);

	return_array = cuesheet_track_array_new_(num_tracks);

	if(0 != return_array) {
		unsigned i;

		for(i = 0; i < num_tracks; i++) {
			if(!copy_track_(return_array+i, object_array+i)) {
				cuesheet_track_array_delete_(return_array, num_tracks);
				return 0;
			}
		}
	}

	return return_array;
}

static FLAC__bool cuesheet_set_track_(FLAC__StreamMetadata *object, FLAC__StreamMetadata_CueSheet_Track *dest, const FLAC__StreamMetadata_CueSheet_Track *src, FLAC__bool copy)
{
	FLAC__StreamMetadata_CueSheet_Index *save;

	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(0 != dest);
	FLAC__ASSERT(0 != src);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_CUESHEET);
	FLAC__ASSERT((0 != src->indices && src->num_indices > 0) || (0 == src->indices && src->num_indices == 0));

	save = dest->indices;

	/* do the copy first so that if we fail we leave the object untouched */
	if(copy) {
		if(!copy_track_(dest, src))
			return false;
	}
	else {
		*dest = *src;
	}

	if(0 != save)
		free(save);

	cuesheet_calculate_length_(object);
	return true;
}


/****************************************************************************
 *
 * Metadata object routines
 *
 ***************************************************************************/

FLAC_API FLAC__StreamMetadata *FLAC__metadata_object_new(FLAC__MetadataType type)
{
	FLAC__StreamMetadata *object;

	if(type > FLAC__MAX_METADATA_TYPE_CODE)
		return 0;

	object = (FLAC__StreamMetadata*)calloc(1, sizeof(FLAC__StreamMetadata));
	if(0 != object) {
		object->is_last = false;
		object->type = type;
		switch(type) {
			case FLAC__METADATA_TYPE_STREAMINFO:
				object->length = FLAC__STREAM_METADATA_STREAMINFO_LENGTH;
				break;
			case FLAC__METADATA_TYPE_PADDING:
				/* calloc() took care of this for us:
				object->length = 0;
				*/
				break;
			case FLAC__METADATA_TYPE_APPLICATION:
				object->length = FLAC__STREAM_METADATA_APPLICATION_ID_LEN / 8;
				/* calloc() took care of this for us:
				object->data.application.data = 0;
				*/
				break;
			case FLAC__METADATA_TYPE_SEEKTABLE:
				/* calloc() took care of this for us:
				object->length = 0;
				object->data.seek_table.num_points = 0;
				object->data.seek_table.points = 0;
				*/
				break;
			case FLAC__METADATA_TYPE_VORBIS_COMMENT:
				object->data.vorbis_comment.vendor_string.length = (unsigned)strlen(FLAC__VENDOR_STRING);
				if(!copy_bytes_(&object->data.vorbis_comment.vendor_string.entry, (const FLAC__byte*)FLAC__VENDOR_STRING, object->data.vorbis_comment.vendor_string.length+1)) {
					free(object);
					return 0;
				}
				vorbiscomment_calculate_length_(object);
				break;
			case FLAC__METADATA_TYPE_CUESHEET:
				cuesheet_calculate_length_(object);
				break;
			case FLAC__METADATA_TYPE_PICTURE:
				object->length = (
					FLAC__STREAM_METADATA_PICTURE_TYPE_LEN +
					FLAC__STREAM_METADATA_PICTURE_MIME_TYPE_LENGTH_LEN + /* empty mime_type string */
					FLAC__STREAM_METADATA_PICTURE_DESCRIPTION_LENGTH_LEN + /* empty description string */
					FLAC__STREAM_METADATA_PICTURE_WIDTH_LEN +
					FLAC__STREAM_METADATA_PICTURE_HEIGHT_LEN +
					FLAC__STREAM_METADATA_PICTURE_DEPTH_LEN +
					FLAC__STREAM_METADATA_PICTURE_COLORS_LEN +
					FLAC__STREAM_METADATA_PICTURE_DATA_LENGTH_LEN +
					0 /* no data */
				) / 8;
				object->data.picture.type = FLAC__STREAM_METADATA_PICTURE_TYPE_OTHER;
				object->data.picture.mime_type = 0;
				object->data.picture.description = 0;
				/* calloc() took care of this for us:
				object->data.picture.width = 0;
				object->data.picture.height = 0;
				object->data.picture.depth = 0;
				object->data.picture.colors = 0;
				object->data.picture.data_length = 0;
				object->data.picture.data = 0;
				*/
				/* now initialize mime_type and description with empty strings to make things easier on the client */
				if(!copy_cstring_(&object->data.picture.mime_type, "")) {
					free(object);
					return 0;
				}
				if(!copy_cstring_((char**)(&object->data.picture.description), "")) {
					if(object->data.picture.mime_type)
						free(object->data.picture.mime_type);
					free(object);
					return 0;
				}
				break;
			default:
				/* calloc() took care of this for us:
				object->length = 0;
				object->data.unknown.data = 0;
				*/
				break;
		}
	}

	return object;
}

FLAC_API FLAC__StreamMetadata *FLAC__metadata_object_clone(const FLAC__StreamMetadata *object)
{
	FLAC__StreamMetadata *to;

	FLAC__ASSERT(0 != object);

	if(0 != (to = FLAC__metadata_object_new(object->type))) {
		to->is_last = object->is_last;
		to->type = object->type;
		to->length = object->length;
		switch(to->type) {
			case FLAC__METADATA_TYPE_STREAMINFO:
				memcpy(&to->data.stream_info, &object->data.stream_info, sizeof(FLAC__StreamMetadata_StreamInfo));
				break;
			case FLAC__METADATA_TYPE_PADDING:
				break;
			case FLAC__METADATA_TYPE_APPLICATION:
				if(to->length < FLAC__STREAM_METADATA_APPLICATION_ID_LEN / 8) { /* underflow check */
					FLAC__metadata_object_delete(to);
					return 0;
				}
				memcpy(&to->data.application.id, &object->data.application.id, FLAC__STREAM_METADATA_APPLICATION_ID_LEN / 8);
				if(!copy_bytes_(&to->data.application.data, object->data.application.data, object->length - FLAC__STREAM_METADATA_APPLICATION_ID_LEN / 8)) {
					FLAC__metadata_object_delete(to);
					return 0;
				}
				break;
			case FLAC__METADATA_TYPE_SEEKTABLE:
				to->data.seek_table.num_points = object->data.seek_table.num_points;
				if(to->data.seek_table.num_points > SIZE_MAX / sizeof(FLAC__StreamMetadata_SeekPoint)) { /* overflow check */
					FLAC__metadata_object_delete(to);
					return 0;
				}
				if(!copy_bytes_((FLAC__byte**)&to->data.seek_table.points, (FLAC__byte*)object->data.seek_table.points, object->data.seek_table.num_points * sizeof(FLAC__StreamMetadata_SeekPoint))) {
					FLAC__metadata_object_delete(to);
					return 0;
				}
				break;
			case FLAC__METADATA_TYPE_VORBIS_COMMENT:
				if(0 != to->data.vorbis_comment.vendor_string.entry) {
					free(to->data.vorbis_comment.vendor_string.entry);
					to->data.vorbis_comment.vendor_string.entry = 0;
				}
				if(!copy_vcentry_(&to->data.vorbis_comment.vendor_string, &object->data.vorbis_comment.vendor_string)) {
					FLAC__metadata_object_delete(to);
					return 0;
				}
				if(object->data.vorbis_comment.num_comments == 0) {
					FLAC__ASSERT(0 == object->data.vorbis_comment.comments);
					to->data.vorbis_comment.comments = 0;
				}
				else {
					FLAC__ASSERT(0 != object->data.vorbis_comment.comments);
					to->data.vorbis_comment.comments = vorbiscomment_entry_array_copy_(object->data.vorbis_comment.comments, object->data.vorbis_comment.num_comments);
					if(0 == to->data.vorbis_comment.comments) {
						FLAC__metadata_object_delete(to);
						return 0;
					}
				}
				to->data.vorbis_comment.num_comments = object->data.vorbis_comment.num_comments;
				break;
			case FLAC__METADATA_TYPE_CUESHEET:
				memcpy(&to->data.cue_sheet, &object->data.cue_sheet, sizeof(FLAC__StreamMetadata_CueSheet));
				if(object->data.cue_sheet.num_tracks == 0) {
					FLAC__ASSERT(0 == object->data.cue_sheet.tracks);
				}
				else {
					FLAC__ASSERT(0 != object->data.cue_sheet.tracks);
					to->data.cue_sheet.tracks = cuesheet_track_array_copy_(object->data.cue_sheet.tracks, object->data.cue_sheet.num_tracks);
					if(0 == to->data.cue_sheet.tracks) {
						FLAC__metadata_object_delete(to);
						return 0;
					}
				}
				break;
			case FLAC__METADATA_TYPE_PICTURE:
				to->data.picture.type = object->data.picture.type;
				if(!copy_cstring_(&to->data.picture.mime_type, object->data.picture.mime_type)) {
					FLAC__metadata_object_delete(to);
					return 0;
				}
				if(!copy_cstring_((char**)(&to->data.picture.description), (const char*)object->data.picture.description)) {
					FLAC__metadata_object_delete(to);
					return 0;
				}
				to->data.picture.width = object->data.picture.width;
				to->data.picture.height = object->data.picture.height;
				to->data.picture.depth = object->data.picture.depth;
				to->data.picture.colors = object->data.picture.colors;
				to->data.picture.data_length = object->data.picture.data_length;
				if(!copy_bytes_((&to->data.picture.data), object->data.picture.data, object->data.picture.data_length)) {
					FLAC__metadata_object_delete(to);
					return 0;
				}
				break;
			default:
				if(!copy_bytes_(&to->data.unknown.data, object->data.unknown.data, object->length)) {
					FLAC__metadata_object_delete(to);
					return 0;
				}
				break;
		}
	}

	return to;
}

void FLAC__metadata_object_delete_data(FLAC__StreamMetadata *object)
{
	FLAC__ASSERT(0 != object);

	switch(object->type) {
		case FLAC__METADATA_TYPE_STREAMINFO:
		case FLAC__METADATA_TYPE_PADDING:
			break;
		case FLAC__METADATA_TYPE_APPLICATION:
			if(0 != object->data.application.data) {
				free(object->data.application.data);
				object->data.application.data = 0;
			}
			break;
		case FLAC__METADATA_TYPE_SEEKTABLE:
			if(0 != object->data.seek_table.points) {
				free(object->data.seek_table.points);
				object->data.seek_table.points = 0;
			}
			break;
		case FLAC__METADATA_TYPE_VORBIS_COMMENT:
			if(0 != object->data.vorbis_comment.vendor_string.entry) {
				free(object->data.vorbis_comment.vendor_string.entry);
				object->data.vorbis_comment.vendor_string.entry = 0;
			}
			if(0 != object->data.vorbis_comment.comments) {
				FLAC__ASSERT(object->data.vorbis_comment.num_comments > 0);
				vorbiscomment_entry_array_delete_(object->data.vorbis_comment.comments, object->data.vorbis_comment.num_comments);
			}
			break;
		case FLAC__METADATA_TYPE_CUESHEET:
			if(0 != object->data.cue_sheet.tracks) {
				FLAC__ASSERT(object->data.cue_sheet.num_tracks > 0);
				cuesheet_track_array_delete_(object->data.cue_sheet.tracks, object->data.cue_sheet.num_tracks);
			}
			break;
		case FLAC__METADATA_TYPE_PICTURE:
			if(0 != object->data.picture.mime_type) {
				free(object->data.picture.mime_type);
				object->data.picture.mime_type = 0;
			}
			if(0 != object->data.picture.description) {
				free(object->data.picture.description);
				object->data.picture.description = 0;
			}
			if(0 != object->data.picture.data) {
				free(object->data.picture.data);
				object->data.picture.data = 0;
			}
			break;
		default:
			if(0 != object->data.unknown.data) {
				free(object->data.unknown.data);
				object->data.unknown.data = 0;
			}
			break;
	}
}

FLAC_API void FLAC__metadata_object_delete(FLAC__StreamMetadata *object)
{
	FLAC__metadata_object_delete_data(object);
	free(object);
}

static FLAC__bool compare_block_data_streaminfo_(const FLAC__StreamMetadata_StreamInfo *block1, const FLAC__StreamMetadata_StreamInfo *block2)
{
	if(block1->min_blocksize != block2->min_blocksize)
		return false;
	if(block1->max_blocksize != block2->max_blocksize)
		return false;
	if(block1->min_framesize != block2->min_framesize)
		return false;
	if(block1->max_framesize != block2->max_framesize)
		return false;
	if(block1->sample_rate != block2->sample_rate)
		return false;
	if(block1->channels != block2->channels)
		return false;
	if(block1->bits_per_sample != block2->bits_per_sample)
		return false;
	if(block1->total_samples != block2->total_samples)
		return false;
	if(0 != memcmp(block1->md5sum, block2->md5sum, 16))
		return false;
	return true;
}

static FLAC__bool compare_block_data_application_(const FLAC__StreamMetadata_Application *block1, const FLAC__StreamMetadata_Application *block2, unsigned block_length)
{
	FLAC__ASSERT(0 != block1);
	FLAC__ASSERT(0 != block2);
	FLAC__ASSERT(block_length >= sizeof(block1->id));

	if(0 != memcmp(block1->id, block2->id, sizeof(block1->id)))
		return false;
	if(0 != block1->data && 0 != block2->data)
		return 0 == memcmp(block1->data, block2->data, block_length - sizeof(block1->id));
	else
		return block1->data == block2->data;
}

static FLAC__bool compare_block_data_seektable_(const FLAC__StreamMetadata_SeekTable *block1, const FLAC__StreamMetadata_SeekTable *block2)
{
	unsigned i;

	FLAC__ASSERT(0 != block1);
	FLAC__ASSERT(0 != block2);

	if(block1->num_points != block2->num_points)
		return false;

	if(0 != block1->points && 0 != block2->points) {
		for(i = 0; i < block1->num_points; i++) {
			if(block1->points[i].sample_number != block2->points[i].sample_number)
				return false;
			if(block1->points[i].stream_offset != block2->points[i].stream_offset)
				return false;
			if(block1->points[i].frame_samples != block2->points[i].frame_samples)
				return false;
		}
		return true;
	}
	else
		return block1->points == block2->points;
}

static FLAC__bool compare_block_data_vorbiscomment_(const FLAC__StreamMetadata_VorbisComment *block1, const FLAC__StreamMetadata_VorbisComment *block2)
{
	unsigned i;

	if(block1->vendor_string.length != block2->vendor_string.length)
		return false;

	if(0 != block1->vendor_string.entry && 0 != block2->vendor_string.entry) {
		if(0 != memcmp(block1->vendor_string.entry, block2->vendor_string.entry, block1->vendor_string.length))
			return false;
	}
	else if(block1->vendor_string.entry != block2->vendor_string.entry)
		return false;

	if(block1->num_comments != block2->num_comments)
		return false;

	for(i = 0; i < block1->num_comments; i++) {
		if(0 != block1->comments[i].entry && 0 != block2->comments[i].entry) {
			if(0 != memcmp(block1->comments[i].entry, block2->comments[i].entry, block1->comments[i].length))
				return false;
		}
		else if(block1->comments[i].entry != block2->comments[i].entry)
			return false;
	}
	return true;
}

static FLAC__bool compare_block_data_cuesheet_(const FLAC__StreamMetadata_CueSheet *block1, const FLAC__StreamMetadata_CueSheet *block2)
{
	unsigned i, j;

	if(0 != strcmp(block1->media_catalog_number, block2->media_catalog_number))
		return false;

	if(block1->lead_in != block2->lead_in)
		return false;

	if(block1->is_cd != block2->is_cd)
		return false;

	if(block1->num_tracks != block2->num_tracks)
		return false;

	if(0 != block1->tracks && 0 != block2->tracks) {
		FLAC__ASSERT(block1->num_tracks > 0);
		for(i = 0; i < block1->num_tracks; i++) {
			if(block1->tracks[i].offset != block2->tracks[i].offset)
				return false;
			if(block1->tracks[i].number != block2->tracks[i].number)
				return false;
			if(0 != memcmp(block1->tracks[i].isrc, block2->tracks[i].isrc, sizeof(block1->tracks[i].isrc)))
				return false;
			if(block1->tracks[i].type != block2->tracks[i].type)
				return false;
			if(block1->tracks[i].pre_emphasis != block2->tracks[i].pre_emphasis)
				return false;
			if(block1->tracks[i].num_indices != block2->tracks[i].num_indices)
				return false;
			if(0 != block1->tracks[i].indices && 0 != block2->tracks[i].indices) {
				FLAC__ASSERT(block1->tracks[i].num_indices > 0);
				for(j = 0; j < block1->tracks[i].num_indices; j++) {
					if(block1->tracks[i].indices[j].offset != block2->tracks[i].indices[j].offset)
						return false;
					if(block1->tracks[i].indices[j].number != block2->tracks[i].indices[j].number)
						return false;
				}
			}
			else if(block1->tracks[i].indices != block2->tracks[i].indices)
				return false;
		}
	}
	else if(block1->tracks != block2->tracks)
		return false;
	return true;
}

static FLAC__bool compare_block_data_picture_(const FLAC__StreamMetadata_Picture *block1, const FLAC__StreamMetadata_Picture *block2)
{
	if(block1->type != block2->type)
		return false;
	if(block1->mime_type != block2->mime_type && (0 == block1->mime_type || 0 == block2->mime_type || strcmp(block1->mime_type, block2->mime_type)))
		return false;
	if(block1->description != block2->description && (0 == block1->description || 0 == block2->description || strcmp((const char *)block1->description, (const char *)block2->description)))
		return false;
	if(block1->width != block2->width)
		return false;
	if(block1->height != block2->height)
		return false;
	if(block1->depth != block2->depth)
		return false;
	if(block1->colors != block2->colors)
		return false;
	if(block1->data_length != block2->data_length)
		return false;
	if(block1->data != block2->data && (0 == block1->data || 0 == block2->data || memcmp(block1->data, block2->data, block1->data_length)))
		return false;
	return true;
}

static FLAC__bool compare_block_data_unknown_(const FLAC__StreamMetadata_Unknown *block1, const FLAC__StreamMetadata_Unknown *block2, unsigned block_length)
{
	FLAC__ASSERT(0 != block1);
	FLAC__ASSERT(0 != block2);

	if(0 != block1->data && 0 != block2->data)
		return 0 == memcmp(block1->data, block2->data, block_length);
	else
		return block1->data == block2->data;
}

FLAC_API FLAC__bool FLAC__metadata_object_is_equal(const FLAC__StreamMetadata *block1, const FLAC__StreamMetadata *block2)
{
	FLAC__ASSERT(0 != block1);
	FLAC__ASSERT(0 != block2);

	if(block1->type != block2->type) {
		return false;
	}
	if(block1->is_last != block2->is_last) {
		return false;
	}
	if(block1->length != block2->length) {
		return false;
	}
	switch(block1->type) {
		case FLAC__METADATA_TYPE_STREAMINFO:
			return compare_block_data_streaminfo_(&block1->data.stream_info, &block2->data.stream_info);
		case FLAC__METADATA_TYPE_PADDING:
			return true; /* we don't compare the padding guts */
		case FLAC__METADATA_TYPE_APPLICATION:
			return compare_block_data_application_(&block1->data.application, &block2->data.application, block1->length);
		case FLAC__METADATA_TYPE_SEEKTABLE:
			return compare_block_data_seektable_(&block1->data.seek_table, &block2->data.seek_table);
		case FLAC__METADATA_TYPE_VORBIS_COMMENT:
			return compare_block_data_vorbiscomment_(&block1->data.vorbis_comment, &block2->data.vorbis_comment);
		case FLAC__METADATA_TYPE_CUESHEET:
			return compare_block_data_cuesheet_(&block1->data.cue_sheet, &block2->data.cue_sheet);
		case FLAC__METADATA_TYPE_PICTURE:
			return compare_block_data_picture_(&block1->data.picture, &block2->data.picture);
		default:
			return compare_block_data_unknown_(&block1->data.unknown, &block2->data.unknown, block1->length);
	}
}

FLAC_API FLAC__bool FLAC__metadata_object_application_set_data(FLAC__StreamMetadata *object, FLAC__byte *data, unsigned length, FLAC__bool copy)
{
	FLAC__byte *save;

	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_APPLICATION);
	FLAC__ASSERT((0 != data && length > 0) || (0 == data && length == 0 && copy == false));

	save = object->data.application.data;

	/* do the copy first so that if we fail we leave the object untouched */
	if(copy) {
		if(!copy_bytes_(&object->data.application.data, data, length))
			return false;
	}
	else {
		object->data.application.data = data;
	}

	if(0 != save)
		free(save);

	object->length = FLAC__STREAM_METADATA_APPLICATION_ID_LEN / 8 + length;
	return true;
}

FLAC_API FLAC__bool FLAC__metadata_object_seektable_resize_points(FLAC__StreamMetadata *object, unsigned new_num_points)
{
	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_SEEKTABLE);

	if(0 == object->data.seek_table.points) {
		FLAC__ASSERT(object->data.seek_table.num_points == 0);
		if(0 == new_num_points)
			return true;
		else if(0 == (object->data.seek_table.points = seekpoint_array_new_(new_num_points)))
			return false;
	}
	else {
		const size_t old_size = object->data.seek_table.num_points * sizeof(FLAC__StreamMetadata_SeekPoint);
		const size_t new_size = new_num_points * sizeof(FLAC__StreamMetadata_SeekPoint);

		/* overflow check */
		if((size_t)new_num_points > SIZE_MAX / sizeof(FLAC__StreamMetadata_SeekPoint))
			return false;

		FLAC__ASSERT(object->data.seek_table.num_points > 0);

		if(new_size == 0) {
			free(object->data.seek_table.points);
			object->data.seek_table.points = 0;
		}
		else if(0 == (object->data.seek_table.points = (FLAC__StreamMetadata_SeekPoint*)realloc(object->data.seek_table.points, new_size)))
			return false;

		/* if growing, set new elements to placeholders */
		if(new_size > old_size) {
			unsigned i;
			for(i = object->data.seek_table.num_points; i < new_num_points; i++) {
				object->data.seek_table.points[i].sample_number = FLAC__STREAM_METADATA_SEEKPOINT_PLACEHOLDER;
				object->data.seek_table.points[i].stream_offset = 0;
				object->data.seek_table.points[i].frame_samples = 0;
			}
		}
	}

	object->data.seek_table.num_points = new_num_points;

	seektable_calculate_length_(object);
	return true;
}

FLAC_API void FLAC__metadata_object_seektable_set_point(FLAC__StreamMetadata *object, unsigned point_num, FLAC__StreamMetadata_SeekPoint point)
{
	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_SEEKTABLE);
	FLAC__ASSERT(point_num < object->data.seek_table.num_points);

	object->data.seek_table.points[point_num] = point;
}

FLAC_API FLAC__bool FLAC__metadata_object_seektable_insert_point(FLAC__StreamMetadata *object, unsigned point_num, FLAC__StreamMetadata_SeekPoint point)
{
	int i;

	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_SEEKTABLE);
	FLAC__ASSERT(point_num <= object->data.seek_table.num_points);

	if(!FLAC__metadata_object_seektable_resize_points(object, object->data.seek_table.num_points+1))
		return false;

	/* move all points >= point_num forward one space */
	for(i = (int)object->data.seek_table.num_points-1; i > (int)point_num; i--)
		object->data.seek_table.points[i] = object->data.seek_table.points[i-1];

	FLAC__metadata_object_seektable_set_point(object, point_num, point);
	seektable_calculate_length_(object);
	return true;
}

FLAC_API FLAC__bool FLAC__metadata_object_seektable_delete_point(FLAC__StreamMetadata *object, unsigned point_num)
{
	unsigned i;

	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_SEEKTABLE);
	FLAC__ASSERT(point_num < object->data.seek_table.num_points);

	/* move all points > point_num backward one space */
	for(i = point_num; i < object->data.seek_table.num_points-1; i++)
		object->data.seek_table.points[i] = object->data.seek_table.points[i+1];

	return FLAC__metadata_object_seektable_resize_points(object, object->data.seek_table.num_points-1);
}

FLAC_API FLAC__bool FLAC__metadata_object_seektable_is_legal(const FLAC__StreamMetadata *object)
{
	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_SEEKTABLE);

	return FLAC__format_seektable_is_legal(&object->data.seek_table);
}

FLAC_API FLAC__bool FLAC__metadata_object_seektable_template_append_placeholders(FLAC__StreamMetadata *object, unsigned num)
{
	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_SEEKTABLE);

	if(num > 0)
		/* WATCHOUT: we rely on the fact that growing the array adds PLACEHOLDERS at the end */
		return FLAC__metadata_object_seektable_resize_points(object, object->data.seek_table.num_points + num);
	else
		return true;
}

FLAC_API FLAC__bool FLAC__metadata_object_seektable_template_append_point(FLAC__StreamMetadata *object, FLAC__uint64 sample_number)
{
	FLAC__StreamMetadata_SeekTable *seek_table;

	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_SEEKTABLE);

	seek_table = &object->data.seek_table;

	if(!FLAC__metadata_object_seektable_resize_points(object, seek_table->num_points + 1))
		return false;

	seek_table->points[seek_table->num_points - 1].sample_number = sample_number;
	seek_table->points[seek_table->num_points - 1].stream_offset = 0;
	seek_table->points[seek_table->num_points - 1].frame_samples = 0;

	return true;
}

FLAC_API FLAC__bool FLAC__metadata_object_seektable_template_append_points(FLAC__StreamMetadata *object, FLAC__uint64 sample_numbers[], unsigned num)
{
	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_SEEKTABLE);
	FLAC__ASSERT(0 != sample_numbers || num == 0);

	if(num > 0) {
		FLAC__StreamMetadata_SeekTable *seek_table = &object->data.seek_table;
		unsigned i, j;

		i = seek_table->num_points;

		if(!FLAC__metadata_object_seektable_resize_points(object, seek_table->num_points + num))
			return false;

		for(j = 0; j < num; i++, j++) {
			seek_table->points[i].sample_number = sample_numbers[j];
			seek_table->points[i].stream_offset = 0;
			seek_table->points[i].frame_samples = 0;
		}
	}

	return true;
}

FLAC_API FLAC__bool FLAC__metadata_object_seektable_template_append_spaced_points(FLAC__StreamMetadata *object, unsigned num, FLAC__uint64 total_samples)
{
	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_SEEKTABLE);
	FLAC__ASSERT(total_samples > 0);

	if(num > 0 && total_samples > 0) {
		FLAC__StreamMetadata_SeekTable *seek_table = &object->data.seek_table;
		unsigned i, j;

		i = seek_table->num_points;

		if(!FLAC__metadata_object_seektable_resize_points(object, seek_table->num_points + num))
			return false;

		for(j = 0; j < num; i++, j++) {
			seek_table->points[i].sample_number = total_samples * (FLAC__uint64)j / (FLAC__uint64)num;
			seek_table->points[i].stream_offset = 0;
			seek_table->points[i].frame_samples = 0;
		}
	}

	return true;
}

FLAC_API FLAC__bool FLAC__metadata_object_seektable_template_append_spaced_points_by_samples(FLAC__StreamMetadata *object, unsigned samples, FLAC__uint64 total_samples)
{
	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_SEEKTABLE);
	FLAC__ASSERT(samples > 0);
	FLAC__ASSERT(total_samples > 0);

	if(samples > 0 && total_samples > 0) {
		FLAC__StreamMetadata_SeekTable *seek_table = &object->data.seek_table;
		unsigned i, j;
		FLAC__uint64 num, sample;

		num = 1 + total_samples / samples; /* 1+ for the first sample at 0 */
		/* now account for the fact that we don't place a seekpoint at "total_samples" since samples are number from 0: */
		if(total_samples % samples == 0)
			num--;

		i = seek_table->num_points;

		if(!FLAC__metadata_object_seektable_resize_points(object, seek_table->num_points + (unsigned)num))
			return false;

		sample = 0;
		for(j = 0; j < num; i++, j++, sample += samples) {
			seek_table->points[i].sample_number = sample;
			seek_table->points[i].stream_offset = 0;
			seek_table->points[i].frame_samples = 0;
		}
	}

	return true;
}

FLAC_API FLAC__bool FLAC__metadata_object_seektable_template_sort(FLAC__StreamMetadata *object, FLAC__bool compact)
{
	unsigned unique;

	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_SEEKTABLE);

	unique = FLAC__format_seektable_sort(&object->data.seek_table);

	return !compact || FLAC__metadata_object_seektable_resize_points(object, unique);
}

FLAC_API FLAC__bool FLAC__metadata_object_vorbiscomment_set_vendor_string(FLAC__StreamMetadata *object, FLAC__StreamMetadata_VorbisComment_Entry entry, FLAC__bool copy)
{
	if(!FLAC__format_vorbiscomment_entry_value_is_legal(entry.entry, entry.length))
		return false;
	return vorbiscomment_set_entry_(object, &object->data.vorbis_comment.vendor_string, &entry, copy);
}

FLAC_API FLAC__bool FLAC__metadata_object_vorbiscomment_resize_comments(FLAC__StreamMetadata *object, unsigned new_num_comments)
{
	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_VORBIS_COMMENT);

	if(0 == object->data.vorbis_comment.comments) {
		FLAC__ASSERT(object->data.vorbis_comment.num_comments == 0);
		if(0 == new_num_comments)
			return true;
		else if(0 == (object->data.vorbis_comment.comments = vorbiscomment_entry_array_new_(new_num_comments)))
			return false;
	}
	else {
		const size_t old_size = object->data.vorbis_comment.num_comments * sizeof(FLAC__StreamMetadata_VorbisComment_Entry);
		const size_t new_size = new_num_comments * sizeof(FLAC__StreamMetadata_VorbisComment_Entry);

		/* overflow check */
		if((size_t)new_num_comments > SIZE_MAX / sizeof(FLAC__StreamMetadata_VorbisComment_Entry))
			return false;

		FLAC__ASSERT(object->data.vorbis_comment.num_comments > 0);

		/* if shrinking, free the truncated entries */
		if(new_num_comments < object->data.vorbis_comment.num_comments) {
			unsigned i;
			for(i = new_num_comments; i < object->data.vorbis_comment.num_comments; i++)
				if(0 != object->data.vorbis_comment.comments[i].entry)
					free(object->data.vorbis_comment.comments[i].entry);
		}

		if(new_size == 0) {
			free(object->data.vorbis_comment.comments);
			object->data.vorbis_comment.comments = 0;
		}
		else if(0 == (object->data.vorbis_comment.comments = (FLAC__StreamMetadata_VorbisComment_Entry*)realloc(object->data.vorbis_comment.comments, new_size)))
			return false;

		/* if growing, zero all the length/pointers of new elements */
		if(new_size > old_size)
			memset(object->data.vorbis_comment.comments + object->data.vorbis_comment.num_comments, 0, new_size - old_size);
	}

	object->data.vorbis_comment.num_comments = new_num_comments;

	vorbiscomment_calculate_length_(object);
	return true;
}

FLAC_API FLAC__bool FLAC__metadata_object_vorbiscomment_set_comment(FLAC__StreamMetadata *object, unsigned comment_num, FLAC__StreamMetadata_VorbisComment_Entry entry, FLAC__bool copy)
{
	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(comment_num < object->data.vorbis_comment.num_comments);

	if(!FLAC__format_vorbiscomment_entry_is_legal(entry.entry, entry.length))
		return false;
	return vorbiscomment_set_entry_(object, &object->data.vorbis_comment.comments[comment_num], &entry, copy);
}

FLAC_API FLAC__bool FLAC__metadata_object_vorbiscomment_insert_comment(FLAC__StreamMetadata *object, unsigned comment_num, FLAC__StreamMetadata_VorbisComment_Entry entry, FLAC__bool copy)
{
	FLAC__StreamMetadata_VorbisComment *vc;

	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_VORBIS_COMMENT);
	FLAC__ASSERT(comment_num <= object->data.vorbis_comment.num_comments);

	if(!FLAC__format_vorbiscomment_entry_is_legal(entry.entry, entry.length))
		return false;

	vc = &object->data.vorbis_comment;

	if(!FLAC__metadata_object_vorbiscomment_resize_comments(object, vc->num_comments+1))
		return false;

	/* move all comments >= comment_num forward one space */
	memmove(&vc->comments[comment_num+1], &vc->comments[comment_num], sizeof(FLAC__StreamMetadata_VorbisComment_Entry)*(vc->num_comments-1-comment_num));
	vc->comments[comment_num].length = 0;
	vc->comments[comment_num].entry = 0;

	return FLAC__metadata_object_vorbiscomment_set_comment(object, comment_num, entry, copy);
}

FLAC_API FLAC__bool FLAC__metadata_object_vorbiscomment_append_comment(FLAC__StreamMetadata *object, FLAC__StreamMetadata_VorbisComment_Entry entry, FLAC__bool copy)
{
	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_VORBIS_COMMENT);
	return FLAC__metadata_object_vorbiscomment_insert_comment(object, object->data.vorbis_comment.num_comments, entry, copy);
}

FLAC_API FLAC__bool FLAC__metadata_object_vorbiscomment_replace_comment(FLAC__StreamMetadata *object, FLAC__StreamMetadata_VorbisComment_Entry entry, FLAC__bool all, FLAC__bool copy)
{
	FLAC__ASSERT(0 != entry.entry && entry.length > 0);

	if(!FLAC__format_vorbiscomment_entry_is_legal(entry.entry, entry.length))
		return false;

	{
		int i;
		size_t field_name_length;
		const FLAC__byte *eq = (FLAC__byte*)memchr(entry.entry, '=', entry.length);

		FLAC__ASSERT(0 != eq);

		if(0 == eq)
			return false; /* double protection */

		field_name_length = eq-entry.entry;

		if((i = vorbiscomment_find_entry_from_(object, 0, (const char *)entry.entry, field_name_length)) >= 0) {
			unsigned index = (unsigned)i;
			if(!FLAC__metadata_object_vorbiscomment_set_comment(object, index, entry, copy))
				return false;
			if(all && (index+1 < object->data.vorbis_comment.num_comments)) {
				for(i = vorbiscomment_find_entry_from_(object, index+1, (const char *)entry.entry, field_name_length); i >= 0; ) {
					if(!FLAC__metadata_object_vorbiscomment_delete_comment(object, (unsigned)i))
						return false;
					if((unsigned)i < object->data.vorbis_comment.num_comments)
						i = vorbiscomment_find_entry_from_(object, (unsigned)i, (const char *)entry.entry, field_name_length);
					else
						i = -1;
				}
			}
			return true;
		}
		else
			return FLAC__metadata_object_vorbiscomment_append_comment(object, entry, copy);
	}
}

FLAC_API FLAC__bool FLAC__metadata_object_vorbiscomment_delete_comment(FLAC__StreamMetadata *object, unsigned comment_num)
{
	FLAC__StreamMetadata_VorbisComment *vc;

	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_VORBIS_COMMENT);
	FLAC__ASSERT(comment_num < object->data.vorbis_comment.num_comments);

	vc = &object->data.vorbis_comment;

	/* free the comment at comment_num */
	if(0 != vc->comments[comment_num].entry)
		free(vc->comments[comment_num].entry);

	/* move all comments > comment_num backward one space */
	memmove(&vc->comments[comment_num], &vc->comments[comment_num+1], sizeof(FLAC__StreamMetadata_VorbisComment_Entry)*(vc->num_comments-comment_num-1));
	vc->comments[vc->num_comments-1].length = 0;
	vc->comments[vc->num_comments-1].entry = 0;

	return FLAC__metadata_object_vorbiscomment_resize_comments(object, vc->num_comments-1);
}

FLAC_API FLAC__bool FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(FLAC__StreamMetadata_VorbisComment_Entry *entry, const char *field_name, const char *field_value)
{
	FLAC__ASSERT(0 != entry);
	FLAC__ASSERT(0 != field_name);
	FLAC__ASSERT(0 != field_value);

	if(!FLAC__format_vorbiscomment_entry_name_is_legal(field_name))
		return false;
	if(!FLAC__format_vorbiscomment_entry_value_is_legal((const FLAC__byte *)field_value, (unsigned)(-1)))
		return false;

	{
		const size_t nn = strlen(field_name);
		const size_t nv = strlen(field_value);
		entry->length = nn + 1 /*=*/ + nv;
		if(0 == (entry->entry = (FLAC__byte*)safe_malloc_add_4op_(nn, /*+*/1, /*+*/nv, /*+*/1)))
			return false;
		memcpy(entry->entry, field_name, nn);
		entry->entry[nn] = '=';
		memcpy(entry->entry+nn+1, field_value, nv);
		entry->entry[entry->length] = '\0';
	}
	
	return true;
}

FLAC_API FLAC__bool FLAC__metadata_object_vorbiscomment_entry_to_name_value_pair(const FLAC__StreamMetadata_VorbisComment_Entry entry, char **field_name, char **field_value)
{
	FLAC__ASSERT(0 != entry.entry && entry.length > 0);
	FLAC__ASSERT(0 != field_name);
	FLAC__ASSERT(0 != field_value);

	if(!FLAC__format_vorbiscomment_entry_is_legal(entry.entry, entry.length))
		return false;

	{
		const FLAC__byte *eq = (FLAC__byte*)memchr(entry.entry, '=', entry.length);
		const size_t nn = eq-entry.entry;
		const size_t nv = entry.length-nn-1; /* -1 for the '=' */
		FLAC__ASSERT(0 != eq);
		if(0 == eq)
			return false; /* double protection */
		if(0 == (*field_name = (char*)safe_malloc_add_2op_(nn, /*+*/1)))
			return false;
		if(0 == (*field_value = (char*)safe_malloc_add_2op_(nv, /*+*/1))) {
			free(*field_name);
			return false;
		}
		memcpy(*field_name, entry.entry, nn);
		memcpy(*field_value, entry.entry+nn+1, nv);
		(*field_name)[nn] = '\0';
		(*field_value)[nv] = '\0';
	}

	return true;
}

FLAC_API FLAC__bool FLAC__metadata_object_vorbiscomment_entry_matches(const FLAC__StreamMetadata_VorbisComment_Entry entry, const char *field_name, unsigned field_name_length)
{
	FLAC__ASSERT(0 != entry.entry && entry.length > 0);
	{
		const FLAC__byte *eq = (FLAC__byte*)memchr(entry.entry, '=', entry.length);
#if defined _MSC_VER || defined __BORLANDC__ || defined __MINGW32__ || defined __EMX__
#define FLAC__STRNCASECMP strnicmp
#else
#define FLAC__STRNCASECMP strncasecmp
#endif
		return (0 != eq && (unsigned)(eq-entry.entry) == field_name_length && 0 == FLAC__STRNCASECMP(field_name, (const char *)entry.entry, field_name_length));
#undef FLAC__STRNCASECMP
	}
}

FLAC_API int FLAC__metadata_object_vorbiscomment_find_entry_from(const FLAC__StreamMetadata *object, unsigned offset, const char *field_name)
{
	FLAC__ASSERT(0 != field_name);

	return vorbiscomment_find_entry_from_(object, offset, field_name, strlen(field_name));
}

FLAC_API int FLAC__metadata_object_vorbiscomment_remove_entry_matching(FLAC__StreamMetadata *object, const char *field_name)
{
	const unsigned field_name_length = strlen(field_name);
	unsigned i;

	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_VORBIS_COMMENT);

	for(i = 0; i < object->data.vorbis_comment.num_comments; i++) {
		if(FLAC__metadata_object_vorbiscomment_entry_matches(object->data.vorbis_comment.comments[i], field_name, field_name_length)) {
			if(!FLAC__metadata_object_vorbiscomment_delete_comment(object, i))
				return -1;
			else
				return 1;
		}
	}

	return 0;
}

FLAC_API int FLAC__metadata_object_vorbiscomment_remove_entries_matching(FLAC__StreamMetadata *object, const char *field_name)
{
	FLAC__bool ok = true;
	unsigned matching = 0;
	const unsigned field_name_length = strlen(field_name);
	int i;

	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_VORBIS_COMMENT);

	/* must delete from end to start otherwise it will interfere with our iteration */
	for(i = (int)object->data.vorbis_comment.num_comments - 1; ok && i >= 0; i--) {
		if(FLAC__metadata_object_vorbiscomment_entry_matches(object->data.vorbis_comment.comments[i], field_name, field_name_length)) {
			matching++;
			ok &= FLAC__metadata_object_vorbiscomment_delete_comment(object, (unsigned)i);
		}
	}

	return ok? (int)matching : -1;
}

FLAC_API FLAC__StreamMetadata_CueSheet_Track *FLAC__metadata_object_cuesheet_track_new(void)
{
	return (FLAC__StreamMetadata_CueSheet_Track*)calloc(1, sizeof(FLAC__StreamMetadata_CueSheet_Track));
}

FLAC_API FLAC__StreamMetadata_CueSheet_Track *FLAC__metadata_object_cuesheet_track_clone(const FLAC__StreamMetadata_CueSheet_Track *object)
{
	FLAC__StreamMetadata_CueSheet_Track *to;

	FLAC__ASSERT(0 != object);

	if(0 != (to = FLAC__metadata_object_cuesheet_track_new())) {
		if(!copy_track_(to, object)) {
			FLAC__metadata_object_cuesheet_track_delete(to);
			return 0;
		}
	}

	return to;
}

void FLAC__metadata_object_cuesheet_track_delete_data(FLAC__StreamMetadata_CueSheet_Track *object)
{
	FLAC__ASSERT(0 != object);

	if(0 != object->indices) {
		FLAC__ASSERT(object->num_indices > 0);
		free(object->indices);
	}
}

FLAC_API void FLAC__metadata_object_cuesheet_track_delete(FLAC__StreamMetadata_CueSheet_Track *object)
{
	FLAC__metadata_object_cuesheet_track_delete_data(object);
	free(object);
}

FLAC_API FLAC__bool FLAC__metadata_object_cuesheet_track_resize_indices(FLAC__StreamMetadata *object, unsigned track_num, unsigned new_num_indices)
{
	FLAC__StreamMetadata_CueSheet_Track *track;
	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_CUESHEET);
	FLAC__ASSERT(track_num < object->data.cue_sheet.num_tracks);

	track = &object->data.cue_sheet.tracks[track_num];

	if(0 == track->indices) {
		FLAC__ASSERT(track->num_indices == 0);
		if(0 == new_num_indices)
			return true;
		else if(0 == (track->indices = cuesheet_track_index_array_new_(new_num_indices)))
			return false;
	}
	else {
		const size_t old_size = track->num_indices * sizeof(FLAC__StreamMetadata_CueSheet_Index);
		const size_t new_size = new_num_indices * sizeof(FLAC__StreamMetadata_CueSheet_Index);

		/* overflow check */
		if((size_t)new_num_indices > SIZE_MAX / sizeof(FLAC__StreamMetadata_CueSheet_Index))
			return false;

		FLAC__ASSERT(track->num_indices > 0);

		if(new_size == 0) {
			free(track->indices);
			track->indices = 0;
		}
		else if(0 == (track->indices = (FLAC__StreamMetadata_CueSheet_Index*)realloc(track->indices, new_size)))
			return false;

		/* if growing, zero all the lengths/pointers of new elements */
		if(new_size > old_size)
			memset(track->indices + track->num_indices, 0, new_size - old_size);
	}

	track->num_indices = new_num_indices;

	cuesheet_calculate_length_(object);
	return true;
}

FLAC_API FLAC__bool FLAC__metadata_object_cuesheet_track_insert_index(FLAC__StreamMetadata *object, unsigned track_num, unsigned index_num, FLAC__StreamMetadata_CueSheet_Index index)
{
	FLAC__StreamMetadata_CueSheet_Track *track;

	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_CUESHEET);
	FLAC__ASSERT(track_num < object->data.cue_sheet.num_tracks);
	FLAC__ASSERT(index_num <= object->data.cue_sheet.tracks[track_num].num_indices);

	track = &object->data.cue_sheet.tracks[track_num];

	if(!FLAC__metadata_object_cuesheet_track_resize_indices(object, track_num, track->num_indices+1))
		return false;

	/* move all indices >= index_num forward one space */
	memmove(&track->indices[index_num+1], &track->indices[index_num], sizeof(FLAC__StreamMetadata_CueSheet_Index)*(track->num_indices-1-index_num));

	track->indices[index_num] = index;
	cuesheet_calculate_length_(object);
	return true;
}

FLAC_API FLAC__bool FLAC__metadata_object_cuesheet_track_insert_blank_index(FLAC__StreamMetadata *object, unsigned track_num, unsigned index_num)
{
	FLAC__StreamMetadata_CueSheet_Index index;
	memset(&index, 0, sizeof(index));
	return FLAC__metadata_object_cuesheet_track_insert_index(object, track_num, index_num, index);
}

FLAC_API FLAC__bool FLAC__metadata_object_cuesheet_track_delete_index(FLAC__StreamMetadata *object, unsigned track_num, unsigned index_num)
{
	FLAC__StreamMetadata_CueSheet_Track *track;

	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_CUESHEET);
	FLAC__ASSERT(track_num < object->data.cue_sheet.num_tracks);
	FLAC__ASSERT(index_num < object->data.cue_sheet.tracks[track_num].num_indices);

	track = &object->data.cue_sheet.tracks[track_num];

	/* move all indices > index_num backward one space */
	memmove(&track->indices[index_num], &track->indices[index_num+1], sizeof(FLAC__StreamMetadata_CueSheet_Index)*(track->num_indices-index_num-1));

	FLAC__metadata_object_cuesheet_track_resize_indices(object, track_num, track->num_indices-1);
	cuesheet_calculate_length_(object);
	return true;
}

FLAC_API FLAC__bool FLAC__metadata_object_cuesheet_resize_tracks(FLAC__StreamMetadata *object, unsigned new_num_tracks)
{
	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_CUESHEET);

	if(0 == object->data.cue_sheet.tracks) {
		FLAC__ASSERT(object->data.cue_sheet.num_tracks == 0);
		if(0 == new_num_tracks)
			return true;
		else if(0 == (object->data.cue_sheet.tracks = cuesheet_track_array_new_(new_num_tracks)))
			return false;
	}
	else {
		const size_t old_size = object->data.cue_sheet.num_tracks * sizeof(FLAC__StreamMetadata_CueSheet_Track);
		const size_t new_size = new_num_tracks * sizeof(FLAC__StreamMetadata_CueSheet_Track);

		/* overflow check */
		if((size_t)new_num_tracks > SIZE_MAX / sizeof(FLAC__StreamMetadata_CueSheet_Track))
			return false;

		FLAC__ASSERT(object->data.cue_sheet.num_tracks > 0);

		/* if shrinking, free the truncated entries */
		if(new_num_tracks < object->data.cue_sheet.num_tracks) {
			unsigned i;
			for(i = new_num_tracks; i < object->data.cue_sheet.num_tracks; i++)
				if(0 != object->data.cue_sheet.tracks[i].indices)
					free(object->data.cue_sheet.tracks[i].indices);
		}

		if(new_size == 0) {
			free(object->data.cue_sheet.tracks);
			object->data.cue_sheet.tracks = 0;
		}
		else if(0 == (object->data.cue_sheet.tracks = (FLAC__StreamMetadata_CueSheet_Track*)realloc(object->data.cue_sheet.tracks, new_size)))
			return false;

		/* if growing, zero all the lengths/pointers of new elements */
		if(new_size > old_size)
			memset(object->data.cue_sheet.tracks + object->data.cue_sheet.num_tracks, 0, new_size - old_size);
	}

	object->data.cue_sheet.num_tracks = new_num_tracks;

	cuesheet_calculate_length_(object);
	return true;
}

FLAC_API FLAC__bool FLAC__metadata_object_cuesheet_set_track(FLAC__StreamMetadata *object, unsigned track_num, FLAC__StreamMetadata_CueSheet_Track *track, FLAC__bool copy)
{
	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(track_num < object->data.cue_sheet.num_tracks);

	return cuesheet_set_track_(object, object->data.cue_sheet.tracks + track_num, track, copy);
}

FLAC_API FLAC__bool FLAC__metadata_object_cuesheet_insert_track(FLAC__StreamMetadata *object, unsigned track_num, FLAC__StreamMetadata_CueSheet_Track *track, FLAC__bool copy)
{
	FLAC__StreamMetadata_CueSheet *cs;

	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_CUESHEET);
	FLAC__ASSERT(track_num <= object->data.cue_sheet.num_tracks);

	cs = &object->data.cue_sheet;

	if(!FLAC__metadata_object_cuesheet_resize_tracks(object, cs->num_tracks+1))
		return false;

	/* move all tracks >= track_num forward one space */
	memmove(&cs->tracks[track_num+1], &cs->tracks[track_num], sizeof(FLAC__StreamMetadata_CueSheet_Track)*(cs->num_tracks-1-track_num));
	cs->tracks[track_num].num_indices = 0;
	cs->tracks[track_num].indices = 0;

	return FLAC__metadata_object_cuesheet_set_track(object, track_num, track, copy);
}

FLAC_API FLAC__bool FLAC__metadata_object_cuesheet_insert_blank_track(FLAC__StreamMetadata *object, unsigned track_num)
{
	FLAC__StreamMetadata_CueSheet_Track track;
	memset(&track, 0, sizeof(track));
	return FLAC__metadata_object_cuesheet_insert_track(object, track_num, &track, /*copy=*/false);
}

FLAC_API FLAC__bool FLAC__metadata_object_cuesheet_delete_track(FLAC__StreamMetadata *object, unsigned track_num)
{
	FLAC__StreamMetadata_CueSheet *cs;

	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_CUESHEET);
	FLAC__ASSERT(track_num < object->data.cue_sheet.num_tracks);

	cs = &object->data.cue_sheet;

	/* free the track at track_num */
	if(0 != cs->tracks[track_num].indices)
		free(cs->tracks[track_num].indices);

	/* move all tracks > track_num backward one space */
	memmove(&cs->tracks[track_num], &cs->tracks[track_num+1], sizeof(FLAC__StreamMetadata_CueSheet_Track)*(cs->num_tracks-track_num-1));
	cs->tracks[cs->num_tracks-1].num_indices = 0;
	cs->tracks[cs->num_tracks-1].indices = 0;

	return FLAC__metadata_object_cuesheet_resize_tracks(object, cs->num_tracks-1);
}

FLAC_API FLAC__bool FLAC__metadata_object_cuesheet_is_legal(const FLAC__StreamMetadata *object, FLAC__bool check_cd_da_subset, const char **violation)
{
	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_CUESHEET);

	return FLAC__format_cuesheet_is_legal(&object->data.cue_sheet, check_cd_da_subset, violation);
}

static FLAC__uint64 get_index_01_offset_(const FLAC__StreamMetadata_CueSheet *cs, unsigned track)
{
	if (track >= (cs->num_tracks-1) || cs->tracks[track].num_indices < 1)
		return 0;
	else if (cs->tracks[track].indices[0].number == 1)
		return cs->tracks[track].indices[0].offset + cs->tracks[track].offset + cs->lead_in;
	else if (cs->tracks[track].num_indices < 2)
		return 0;
	else if (cs->tracks[track].indices[1].number == 1)
		return cs->tracks[track].indices[1].offset + cs->tracks[track].offset + cs->lead_in;
	else
		return 0;
}

static FLAC__uint32 cddb_add_digits_(FLAC__uint32 x)
{
	FLAC__uint32 n = 0;
	while (x) {
		n += (x%10);
		x /= 10;
	}
	return n;
}

/*@@@@add to tests*/
FLAC_API FLAC__uint32 FLAC__metadata_object_cuesheet_calculate_cddb_id(const FLAC__StreamMetadata *object)
{
	const FLAC__StreamMetadata_CueSheet *cs;

	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_CUESHEET);

	cs = &object->data.cue_sheet;

	if (cs->num_tracks < 2) /* need at least one real track and the lead-out track */
		return 0;

	{
		FLAC__uint32 i, length, sum = 0;
		for (i = 0; i < (cs->num_tracks-1); i++) /* -1 to avoid counting the lead-out */
			sum += cddb_add_digits_((FLAC__uint32)(get_index_01_offset_(cs, i) / 44100));
		length = (FLAC__uint32)((cs->tracks[cs->num_tracks-1].offset+cs->lead_in) / 44100) - (FLAC__uint32)(get_index_01_offset_(cs, 0) / 44100);

		return (sum % 0xFF) << 24 | length << 8 | (FLAC__uint32)(cs->num_tracks-1);
	}
}

FLAC_API FLAC__bool FLAC__metadata_object_picture_set_mime_type(FLAC__StreamMetadata *object, char *mime_type, FLAC__bool copy)
{
	char *old;
	size_t old_length, new_length;

	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_PICTURE);
	FLAC__ASSERT(0 != mime_type);

	old = object->data.picture.mime_type;
	old_length = old? strlen(old) : 0;
	new_length = strlen(mime_type);

	/* do the copy first so that if we fail we leave the object untouched */
	if(copy) {
		if(new_length >= SIZE_MAX) /* overflow check */
			return false;
		if(!copy_bytes_((FLAC__byte**)(&object->data.picture.mime_type), (FLAC__byte*)mime_type, new_length+1))
			return false;
	}
	else {
		object->data.picture.mime_type = mime_type;
	}

	if(0 != old)
		free(old);

	object->length -= old_length;
	object->length += new_length;
	return true;
}

FLAC_API FLAC__bool FLAC__metadata_object_picture_set_description(FLAC__StreamMetadata *object, FLAC__byte *description, FLAC__bool copy)
{
	FLAC__byte *old;
	size_t old_length, new_length;

	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_PICTURE);
	FLAC__ASSERT(0 != description);

	old = object->data.picture.description;
	old_length = old? strlen((const char *)old) : 0;
	new_length = strlen((const char *)description);

	/* do the copy first so that if we fail we leave the object untouched */
	if(copy) {
		if(new_length >= SIZE_MAX) /* overflow check */
			return false;
		if(!copy_bytes_(&object->data.picture.description, description, new_length+1))
			return false;
	}
	else {
		object->data.picture.description = description;
	}

	if(0 != old)
		free(old);

	object->length -= old_length;
	object->length += new_length;
	return true;
}

FLAC_API FLAC__bool FLAC__metadata_object_picture_set_data(FLAC__StreamMetadata *object, FLAC__byte *data, FLAC__uint32 length, FLAC__bool copy)
{
	FLAC__byte *old;

	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_PICTURE);
	FLAC__ASSERT((0 != data && length > 0) || (0 == data && length == 0 && copy == false));

	old = object->data.picture.data;

	/* do the copy first so that if we fail we leave the object untouched */
	if(copy) {
		if(!copy_bytes_(&object->data.picture.data, data, length))
			return false;
	}
	else {
		object->data.picture.data = data;
	}

	if(0 != old)
		free(old);

	object->length -= object->data.picture.data_length;
	object->data.picture.data_length = length;
	object->length += length;
	return true;
}

FLAC_API FLAC__bool FLAC__metadata_object_picture_is_legal(const FLAC__StreamMetadata *object, const char **violation)
{
	FLAC__ASSERT(0 != object);
	FLAC__ASSERT(object->type == FLAC__METADATA_TYPE_PICTURE);

	return FLAC__format_picture_is_legal(&object->data.picture, violation);
}
//++-- metadata_object.c 

#if 1 //++ stream_decoder.c

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#if defined _MSC_VER || defined __MINGW32__
#include <io.h> /* for _setmode() */
#include <fcntl.h> /* for _O_BINARY */
#endif
#if defined __CYGWIN__ || defined __EMX__
#include <io.h> /* for setmode(), O_BINARY */
#include <fcntl.h> /* for _O_BINARY */
#endif
#include <stdio.h>
#include <stdlib.h> /* for malloc() */
#include <string.h> /* for memset/memcpy() */
#include <sys/stat.h> /* for stat() */
#include <sys/types.h> /* for off_t */
#if defined _MSC_VER || defined __BORLANDC__ || defined __MINGW32__
#if _MSC_VER <= 1600 || defined __BORLANDC__ /* @@@ [2G limit] */
#define fseeko fseek
#define ftello ftell
#endif
#endif

/* adjust for compilers that can't understand using LLU suffix for uint64_t literals */
#ifdef _MSC_VER
#define FLAC__U64L(x) x
#else
#define FLAC__U64L(x) x##LLU
#endif


/* technically this should be in an "export.c" but this is convenient enough */
FLAC_API int FLAC_API_SUPPORTS_OGG_FLAC =
#if FLAC__HAS_OGG
	1
#else
	0
#endif
;


/***********************************************************************
 *
 * Private static data
 *
 ***********************************************************************/

static FLAC__byte ID3V2_TAG_[3] = { 'I', 'D', '3' };

/***********************************************************************
 *
 * Private class method prototypes
 *
 ***********************************************************************/

static void set_defaults_decode(FLAC__StreamDecoder *decoder);
static FILE *get_binary_stdin_(void);
static FLAC__bool allocate_output_(FLAC__StreamDecoder *decoder, unsigned size, unsigned channels);
static FLAC__bool has_id_filtered_(FLAC__StreamDecoder *decoder, FLAC__byte *id);
static FLAC__bool find_metadata_(FLAC__StreamDecoder *decoder);
static FLAC__bool read_metadata_(FLAC__StreamDecoder *decoder);
static FLAC__bool read_metadata_streaminfo_(FLAC__StreamDecoder *decoder, FLAC__bool is_last, unsigned length);
static FLAC__bool read_metadata_seektable_(FLAC__StreamDecoder *decoder, FLAC__bool is_last, unsigned length);
static FLAC__bool read_metadata_vorbiscomment_(FLAC__StreamDecoder *decoder, FLAC__StreamMetadata_VorbisComment *obj);
static FLAC__bool read_metadata_cuesheet_(FLAC__StreamDecoder *decoder, FLAC__StreamMetadata_CueSheet *obj);
static FLAC__bool read_metadata_picture_(FLAC__StreamDecoder *decoder, FLAC__StreamMetadata_Picture *obj);
static FLAC__bool skip_id3v2_tag_(FLAC__StreamDecoder *decoder);
static FLAC__bool frame_sync_(FLAC__StreamDecoder *decoder);
static FLAC__bool read_frame_(FLAC__StreamDecoder *decoder, FLAC__bool *got_a_frame, FLAC__bool do_full_decode);
static FLAC__bool read_frame_header_(FLAC__StreamDecoder *decoder);
static FLAC__bool read_subframe_(FLAC__StreamDecoder *decoder, unsigned channel, unsigned bps, FLAC__bool do_full_decode);
static FLAC__bool read_subframe_constant_(FLAC__StreamDecoder *decoder, unsigned channel, unsigned bps, FLAC__bool do_full_decode);
static FLAC__bool read_subframe_fixed_(FLAC__StreamDecoder *decoder, unsigned channel, unsigned bps, const unsigned order, FLAC__bool do_full_decode);
static FLAC__bool read_subframe_lpc_(FLAC__StreamDecoder *decoder, unsigned channel, unsigned bps, const unsigned order, FLAC__bool do_full_decode);
static FLAC__bool read_subframe_verbatim_(FLAC__StreamDecoder *decoder, unsigned channel, unsigned bps, FLAC__bool do_full_decode);
static FLAC__bool read_residual_partitioned_rice_(FLAC__StreamDecoder *decoder, unsigned predictor_order, unsigned partition_order, FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents, FLAC__int32 *residual, FLAC__bool is_extended);
static FLAC__bool read_zero_padding_(FLAC__StreamDecoder *decoder);
static FLAC__bool read_callback_(FLAC__byte buffer[], size_t *bytes, void *client_data);
#if FLAC__HAS_OGG
static FLAC__StreamDecoderReadStatus read_callback_ogg_aspect_(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes);
static FLAC__OggDecoderAspectReadStatus read_callback_proxy_(const void *void_decoder, FLAC__byte buffer[], size_t *bytes, void *client_data);
#endif
static FLAC__StreamDecoderWriteStatus write_audio_frame_to_client_(FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[]);
static void send_error_to_client_(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status);
static FLAC__bool seek_to_absolute_sample_(FLAC__StreamDecoder *decoder, FLAC__uint64 stream_length, FLAC__uint64 target_sample);
#if FLAC__HAS_OGG
static FLAC__bool seek_to_absolute_sample_ogg_(FLAC__StreamDecoder *decoder, FLAC__uint64 stream_length, FLAC__uint64 target_sample);
#endif
static FLAC__StreamDecoderReadStatus file_read_callback_decode(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data);
static FLAC__StreamDecoderSeekStatus file_seek_callback_decode(const FLAC__StreamDecoder *decoder, FLAC__uint64 absolute_byte_offset, void *client_data);
static FLAC__StreamDecoderTellStatus file_tell_callback_decode(const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *client_data);
static FLAC__StreamDecoderLengthStatus file_length_callback_(const FLAC__StreamDecoder *decoder, FLAC__uint64 *stream_length, void *client_data);
static FLAC__bool file_eof_callback_(const FLAC__StreamDecoder *decoder, void *client_data);

/***********************************************************************
 *
 * Private class data
 *
 ***********************************************************************/

typedef struct FLAC__StreamDecoderPrivate {
#if FLAC__HAS_OGG
	FLAC__bool is_ogg;
#endif
	FLAC__StreamDecoderReadCallback read_callback;
	FLAC__StreamDecoderSeekCallback seek_callback;
	FLAC__StreamDecoderTellCallback tell_callback;
	FLAC__StreamDecoderLengthCallback length_callback;
	FLAC__StreamDecoderEofCallback eof_callback;
	FLAC__StreamDecoderWriteCallback write_callback;
	FLAC__StreamDecoderMetadataCallback metadata_callback;
	FLAC__StreamDecoderErrorCallback error_callback;
	/* generic 32-bit datapath: */
	void (*local_lpc_restore_signal)(const FLAC__int32 residual[], unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 data[]);
	/* generic 64-bit datapath: */
	void (*local_lpc_restore_signal_64bit)(const FLAC__int32 residual[], unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 data[]);
	/* for use when the signal is <= 16 bits-per-sample, or <= 15 bits-per-sample on a side channel (which requires 1 extra bit): */
	void (*local_lpc_restore_signal_16bit)(const FLAC__int32 residual[], unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 data[]);
	/* for use when the signal is <= 16 bits-per-sample, or <= 15 bits-per-sample on a side channel (which requires 1 extra bit), AND order <= 8: */
	void (*local_lpc_restore_signal_16bit_order8)(const FLAC__int32 residual[], unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 data[]);
	FLAC__bool (*local_bitreader_read_rice_signed_block)(FLAC__BitReader *br, int vals[], unsigned nvals, unsigned parameter);
	void *client_data;
	FILE *file; /* only used if FLAC__stream_decoder_init_file()/FLAC__stream_decoder_init_file() called, else NULL */
	FLAC__BitReader *input;
	FLAC__int32 *output[FLAC__MAX_CHANNELS];
	FLAC__int32 *residual[FLAC__MAX_CHANNELS]; /* WATCHOUT: these are the aligned pointers; the real pointers that should be free()'d are residual_unaligned[] below */
	FLAC__EntropyCodingMethod_PartitionedRiceContents partitioned_rice_contents[FLAC__MAX_CHANNELS];
	unsigned output_capacity, output_channels;
	FLAC__uint32 fixed_block_size, next_fixed_block_size;
	FLAC__uint64 samples_decoded;
	FLAC__bool has_stream_info, has_seek_table;
	FLAC__StreamMetadata stream_info;
	FLAC__StreamMetadata seek_table;
	FLAC__bool metadata_filter[128]; /* MAGIC number 128 == total number of metadata block types == 1 << 7 */
	FLAC__byte *metadata_filter_ids;
	size_t metadata_filter_ids_count, metadata_filter_ids_capacity; /* units for both are IDs, not bytes */
	FLAC__Frame frame;
	FLAC__bool cached; /* true if there is a byte in lookahead */
	FLAC__CPUInfo cpuinfo;
	FLAC__byte header_warmup[2]; /* contains the sync code and reserved bits */
	FLAC__byte lookahead; /* temp storage when we need to look ahead one byte in the stream */
	/* unaligned (original) pointers to allocated data */
	FLAC__int32 *residual_unaligned[FLAC__MAX_CHANNELS];
	FLAC__bool do_md5_checking; /* initially gets protected_->md5_checking but is turned off after a seek or if the metadata has a zero MD5 */
	FLAC__bool internal_reset_hack; /* used only during init() so we can call reset to set up the decoder without rewinding the input */
	FLAC__bool is_seeking;
	FLAC__MD5Context md5context;
	FLAC__byte computed_md5sum[16]; /* this is the sum we computed from the decoded data */
	/* (the rest of these are only used for seeking) */
	FLAC__Frame last_frame; /* holds the info of the last frame we seeked to */
	FLAC__uint64 first_frame_offset; /* hint to the seek routine of where in the stream the first audio frame starts */
	FLAC__uint64 target_sample;
	unsigned unparseable_frame_count; /* used to tell whether we're decoding a future version of FLAC or just got a bad sync */
#if FLAC__HAS_OGG
	FLAC__bool got_a_frame; /* hack needed in Ogg FLAC seek routine to check when process_single() actually writes a frame */
#endif
} FLAC__StreamDecoderPrivate;

/***********************************************************************
 *
 * Public static class data
 *
 ***********************************************************************/

FLAC_API const char * const FLAC__StreamDecoderStateString[] = {
	"FLAC__STREAM_DECODER_SEARCH_FOR_METADATA",
	"FLAC__STREAM_DECODER_READ_METADATA",
	"FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC",
	"FLAC__STREAM_DECODER_READ_FRAME",
	"FLAC__STREAM_DECODER_END_OF_STREAM",
	"FLAC__STREAM_DECODER_OGG_ERROR",
	"FLAC__STREAM_DECODER_SEEK_ERROR",
	"FLAC__STREAM_DECODER_ABORTED",
	"FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR",
	"FLAC__STREAM_DECODER_UNINITIALIZED"
};

FLAC_API const char * const FLAC__StreamDecoderInitStatusString[] = {
	"FLAC__STREAM_DECODER_INIT_STATUS_OK",
	"FLAC__STREAM_DECODER_INIT_STATUS_UNSUPPORTED_CONTAINER",
	"FLAC__STREAM_DECODER_INIT_STATUS_INVALID_CALLBACKS",
	"FLAC__STREAM_DECODER_INIT_STATUS_MEMORY_ALLOCATION_ERROR",
	"FLAC__STREAM_DECODER_INIT_STATUS_ERROR_OPENING_FILE",
	"FLAC__STREAM_DECODER_INIT_STATUS_ALREADY_INITIALIZED"
};

FLAC_API const char * const FLAC__StreamDecoderReadStatusString[] = {
	"FLAC__STREAM_DECODER_READ_STATUS_CONTINUE",
	"FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM",
	"FLAC__STREAM_DECODER_READ_STATUS_ABORT"
};

FLAC_API const char * const FLAC__StreamDecoderSeekStatusString[] = {
	"FLAC__STREAM_DECODER_SEEK_STATUS_OK",
	"FLAC__STREAM_DECODER_SEEK_STATUS_ERROR",
	"FLAC__STREAM_DECODER_SEEK_STATUS_UNSUPPORTED"
};

FLAC_API const char * const FLAC__StreamDecoderTellStatusString[] = {
	"FLAC__STREAM_DECODER_TELL_STATUS_OK",
	"FLAC__STREAM_DECODER_TELL_STATUS_ERROR",
	"FLAC__STREAM_DECODER_TELL_STATUS_UNSUPPORTED"
};

FLAC_API const char * const FLAC__StreamDecoderLengthStatusString[] = {
	"FLAC__STREAM_DECODER_LENGTH_STATUS_OK",
	"FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR",
	"FLAC__STREAM_DECODER_LENGTH_STATUS_UNSUPPORTED"
};

FLAC_API const char * const FLAC__StreamDecoderWriteStatusString[] = {
	"FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE",
	"FLAC__STREAM_DECODER_WRITE_STATUS_ABORT"
};

FLAC_API const char * const FLAC__StreamDecoderErrorStatusString[] = {
	"FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC",
	"FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER",
	"FLAC__STREAM_DECODER_ERROR_STATUS_FRAME_CRC_MISMATCH",
	"FLAC__STREAM_DECODER_ERROR_STATUS_UNPARSEABLE_STREAM"
};

/***********************************************************************
 *
 * Class constructor/destructor
 *
 ***********************************************************************/
FLAC_API FLAC__StreamDecoder *FLAC__stream_decoder_new(void)
{
	FLAC__StreamDecoder *decoder;
	unsigned i;

	FLAC__ASSERT(sizeof(int) >= 4); /* we want to die right away if this is not true */

	decoder = (FLAC__StreamDecoder*)calloc(1, sizeof(FLAC__StreamDecoder));
	if(decoder == 0) {
		return 0;
	}

	decoder->protected_ = (FLAC__StreamDecoderProtected*)calloc(1, sizeof(FLAC__StreamDecoderProtected));
	if(decoder->protected_ == 0) {
		free(decoder);
		return 0;
	}

	decoder->private_ = (FLAC__StreamDecoderPrivate*)calloc(1, sizeof(FLAC__StreamDecoderPrivate));
	if(decoder->private_ == 0) {
		free(decoder->protected_);
		free(decoder);
		return 0;
	}

	decoder->private_->input = FLAC__bitreader_new();
	if(decoder->private_->input == 0) {
		free(decoder->private_);
		free(decoder->protected_);
		free(decoder);
		return 0;
	}

	decoder->private_->metadata_filter_ids_capacity = 16;
	if(0 == (decoder->private_->metadata_filter_ids = (FLAC__byte*)malloc((FLAC__STREAM_METADATA_APPLICATION_ID_LEN/8) * decoder->private_->metadata_filter_ids_capacity))) {
		FLAC__bitreader_delete(decoder->private_->input);
		free(decoder->private_);
		free(decoder->protected_);
		free(decoder);
		return 0;
	}

	for(i = 0; i < FLAC__MAX_CHANNELS; i++) {
		decoder->private_->output[i] = 0;
		decoder->private_->residual_unaligned[i] = decoder->private_->residual[i] = 0;
	}

	decoder->private_->output_capacity = 0;
	decoder->private_->output_channels = 0;
	decoder->private_->has_seek_table = false;

	for(i = 0; i < FLAC__MAX_CHANNELS; i++)
		FLAC__format_entropy_coding_method_partitioned_rice_contents_init(&decoder->private_->partitioned_rice_contents[i]);

	decoder->private_->file = 0;

	set_defaults_decode(decoder);

	decoder->protected_->state = FLAC__STREAM_DECODER_UNINITIALIZED;

	return decoder;
}

FLAC_API void FLAC__stream_decoder_delete(FLAC__StreamDecoder *decoder)
{
	unsigned i;

	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != decoder->private_->input);

	(void)FLAC__stream_decoder_finish(decoder);

	if(0 != decoder->private_->metadata_filter_ids)
		free(decoder->private_->metadata_filter_ids);

	FLAC__bitreader_delete(decoder->private_->input);

	for(i = 0; i < FLAC__MAX_CHANNELS; i++)
		FLAC__format_entropy_coding_method_partitioned_rice_contents_clear(&decoder->private_->partitioned_rice_contents[i]);

	free(decoder->private_);
	free(decoder->protected_);
	free(decoder);
}

/***********************************************************************
 *
 * Public class methods
 *
 ***********************************************************************/

static FLAC__StreamDecoderInitStatus init_stream_internal_decode(
	FLAC__StreamDecoder *decoder,
	FLAC__StreamDecoderReadCallback read_callback,
	FLAC__StreamDecoderSeekCallback seek_callback,
	FLAC__StreamDecoderTellCallback tell_callback,
	FLAC__StreamDecoderLengthCallback length_callback,
	FLAC__StreamDecoderEofCallback eof_callback,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data,
	FLAC__bool is_ogg
)
{
	FLAC__ASSERT(0 != decoder);

	if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED)
		return FLAC__STREAM_DECODER_INIT_STATUS_ALREADY_INITIALIZED;

#if !FLAC__HAS_OGG
	if(is_ogg)
		return FLAC__STREAM_DECODER_INIT_STATUS_UNSUPPORTED_CONTAINER;
#endif

	if(
		0 == read_callback ||
		0 == write_callback ||
		0 == error_callback ||
		(seek_callback && (0 == tell_callback || 0 == length_callback || 0 == eof_callback))
	)
		return FLAC__STREAM_DECODER_INIT_STATUS_INVALID_CALLBACKS;

#if FLAC__HAS_OGG
	decoder->private_->is_ogg = is_ogg;
	if(is_ogg && !FLAC__ogg_decoder_aspect_init(&decoder->protected_->ogg_decoder_aspect))
		return decoder->protected_->state = FLAC__STREAM_DECODER_OGG_ERROR;
#endif

	/*
	 * get the CPU info and set the function pointers
	 */
	FLAC__cpu_info(&decoder->private_->cpuinfo);
	/* first default to the non-asm routines */
	decoder->private_->local_lpc_restore_signal = FLAC__lpc_restore_signal;
	decoder->private_->local_lpc_restore_signal_64bit = FLAC__lpc_restore_signal_wide;
	decoder->private_->local_lpc_restore_signal_16bit = FLAC__lpc_restore_signal;
	decoder->private_->local_lpc_restore_signal_16bit_order8 = FLAC__lpc_restore_signal;
	decoder->private_->local_bitreader_read_rice_signed_block = FLAC__bitreader_read_rice_signed_block;
	/* now override with asm where appropriate */
#ifndef FLAC__NO_ASM
	if(decoder->private_->cpuinfo.use_asm) {
#ifdef FLAC__CPU_IA32
		FLAC__ASSERT(decoder->private_->cpuinfo.type == FLAC__CPUINFO_TYPE_IA32);
#ifdef FLAC__HAS_NASM
#if 1 /*@@@@@@ OPT: not clearly faster, needs more testing */
		if(decoder->private_->cpuinfo.data.ia32.bswap)
			decoder->private_->local_bitreader_read_rice_signed_block = FLAC__bitreader_read_rice_signed_block_asm_ia32_bswap;
#endif
		if(decoder->private_->cpuinfo.data.ia32.mmx) {
			decoder->private_->local_lpc_restore_signal = FLAC__lpc_restore_signal_asm_ia32;
			decoder->private_->local_lpc_restore_signal_16bit = FLAC__lpc_restore_signal_asm_ia32_mmx;
			decoder->private_->local_lpc_restore_signal_16bit_order8 = FLAC__lpc_restore_signal_asm_ia32_mmx;
		}
		else {
			decoder->private_->local_lpc_restore_signal = FLAC__lpc_restore_signal_asm_ia32;
			decoder->private_->local_lpc_restore_signal_16bit = FLAC__lpc_restore_signal_asm_ia32;
			decoder->private_->local_lpc_restore_signal_16bit_order8 = FLAC__lpc_restore_signal_asm_ia32;
		}
#endif
#elif defined FLAC__CPU_PPC
		FLAC__ASSERT(decoder->private_->cpuinfo.type == FLAC__CPUINFO_TYPE_PPC);
		if(decoder->private_->cpuinfo.data.ppc.altivec) {
			decoder->private_->local_lpc_restore_signal_16bit = FLAC__lpc_restore_signal_asm_ppc_altivec_16;
			decoder->private_->local_lpc_restore_signal_16bit_order8 = FLAC__lpc_restore_signal_asm_ppc_altivec_16_order8;
		}
#endif
	}
#endif

	/* from here on, errors are fatal */

	if(!FLAC__bitreader_init(decoder->private_->input, decoder->private_->cpuinfo, read_callback_, decoder)) {
		decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
		return FLAC__STREAM_DECODER_INIT_STATUS_MEMORY_ALLOCATION_ERROR;
	}

	decoder->private_->read_callback = read_callback;
	decoder->private_->seek_callback = seek_callback;
	decoder->private_->tell_callback = tell_callback;
	decoder->private_->length_callback = length_callback;
	decoder->private_->eof_callback = eof_callback;
	decoder->private_->write_callback = write_callback;
	decoder->private_->metadata_callback = metadata_callback;
	decoder->private_->error_callback = error_callback;
	decoder->private_->client_data = client_data;
	decoder->private_->fixed_block_size = decoder->private_->next_fixed_block_size = 0;
	decoder->private_->samples_decoded = 0;
	decoder->private_->has_stream_info = false;
	decoder->private_->cached = false;

	decoder->private_->do_md5_checking = decoder->protected_->md5_checking;
	decoder->private_->is_seeking = false;

	decoder->private_->internal_reset_hack = true; /* so the following reset does not try to rewind the input */
	if(!FLAC__stream_decoder_reset(decoder)) {
		/* above call sets the state for us */
		return FLAC__STREAM_DECODER_INIT_STATUS_MEMORY_ALLOCATION_ERROR;
	}

	return FLAC__STREAM_DECODER_INIT_STATUS_OK;
}

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
)
{
	return init_stream_internal_decode(
		decoder,
		read_callback,
		seek_callback,
		tell_callback,
		length_callback,
		eof_callback,
		write_callback,
		metadata_callback,
		error_callback,
		client_data,
		/*is_ogg=*/false
	);
}

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
)
{
	return init_stream_internal_decode(
		decoder,
		read_callback,
		seek_callback,
		tell_callback,
		length_callback,
		eof_callback,
		write_callback,
		metadata_callback,
		error_callback,
		client_data,
		/*is_ogg=*/true
	);
}

static FLAC__StreamDecoderInitStatus init_FILE_internal_decode(
	FLAC__StreamDecoder *decoder,
	FILE *file,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data,
	FLAC__bool is_ogg
)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != file);

	if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED)
		return (FLAC__StreamDecoderInitStatus)(decoder->protected_->state = (FLAC__StreamDecoderState)FLAC__STREAM_DECODER_INIT_STATUS_ALREADY_INITIALIZED);

	if(0 == write_callback || 0 == error_callback)
		return FLAC__StreamDecoderInitStatus(decoder->protected_->state = (FLAC__StreamDecoderState )FLAC__STREAM_DECODER_INIT_STATUS_INVALID_CALLBACKS);

	/*
	 * To make sure that our file does not go unclosed after an error, we
	 * must assign the FILE pointer before any further error can occur in
	 * this routine.
	 */
	if(file == stdin)
		file = get_binary_stdin_(); /* just to be safe */

	decoder->private_->file = file;

	return init_stream_internal_decode(
		decoder,
		file_read_callback_decode,
		decoder->private_->file == stdin? 0: file_seek_callback_decode,
		decoder->private_->file == stdin? 0: file_tell_callback_decode,
		decoder->private_->file == stdin? 0: file_length_callback_,
		file_eof_callback_,
		write_callback,
		metadata_callback,
		error_callback,
		client_data,
		is_ogg
	);
}

FLAC_API FLAC__StreamDecoderInitStatus FLAC__stream_decoder_init_FILE(
	FLAC__StreamDecoder *decoder,
	FILE *file,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data
)
{
	return init_FILE_internal_decode(decoder, file, write_callback, metadata_callback, error_callback, client_data, /*is_ogg=*/false);
}

FLAC_API FLAC__StreamDecoderInitStatus FLAC__stream_decoder_init_ogg_FILE(
	FLAC__StreamDecoder *decoder,
	FILE *file,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data
)
{
	return init_FILE_internal_decode(decoder, file, write_callback, metadata_callback, error_callback, client_data, /*is_ogg=*/true);
}

static FLAC__StreamDecoderInitStatus init_file_internal_decode(
	FLAC__StreamDecoder *decoder,
	const char *filename,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data,
	FLAC__bool is_ogg
)
{
	FILE *file;

	FLAC__ASSERT(0 != decoder);

	/*
	 * To make sure that our file does not go unclosed after an error, we
	 * have to do the same entrance checks here that are later performed
	 * in FLAC__stream_decoder_init_FILE() before the FILE* is assigned.
	 */
	if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED)
		return FLAC__StreamDecoderInitStatus(decoder->protected_->state = (FLAC__StreamDecoderState)FLAC__STREAM_DECODER_INIT_STATUS_ALREADY_INITIALIZED);

	if(0 == write_callback || 0 == error_callback)
		return FLAC__StreamDecoderInitStatus(decoder->protected_->state = (FLAC__StreamDecoderState)FLAC__STREAM_DECODER_INIT_STATUS_INVALID_CALLBACKS);

	file = filename? fopen(filename, "rb") : stdin;

	if(0 == file)
		return FLAC__STREAM_DECODER_INIT_STATUS_ERROR_OPENING_FILE;

	return init_FILE_internal_decode(decoder, file, write_callback, metadata_callback, error_callback, client_data, is_ogg);
}

FLAC_API FLAC__StreamDecoderInitStatus FLAC__stream_decoder_init_file(
	FLAC__StreamDecoder *decoder,
	const char *filename,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data
)
{
	return init_file_internal_decode(decoder, filename, write_callback, metadata_callback, error_callback, client_data, /*is_ogg=*/false);
}

FLAC_API FLAC__StreamDecoderInitStatus FLAC__stream_decoder_init_ogg_file(
	FLAC__StreamDecoder *decoder,
	const char *filename,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data
)
{
	return init_file_internal_decode(decoder, filename, write_callback, metadata_callback, error_callback, client_data, /*is_ogg=*/true);
}

FLAC_API FLAC__bool FLAC__stream_decoder_finish(FLAC__StreamDecoder *decoder)
{
	FLAC__bool md5_failed = false;
	unsigned i;

	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != decoder->protected_);

	if(decoder->protected_->state == FLAC__STREAM_DECODER_UNINITIALIZED)
		return true;

	/* see the comment in FLAC__seekable_stream_decoder_reset() as to why we
	 * always call FLAC__MD5Final()
	 */
	FLAC__MD5Final(decoder->private_->computed_md5sum, &decoder->private_->md5context);

	if(decoder->private_->has_seek_table && 0 != decoder->private_->seek_table.data.seek_table.points) {
		free(decoder->private_->seek_table.data.seek_table.points);
		decoder->private_->seek_table.data.seek_table.points = 0;
		decoder->private_->has_seek_table = false;
	}
	FLAC__bitreader_free(decoder->private_->input);
	for(i = 0; i < FLAC__MAX_CHANNELS; i++) {
		/* WATCHOUT:
		 * FLAC__lpc_restore_signal_asm_ia32_mmx() requires that the
		 * output arrays have a buffer of up to 3 zeroes in front
		 * (at negative indices) for alignment purposes; we use 4
		 * to keep the data well-aligned.
		 */
		if(0 != decoder->private_->output[i]) {
			free(decoder->private_->output[i]-4);
			decoder->private_->output[i] = 0;
		}
		if(0 != decoder->private_->residual_unaligned[i]) {
			free(decoder->private_->residual_unaligned[i]);
			decoder->private_->residual_unaligned[i] = decoder->private_->residual[i] = 0;
		}
	}
	decoder->private_->output_capacity = 0;
	decoder->private_->output_channels = 0;

#if FLAC__HAS_OGG
	if(decoder->private_->is_ogg)
		FLAC__ogg_decoder_aspect_finish(&decoder->protected_->ogg_decoder_aspect);
#endif

	if(0 != decoder->private_->file) {
		if(decoder->private_->file != stdin)
			fclose(decoder->private_->file);
		decoder->private_->file = 0;
	}

	if(decoder->private_->do_md5_checking) {
		if(memcmp(decoder->private_->stream_info.data.stream_info.md5sum, decoder->private_->computed_md5sum, 16))
			md5_failed = true;
	}
	decoder->private_->is_seeking = false;

	set_defaults_decode(decoder);

	decoder->protected_->state = FLAC__STREAM_DECODER_UNINITIALIZED;

	return !md5_failed;
}

FLAC_API FLAC__bool FLAC__stream_decoder_set_ogg_serial_number(FLAC__StreamDecoder *decoder, long value)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != decoder->protected_);
	if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED)
		return false;
#if FLAC__HAS_OGG
	/* can't check decoder->private_->is_ogg since that's not set until init time */
	FLAC__ogg_decoder_aspect_set_serial_number(&decoder->protected_->ogg_decoder_aspect, value);
	return true;
#else
	(void)value;
	return false;
#endif
}

FLAC_API FLAC__bool FLAC__stream_decoder_set_md5_checking(FLAC__StreamDecoder *decoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);
	if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED)
		return false;
	decoder->protected_->md5_checking = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_respond(FLAC__StreamDecoder *decoder, FLAC__MetadataType type)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != decoder->protected_);
	FLAC__ASSERT((unsigned)type <= FLAC__MAX_METADATA_TYPE_CODE);
	/* double protection */
	if((unsigned)type > FLAC__MAX_METADATA_TYPE_CODE)
		return false;
	if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED)
		return false;
	decoder->private_->metadata_filter[type] = true;
	if(type == FLAC__METADATA_TYPE_APPLICATION)
		decoder->private_->metadata_filter_ids_count = 0;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_respond_application(FLAC__StreamDecoder *decoder, const FLAC__byte id[4])
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != decoder->protected_);
	FLAC__ASSERT(0 != id);
	if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED)
		return false;

	if(decoder->private_->metadata_filter[FLAC__METADATA_TYPE_APPLICATION])
		return true;

	FLAC__ASSERT(0 != decoder->private_->metadata_filter_ids);

	if(decoder->private_->metadata_filter_ids_count == decoder->private_->metadata_filter_ids_capacity) {
		if(0 == (decoder->private_->metadata_filter_ids = (FLAC__byte*)safe_realloc_mul_2op_(decoder->private_->metadata_filter_ids, decoder->private_->metadata_filter_ids_capacity, /*times*/2))) {
			decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
			return false;
		}
		decoder->private_->metadata_filter_ids_capacity *= 2;
	}

	memcpy(decoder->private_->metadata_filter_ids + decoder->private_->metadata_filter_ids_count * (FLAC__STREAM_METADATA_APPLICATION_ID_LEN/8), id, (FLAC__STREAM_METADATA_APPLICATION_ID_LEN/8));
	decoder->private_->metadata_filter_ids_count++;

	return true;
}

FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_respond_all(FLAC__StreamDecoder *decoder)
{
	unsigned i;
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != decoder->protected_);
	if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED)
		return false;
	for(i = 0; i < sizeof(decoder->private_->metadata_filter) / sizeof(decoder->private_->metadata_filter[0]); i++)
		decoder->private_->metadata_filter[i] = true;
	decoder->private_->metadata_filter_ids_count = 0;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_ignore(FLAC__StreamDecoder *decoder, FLAC__MetadataType type)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != decoder->protected_);
	FLAC__ASSERT((unsigned)type <= FLAC__MAX_METADATA_TYPE_CODE);
	/* double protection */
	if((unsigned)type > FLAC__MAX_METADATA_TYPE_CODE)
		return false;
	if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED)
		return false;
	decoder->private_->metadata_filter[type] = false;
	if(type == FLAC__METADATA_TYPE_APPLICATION)
		decoder->private_->metadata_filter_ids_count = 0;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_ignore_application(FLAC__StreamDecoder *decoder, const FLAC__byte id[4])
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != decoder->protected_);
	FLAC__ASSERT(0 != id);
	if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED)
		return false;

	if(!decoder->private_->metadata_filter[FLAC__METADATA_TYPE_APPLICATION])
		return true;

	FLAC__ASSERT(0 != decoder->private_->metadata_filter_ids);

	if(decoder->private_->metadata_filter_ids_count == decoder->private_->metadata_filter_ids_capacity) {
		if(0 == (decoder->private_->metadata_filter_ids = (FLAC__byte*)safe_realloc_mul_2op_(decoder->private_->metadata_filter_ids, decoder->private_->metadata_filter_ids_capacity, /*times*/2))) {
			decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
			return false;
		}
		decoder->private_->metadata_filter_ids_capacity *= 2;
	}

	memcpy(decoder->private_->metadata_filter_ids + decoder->private_->metadata_filter_ids_count * (FLAC__STREAM_METADATA_APPLICATION_ID_LEN/8), id, (FLAC__STREAM_METADATA_APPLICATION_ID_LEN/8));
	decoder->private_->metadata_filter_ids_count++;

	return true;
}

FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_ignore_all(FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != decoder->protected_);
	if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED)
		return false;
	memset(decoder->private_->metadata_filter, 0, sizeof(decoder->private_->metadata_filter));
	decoder->private_->metadata_filter_ids_count = 0;
	return true;
}

FLAC_API FLAC__StreamDecoderState FLAC__stream_decoder_get_state(const FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);
	return decoder->protected_->state;
}

FLAC_API const char *FLAC__stream_decoder_get_resolved_state_string(const FLAC__StreamDecoder *decoder)
{
	return FLAC__StreamDecoderStateString[decoder->protected_->state];
}

FLAC_API FLAC__bool FLAC__stream_decoder_get_md5_checking(const FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);
	return decoder->protected_->md5_checking;
}

FLAC_API FLAC__uint64 FLAC__stream_decoder_get_total_samples(const FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);
	return decoder->private_->has_stream_info? decoder->private_->stream_info.data.stream_info.total_samples : 0;
}

FLAC_API unsigned FLAC__stream_decoder_get_channels(const FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);
	return decoder->protected_->channels;
}

FLAC_API FLAC__ChannelAssignment FLAC__stream_decoder_get_channel_assignment(const FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);
	return decoder->protected_->channel_assignment;
}

FLAC_API unsigned FLAC__stream_decoder_get_bits_per_sample(const FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);
	return decoder->protected_->bits_per_sample;
}

FLAC_API unsigned FLAC__stream_decoder_get_sample_rate(const FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);
	return decoder->protected_->sample_rate;
}

FLAC_API unsigned FLAC__stream_decoder_get_blocksize(const FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);
	return decoder->protected_->blocksize;
}

FLAC_API FLAC__bool FLAC__stream_decoder_get_decode_position(const FLAC__StreamDecoder *decoder, FLAC__uint64 *position)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != position);

#if FLAC__HAS_OGG
	if(decoder->private_->is_ogg)
		return false;
#endif
	if(0 == decoder->private_->tell_callback)
		return false;
	if(decoder->private_->tell_callback(decoder, position, decoder->private_->client_data) != FLAC__STREAM_DECODER_TELL_STATUS_OK)
		return false;
	/* should never happen since all FLAC frames and metadata blocks are byte aligned, but check just in case */
	if(!FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input))
		return false;
	FLAC__ASSERT(*position >= FLAC__stream_decoder_get_input_bytes_unconsumed(decoder));
	*position -= FLAC__stream_decoder_get_input_bytes_unconsumed(decoder);
	return true;
}

FLAC_API FLAC__bool FLAC__stream_decoder_flush(FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != decoder->protected_);

	decoder->private_->samples_decoded = 0;
	decoder->private_->do_md5_checking = false;

#if FLAC__HAS_OGG
	if(decoder->private_->is_ogg)
		FLAC__ogg_decoder_aspect_flush(&decoder->protected_->ogg_decoder_aspect);
#endif

	if(!FLAC__bitreader_clear(decoder->private_->input)) {
		decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}
	decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;

	return true;
}

FLAC_API FLAC__bool FLAC__stream_decoder_reset(FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != decoder->protected_);

	if(!FLAC__stream_decoder_flush(decoder)) {
		/* above call sets the state for us */
		return false;
	}

#if FLAC__HAS_OGG
	/*@@@ could go in !internal_reset_hack block below */
	if(decoder->private_->is_ogg)
		FLAC__ogg_decoder_aspect_reset(&decoder->protected_->ogg_decoder_aspect);
#endif

	/* Rewind if necessary.  If FLAC__stream_decoder_init() is calling us,
	 * (internal_reset_hack) don't try to rewind since we are already at
	 * the beginning of the stream and don't want to fail if the input is
	 * not seekable.
	 */
	if(!decoder->private_->internal_reset_hack) {
		if(decoder->private_->file == stdin)
			return false; /* can't rewind stdin, reset fails */
		if(decoder->private_->seek_callback && decoder->private_->seek_callback(decoder, 0, decoder->private_->client_data) == FLAC__STREAM_DECODER_SEEK_STATUS_ERROR)
			return false; /* seekable and seek fails, reset fails */
	}
	else
		decoder->private_->internal_reset_hack = false;

	decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_METADATA;

	decoder->private_->has_stream_info = false;
	if(decoder->private_->has_seek_table && 0 != decoder->private_->seek_table.data.seek_table.points) {
		free(decoder->private_->seek_table.data.seek_table.points);
		decoder->private_->seek_table.data.seek_table.points = 0;
		decoder->private_->has_seek_table = false;
	}
	decoder->private_->do_md5_checking = decoder->protected_->md5_checking;
	/*
	 * This goes in reset() and not flush() because according to the spec, a
	 * fixed-blocksize stream must stay that way through the whole stream.
	 */
	decoder->private_->fixed_block_size = decoder->private_->next_fixed_block_size = 0;

	/* We initialize the FLAC__MD5Context even though we may never use it.  This
	 * is because md5 checking may be turned on to start and then turned off if
	 * a seek occurs.  So we init the context here and finalize it in
	 * FLAC__stream_decoder_finish() to make sure things are always cleaned up
	 * properly.
	 */
	FLAC__MD5Init(&decoder->private_->md5context);

	decoder->private_->first_frame_offset = 0;
	decoder->private_->unparseable_frame_count = 0;

	return true;
}

FLAC_API FLAC__bool FLAC__stream_decoder_process_single(FLAC__StreamDecoder *decoder)
{
	FLAC__bool got_a_frame;
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);

	while(1) {
		switch(decoder->protected_->state) {
			case FLAC__STREAM_DECODER_SEARCH_FOR_METADATA:
				if(!find_metadata_(decoder))
					return false; /* above function sets the status for us */
				break;
			case FLAC__STREAM_DECODER_READ_METADATA:
				if(!read_metadata_(decoder))
					return false; /* above function sets the status for us */
				else
					return true;
			case FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC:
				if(!frame_sync_(decoder))
					return true; /* above function sets the status for us */
				break;
			case FLAC__STREAM_DECODER_READ_FRAME:
				if(!read_frame_(decoder, &got_a_frame, /*do_full_decode=*/true))
					return false; /* above function sets the status for us */
				if(got_a_frame)
					return true; /* above function sets the status for us */
				break;
			case FLAC__STREAM_DECODER_END_OF_STREAM:
			case FLAC__STREAM_DECODER_ABORTED:
				return true;
			default:
				FLAC__ASSERT(0);
				return false;
		}
	}
}

FLAC_API FLAC__bool FLAC__stream_decoder_process_until_end_of_metadata(FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);

	while(1) {
		switch(decoder->protected_->state) {
			case FLAC__STREAM_DECODER_SEARCH_FOR_METADATA:
				if(!find_metadata_(decoder))
					return false; /* above function sets the status for us */
				break;
			case FLAC__STREAM_DECODER_READ_METADATA:
				if(!read_metadata_(decoder))
					return false; /* above function sets the status for us */
				break;
			case FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC:
			case FLAC__STREAM_DECODER_READ_FRAME:
			case FLAC__STREAM_DECODER_END_OF_STREAM:
			case FLAC__STREAM_DECODER_ABORTED:
				return true;
			default:
				FLAC__ASSERT(0);
				return false;
		}
	}
}

FLAC_API FLAC__bool FLAC__stream_decoder_process_until_end_of_stream(FLAC__StreamDecoder *decoder)
{
	FLAC__bool dummy;
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);

	while(1) {
		switch(decoder->protected_->state) {
			case FLAC__STREAM_DECODER_SEARCH_FOR_METADATA:
				if(!find_metadata_(decoder))
					return false; /* above function sets the status for us */
				break;
			case FLAC__STREAM_DECODER_READ_METADATA:
				if(!read_metadata_(decoder))
					return false; /* above function sets the status for us */
				break;
			case FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC:
				if(!frame_sync_(decoder))
					return true; /* above function sets the status for us */
				break;
			case FLAC__STREAM_DECODER_READ_FRAME:
				if(!read_frame_(decoder, &dummy, /*do_full_decode=*/true))
					return false; /* above function sets the status for us */
				break;
			case FLAC__STREAM_DECODER_END_OF_STREAM:
			case FLAC__STREAM_DECODER_ABORTED:
				return true;
			default:
				FLAC__ASSERT(0);
				return false;
		}
	}
}

FLAC_API FLAC__bool FLAC__stream_decoder_skip_single_frame(FLAC__StreamDecoder *decoder)
{
	FLAC__bool got_a_frame;
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);

	while(1) {
		switch(decoder->protected_->state) {
			case FLAC__STREAM_DECODER_SEARCH_FOR_METADATA:
			case FLAC__STREAM_DECODER_READ_METADATA:
				return false; /* above function sets the status for us */
			case FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC:
				if(!frame_sync_(decoder))
					return true; /* above function sets the status for us */
				break;
			case FLAC__STREAM_DECODER_READ_FRAME:
				if(!read_frame_(decoder, &got_a_frame, /*do_full_decode=*/false))
					return false; /* above function sets the status for us */
				if(got_a_frame)
					return true; /* above function sets the status for us */
				break;
			case FLAC__STREAM_DECODER_END_OF_STREAM:
			case FLAC__STREAM_DECODER_ABORTED:
				return true;
			default:
				FLAC__ASSERT(0);
				return false;
		}
	}
}

FLAC_API FLAC__bool FLAC__stream_decoder_seek_absolute(FLAC__StreamDecoder *decoder, FLAC__uint64 sample)
{
	FLAC__uint64 length;

	FLAC__ASSERT(0 != decoder);

	if(
		decoder->protected_->state != FLAC__STREAM_DECODER_SEARCH_FOR_METADATA &&
		decoder->protected_->state != FLAC__STREAM_DECODER_READ_METADATA &&
		decoder->protected_->state != FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC &&
		decoder->protected_->state != FLAC__STREAM_DECODER_READ_FRAME &&
		decoder->protected_->state != FLAC__STREAM_DECODER_END_OF_STREAM
	)
		return false;

	if(0 == decoder->private_->seek_callback)
		return false;

	FLAC__ASSERT(decoder->private_->seek_callback);
	FLAC__ASSERT(decoder->private_->tell_callback);
	FLAC__ASSERT(decoder->private_->length_callback);
	FLAC__ASSERT(decoder->private_->eof_callback);

	if(FLAC__stream_decoder_get_total_samples(decoder) > 0 && sample >= FLAC__stream_decoder_get_total_samples(decoder))
		return false;

	decoder->private_->is_seeking = true;

	/* turn off md5 checking if a seek is attempted */
	decoder->private_->do_md5_checking = false;

	/* get the file length (currently our algorithm needs to know the length so it's also an error to get FLAC__STREAM_DECODER_LENGTH_STATUS_UNSUPPORTED) */
	if(decoder->private_->length_callback(decoder, &length, decoder->private_->client_data) != FLAC__STREAM_DECODER_LENGTH_STATUS_OK) {
		decoder->private_->is_seeking = false;
		return false;
	}

	/* if we haven't finished processing the metadata yet, do that so we have the STREAMINFO, SEEK_TABLE, and first_frame_offset */
	if(
		decoder->protected_->state == FLAC__STREAM_DECODER_SEARCH_FOR_METADATA ||
		decoder->protected_->state == FLAC__STREAM_DECODER_READ_METADATA
	) {
		if(!FLAC__stream_decoder_process_until_end_of_metadata(decoder)) {
			/* above call sets the state for us */
			decoder->private_->is_seeking = false;
			return false;
		}
		/* check this again in case we didn't know total_samples the first time */
		if(FLAC__stream_decoder_get_total_samples(decoder) > 0 && sample >= FLAC__stream_decoder_get_total_samples(decoder)) {
			decoder->private_->is_seeking = false;
			return false;
		}
	}

	{
		const FLAC__bool ok =
#if FLAC__HAS_OGG
			decoder->private_->is_ogg?
			seek_to_absolute_sample_ogg_(decoder, length, sample) :
#endif
			seek_to_absolute_sample_(decoder, length, sample)
		;
		decoder->private_->is_seeking = false;
		return ok;
	}
}

/***********************************************************************
 *
 * Protected class methods
 *
 ***********************************************************************/

unsigned FLAC__stream_decoder_get_input_bytes_unconsumed(const FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input));
	FLAC__ASSERT(!(FLAC__bitreader_get_input_bits_unconsumed(decoder->private_->input) & 7));
	return FLAC__bitreader_get_input_bits_unconsumed(decoder->private_->input) / 8;
}

/***********************************************************************
 *
 * Private class methods
 *
 ***********************************************************************/

void set_defaults_decode(FLAC__StreamDecoder *decoder)
{
#if FLAC__HAS_OGG
	decoder->private_->is_ogg = false;
#endif
	decoder->private_->read_callback = 0;
	decoder->private_->seek_callback = 0;
	decoder->private_->tell_callback = 0;
	decoder->private_->length_callback = 0;
	decoder->private_->eof_callback = 0;
	decoder->private_->write_callback = 0;
	decoder->private_->metadata_callback = 0;
	decoder->private_->error_callback = 0;
	decoder->private_->client_data = 0;

	memset(decoder->private_->metadata_filter, 0, sizeof(decoder->private_->metadata_filter));
	decoder->private_->metadata_filter[FLAC__METADATA_TYPE_STREAMINFO] = true;
	decoder->private_->metadata_filter_ids_count = 0;

	decoder->protected_->md5_checking = false;

#if FLAC__HAS_OGG
	FLAC__ogg_decoder_aspect_set_defaults(&decoder->protected_->ogg_decoder_aspect);
#endif
}

/*
 * This will forcibly set stdin to binary mode (for OSes that require it)
 */
FILE *get_binary_stdin_(void)
{
	/* if something breaks here it is probably due to the presence or
	 * absence of an underscore before the identifiers 'setmode',
	 * 'fileno', and/or 'O_BINARY'; check your system header files.
	 */
#if defined _MSC_VER || defined __MINGW32__
	_setmode(_fileno(stdin), _O_BINARY);
#elif defined __CYGWIN__ 
	/* almost certainly not needed for any modern Cygwin, but let's be safe... */
	setmode(_fileno(stdin), _O_BINARY);
#elif defined __EMX__
	setmode(fileno(stdin), O_BINARY);
#endif

	return stdin;
}

FLAC__bool allocate_output_(FLAC__StreamDecoder *decoder, unsigned size, unsigned channels)
{
	unsigned i;
	FLAC__int32 *tmp;

	if(size <= decoder->private_->output_capacity && channels <= decoder->private_->output_channels)
		return true;

	/* simply using realloc() is not practical because the number of channels may change mid-stream */

	for(i = 0; i < FLAC__MAX_CHANNELS; i++) {
		if(0 != decoder->private_->output[i]) {
			free(decoder->private_->output[i]-4);
			decoder->private_->output[i] = 0;
		}
		if(0 != decoder->private_->residual_unaligned[i]) {
			free(decoder->private_->residual_unaligned[i]);
			decoder->private_->residual_unaligned[i] = decoder->private_->residual[i] = 0;
		}
	}

	for(i = 0; i < channels; i++) {
		/* WATCHOUT:
		 * FLAC__lpc_restore_signal_asm_ia32_mmx() requires that the
		 * output arrays have a buffer of up to 3 zeroes in front
		 * (at negative indices) for alignment purposes; we use 4
		 * to keep the data well-aligned.
		 */
		tmp = (FLAC__int32*)safe_malloc_muladd2_(sizeof(FLAC__int32), /*times (*/size, /*+*/4/*)*/);
		if(tmp == 0) {
			decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
			return false;
		}
		memset(tmp, 0, sizeof(FLAC__int32)*4);
		decoder->private_->output[i] = tmp + 4;

		/* WATCHOUT:
		 * minimum of quadword alignment for PPC vector optimizations is REQUIRED:
		 */
		if(!FLAC__memory_alloc_aligned_int32_array(size, &decoder->private_->residual_unaligned[i], &decoder->private_->residual[i])) {
			decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
			return false;
		}
	}

	decoder->private_->output_capacity = size;
	decoder->private_->output_channels = channels;

	return true;
}

FLAC__bool has_id_filtered_(FLAC__StreamDecoder *decoder, FLAC__byte *id)
{
	size_t i;

	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);

	for(i = 0; i < decoder->private_->metadata_filter_ids_count; i++)
		if(0 == memcmp(decoder->private_->metadata_filter_ids + i * (FLAC__STREAM_METADATA_APPLICATION_ID_LEN/8), id, (FLAC__STREAM_METADATA_APPLICATION_ID_LEN/8)))
			return true;

	return false;
}

FLAC__bool find_metadata_(FLAC__StreamDecoder *decoder)
{
	FLAC__uint32 x;
	unsigned i, id;
	FLAC__bool first = true;

	FLAC__ASSERT(FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input));

	for(i = id = 0; i < 4; ) {
		if(decoder->private_->cached) {
			x = (FLAC__uint32)decoder->private_->lookahead;
			decoder->private_->cached = false;
		}
		else {
			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 8))
				return false; /* read_callback_ sets the state for us */
		}
		if(x == FLAC__STREAM_SYNC_STRING[i]) {
			first = true;
			i++;
			id = 0;
			continue;
		}
		if(x == ID3V2_TAG_[id]) {
			id++;
			i = 0;
			if(id == 3) {
				if(!skip_id3v2_tag_(decoder))
					return false; /* skip_id3v2_tag_ sets the state for us */
			}
			continue;
		}
		id = 0;
		if(x == 0xff) { /* MAGIC NUMBER for the first 8 frame sync bits */
			decoder->private_->header_warmup[0] = (FLAC__byte)x;
			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 8))
				return false; /* read_callback_ sets the state for us */

			/* we have to check if we just read two 0xff's in a row; the second may actually be the beginning of the sync code */
			/* else we have to check if the second byte is the end of a sync code */
			if(x == 0xff) { /* MAGIC NUMBER for the first 8 frame sync bits */
				decoder->private_->lookahead = (FLAC__byte)x;
				decoder->private_->cached = true;
			}
			else if(x >> 2 == 0x3e) { /* MAGIC NUMBER for the last 6 sync bits */
				decoder->private_->header_warmup[1] = (FLAC__byte)x;
				decoder->protected_->state = FLAC__STREAM_DECODER_READ_FRAME;
				return true;
			}
		}
		i = 0;
		if(first) {
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC);
			first = false;
		}
	}

	decoder->protected_->state = FLAC__STREAM_DECODER_READ_METADATA;
	return true;
}

FLAC__bool read_metadata_(FLAC__StreamDecoder *decoder)
{
	FLAC__bool is_last;
	FLAC__uint32 i, x, type, length;

	FLAC__ASSERT(FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input));

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_IS_LAST_LEN))
		return false; /* read_callback_ sets the state for us */
	is_last = x? true : false;

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &type, FLAC__STREAM_METADATA_TYPE_LEN))
		return false; /* read_callback_ sets the state for us */

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &length, FLAC__STREAM_METADATA_LENGTH_LEN))
		return false; /* read_callback_ sets the state for us */

	if(type == FLAC__METADATA_TYPE_STREAMINFO) {
		if(!read_metadata_streaminfo_(decoder, is_last, length))
			return false;

		decoder->private_->has_stream_info = true;
		if(0 == memcmp(decoder->private_->stream_info.data.stream_info.md5sum, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16))
			decoder->private_->do_md5_checking = false;
		if(!decoder->private_->is_seeking && decoder->private_->metadata_filter[FLAC__METADATA_TYPE_STREAMINFO] && decoder->private_->metadata_callback)
			decoder->private_->metadata_callback(decoder, &decoder->private_->stream_info, decoder->private_->client_data);
	}
	else if(type == FLAC__METADATA_TYPE_SEEKTABLE) {
		if(!read_metadata_seektable_(decoder, is_last, length))
			return false;

		decoder->private_->has_seek_table = true;
		if(!decoder->private_->is_seeking && decoder->private_->metadata_filter[FLAC__METADATA_TYPE_SEEKTABLE] && decoder->private_->metadata_callback)
			decoder->private_->metadata_callback(decoder, &decoder->private_->seek_table, decoder->private_->client_data);
	}
	else {
		FLAC__bool skip_it = !decoder->private_->metadata_filter[type];
		unsigned real_length = length;
		FLAC__StreamMetadata block;

		block.is_last = is_last;
		block.type = (FLAC__MetadataType)type;
		block.length = length;

		if(type == FLAC__METADATA_TYPE_APPLICATION) {
			if(!FLAC__bitreader_read_byte_block_aligned_no_crc(decoder->private_->input, block.data.application.id, FLAC__STREAM_METADATA_APPLICATION_ID_LEN/8))
				return false; /* read_callback_ sets the state for us */

			if(real_length < FLAC__STREAM_METADATA_APPLICATION_ID_LEN/8) { /* underflow check */
				decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;/*@@@@@@ maybe wrong error? need to resync?*/
				return false;
			}

			real_length -= FLAC__STREAM_METADATA_APPLICATION_ID_LEN/8;

			if(decoder->private_->metadata_filter_ids_count > 0 && has_id_filtered_(decoder, block.data.application.id))
				skip_it = !skip_it;
		}

		if(skip_it) {
			if(!FLAC__bitreader_skip_byte_block_aligned_no_crc(decoder->private_->input, real_length))
				return false; /* read_callback_ sets the state for us */
		}
		else {
			switch(type) {
				case FLAC__METADATA_TYPE_PADDING:
					/* skip the padding bytes */
					if(!FLAC__bitreader_skip_byte_block_aligned_no_crc(decoder->private_->input, real_length))
						return false; /* read_callback_ sets the state for us */
					break;
				case FLAC__METADATA_TYPE_APPLICATION:
					/* remember, we read the ID already */
					if(real_length > 0) {
						if(0 == (block.data.application.data = (FLAC__byte*)malloc(real_length))) {
							decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
							return false;
						}
						if(!FLAC__bitreader_read_byte_block_aligned_no_crc(decoder->private_->input, block.data.application.data, real_length))
							return false; /* read_callback_ sets the state for us */
					}
					else
						block.data.application.data = 0;
					break;
				case FLAC__METADATA_TYPE_VORBIS_COMMENT:
					if(!read_metadata_vorbiscomment_(decoder, &block.data.vorbis_comment))
						return false;
					break;
				case FLAC__METADATA_TYPE_CUESHEET:
					if(!read_metadata_cuesheet_(decoder, &block.data.cue_sheet))
						return false;
					break;
				case FLAC__METADATA_TYPE_PICTURE:
					if(!read_metadata_picture_(decoder, &block.data.picture))
						return false;
					break;
				case FLAC__METADATA_TYPE_STREAMINFO:
				case FLAC__METADATA_TYPE_SEEKTABLE:
					FLAC__ASSERT(0);
					break;
				default:
					if(real_length > 0) {
						if(0 == (block.data.unknown.data = (FLAC__byte*)malloc(real_length))) {
							decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
							return false;
						}
						if(!FLAC__bitreader_read_byte_block_aligned_no_crc(decoder->private_->input, block.data.unknown.data, real_length))
							return false; /* read_callback_ sets the state for us */
					}
					else
						block.data.unknown.data = 0;
					break;
			}
			if(!decoder->private_->is_seeking && decoder->private_->metadata_callback)
				decoder->private_->metadata_callback(decoder, &block, decoder->private_->client_data);

			/* now we have to free any malloc()ed data in the block */
			switch(type) {
				case FLAC__METADATA_TYPE_PADDING:
					break;
				case FLAC__METADATA_TYPE_APPLICATION:
					if(0 != block.data.application.data)
						free(block.data.application.data);
					break;
				case FLAC__METADATA_TYPE_VORBIS_COMMENT:
					if(0 != block.data.vorbis_comment.vendor_string.entry)
						free(block.data.vorbis_comment.vendor_string.entry);
					if(block.data.vorbis_comment.num_comments > 0)
						for(i = 0; i < block.data.vorbis_comment.num_comments; i++)
							if(0 != block.data.vorbis_comment.comments[i].entry)
								free(block.data.vorbis_comment.comments[i].entry);
					if(0 != block.data.vorbis_comment.comments)
						free(block.data.vorbis_comment.comments);
					break;
				case FLAC__METADATA_TYPE_CUESHEET:
					if(block.data.cue_sheet.num_tracks > 0)
						for(i = 0; i < block.data.cue_sheet.num_tracks; i++)
							if(0 != block.data.cue_sheet.tracks[i].indices)
								free(block.data.cue_sheet.tracks[i].indices);
					if(0 != block.data.cue_sheet.tracks)
						free(block.data.cue_sheet.tracks);
					break;
				case FLAC__METADATA_TYPE_PICTURE:
					if(0 != block.data.picture.mime_type)
						free(block.data.picture.mime_type);
					if(0 != block.data.picture.description)
						free(block.data.picture.description);
					if(0 != block.data.picture.data)
						free(block.data.picture.data);
					break;
				case FLAC__METADATA_TYPE_STREAMINFO:
				case FLAC__METADATA_TYPE_SEEKTABLE:
					FLAC__ASSERT(0);
				default:
					if(0 != block.data.unknown.data)
						free(block.data.unknown.data);
					break;
			}
		}
	}

	if(is_last) {
		/* if this fails, it's OK, it's just a hint for the seek routine */
		if(!FLAC__stream_decoder_get_decode_position(decoder, &decoder->private_->first_frame_offset))
			decoder->private_->first_frame_offset = 0;
		decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
	}

	return true;
}

FLAC__bool read_metadata_streaminfo_(FLAC__StreamDecoder *decoder, FLAC__bool is_last, unsigned length)
{
	FLAC__uint32 x;
	unsigned bits, used_bits = 0;

	FLAC__ASSERT(FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input));

	decoder->private_->stream_info.type = FLAC__METADATA_TYPE_STREAMINFO;
	decoder->private_->stream_info.is_last = is_last;
	decoder->private_->stream_info.length = length;

	bits = FLAC__STREAM_METADATA_STREAMINFO_MIN_BLOCK_SIZE_LEN;
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, bits))
		return false; /* read_callback_ sets the state for us */
	decoder->private_->stream_info.data.stream_info.min_blocksize = x;
	used_bits += bits;

	bits = FLAC__STREAM_METADATA_STREAMINFO_MAX_BLOCK_SIZE_LEN;
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_STREAMINFO_MAX_BLOCK_SIZE_LEN))
		return false; /* read_callback_ sets the state for us */
	decoder->private_->stream_info.data.stream_info.max_blocksize = x;
	used_bits += bits;

	bits = FLAC__STREAM_METADATA_STREAMINFO_MIN_FRAME_SIZE_LEN;
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_STREAMINFO_MIN_FRAME_SIZE_LEN))
		return false; /* read_callback_ sets the state for us */
	decoder->private_->stream_info.data.stream_info.min_framesize = x;
	used_bits += bits;

	bits = FLAC__STREAM_METADATA_STREAMINFO_MAX_FRAME_SIZE_LEN;
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_STREAMINFO_MAX_FRAME_SIZE_LEN))
		return false; /* read_callback_ sets the state for us */
	decoder->private_->stream_info.data.stream_info.max_framesize = x;
	used_bits += bits;

	bits = FLAC__STREAM_METADATA_STREAMINFO_SAMPLE_RATE_LEN;
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_STREAMINFO_SAMPLE_RATE_LEN))
		return false; /* read_callback_ sets the state for us */
	decoder->private_->stream_info.data.stream_info.sample_rate = x;
	used_bits += bits;

	bits = FLAC__STREAM_METADATA_STREAMINFO_CHANNELS_LEN;
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_STREAMINFO_CHANNELS_LEN))
		return false; /* read_callback_ sets the state for us */
	decoder->private_->stream_info.data.stream_info.channels = x+1;
	used_bits += bits;

	bits = FLAC__STREAM_METADATA_STREAMINFO_BITS_PER_SAMPLE_LEN;
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_STREAMINFO_BITS_PER_SAMPLE_LEN))
		return false; /* read_callback_ sets the state for us */
	decoder->private_->stream_info.data.stream_info.bits_per_sample = x+1;
	used_bits += bits;

	bits = FLAC__STREAM_METADATA_STREAMINFO_TOTAL_SAMPLES_LEN;
	if(!FLAC__bitreader_read_raw_uint64(decoder->private_->input, &decoder->private_->stream_info.data.stream_info.total_samples, FLAC__STREAM_METADATA_STREAMINFO_TOTAL_SAMPLES_LEN))
		return false; /* read_callback_ sets the state for us */
	used_bits += bits;

	if(!FLAC__bitreader_read_byte_block_aligned_no_crc(decoder->private_->input, decoder->private_->stream_info.data.stream_info.md5sum, 16))
		return false; /* read_callback_ sets the state for us */
	used_bits += 16*8;

	/* skip the rest of the block */
	FLAC__ASSERT(used_bits % 8 == 0);
	length -= (used_bits / 8);
	if(!FLAC__bitreader_skip_byte_block_aligned_no_crc(decoder->private_->input, length))
		return false; /* read_callback_ sets the state for us */

	return true;
}

FLAC__bool read_metadata_seektable_(FLAC__StreamDecoder *decoder, FLAC__bool is_last, unsigned length)
{
	FLAC__uint32 i, x;
	FLAC__uint64 xx;

	FLAC__ASSERT(FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input));

	decoder->private_->seek_table.type = FLAC__METADATA_TYPE_SEEKTABLE;
	decoder->private_->seek_table.is_last = is_last;
	decoder->private_->seek_table.length = length;

	decoder->private_->seek_table.data.seek_table.num_points = length / FLAC__STREAM_METADATA_SEEKPOINT_LENGTH;

	/* use realloc since we may pass through here several times (e.g. after seeking) */
	if(0 == (decoder->private_->seek_table.data.seek_table.points = (FLAC__StreamMetadata_SeekPoint*)safe_realloc_mul_2op_(decoder->private_->seek_table.data.seek_table.points, decoder->private_->seek_table.data.seek_table.num_points, /*times*/sizeof(FLAC__StreamMetadata_SeekPoint)))) {
		decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}
	for(i = 0; i < decoder->private_->seek_table.data.seek_table.num_points; i++) {
		if(!FLAC__bitreader_read_raw_uint64(decoder->private_->input, &xx, FLAC__STREAM_METADATA_SEEKPOINT_SAMPLE_NUMBER_LEN))
			return false; /* read_callback_ sets the state for us */
		decoder->private_->seek_table.data.seek_table.points[i].sample_number = xx;

		if(!FLAC__bitreader_read_raw_uint64(decoder->private_->input, &xx, FLAC__STREAM_METADATA_SEEKPOINT_STREAM_OFFSET_LEN))
			return false; /* read_callback_ sets the state for us */
		decoder->private_->seek_table.data.seek_table.points[i].stream_offset = xx;

		if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_SEEKPOINT_FRAME_SAMPLES_LEN))
			return false; /* read_callback_ sets the state for us */
		decoder->private_->seek_table.data.seek_table.points[i].frame_samples = x;
	}
	length -= (decoder->private_->seek_table.data.seek_table.num_points * FLAC__STREAM_METADATA_SEEKPOINT_LENGTH);
	/* if there is a partial point left, skip over it */
	if(length > 0) {
		/*@@@ do a send_error_to_client_() here?  there's an argument for either way */
		if(!FLAC__bitreader_skip_byte_block_aligned_no_crc(decoder->private_->input, length))
			return false; /* read_callback_ sets the state for us */
	}

	return true;
}

FLAC__bool read_metadata_vorbiscomment_(FLAC__StreamDecoder *decoder, FLAC__StreamMetadata_VorbisComment *obj)
{
	FLAC__uint32 i;

	FLAC__ASSERT(FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input));

	/* read vendor string */
	FLAC__ASSERT(FLAC__STREAM_METADATA_VORBIS_COMMENT_ENTRY_LENGTH_LEN == 32);
	if(!FLAC__bitreader_read_uint32_little_endian(decoder->private_->input, &obj->vendor_string.length))
		return false; /* read_callback_ sets the state for us */
	if(obj->vendor_string.length > 0) {
		if(0 == (obj->vendor_string.entry = (FLAC__byte*)safe_malloc_add_2op_(obj->vendor_string.length, /*+*/1))) {
			decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
			return false;
		}
		if(!FLAC__bitreader_read_byte_block_aligned_no_crc(decoder->private_->input, obj->vendor_string.entry, obj->vendor_string.length))
			return false; /* read_callback_ sets the state for us */
		obj->vendor_string.entry[obj->vendor_string.length] = '\0';
	}
	else
		obj->vendor_string.entry = 0;

	/* read num comments */
	FLAC__ASSERT(FLAC__STREAM_METADATA_VORBIS_COMMENT_NUM_COMMENTS_LEN == 32);
	if(!FLAC__bitreader_read_uint32_little_endian(decoder->private_->input, &obj->num_comments))
		return false; /* read_callback_ sets the state for us */

	/* read comments */
	if(obj->num_comments > 0) {
		if(0 == (obj->comments = (FLAC__StreamMetadata_VorbisComment_Entry*)safe_malloc_mul_2op_(obj->num_comments, /*times*/sizeof(FLAC__StreamMetadata_VorbisComment_Entry)))) {
			decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
			return false;
		}
		for(i = 0; i < obj->num_comments; i++) {
			FLAC__ASSERT(FLAC__STREAM_METADATA_VORBIS_COMMENT_ENTRY_LENGTH_LEN == 32);
			if(!FLAC__bitreader_read_uint32_little_endian(decoder->private_->input, &obj->comments[i].length))
				return false; /* read_callback_ sets the state for us */
			if(obj->comments[i].length > 0) {
				if(0 == (obj->comments[i].entry = (FLAC__byte*)safe_malloc_add_2op_(obj->comments[i].length, /*+*/1))) {
					decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
					return false;
				}
				if(!FLAC__bitreader_read_byte_block_aligned_no_crc(decoder->private_->input, obj->comments[i].entry, obj->comments[i].length))
					return false; /* read_callback_ sets the state for us */
				obj->comments[i].entry[obj->comments[i].length] = '\0';
			}
			else
				obj->comments[i].entry = 0;
		}
	}
	else {
		obj->comments = 0;
	}

	return true;
}

FLAC__bool read_metadata_cuesheet_(FLAC__StreamDecoder *decoder, FLAC__StreamMetadata_CueSheet *obj)
{
	FLAC__uint32 i, j, x;

	FLAC__ASSERT(FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input));

	memset(obj, 0, sizeof(FLAC__StreamMetadata_CueSheet));

	FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_MEDIA_CATALOG_NUMBER_LEN % 8 == 0);
	if(!FLAC__bitreader_read_byte_block_aligned_no_crc(decoder->private_->input, (FLAC__byte*)obj->media_catalog_number, FLAC__STREAM_METADATA_CUESHEET_MEDIA_CATALOG_NUMBER_LEN/8))
		return false; /* read_callback_ sets the state for us */

	if(!FLAC__bitreader_read_raw_uint64(decoder->private_->input, &obj->lead_in, FLAC__STREAM_METADATA_CUESHEET_LEAD_IN_LEN))
		return false; /* read_callback_ sets the state for us */

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_CUESHEET_IS_CD_LEN))
		return false; /* read_callback_ sets the state for us */
	obj->is_cd = x? true : false;

	if(!FLAC__bitreader_skip_bits_no_crc(decoder->private_->input, FLAC__STREAM_METADATA_CUESHEET_RESERVED_LEN))
		return false; /* read_callback_ sets the state for us */

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_CUESHEET_NUM_TRACKS_LEN))
		return false; /* read_callback_ sets the state for us */
	obj->num_tracks = x;

	if(obj->num_tracks > 0) {
		if(0 == (obj->tracks = (FLAC__StreamMetadata_CueSheet_Track*)safe_calloc_(obj->num_tracks, sizeof(FLAC__StreamMetadata_CueSheet_Track)))) {
			decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
			return false;
		}
		for(i = 0; i < obj->num_tracks; i++) {
			FLAC__StreamMetadata_CueSheet_Track *track = &obj->tracks[i];
			if(!FLAC__bitreader_read_raw_uint64(decoder->private_->input, &track->offset, FLAC__STREAM_METADATA_CUESHEET_TRACK_OFFSET_LEN))
				return false; /* read_callback_ sets the state for us */

			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_CUESHEET_TRACK_NUMBER_LEN))
				return false; /* read_callback_ sets the state for us */
			track->number = (FLAC__byte)x;

			FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_TRACK_ISRC_LEN % 8 == 0);
			if(!FLAC__bitreader_read_byte_block_aligned_no_crc(decoder->private_->input, (FLAC__byte*)track->isrc, FLAC__STREAM_METADATA_CUESHEET_TRACK_ISRC_LEN/8))
				return false; /* read_callback_ sets the state for us */

			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_CUESHEET_TRACK_TYPE_LEN))
				return false; /* read_callback_ sets the state for us */
			track->type = x;

			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_CUESHEET_TRACK_PRE_EMPHASIS_LEN))
				return false; /* read_callback_ sets the state for us */
			track->pre_emphasis = x;

			if(!FLAC__bitreader_skip_bits_no_crc(decoder->private_->input, FLAC__STREAM_METADATA_CUESHEET_TRACK_RESERVED_LEN))
				return false; /* read_callback_ sets the state for us */

			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_CUESHEET_TRACK_NUM_INDICES_LEN))
				return false; /* read_callback_ sets the state for us */
			track->num_indices = (FLAC__byte)x;

			if(track->num_indices > 0) {
				if(0 == (track->indices = (FLAC__StreamMetadata_CueSheet_Index*)safe_calloc_(track->num_indices, sizeof(FLAC__StreamMetadata_CueSheet_Index)))) {
					decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
					return false;
				}
				for(j = 0; j < track->num_indices; j++) {
					FLAC__StreamMetadata_CueSheet_Index *index = &track->indices[j];
					if(!FLAC__bitreader_read_raw_uint64(decoder->private_->input, &index->offset, FLAC__STREAM_METADATA_CUESHEET_INDEX_OFFSET_LEN))
						return false; /* read_callback_ sets the state for us */

					if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_CUESHEET_INDEX_NUMBER_LEN))
						return false; /* read_callback_ sets the state for us */
					index->number = (FLAC__byte)x;

					if(!FLAC__bitreader_skip_bits_no_crc(decoder->private_->input, FLAC__STREAM_METADATA_CUESHEET_INDEX_RESERVED_LEN))
						return false; /* read_callback_ sets the state for us */
				}
			}
		}
	}

	return true;
}

FLAC__bool read_metadata_picture_(FLAC__StreamDecoder *decoder, FLAC__StreamMetadata_Picture *obj)
{
	FLAC__uint32 x;

	FLAC__ASSERT(FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input));

	/* read type */
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_PICTURE_TYPE_LEN))
		return false; /* read_callback_ sets the state for us */
	obj->type = (FLAC__StreamMetadata_Picture_Type )x;

	/* read MIME type */
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_PICTURE_MIME_TYPE_LENGTH_LEN))
		return false; /* read_callback_ sets the state for us */
	if(0 == (obj->mime_type = (char*)safe_malloc_add_2op_(x, /*+*/1))) {
		decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}
	if(x > 0) {
		if(!FLAC__bitreader_read_byte_block_aligned_no_crc(decoder->private_->input, (FLAC__byte*)obj->mime_type, x))
			return false; /* read_callback_ sets the state for us */
	}
	obj->mime_type[x] = '\0';

	/* read description */
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_PICTURE_DESCRIPTION_LENGTH_LEN))
		return false; /* read_callback_ sets the state for us */
	if(0 == (obj->description = (FLAC__byte*)safe_malloc_add_2op_(x, /*+*/1))) {
		decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}
	if(x > 0) {
		if(!FLAC__bitreader_read_byte_block_aligned_no_crc(decoder->private_->input, obj->description, x))
			return false; /* read_callback_ sets the state for us */
	}
	obj->description[x] = '\0';

	/* read width */
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &obj->width, FLAC__STREAM_METADATA_PICTURE_WIDTH_LEN))
		return false; /* read_callback_ sets the state for us */

	/* read height */
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &obj->height, FLAC__STREAM_METADATA_PICTURE_HEIGHT_LEN))
		return false; /* read_callback_ sets the state for us */

	/* read depth */
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &obj->depth, FLAC__STREAM_METADATA_PICTURE_DEPTH_LEN))
		return false; /* read_callback_ sets the state for us */

	/* read colors */
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &obj->colors, FLAC__STREAM_METADATA_PICTURE_COLORS_LEN))
		return false; /* read_callback_ sets the state for us */

	/* read data */
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &(obj->data_length), FLAC__STREAM_METADATA_PICTURE_DATA_LENGTH_LEN))
		return false; /* read_callback_ sets the state for us */
	if(0 == (obj->data = (FLAC__byte*)safe_malloc_(obj->data_length))) {
		decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}
	if(obj->data_length > 0) {
		if(!FLAC__bitreader_read_byte_block_aligned_no_crc(decoder->private_->input, obj->data, obj->data_length))
			return false; /* read_callback_ sets the state for us */
	}

	return true;
}

FLAC__bool skip_id3v2_tag_(FLAC__StreamDecoder *decoder)
{
	FLAC__uint32 x;
	unsigned i, skip;

	/* skip the version and flags bytes */
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 24))
		return false; /* read_callback_ sets the state for us */
	/* get the size (in bytes) to skip */
	skip = 0;
	for(i = 0; i < 4; i++) {
		if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 8))
			return false; /* read_callback_ sets the state for us */
		skip <<= 7;
		skip |= (x & 0x7f);
	}
	/* skip the rest of the tag */
	if(!FLAC__bitreader_skip_byte_block_aligned_no_crc(decoder->private_->input, skip))
		return false; /* read_callback_ sets the state for us */
	return true;
}

FLAC__bool frame_sync_(FLAC__StreamDecoder *decoder)
{
	FLAC__uint32 x;
	FLAC__bool first = true;

	/* If we know the total number of samples in the stream, stop if we've read that many. */
	/* This will stop us, for example, from wasting time trying to sync on an ID3V1 tag. */
	if(FLAC__stream_decoder_get_total_samples(decoder) > 0) {
		if(decoder->private_->samples_decoded >= FLAC__stream_decoder_get_total_samples(decoder)) {
			decoder->protected_->state = FLAC__STREAM_DECODER_END_OF_STREAM;
			return true;
		}
	}

	/* make sure we're byte aligned */
	if(!FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input)) {
		if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__bitreader_bits_left_for_byte_alignment(decoder->private_->input)))
			return false; /* read_callback_ sets the state for us */
	}

	while(1) {
		if(decoder->private_->cached) {
			x = (FLAC__uint32)decoder->private_->lookahead;
			decoder->private_->cached = false;
		}
		else {
			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 8))
				return false; /* read_callback_ sets the state for us */
		}
		if(x == 0xff) { /* MAGIC NUMBER for the first 8 frame sync bits */
			decoder->private_->header_warmup[0] = (FLAC__byte)x;
			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 8))
				return false; /* read_callback_ sets the state for us */

			/* we have to check if we just read two 0xff's in a row; the second may actually be the beginning of the sync code */
			/* else we have to check if the second byte is the end of a sync code */
			if(x == 0xff) { /* MAGIC NUMBER for the first 8 frame sync bits */
				decoder->private_->lookahead = (FLAC__byte)x;
				decoder->private_->cached = true;
			}
			else if(x >> 2 == 0x3e) { /* MAGIC NUMBER for the last 6 sync bits */
				decoder->private_->header_warmup[1] = (FLAC__byte)x;
				decoder->protected_->state = FLAC__STREAM_DECODER_READ_FRAME;
				return true;
			}
		}
		if(first) {
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC);
			first = false;
		}
	}

	return true;
}

FLAC__bool read_frame_(FLAC__StreamDecoder *decoder, FLAC__bool *got_a_frame, FLAC__bool do_full_decode)
{
	unsigned channel;
	unsigned i;
	FLAC__int32 mid, side;
	unsigned frame_crc; /* the one we calculate from the input stream */
	FLAC__uint32 x;

	*got_a_frame = false;

	/* init the CRC */
	frame_crc = 0;
	frame_crc = FLAC__CRC16_UPDATE(decoder->private_->header_warmup[0], frame_crc);
	frame_crc = FLAC__CRC16_UPDATE(decoder->private_->header_warmup[1], frame_crc);
	FLAC__bitreader_reset_read_crc16(decoder->private_->input, (FLAC__uint16)frame_crc);

	if(!read_frame_header_(decoder))
		return false;
	if(decoder->protected_->state == FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC) /* means we didn't sync on a valid header */
		return true;
	if(!allocate_output_(decoder, decoder->private_->frame.header.blocksize, decoder->private_->frame.header.channels))
		return false;
	for(channel = 0; channel < decoder->private_->frame.header.channels; channel++) {
		/*
		 * first figure the correct bits-per-sample of the subframe
		 */
		unsigned bps = decoder->private_->frame.header.bits_per_sample;
		switch(decoder->private_->frame.header.channel_assignment) {
			case FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT:
				/* no adjustment needed */
				break;
			case FLAC__CHANNEL_ASSIGNMENT_LEFT_SIDE:
				FLAC__ASSERT(decoder->private_->frame.header.channels == 2);
				if(channel == 1)
					bps++;
				break;
			case FLAC__CHANNEL_ASSIGNMENT_RIGHT_SIDE:
				FLAC__ASSERT(decoder->private_->frame.header.channels == 2);
				if(channel == 0)
					bps++;
				break;
			case FLAC__CHANNEL_ASSIGNMENT_MID_SIDE:
				FLAC__ASSERT(decoder->private_->frame.header.channels == 2);
				if(channel == 1)
					bps++;
				break;
			default:
				FLAC__ASSERT(0);
		}
		/*
		 * now read it
		 */
		if(!read_subframe_(decoder, channel, bps, do_full_decode))
			return false;
		if(decoder->protected_->state == FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC) /* means bad sync or got corruption */
			return true;
	}
	if(!read_zero_padding_(decoder))
		return false;
	if(decoder->protected_->state == FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC) /* means bad sync or got corruption (i.e. "zero bits" were not all zeroes) */
		return true;

	/*
	 * Read the frame CRC-16 from the footer and check
	 */
	frame_crc = FLAC__bitreader_get_read_crc16(decoder->private_->input);
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__FRAME_FOOTER_CRC_LEN))
		return false; /* read_callback_ sets the state for us */
	if(frame_crc == x) {
		if(do_full_decode) {
			/* Undo any special channel coding */
			switch(decoder->private_->frame.header.channel_assignment) {
				case FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT:
					/* do nothing */
					break;
				case FLAC__CHANNEL_ASSIGNMENT_LEFT_SIDE:
					FLAC__ASSERT(decoder->private_->frame.header.channels == 2);
					for(i = 0; i < decoder->private_->frame.header.blocksize; i++)
						decoder->private_->output[1][i] = decoder->private_->output[0][i] - decoder->private_->output[1][i];
					break;
				case FLAC__CHANNEL_ASSIGNMENT_RIGHT_SIDE:
					FLAC__ASSERT(decoder->private_->frame.header.channels == 2);
					for(i = 0; i < decoder->private_->frame.header.blocksize; i++)
						decoder->private_->output[0][i] += decoder->private_->output[1][i];
					break;
				case FLAC__CHANNEL_ASSIGNMENT_MID_SIDE:
					FLAC__ASSERT(decoder->private_->frame.header.channels == 2);
					for(i = 0; i < decoder->private_->frame.header.blocksize; i++) {
#if 1
						mid = decoder->private_->output[0][i];
						side = decoder->private_->output[1][i];
						mid <<= 1;
						mid |= (side & 1); /* i.e. if 'side' is odd... */
						decoder->private_->output[0][i] = (mid + side) >> 1;
						decoder->private_->output[1][i] = (mid - side) >> 1;
#else
						/* OPT: without 'side' temp variable */
						mid = (decoder->private_->output[0][i] << 1) | (decoder->private_->output[1][i] & 1); /* i.e. if 'side' is odd... */
						decoder->private_->output[0][i] = (mid + decoder->private_->output[1][i]) >> 1;
						decoder->private_->output[1][i] = (mid - decoder->private_->output[1][i]) >> 1;
#endif
					}
					break;
				default:
					FLAC__ASSERT(0);
					break;
			}
		}
	}
	else {
		/* Bad frame, emit error and zero the output signal */
		send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_FRAME_CRC_MISMATCH);
		if(do_full_decode) {
			for(channel = 0; channel < decoder->private_->frame.header.channels; channel++) {
				memset(decoder->private_->output[channel], 0, sizeof(FLAC__int32) * decoder->private_->frame.header.blocksize);
			}
		}
	}

	*got_a_frame = true;

	/* we wait to update fixed_block_size until here, when we're sure we've got a proper frame and hence a correct blocksize */
	if(decoder->private_->next_fixed_block_size)
		decoder->private_->fixed_block_size = decoder->private_->next_fixed_block_size;

	/* put the latest values into the public section of the decoder instance */
	decoder->protected_->channels = decoder->private_->frame.header.channels;
	decoder->protected_->channel_assignment = decoder->private_->frame.header.channel_assignment;
	decoder->protected_->bits_per_sample = decoder->private_->frame.header.bits_per_sample;
	decoder->protected_->sample_rate = decoder->private_->frame.header.sample_rate;
	decoder->protected_->blocksize = decoder->private_->frame.header.blocksize;

	FLAC__ASSERT(decoder->private_->frame.header.number_type == FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER);
	decoder->private_->samples_decoded = decoder->private_->frame.header.number.sample_number + decoder->private_->frame.header.blocksize;

	/* write it */
	if(do_full_decode) {
		if(write_audio_frame_to_client_(decoder, &decoder->private_->frame, (const FLAC__int32 * const *)decoder->private_->output) != FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE)
			return false;
	}

	decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
	return true;
}

FLAC__bool read_frame_header_(FLAC__StreamDecoder *decoder)
{
	FLAC__uint32 x;
	FLAC__uint64 xx;
	unsigned i, blocksize_hint = 0, sample_rate_hint = 0;
	FLAC__byte crc8, raw_header[16]; /* MAGIC NUMBER based on the maximum frame header size, including CRC */
	unsigned raw_header_len;
	FLAC__bool is_unparseable = false;

	FLAC__ASSERT(FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input));

	/* init the raw header with the saved bits from synchronization */
	raw_header[0] = decoder->private_->header_warmup[0];
	raw_header[1] = decoder->private_->header_warmup[1];
	raw_header_len = 2;

	/* check to make sure that reserved bit is 0 */
	if(raw_header[1] & 0x02) /* MAGIC NUMBER */
		is_unparseable = true;

	/*
	 * Note that along the way as we read the header, we look for a sync
	 * code inside.  If we find one it would indicate that our original
	 * sync was bad since there cannot be a sync code in a valid header.
	 *
	 * Three kinds of things can go wrong when reading the frame header:
	 *  1) We may have sync'ed incorrectly and not landed on a frame header.
	 *     If we don't find a sync code, it can end up looking like we read
	 *     a valid but unparseable header, until getting to the frame header
	 *     CRC.  Even then we could get a false positive on the CRC.
	 *  2) We may have sync'ed correctly but on an unparseable frame (from a
	 *     future encoder).
	 *  3) We may be on a damaged frame which appears valid but unparseable.
	 *
	 * For all these reasons, we try and read a complete frame header as
	 * long as it seems valid, even if unparseable, up until the frame
	 * header CRC.
	 */

	/*
	 * read in the raw header as bytes so we can CRC it, and parse it on the way
	 */
	for(i = 0; i < 2; i++) {
		if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 8))
			return false; /* read_callback_ sets the state for us */
		if(x == 0xff) { /* MAGIC NUMBER for the first 8 frame sync bits */
			/* if we get here it means our original sync was erroneous since the sync code cannot appear in the header */
			decoder->private_->lookahead = (FLAC__byte)x;
			decoder->private_->cached = true;
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER);
			decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
			return true;
		}
		raw_header[raw_header_len++] = (FLAC__byte)x;
	}

	switch(x = raw_header[2] >> 4) {
		case 0:
			is_unparseable = true;
			break;
		case 1:
			decoder->private_->frame.header.blocksize = 192;
			break;
		case 2:
		case 3:
		case 4:
		case 5:
			decoder->private_->frame.header.blocksize = 576 << (x-2);
			break;
		case 6:
		case 7:
			blocksize_hint = x;
			break;
		case 8:
		case 9:
		case 10:
		case 11:
		case 12:
		case 13:
		case 14:
		case 15:
			decoder->private_->frame.header.blocksize = 256 << (x-8);
			break;
		default:
			FLAC__ASSERT(0);
			break;
	}

	switch(x = raw_header[2] & 0x0f) {
		case 0:
			if(decoder->private_->has_stream_info)
				decoder->private_->frame.header.sample_rate = decoder->private_->stream_info.data.stream_info.sample_rate;
			else
				is_unparseable = true;
			break;
		case 1:
			decoder->private_->frame.header.sample_rate = 88200;
			break;
		case 2:
			decoder->private_->frame.header.sample_rate = 176400;
			break;
		case 3:
			decoder->private_->frame.header.sample_rate = 192000;
			break;
		case 4:
			decoder->private_->frame.header.sample_rate = 8000;
			break;
		case 5:
			decoder->private_->frame.header.sample_rate = 16000;
			break;
		case 6:
			decoder->private_->frame.header.sample_rate = 22050;
			break;
		case 7:
			decoder->private_->frame.header.sample_rate = 24000;
			break;
		case 8:
			decoder->private_->frame.header.sample_rate = 32000;
			break;
		case 9:
			decoder->private_->frame.header.sample_rate = 44100;
			break;
		case 10:
			decoder->private_->frame.header.sample_rate = 48000;
			break;
		case 11:
			decoder->private_->frame.header.sample_rate = 96000;
			break;
		case 12:
		case 13:
		case 14:
			sample_rate_hint = x;
			break;
		case 15:
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER);
			decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
			return true;
		default:
			FLAC__ASSERT(0);
	}

	x = (unsigned)(raw_header[3] >> 4);
	if(x & 8) {
		decoder->private_->frame.header.channels = 2;
		switch(x & 7) {
			case 0:
				decoder->private_->frame.header.channel_assignment = FLAC__CHANNEL_ASSIGNMENT_LEFT_SIDE;
				break;
			case 1:
				decoder->private_->frame.header.channel_assignment = FLAC__CHANNEL_ASSIGNMENT_RIGHT_SIDE;
				break;
			case 2:
				decoder->private_->frame.header.channel_assignment = FLAC__CHANNEL_ASSIGNMENT_MID_SIDE;
				break;
			default:
				is_unparseable = true;
				break;
		}
	}
	else {
		decoder->private_->frame.header.channels = (unsigned)x + 1;
		decoder->private_->frame.header.channel_assignment = FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT;
	}

	switch(x = (unsigned)(raw_header[3] & 0x0e) >> 1) {
		case 0:
			if(decoder->private_->has_stream_info)
				decoder->private_->frame.header.bits_per_sample = decoder->private_->stream_info.data.stream_info.bits_per_sample;
			else
				is_unparseable = true;
			break;
		case 1:
			decoder->private_->frame.header.bits_per_sample = 8;
			break;
		case 2:
			decoder->private_->frame.header.bits_per_sample = 12;
			break;
		case 4:
			decoder->private_->frame.header.bits_per_sample = 16;
			break;
		case 5:
			decoder->private_->frame.header.bits_per_sample = 20;
			break;
		case 6:
			decoder->private_->frame.header.bits_per_sample = 24;
			break;
		case 3:
		case 7:
			is_unparseable = true;
			break;
		default:
			FLAC__ASSERT(0);
			break;
	}

	/* check to make sure that reserved bit is 0 */
	if(raw_header[3] & 0x01) /* MAGIC NUMBER */
		is_unparseable = true;

	/* read the frame's starting sample number (or frame number as the case may be) */
	if(
		raw_header[1] & 0x01 ||
		/*@@@ this clause is a concession to the old way of doing variable blocksize; the only known implementation is flake and can probably be removed without inconveniencing anyone */
		(decoder->private_->has_stream_info && decoder->private_->stream_info.data.stream_info.min_blocksize != decoder->private_->stream_info.data.stream_info.max_blocksize)
	) { /* variable blocksize */
		if(!FLAC__bitreader_read_utf8_uint64(decoder->private_->input, &xx, raw_header, &raw_header_len))
			return false; /* read_callback_ sets the state for us */
		if(xx == FLAC__U64L(0xffffffffffffffff)) { /* i.e. non-UTF8 code... */
			decoder->private_->lookahead = raw_header[raw_header_len-1]; /* back up as much as we can */
			decoder->private_->cached = true;
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER);
			decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
			return true;
		}
		decoder->private_->frame.header.number_type = FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER;
		decoder->private_->frame.header.number.sample_number = xx;
	}
	else { /* fixed blocksize */
		if(!FLAC__bitreader_read_utf8_uint32(decoder->private_->input, &x, raw_header, &raw_header_len))
			return false; /* read_callback_ sets the state for us */
		if(x == 0xffffffff) { /* i.e. non-UTF8 code... */
			decoder->private_->lookahead = raw_header[raw_header_len-1]; /* back up as much as we can */
			decoder->private_->cached = true;
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER);
			decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
			return true;
		}
		decoder->private_->frame.header.number_type = FLAC__FRAME_NUMBER_TYPE_FRAME_NUMBER;
		decoder->private_->frame.header.number.frame_number = x;
	}

	if(blocksize_hint) {
		if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 8))
			return false; /* read_callback_ sets the state for us */
		raw_header[raw_header_len++] = (FLAC__byte)x;
		if(blocksize_hint == 7) {
			FLAC__uint32 _x;
			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &_x, 8))
				return false; /* read_callback_ sets the state for us */
			raw_header[raw_header_len++] = (FLAC__byte)_x;
			x = (x << 8) | _x;
		}
		decoder->private_->frame.header.blocksize = x+1;
	}

	if(sample_rate_hint) {
		if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 8))
			return false; /* read_callback_ sets the state for us */
		raw_header[raw_header_len++] = (FLAC__byte)x;
		if(sample_rate_hint != 12) {
			FLAC__uint32 _x;
			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &_x, 8))
				return false; /* read_callback_ sets the state for us */
			raw_header[raw_header_len++] = (FLAC__byte)_x;
			x = (x << 8) | _x;
		}
		if(sample_rate_hint == 12)
			decoder->private_->frame.header.sample_rate = x*1000;
		else if(sample_rate_hint == 13)
			decoder->private_->frame.header.sample_rate = x;
		else
			decoder->private_->frame.header.sample_rate = x*10;
	}

	/* read the CRC-8 byte */
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 8))
		return false; /* read_callback_ sets the state for us */
	crc8 = (FLAC__byte)x;

	if(FLAC__crc8(raw_header, raw_header_len) != crc8) {
		send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER);
		decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
		return true;
	}

	/* calculate the sample number from the frame number if needed */
	decoder->private_->next_fixed_block_size = 0;
	if(decoder->private_->frame.header.number_type == FLAC__FRAME_NUMBER_TYPE_FRAME_NUMBER) {
		x = decoder->private_->frame.header.number.frame_number;
		decoder->private_->frame.header.number_type = FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER;
		if(decoder->private_->fixed_block_size)
			decoder->private_->frame.header.number.sample_number = (FLAC__uint64)decoder->private_->fixed_block_size * (FLAC__uint64)x;
		else if(decoder->private_->has_stream_info) {
			if(decoder->private_->stream_info.data.stream_info.min_blocksize == decoder->private_->stream_info.data.stream_info.max_blocksize) {
				decoder->private_->frame.header.number.sample_number = (FLAC__uint64)decoder->private_->stream_info.data.stream_info.min_blocksize * (FLAC__uint64)x;
				decoder->private_->next_fixed_block_size = decoder->private_->stream_info.data.stream_info.max_blocksize;
			}
			else
				is_unparseable = true;
		}
		else if(x == 0) {
			decoder->private_->frame.header.number.sample_number = 0;
			decoder->private_->next_fixed_block_size = decoder->private_->frame.header.blocksize;
		}
		else {
			/* can only get here if the stream has invalid frame numbering and no STREAMINFO, so assume it's not the last (possibly short) frame */
			decoder->private_->frame.header.number.sample_number = (FLAC__uint64)decoder->private_->frame.header.blocksize * (FLAC__uint64)x;
		}
	}

	if(is_unparseable) {
		send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_UNPARSEABLE_STREAM);
		decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
		return true;
	}

	return true;
}

FLAC__bool read_subframe_(FLAC__StreamDecoder *decoder, unsigned channel, unsigned bps, FLAC__bool do_full_decode)
{
	FLAC__uint32 x;
	FLAC__bool wasted_bits;
	unsigned i;

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 8)) /* MAGIC NUMBER */
		return false; /* read_callback_ sets the state for us */

	wasted_bits = (x & 1);
	x &= 0xfe;

	if(wasted_bits) {
		unsigned u;
		if(!FLAC__bitreader_read_unary_unsigned(decoder->private_->input, &u))
			return false; /* read_callback_ sets the state for us */
		decoder->private_->frame.subframes[channel].wasted_bits = u+1;
		bps -= decoder->private_->frame.subframes[channel].wasted_bits;
	}
	else
		decoder->private_->frame.subframes[channel].wasted_bits = 0;

	/*
	 * Lots of magic numbers here
	 */
	if(x & 0x80) {
		send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC);
		decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
		return true;
	}
	else if(x == 0) {
		if(!read_subframe_constant_(decoder, channel, bps, do_full_decode))
			return false;
	}
	else if(x == 2) {
		if(!read_subframe_verbatim_(decoder, channel, bps, do_full_decode))
			return false;
	}
	else if(x < 16) {
		send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_UNPARSEABLE_STREAM);
		decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
		return true;
	}
	else if(x <= 24) {
		if(!read_subframe_fixed_(decoder, channel, bps, (x>>1)&7, do_full_decode))
			return false;
		if(decoder->protected_->state == FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC) /* means bad sync or got corruption */
			return true;
	}
	else if(x < 64) {
		send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_UNPARSEABLE_STREAM);
		decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
		return true;
	}
	else {
		if(!read_subframe_lpc_(decoder, channel, bps, ((x>>1)&31)+1, do_full_decode))
			return false;
		if(decoder->protected_->state == FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC) /* means bad sync or got corruption */
			return true;
	}

	if(wasted_bits && do_full_decode) {
		x = decoder->private_->frame.subframes[channel].wasted_bits;
		for(i = 0; i < decoder->private_->frame.header.blocksize; i++)
			decoder->private_->output[channel][i] <<= x;
	}

	return true;
}

FLAC__bool read_subframe_constant_(FLAC__StreamDecoder *decoder, unsigned channel, unsigned bps, FLAC__bool do_full_decode)
{
	FLAC__Subframe_Constant *subframe = &decoder->private_->frame.subframes[channel].data.constant;
	FLAC__int32 x;
	unsigned i;
	FLAC__int32 *output = decoder->private_->output[channel];

	decoder->private_->frame.subframes[channel].type = FLAC__SUBFRAME_TYPE_CONSTANT;

	if(!FLAC__bitreader_read_raw_int32(decoder->private_->input, &x, bps))
		return false; /* read_callback_ sets the state for us */

	subframe->value = x;

	/* decode the subframe */
	if(do_full_decode) {
		for(i = 0; i < decoder->private_->frame.header.blocksize; i++)
			output[i] = x;
	}

	return true;
}

FLAC__bool read_subframe_fixed_(FLAC__StreamDecoder *decoder, unsigned channel, unsigned bps, const unsigned order, FLAC__bool do_full_decode)
{
	FLAC__Subframe_Fixed *subframe = &decoder->private_->frame.subframes[channel].data.fixed;
	FLAC__int32 i32;
	FLAC__uint32 u32;
	unsigned u;

	decoder->private_->frame.subframes[channel].type = FLAC__SUBFRAME_TYPE_FIXED;

	subframe->residual = decoder->private_->residual[channel];
	subframe->order = order;

	/* read warm-up samples */
	for(u = 0; u < order; u++) {
		if(!FLAC__bitreader_read_raw_int32(decoder->private_->input, &i32, bps))
			return false; /* read_callback_ sets the state for us */
		subframe->warmup[u] = i32;
	}

	/* read entropy coding method info */
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &u32, FLAC__ENTROPY_CODING_METHOD_TYPE_LEN))
		return false; /* read_callback_ sets the state for us */
	subframe->entropy_coding_method.type = (FLAC__EntropyCodingMethodType)u32;
	switch(subframe->entropy_coding_method.type) {
		case FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE:
		case FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2:
			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &u32, FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ORDER_LEN))
				return false; /* read_callback_ sets the state for us */
			subframe->entropy_coding_method.data.partitioned_rice.order = u32;
			subframe->entropy_coding_method.data.partitioned_rice.contents = &decoder->private_->partitioned_rice_contents[channel];
			break;
		default:
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_UNPARSEABLE_STREAM);
			decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
			return true;
	}

	/* read residual */
	switch(subframe->entropy_coding_method.type) {
		case FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE:
		case FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2:
			if(!read_residual_partitioned_rice_(decoder, order, subframe->entropy_coding_method.data.partitioned_rice.order, &decoder->private_->partitioned_rice_contents[channel], decoder->private_->residual[channel], /*is_extended=*/subframe->entropy_coding_method.type == FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2))
				return false;
			break;
		default:
			FLAC__ASSERT(0);
	}

	/* decode the subframe */
	if(do_full_decode) {
		memcpy(decoder->private_->output[channel], subframe->warmup, sizeof(FLAC__int32) * order);
		FLAC__fixed_restore_signal(decoder->private_->residual[channel], decoder->private_->frame.header.blocksize-order, order, decoder->private_->output[channel]+order);
	}

	return true;
}

FLAC__bool read_subframe_lpc_(FLAC__StreamDecoder *decoder, unsigned channel, unsigned bps, const unsigned order, FLAC__bool do_full_decode)
{
	FLAC__Subframe_LPC *subframe = &decoder->private_->frame.subframes[channel].data.lpc;
	FLAC__int32 i32;
	FLAC__uint32 u32;
	unsigned u;

	decoder->private_->frame.subframes[channel].type = FLAC__SUBFRAME_TYPE_LPC;

	subframe->residual = decoder->private_->residual[channel];
	subframe->order = order;

	/* read warm-up samples */
	for(u = 0; u < order; u++) {
		if(!FLAC__bitreader_read_raw_int32(decoder->private_->input, &i32, bps))
			return false; /* read_callback_ sets the state for us */
		subframe->warmup[u] = i32;
	}

	/* read qlp coeff precision */
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &u32, FLAC__SUBFRAME_LPC_QLP_COEFF_PRECISION_LEN))
		return false; /* read_callback_ sets the state for us */
	if(u32 == (1u << FLAC__SUBFRAME_LPC_QLP_COEFF_PRECISION_LEN) - 1) {
		send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC);
		decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
		return true;
	}
	subframe->qlp_coeff_precision = u32+1;

	/* read qlp shift */
	if(!FLAC__bitreader_read_raw_int32(decoder->private_->input, &i32, FLAC__SUBFRAME_LPC_QLP_SHIFT_LEN))
		return false; /* read_callback_ sets the state for us */
	subframe->quantization_level = i32;

	/* read quantized lp coefficiencts */
	for(u = 0; u < order; u++) {
		if(!FLAC__bitreader_read_raw_int32(decoder->private_->input, &i32, subframe->qlp_coeff_precision))
			return false; /* read_callback_ sets the state for us */
		subframe->qlp_coeff[u] = i32;
	}

	/* read entropy coding method info */
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &u32, FLAC__ENTROPY_CODING_METHOD_TYPE_LEN))
		return false; /* read_callback_ sets the state for us */
	subframe->entropy_coding_method.type = (FLAC__EntropyCodingMethodType)u32;
	switch(subframe->entropy_coding_method.type) {
		case FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE:
		case FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2:
			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &u32, FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ORDER_LEN))
				return false; /* read_callback_ sets the state for us */
			subframe->entropy_coding_method.data.partitioned_rice.order = u32;
			subframe->entropy_coding_method.data.partitioned_rice.contents = &decoder->private_->partitioned_rice_contents[channel];
			break;
		default:
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_UNPARSEABLE_STREAM);
			decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
			return true;
	}

	/* read residual */
	switch(subframe->entropy_coding_method.type) {
		case FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE:
		case FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2:
			if(!read_residual_partitioned_rice_(decoder, order, subframe->entropy_coding_method.data.partitioned_rice.order, &decoder->private_->partitioned_rice_contents[channel], decoder->private_->residual[channel], /*is_extended=*/subframe->entropy_coding_method.type == FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2))
				return false;
			break;
		default:
			FLAC__ASSERT(0);
	}

	/* decode the subframe */
	if(do_full_decode) {
		memcpy(decoder->private_->output[channel], subframe->warmup, sizeof(FLAC__int32) * order);
		/*@@@@@@ technically not pessimistic enough, should be more like
		if( (FLAC__uint64)order * ((((FLAC__uint64)1)<<bps)-1) * ((1<<subframe->qlp_coeff_precision)-1) < (((FLAC__uint64)-1) << 32) )
		*/
		if(bps + subframe->qlp_coeff_precision + FLAC__bitmath_ilog2(order) <= 32)
			if(bps <= 16 && subframe->qlp_coeff_precision <= 16) {
				if(order <= 8)
					decoder->private_->local_lpc_restore_signal_16bit_order8(decoder->private_->residual[channel], decoder->private_->frame.header.blocksize-order, subframe->qlp_coeff, order, subframe->quantization_level, decoder->private_->output[channel]+order);
				else
					decoder->private_->local_lpc_restore_signal_16bit(decoder->private_->residual[channel], decoder->private_->frame.header.blocksize-order, subframe->qlp_coeff, order, subframe->quantization_level, decoder->private_->output[channel]+order);
			}
			else
				decoder->private_->local_lpc_restore_signal(decoder->private_->residual[channel], decoder->private_->frame.header.blocksize-order, subframe->qlp_coeff, order, subframe->quantization_level, decoder->private_->output[channel]+order);
		else
			decoder->private_->local_lpc_restore_signal_64bit(decoder->private_->residual[channel], decoder->private_->frame.header.blocksize-order, subframe->qlp_coeff, order, subframe->quantization_level, decoder->private_->output[channel]+order);
	}

	return true;
}

FLAC__bool read_subframe_verbatim_(FLAC__StreamDecoder *decoder, unsigned channel, unsigned bps, FLAC__bool do_full_decode)
{
	FLAC__Subframe_Verbatim *subframe = &decoder->private_->frame.subframes[channel].data.verbatim;
	FLAC__int32 x, *residual = decoder->private_->residual[channel];
	unsigned i;

	decoder->private_->frame.subframes[channel].type = FLAC__SUBFRAME_TYPE_VERBATIM;

	subframe->data = residual;

	for(i = 0; i < decoder->private_->frame.header.blocksize; i++) {
		if(!FLAC__bitreader_read_raw_int32(decoder->private_->input, &x, bps))
			return false; /* read_callback_ sets the state for us */
		residual[i] = x;
	}

	/* decode the subframe */
	if(do_full_decode)
		memcpy(decoder->private_->output[channel], subframe->data, sizeof(FLAC__int32) * decoder->private_->frame.header.blocksize);

	return true;
}

FLAC__bool read_residual_partitioned_rice_(FLAC__StreamDecoder *decoder, unsigned predictor_order, unsigned partition_order, FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents, FLAC__int32 *residual, FLAC__bool is_extended)
{
	FLAC__uint32 rice_parameter;
	int i;
	unsigned partition, sample, u;
	const unsigned partitions = 1u << partition_order;
	const unsigned partition_samples = partition_order > 0? decoder->private_->frame.header.blocksize >> partition_order : decoder->private_->frame.header.blocksize - predictor_order;
	const unsigned plen = is_extended? FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2_PARAMETER_LEN : FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_PARAMETER_LEN;
	const unsigned pesc = is_extended? FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2_ESCAPE_PARAMETER : FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER;

	/* sanity checks */
	if(partition_order == 0) {
		if(decoder->private_->frame.header.blocksize < predictor_order) {
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC);
			decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
			return true;
		}
	}
	else {
		if(partition_samples < predictor_order) {
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC);
			decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
			return true;
		}
	}

	if(!FLAC__format_entropy_coding_method_partitioned_rice_contents_ensure_size(partitioned_rice_contents, max(6, partition_order))) {
		decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}

	sample = 0;
	for(partition = 0; partition < partitions; partition++) {
		if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &rice_parameter, plen))
			return false; /* read_callback_ sets the state for us */
		partitioned_rice_contents->parameters[partition] = rice_parameter;
		if(rice_parameter < pesc) {
			partitioned_rice_contents->raw_bits[partition] = 0;
			u = (partition_order == 0 || partition > 0)? partition_samples : partition_samples - predictor_order;
			if(!decoder->private_->local_bitreader_read_rice_signed_block(decoder->private_->input, residual + sample, u, rice_parameter))
				return false; /* read_callback_ sets the state for us */
			sample += u;
		}
		else {
			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &rice_parameter, FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_RAW_LEN))
				return false; /* read_callback_ sets the state for us */
			partitioned_rice_contents->raw_bits[partition] = rice_parameter;
			for(u = (partition_order == 0 || partition > 0)? 0 : predictor_order; u < partition_samples; u++, sample++) {
				if(!FLAC__bitreader_read_raw_int32(decoder->private_->input, &i, rice_parameter))
					return false; /* read_callback_ sets the state for us */
				residual[sample] = i;
			}
		}
	}

	return true;
}

FLAC__bool read_zero_padding_(FLAC__StreamDecoder *decoder)
{
	if(!FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input)) {
		FLAC__uint32 zero = 0;
		if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &zero, FLAC__bitreader_bits_left_for_byte_alignment(decoder->private_->input)))
			return false; /* read_callback_ sets the state for us */
		if(zero != 0) {
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC);
			decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
		}
	}
	return true;
}

FLAC__bool read_callback_(FLAC__byte buffer[], size_t *bytes, void *client_data)
{
	FLAC__StreamDecoder *decoder = (FLAC__StreamDecoder *)client_data;

	if(
#if FLAC__HAS_OGG
		/* see [1] HACK NOTE below for why we don't call the eof_callback when decoding Ogg FLAC */
		!decoder->private_->is_ogg &&
#endif
		decoder->private_->eof_callback && decoder->private_->eof_callback(decoder, decoder->private_->client_data)
	) {
		*bytes = 0;
		decoder->protected_->state = FLAC__STREAM_DECODER_END_OF_STREAM;
		return false;
	}
	else if(*bytes > 0) {
		/* While seeking, it is possible for our seek to land in the
		 * middle of audio data that looks exactly like a frame header
		 * from a future version of an encoder.  When that happens, our
		 * error callback will get an
		 * FLAC__STREAM_DECODER_UNPARSEABLE_STREAM and increment its
		 * unparseable_frame_count.  But there is a remote possibility
		 * that it is properly synced at such a "future-codec frame",
		 * so to make sure, we wait to see many "unparseable" errors in
		 * a row before bailing out.
		 */
		if(decoder->private_->is_seeking && decoder->private_->unparseable_frame_count > 20) {
			decoder->protected_->state = FLAC__STREAM_DECODER_ABORTED;
			return false;
		}
		else {
			const FLAC__StreamDecoderReadStatus status =
#if FLAC__HAS_OGG
				decoder->private_->is_ogg?
				read_callback_ogg_aspect_(decoder, buffer, bytes) :
#endif
				decoder->private_->read_callback(decoder, buffer, bytes, decoder->private_->client_data)
			;
			if(status == FLAC__STREAM_DECODER_READ_STATUS_ABORT) {
				decoder->protected_->state = FLAC__STREAM_DECODER_ABORTED;
				return false;
			}
			else if(*bytes == 0) {
				if(
					status == FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM ||
					(
#if FLAC__HAS_OGG
						/* see [1] HACK NOTE below for why we don't call the eof_callback when decoding Ogg FLAC */
						!decoder->private_->is_ogg &&
#endif
						decoder->private_->eof_callback && decoder->private_->eof_callback(decoder, decoder->private_->client_data)
					)
				) {
					decoder->protected_->state = FLAC__STREAM_DECODER_END_OF_STREAM;
					return false;
				}
				else
					return true;
			}
			else
				return true;
		}
	}
	else {
		/* abort to avoid a deadlock */
		decoder->protected_->state = FLAC__STREAM_DECODER_ABORTED;
		return false;
	}
	/* [1] @@@ HACK NOTE: The end-of-stream checking has to be hacked around
	 * for Ogg FLAC.  This is because the ogg decoder aspect can lose sync
	 * and at the same time hit the end of the stream (for example, seeking
	 * to a point that is after the beginning of the last Ogg page).  There
	 * is no way to report an Ogg sync loss through the callbacks (see note
	 * in read_callback_ogg_aspect_()) so it returns CONTINUE with *bytes==0.
	 * So to keep the decoder from stopping at this point we gate the call
	 * to the eof_callback and let the Ogg decoder aspect set the
	 * end-of-stream state when it is needed.
	 */
}

#if FLAC__HAS_OGG
FLAC__StreamDecoderReadStatus read_callback_ogg_aspect_(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes)
{
	switch(FLAC__ogg_decoder_aspect_read_callback_wrapper(&decoder->protected_->ogg_decoder_aspect, buffer, bytes, read_callback_proxy_, decoder, decoder->private_->client_data)) {
		case FLAC__OGG_DECODER_ASPECT_READ_STATUS_OK:
			return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
		/* we don't really have a way to handle lost sync via read
		 * callback so we'll let it pass and let the underlying
		 * FLAC decoder catch the error
		 */
		case FLAC__OGG_DECODER_ASPECT_READ_STATUS_LOST_SYNC:
			return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
		case FLAC__OGG_DECODER_ASPECT_READ_STATUS_END_OF_STREAM:
			return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
		case FLAC__OGG_DECODER_ASPECT_READ_STATUS_NOT_FLAC:
		case FLAC__OGG_DECODER_ASPECT_READ_STATUS_UNSUPPORTED_MAPPING_VERSION:
		case FLAC__OGG_DECODER_ASPECT_READ_STATUS_ABORT:
		case FLAC__OGG_DECODER_ASPECT_READ_STATUS_ERROR:
		case FLAC__OGG_DECODER_ASPECT_READ_STATUS_MEMORY_ALLOCATION_ERROR:
			return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
		default:
			FLAC__ASSERT(0);
			/* double protection */
			return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
	}
}

FLAC__OggDecoderAspectReadStatus read_callback_proxy_(const void *void_decoder, FLAC__byte buffer[], size_t *bytes, void *client_data)
{
	FLAC__StreamDecoder *decoder = (FLAC__StreamDecoder*)void_decoder;

	switch(decoder->private_->read_callback(decoder, buffer, bytes, client_data)) {
		case FLAC__STREAM_DECODER_READ_STATUS_CONTINUE:
			return FLAC__OGG_DECODER_ASPECT_READ_STATUS_OK;
		case FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM:
			return FLAC__OGG_DECODER_ASPECT_READ_STATUS_END_OF_STREAM;
		case FLAC__STREAM_DECODER_READ_STATUS_ABORT:
			return FLAC__OGG_DECODER_ASPECT_READ_STATUS_ABORT;
		default:
			/* double protection: */
			FLAC__ASSERT(0);
			return FLAC__OGG_DECODER_ASPECT_READ_STATUS_ABORT;
	}
}
#endif

FLAC__StreamDecoderWriteStatus write_audio_frame_to_client_(FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[])
{
	if(decoder->private_->is_seeking) {
		FLAC__uint64 this_frame_sample = frame->header.number.sample_number;
		FLAC__uint64 next_frame_sample = this_frame_sample + (FLAC__uint64)frame->header.blocksize;
		FLAC__uint64 target_sample = decoder->private_->target_sample;

		FLAC__ASSERT(frame->header.number_type == FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER);

#if FLAC__HAS_OGG
		decoder->private_->got_a_frame = true;
#endif
		decoder->private_->last_frame = *frame; /* save the frame */
		if(this_frame_sample <= target_sample && target_sample < next_frame_sample) { /* we hit our target frame */
			unsigned delta = (unsigned)(target_sample - this_frame_sample);
			/* kick out of seek mode */
			decoder->private_->is_seeking = false;
			/* shift out the samples before target_sample */
			if(delta > 0) {
				unsigned channel;
				const FLAC__int32 *newbuffer[FLAC__MAX_CHANNELS];
				for(channel = 0; channel < frame->header.channels; channel++)
					newbuffer[channel] = buffer[channel] + delta;
				decoder->private_->last_frame.header.blocksize -= delta;
				decoder->private_->last_frame.header.number.sample_number += (FLAC__uint64)delta;
				/* write the relevant samples */
				return decoder->private_->write_callback(decoder, &decoder->private_->last_frame, newbuffer, decoder->private_->client_data);
			}
			else {
				/* write the relevant samples */
				return decoder->private_->write_callback(decoder, frame, buffer, decoder->private_->client_data);
			}
		}
		else {
			return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
		}
	}
	else {
		/*
		 * If we never got STREAMINFO, turn off MD5 checking to save
		 * cycles since we don't have a sum to compare to anyway
		 */
		if(!decoder->private_->has_stream_info)
			decoder->private_->do_md5_checking = false;
		if(decoder->private_->do_md5_checking) {
			if(!FLAC__MD5Accumulate(&decoder->private_->md5context, buffer, frame->header.channels, frame->header.blocksize, (frame->header.bits_per_sample+7) / 8))
				return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
		}
		return decoder->private_->write_callback(decoder, frame, buffer, decoder->private_->client_data);
	}
}

void send_error_to_client_(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status)
{
	if(!decoder->private_->is_seeking)
		decoder->private_->error_callback(decoder, status, decoder->private_->client_data);
	else if(status == FLAC__STREAM_DECODER_ERROR_STATUS_UNPARSEABLE_STREAM)
		decoder->private_->unparseable_frame_count++;
}

FLAC__bool seek_to_absolute_sample_(FLAC__StreamDecoder *decoder, FLAC__uint64 stream_length, FLAC__uint64 target_sample)
{
	FLAC__uint64 first_frame_offset = decoder->private_->first_frame_offset, lower_bound, upper_bound, lower_bound_sample, upper_bound_sample, this_frame_sample;
	FLAC__int64 pos = -1;
	int i;
	unsigned approx_bytes_per_frame;
	FLAC__bool first_seek = true;
	const FLAC__uint64 total_samples = FLAC__stream_decoder_get_total_samples(decoder);
	const unsigned min_blocksize = decoder->private_->stream_info.data.stream_info.min_blocksize;
	const unsigned max_blocksize = decoder->private_->stream_info.data.stream_info.max_blocksize;
	const unsigned max_framesize = decoder->private_->stream_info.data.stream_info.max_framesize;
	const unsigned min_framesize = decoder->private_->stream_info.data.stream_info.min_framesize;
	/* take these from the current frame in case they've changed mid-stream */
	unsigned channels = FLAC__stream_decoder_get_channels(decoder);
	unsigned bps = FLAC__stream_decoder_get_bits_per_sample(decoder);
	const FLAC__StreamMetadata_SeekTable *seek_table = decoder->private_->has_seek_table? &decoder->private_->seek_table.data.seek_table : 0;

	/* use values from stream info if we didn't decode a frame */
	if(channels == 0)
		channels = decoder->private_->stream_info.data.stream_info.channels;
	if(bps == 0)
		bps = decoder->private_->stream_info.data.stream_info.bits_per_sample;

	/* we are just guessing here */
	if(max_framesize > 0)
		approx_bytes_per_frame = (max_framesize + min_framesize) / 2 + 1;
	/*
	 * Check if it's a known fixed-blocksize stream.  Note that though
	 * the spec doesn't allow zeroes in the STREAMINFO block, we may
	 * never get a STREAMINFO block when decoding so the value of
	 * min_blocksize might be zero.
	 */
	else if(min_blocksize == max_blocksize && min_blocksize > 0) {
		/* note there are no () around 'bps/8' to keep precision up since it's an integer calulation */
		approx_bytes_per_frame = min_blocksize * channels * bps/8 + 64;
	}
	else
		approx_bytes_per_frame = 4096 * channels * bps/8 + 64;

	/*
	 * First, we set an upper and lower bound on where in the
	 * stream we will search.  For now we assume the worst case
	 * scenario, which is our best guess at the beginning of
	 * the first frame and end of the stream.
	 */
	lower_bound = first_frame_offset;
	lower_bound_sample = 0;
	upper_bound = stream_length;
	upper_bound_sample = total_samples > 0 ? total_samples : target_sample /*estimate it*/;

	/*
	 * Now we refine the bounds if we have a seektable with
	 * suitable points.  Note that according to the spec they
	 * must be ordered by ascending sample number.
	 *
	 * Note: to protect against invalid seek tables we will ignore points
	 * that have frame_samples==0 or sample_number>=total_samples
	 */
	if(seek_table) {
		FLAC__uint64 new_lower_bound = lower_bound;
		FLAC__uint64 new_upper_bound = upper_bound;
		FLAC__uint64 new_lower_bound_sample = lower_bound_sample;
		FLAC__uint64 new_upper_bound_sample = upper_bound_sample;

		/* find the closest seek point <= target_sample, if it exists */
		for(i = (int)seek_table->num_points - 1; i >= 0; i--) {
			if(
				seek_table->points[i].sample_number != FLAC__STREAM_METADATA_SEEKPOINT_PLACEHOLDER &&
				seek_table->points[i].frame_samples > 0 && /* defense against bad seekpoints */
				(total_samples <= 0 || seek_table->points[i].sample_number < total_samples) && /* defense against bad seekpoints */
				seek_table->points[i].sample_number <= target_sample
			)
				break;
		}
		if(i >= 0) { /* i.e. we found a suitable seek point... */
			new_lower_bound = first_frame_offset + seek_table->points[i].stream_offset;
			new_lower_bound_sample = seek_table->points[i].sample_number;
		}

		/* find the closest seek point > target_sample, if it exists */
		for(i = 0; i < (int)seek_table->num_points; i++) {
			if(
				seek_table->points[i].sample_number != FLAC__STREAM_METADATA_SEEKPOINT_PLACEHOLDER &&
				seek_table->points[i].frame_samples > 0 && /* defense against bad seekpoints */
				(total_samples <= 0 || seek_table->points[i].sample_number < total_samples) && /* defense against bad seekpoints */
				seek_table->points[i].sample_number > target_sample
			)
				break;
		}
		if(i < (int)seek_table->num_points) { /* i.e. we found a suitable seek point... */
			new_upper_bound = first_frame_offset + seek_table->points[i].stream_offset;
			new_upper_bound_sample = seek_table->points[i].sample_number;
		}
		/* final protection against unsorted seek tables; keep original values if bogus */
		if(new_upper_bound >= new_lower_bound) {
			lower_bound = new_lower_bound;
			upper_bound = new_upper_bound;
			lower_bound_sample = new_lower_bound_sample;
			upper_bound_sample = new_upper_bound_sample;
		}
	}

	FLAC__ASSERT(upper_bound_sample >= lower_bound_sample);
	/* there are 2 insidious ways that the following equality occurs, which
	 * we need to fix:
	 *  1) total_samples is 0 (unknown) and target_sample is 0
	 *  2) total_samples is 0 (unknown) and target_sample happens to be
	 *     exactly equal to the last seek point in the seek table; this
	 *     means there is no seek point above it, and upper_bound_samples
	 *     remains equal to the estimate (of target_samples) we made above
	 * in either case it does not hurt to move upper_bound_sample up by 1
	 */
	if(upper_bound_sample == lower_bound_sample)
		upper_bound_sample++;

	decoder->private_->target_sample = target_sample;
	while(1) {
		/* check if the bounds are still ok */
		if (lower_bound_sample >= upper_bound_sample || lower_bound > upper_bound) {
			decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
			return false;
		}
#ifndef FLAC__INTEGER_ONLY_LIBRARY
#if defined _MSC_VER || defined __MINGW32__
		/* with VC++ you have to spoon feed it the casting */
		pos = (FLAC__int64)lower_bound + (FLAC__int64)((FLAC__double)(FLAC__int64)(target_sample - lower_bound_sample) / (FLAC__double)(FLAC__int64)(upper_bound_sample - lower_bound_sample) * (FLAC__double)(FLAC__int64)(upper_bound - lower_bound)) - approx_bytes_per_frame;
#else
		pos = (FLAC__int64)lower_bound + (FLAC__int64)((FLAC__double)(target_sample - lower_bound_sample) / (FLAC__double)(upper_bound_sample - lower_bound_sample) * (FLAC__double)(upper_bound - lower_bound)) - approx_bytes_per_frame;
#endif
#else
		/* a little less accurate: */
		if(upper_bound - lower_bound < 0xffffffff)
			pos = (FLAC__int64)lower_bound + (FLAC__int64)(((target_sample - lower_bound_sample) * (upper_bound - lower_bound)) / (upper_bound_sample - lower_bound_sample)) - approx_bytes_per_frame;
		else /* @@@ WATCHOUT, ~2TB limit */
			pos = (FLAC__int64)lower_bound + (FLAC__int64)((((target_sample - lower_bound_sample)>>8) * ((upper_bound - lower_bound)>>8)) / ((upper_bound_sample - lower_bound_sample)>>16)) - approx_bytes_per_frame;
#endif
		if(pos >= (FLAC__int64)upper_bound)
			pos = (FLAC__int64)upper_bound - 1;
		if(pos < (FLAC__int64)lower_bound)
			pos = (FLAC__int64)lower_bound;
		if(decoder->private_->seek_callback(decoder, (FLAC__uint64)pos, decoder->private_->client_data) != FLAC__STREAM_DECODER_SEEK_STATUS_OK) {
			decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
			return false;
		}
		if(!FLAC__stream_decoder_flush(decoder)) {
			/* above call sets the state for us */
			return false;
		}
		/* Now we need to get a frame.  First we need to reset our
		 * unparseable_frame_count; if we get too many unparseable
		 * frames in a row, the read callback will return
		 * FLAC__STREAM_DECODER_READ_STATUS_ABORT, causing
		 * FLAC__stream_decoder_process_single() to return false.
		 */
		decoder->private_->unparseable_frame_count = 0;
		if(!FLAC__stream_decoder_process_single(decoder)) {
			decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
			return false;
		}
		/* our write callback will change the state when it gets to the target frame */
		/* actually, we could have got_a_frame if our decoder is at FLAC__STREAM_DECODER_END_OF_STREAM so we need to check for that also */
#if 0
		/*@@@@@@ used to be the following; not clear if the check for end of stream is needed anymore */
		if(decoder->protected_->state != FLAC__SEEKABLE_STREAM_DECODER_SEEKING && decoder->protected_->state != FLAC__STREAM_DECODER_END_OF_STREAM)
			break;
#endif
		if(!decoder->private_->is_seeking)
			break;

		FLAC__ASSERT(decoder->private_->last_frame.header.number_type == FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER);
		this_frame_sample = decoder->private_->last_frame.header.number.sample_number;

		if (0 == decoder->private_->samples_decoded || (this_frame_sample + decoder->private_->last_frame.header.blocksize >= upper_bound_sample && !first_seek)) {
			if (pos == (FLAC__int64)lower_bound) {
				/* can't move back any more than the first frame, something is fatally wrong */
				decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
				return false;
			}
			/* our last move backwards wasn't big enough, try again */
			approx_bytes_per_frame = approx_bytes_per_frame? approx_bytes_per_frame * 2 : 16;
			continue;	
		}
		/* allow one seek over upper bound, so we can get a correct upper_bound_sample for streams with unknown total_samples */
		first_seek = false;
		
		/* make sure we are not seeking in corrupted stream */
		if (this_frame_sample < lower_bound_sample) {
			decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
			return false;
		}

		/* we need to narrow the search */
		if(target_sample < this_frame_sample) {
			upper_bound_sample = this_frame_sample + decoder->private_->last_frame.header.blocksize;
/*@@@@@@ what will decode position be if at end of stream? */
			if(!FLAC__stream_decoder_get_decode_position(decoder, &upper_bound)) {
				decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
				return false;
			}
			approx_bytes_per_frame = (unsigned)(2 * (upper_bound - pos) / 3 + 16);
		}
		else { /* target_sample >= this_frame_sample + this frame's blocksize */
			lower_bound_sample = this_frame_sample + decoder->private_->last_frame.header.blocksize;
			if(!FLAC__stream_decoder_get_decode_position(decoder, &lower_bound)) {
				decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
				return false;
			}
			approx_bytes_per_frame = (unsigned)(2 * (lower_bound - pos) / 3 + 16);
		}
	}

	return true;
}

#if FLAC__HAS_OGG
FLAC__bool seek_to_absolute_sample_ogg_(FLAC__StreamDecoder *decoder, FLAC__uint64 stream_length, FLAC__uint64 target_sample)
{
	FLAC__uint64 left_pos = 0, right_pos = stream_length;
	FLAC__uint64 left_sample = 0, right_sample = FLAC__stream_decoder_get_total_samples(decoder);
	FLAC__uint64 this_frame_sample = (FLAC__uint64)0 - 1;
	FLAC__uint64 pos = 0; /* only initialized to avoid compiler warning */
	FLAC__bool did_a_seek;
	unsigned iteration = 0;

	/* In the first iterations, we will calculate the target byte position 
	 * by the distance from the target sample to left_sample and
	 * right_sample (let's call it "proportional search").  After that, we
	 * will switch to binary search.
	 */
	unsigned BINARY_SEARCH_AFTER_ITERATION = 2;

	/* We will switch to a linear search once our current sample is less
	 * than this number of samples ahead of the target sample
	 */
	static const FLAC__uint64 LINEAR_SEARCH_WITHIN_SAMPLES = FLAC__MAX_BLOCK_SIZE * 2;

	/* If the total number of samples is unknown, use a large value, and
	 * force binary search immediately.
	 */
	if(right_sample == 0) {
		right_sample = (FLAC__uint64)(-1);
		BINARY_SEARCH_AFTER_ITERATION = 0;
	}

	decoder->private_->target_sample = target_sample;
	for( ; ; iteration++) {
		if (iteration == 0 || this_frame_sample > target_sample || target_sample - this_frame_sample > LINEAR_SEARCH_WITHIN_SAMPLES) {
			if (iteration >= BINARY_SEARCH_AFTER_ITERATION) {
				pos = (right_pos + left_pos) / 2;
			}
			else {
#ifndef FLAC__INTEGER_ONLY_LIBRARY
#if defined _MSC_VER || defined __MINGW32__
				/* with MSVC you have to spoon feed it the casting */
				pos = (FLAC__uint64)((FLAC__double)(FLAC__int64)(target_sample - left_sample) / (FLAC__double)(FLAC__int64)(right_sample - left_sample) * (FLAC__double)(FLAC__int64)(right_pos - left_pos));
#else
				pos = (FLAC__uint64)((FLAC__double)(target_sample - left_sample) / (FLAC__double)(right_sample - left_sample) * (FLAC__double)(right_pos - left_pos));
#endif
#else
				/* a little less accurate: */
				if ((target_sample-left_sample <= 0xffffffff) && (right_pos-left_pos <= 0xffffffff))
					pos = (FLAC__int64)(((target_sample-left_sample) * (right_pos-left_pos)) / (right_sample-left_sample));
				else /* @@@ WATCHOUT, ~2TB limit */
					pos = (FLAC__int64)((((target_sample-left_sample)>>8) * ((right_pos-left_pos)>>8)) / ((right_sample-left_sample)>>16));
#endif
				/* @@@ TODO: might want to limit pos to some distance
				 * before EOF, to make sure we land before the last frame,
				 * thereby getting a this_frame_sample and so having a better
				 * estimate.
				 */
			}

			/* physical seek */
			if(decoder->private_->seek_callback((FLAC__StreamDecoder*)decoder, (FLAC__uint64)pos, decoder->private_->client_data) != FLAC__STREAM_DECODER_SEEK_STATUS_OK) {
				decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
				return false;
			}
			if(!FLAC__stream_decoder_flush(decoder)) {
				/* above call sets the state for us */
				return false;
			}
			did_a_seek = true;
		}
		else
			did_a_seek = false;

		decoder->private_->got_a_frame = false;
		if(!FLAC__stream_decoder_process_single(decoder)) {
			decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
			return false;
		}
		if(!decoder->private_->got_a_frame) {
			if(did_a_seek) {
				/* this can happen if we seek to a point after the last frame; we drop
				 * to binary search right away in this case to avoid any wasted
				 * iterations of proportional search.
				 */
				right_pos = pos;
				BINARY_SEARCH_AFTER_ITERATION = 0;
			}
			else {
				/* this can probably only happen if total_samples is unknown and the
				 * target_sample is past the end of the stream
				 */
				decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
				return false;
			}
		}
		/* our write callback will change the state when it gets to the target frame */
		else if(!decoder->private_->is_seeking) {
			break;
		}
		else {
			this_frame_sample = decoder->private_->last_frame.header.number.sample_number;
			FLAC__ASSERT(decoder->private_->last_frame.header.number_type == FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER);

			if (did_a_seek) {
				if (this_frame_sample <= target_sample) {
					/* The 'equal' case should not happen, since
					 * FLAC__stream_decoder_process_single()
					 * should recognize that it has hit the
					 * target sample and we would exit through
					 * the 'break' above.
					 */
					FLAC__ASSERT(this_frame_sample != target_sample);

					left_sample = this_frame_sample;
					/* sanity check to avoid infinite loop */
					if (left_pos == pos) {
						decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
						return false;
					}
					left_pos = pos;
				}
				else if(this_frame_sample > target_sample) {
					right_sample = this_frame_sample;
					/* sanity check to avoid infinite loop */
					if (right_pos == pos) {
						decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
						return false;
					}
					right_pos = pos;
				}
			}
		}
	}

	return true;
}
#endif

FLAC__StreamDecoderReadStatus file_read_callback_decode(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data)
{
	(void)client_data;

	if(*bytes > 0) {
		*bytes = fread(buffer, sizeof(FLAC__byte), *bytes, decoder->private_->file);
		if(ferror(decoder->private_->file))
			return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
		else if(*bytes == 0)
			return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
		else
			return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
	}
	else
		return FLAC__STREAM_DECODER_READ_STATUS_ABORT; /* abort to avoid a deadlock */
}

FLAC__StreamDecoderSeekStatus file_seek_callback_decode(const FLAC__StreamDecoder *decoder, FLAC__uint64 absolute_byte_offset, void *client_data)
{
	(void)client_data;

	if(decoder->private_->file == stdin)
		return FLAC__STREAM_DECODER_SEEK_STATUS_UNSUPPORTED;
	else if(fseeko(decoder->private_->file, (off_t)absolute_byte_offset, SEEK_SET) < 0)
		return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
	else
		return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
}

FLAC__StreamDecoderTellStatus file_tell_callback_decode(const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *client_data)
{
	off_t pos;
	(void)client_data;

	if(decoder->private_->file == stdin)
		return FLAC__STREAM_DECODER_TELL_STATUS_UNSUPPORTED;
	else if((pos = ftello(decoder->private_->file)) < 0)
		return FLAC__STREAM_DECODER_TELL_STATUS_ERROR;
	else {
		*absolute_byte_offset = (FLAC__uint64)pos;
		return FLAC__STREAM_DECODER_TELL_STATUS_OK;
	}
}

FLAC__StreamDecoderLengthStatus file_length_callback_(const FLAC__StreamDecoder *decoder, FLAC__uint64 *stream_length, void *client_data)
{
	struct stat filestats;
	(void)client_data;

	if(decoder->private_->file == stdin)
		return FLAC__STREAM_DECODER_LENGTH_STATUS_UNSUPPORTED;
	else if(fstat(fileno(decoder->private_->file), &filestats) != 0)
		return FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;
	else {
		*stream_length = (FLAC__uint64)filestats.st_size;
		return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
	}
}

FLAC__bool file_eof_callback_(const FLAC__StreamDecoder *decoder, void *client_data)
{
	(void)client_data;

	return feof(decoder->private_->file)? true : false;
}

#endif //++-- stream_decoder.c 