/*
 * This example shows how to use libFLAC++ to encode a WAVE file to a FLAC
 * file.  It only supports 16-bit mono/stereo files in canonical WAVE format.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include "flacpp.h"



class OurEncoder: public FLAC::Encoder::File {
public:
	OurEncoder(): FLAC::Encoder::File() { }
	static int EncodeWave(FILE *fin, unsigned sample_rate, unsigned channels, unsigned bps, char* flac_path);
	static int ConvertWaveFile(const char* wave, const char* flac);
protected:
	virtual void progress_callback(FLAC__uint64 bytes_written, FLAC__uint64 samples_written, unsigned frames_written, unsigned total_frames_estimate);
};

