/* libFLAC++ - Free Lossless Audio Codec library
* Copyright (C) 2002,2003,2004,2005,2006,2007  Josh Coalson
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

//++ metadata.cpp 
#define __STDC_LIMIT_MACROS 1 /* otherwise SIZE_MAX is not defined for c++ */
#include "flacpp.h"
#include <stdlib.h> // for malloc(), free()
#include <string.h> // for memcpy() etc.

#ifdef _MSC_VER
// warning C4800: 'int' : forcing to bool 'true' or 'false' (performance warning)
#pragma warning ( disable : 4800 )
#endif

namespace FLAC {
	namespace Metadata {

		// local utility routines

		namespace local {

			Prototype *construct_block(::FLAC__StreamMetadata *object)
			{
				Prototype *ret = 0;
				switch(object->type) {
				case FLAC__METADATA_TYPE_STREAMINFO:
					ret = new StreamInfo(object, /*copy=*/false);
					break;
				case FLAC__METADATA_TYPE_PADDING:
					ret = new Padding(object, /*copy=*/false);
					break;
				case FLAC__METADATA_TYPE_APPLICATION:
					ret = new Application(object, /*copy=*/false);
					break;
				case FLAC__METADATA_TYPE_SEEKTABLE:
					ret = new SeekTable(object, /*copy=*/false);
					break;
				case FLAC__METADATA_TYPE_VORBIS_COMMENT:
					ret = new VorbisComment(object, /*copy=*/false);
					break;
				case FLAC__METADATA_TYPE_CUESHEET:
					ret = new CueSheet(object, /*copy=*/false);
					break;
				case FLAC__METADATA_TYPE_PICTURE:
					ret = new Picture(object, /*copy=*/false);
					break;
				default:
					ret = new Unknown(object, /*copy=*/false);
					break;
				}
				return ret;
			}

		}

		FLACPP_API Prototype *clone(const Prototype *object)
		{
			FLAC__ASSERT(0 != object);

			const StreamInfo *streaminfo = dynamic_cast<const StreamInfo *>(object);
			const Padding *padding = dynamic_cast<const Padding *>(object);
			const Application *application = dynamic_cast<const Application *>(object);
			const SeekTable *seektable = dynamic_cast<const SeekTable *>(object);
			const VorbisComment *vorbiscomment = dynamic_cast<const VorbisComment *>(object);
			const CueSheet *cuesheet = dynamic_cast<const CueSheet *>(object);
			const Picture *picture = dynamic_cast<const Picture *>(object);
			const Unknown *unknown = dynamic_cast<const Unknown *>(object);

			if(0 != streaminfo)
				return new StreamInfo(*streaminfo);
			else if(0 != padding)
				return new Padding(*padding);
			else if(0 != application)
				return new Application(*application);
			else if(0 != seektable)
				return new SeekTable(*seektable);
			else if(0 != vorbiscomment)
				return new VorbisComment(*vorbiscomment);
			else if(0 != cuesheet)
				return new CueSheet(*cuesheet);
			else if(0 != picture)
				return new Picture(*picture);
			else if(0 != unknown)
				return new Unknown(*unknown);
			else {
				FLAC__ASSERT(0);
				return 0;
			}
		}

		//
		// Prototype
		//

		Prototype::Prototype(const Prototype &object):
		object_(::FLAC__metadata_object_clone(object.object_)),
			is_reference_(false)
		{
			FLAC__ASSERT(object.is_valid());
		}

		Prototype::Prototype(const ::FLAC__StreamMetadata &object):
		object_(::FLAC__metadata_object_clone(&object)),
			is_reference_(false)
		{
		}

		Prototype::Prototype(const ::FLAC__StreamMetadata *object):
		object_(::FLAC__metadata_object_clone(object)),
			is_reference_(false)
		{
			FLAC__ASSERT(0 != object);
		}

		Prototype::Prototype(::FLAC__StreamMetadata *object, bool copy):
		object_(copy? ::FLAC__metadata_object_clone(object) : object),
			is_reference_(false)
		{
			FLAC__ASSERT(0 != object);
		}

		Prototype::~Prototype()
		{
			clear();
		}

		void Prototype::clear()
		{
			if(0 != object_ && !is_reference_)
				FLAC__metadata_object_delete(object_);
			object_ = 0;
		}

		Prototype &Prototype::operator=(const Prototype &object)
		{
			FLAC__ASSERT(object.is_valid());
			clear();
			is_reference_ = false;
			object_ = ::FLAC__metadata_object_clone(object.object_);
			return *this;
		}

		Prototype &Prototype::operator=(const ::FLAC__StreamMetadata &object)
		{
			clear();
			is_reference_ = false;
			object_ = ::FLAC__metadata_object_clone(&object);
			return *this;
		}

		Prototype &Prototype::operator=(const ::FLAC__StreamMetadata *object)
		{
			FLAC__ASSERT(0 != object);
			clear();
			is_reference_ = false;
			object_ = ::FLAC__metadata_object_clone(object);
			return *this;
		}

		Prototype &Prototype::assign_object(::FLAC__StreamMetadata *object, bool copy)
		{
			FLAC__ASSERT(0 != object);
			clear();
			object_ = (copy? ::FLAC__metadata_object_clone(object) : object);
			is_reference_ = false;
			return *this;
		}

		bool Prototype::get_is_last() const
		{
			FLAC__ASSERT(is_valid());
			return (bool)object_->is_last;
		}

		FLAC__MetadataType Prototype::get_type() const
		{
			FLAC__ASSERT(is_valid());
			return object_->type;
		}

		unsigned Prototype::get_length() const
		{
			FLAC__ASSERT(is_valid());
			return object_->length;
		}

		void Prototype::set_is_last(bool value)
		{
			FLAC__ASSERT(is_valid());
			object_->is_last = value;
		}


		//
		// StreamInfo
		//

		StreamInfo::StreamInfo():
		Prototype(FLAC__metadata_object_new(FLAC__METADATA_TYPE_STREAMINFO), /*copy=*/false)
		{ }

		StreamInfo::~StreamInfo()
		{ }

		unsigned StreamInfo::get_min_blocksize() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.stream_info.min_blocksize;
		}

		unsigned StreamInfo::get_max_blocksize() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.stream_info.max_blocksize;
		}

		unsigned StreamInfo::get_min_framesize() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.stream_info.min_framesize;
		}

		unsigned StreamInfo::get_max_framesize() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.stream_info.max_framesize;
		}

		unsigned StreamInfo::get_sample_rate() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.stream_info.sample_rate;
		}

		unsigned StreamInfo::get_channels() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.stream_info.channels;
		}

		unsigned StreamInfo::get_bits_per_sample() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.stream_info.bits_per_sample;
		}

		FLAC__uint64 StreamInfo::get_total_samples() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.stream_info.total_samples;
		}

		const FLAC__byte *StreamInfo::get_md5sum() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.stream_info.md5sum;
		}

		void StreamInfo::set_min_blocksize(unsigned value)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(value >= FLAC__MIN_BLOCK_SIZE);
			FLAC__ASSERT(value <= FLAC__MAX_BLOCK_SIZE);
			object_->data.stream_info.min_blocksize = value;
		}

		void StreamInfo::set_max_blocksize(unsigned value)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(value >= FLAC__MIN_BLOCK_SIZE);
			FLAC__ASSERT(value <= FLAC__MAX_BLOCK_SIZE);
			object_->data.stream_info.max_blocksize = value;
		}

		void StreamInfo::set_min_framesize(unsigned value)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(value < (1u << FLAC__STREAM_METADATA_STREAMINFO_MIN_FRAME_SIZE_LEN));
			object_->data.stream_info.min_framesize = value;
		}

		void StreamInfo::set_max_framesize(unsigned value)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(value < (1u << FLAC__STREAM_METADATA_STREAMINFO_MAX_FRAME_SIZE_LEN));
			object_->data.stream_info.max_framesize = value;
		}

		void StreamInfo::set_sample_rate(unsigned value)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(FLAC__format_sample_rate_is_valid(value));
			object_->data.stream_info.sample_rate = value;
		}

		void StreamInfo::set_channels(unsigned value)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(value > 0);
			FLAC__ASSERT(value <= FLAC__MAX_CHANNELS);
			object_->data.stream_info.channels = value;
		}

		void StreamInfo::set_bits_per_sample(unsigned value)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(value >= FLAC__MIN_BITS_PER_SAMPLE);
			FLAC__ASSERT(value <= FLAC__MAX_BITS_PER_SAMPLE);
			object_->data.stream_info.bits_per_sample = value;
		}

		void StreamInfo::set_total_samples(FLAC__uint64 value)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(value < (((FLAC__uint64)1) << FLAC__STREAM_METADATA_STREAMINFO_TOTAL_SAMPLES_LEN));
			object_->data.stream_info.total_samples = value;
		}

		void StreamInfo::set_md5sum(const FLAC__byte value[16])
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(0 != value);
			memcpy(object_->data.stream_info.md5sum, value, 16);
		}


		//
		// Padding
		//

		Padding::Padding():
		Prototype(FLAC__metadata_object_new(FLAC__METADATA_TYPE_PADDING), /*copy=*/false)
		{ }

		Padding::~Padding()
		{ }

		void Padding::set_length(unsigned length)
		{
			FLAC__ASSERT(is_valid());
			object_->length = length;
		}


		//
		// Application
		//

		Application::Application():
		Prototype(FLAC__metadata_object_new(FLAC__METADATA_TYPE_APPLICATION), /*copy=*/false)
		{ }

		Application::~Application()
		{ }

		const FLAC__byte *Application::get_id() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.application.id;
		}

		const FLAC__byte *Application::get_data() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.application.data;
		}

		void Application::set_id(const FLAC__byte value[4])
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(0 != value);
			memcpy(object_->data.application.id, value, 4);
		}

		bool Application::set_data(const FLAC__byte *data, unsigned length)
		{
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_object_application_set_data(object_, (FLAC__byte*)data, length, true);
		}

		bool Application::set_data(FLAC__byte *data, unsigned length, bool copy)
		{
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_object_application_set_data(object_, data, length, copy);
		}


		//
		// SeekTable
		//

		SeekTable::SeekTable():
		Prototype(FLAC__metadata_object_new(FLAC__METADATA_TYPE_SEEKTABLE), /*copy=*/false)
		{ }

		SeekTable::~SeekTable()
		{ }

		unsigned SeekTable::get_num_points() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.seek_table.num_points;
		}

		::FLAC__StreamMetadata_SeekPoint SeekTable::get_point(unsigned index) const
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(index < object_->data.seek_table.num_points);
			return object_->data.seek_table.points[index];
		}

		void SeekTable::set_point(unsigned index, const ::FLAC__StreamMetadata_SeekPoint &point)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(index < object_->data.seek_table.num_points);
			::FLAC__metadata_object_seektable_set_point(object_, index, point);
		}

		bool SeekTable::insert_point(unsigned index, const ::FLAC__StreamMetadata_SeekPoint &point)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(index <= object_->data.seek_table.num_points);
			return (bool)::FLAC__metadata_object_seektable_insert_point(object_, index, point);
		}

		bool SeekTable::delete_point(unsigned index)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(index < object_->data.seek_table.num_points);
			return (bool)::FLAC__metadata_object_seektable_delete_point(object_, index);
		}

		bool SeekTable::is_legal() const
		{
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_object_seektable_is_legal(object_);
		}


		//
		// VorbisComment::Entry
		//

		VorbisComment::Entry::Entry()
		{
			zero();
		}

		VorbisComment::Entry::Entry(const char *field, unsigned field_length)
		{
			zero();
			construct(field, field_length);
		}

		VorbisComment::Entry::Entry(const char *field)
		{
			zero();
			construct(field);
		}

		VorbisComment::Entry::Entry(const char *field_name, const char *field_value, unsigned field_value_length)
		{
			zero();
			construct(field_name, field_value, field_value_length);
		}

		VorbisComment::Entry::Entry(const char *field_name, const char *field_value)
		{
			zero();
			construct(field_name, field_value);
		}

		VorbisComment::Entry::Entry(const Entry &entry)
		{
			FLAC__ASSERT(entry.is_valid());
			zero();
			construct((const char *)entry.entry_.entry, entry.entry_.length);
		}

		VorbisComment::Entry &VorbisComment::Entry::operator=(const Entry &entry)
		{
			FLAC__ASSERT(entry.is_valid());
			clear();
			construct((const char *)entry.entry_.entry, entry.entry_.length);
			return *this;
		}

		VorbisComment::Entry::~Entry()
		{
			clear();
		}

		bool VorbisComment::Entry::is_valid() const
		{
			return is_valid_;
		}

		unsigned VorbisComment::Entry::get_field_length() const
		{
			FLAC__ASSERT(is_valid());
			return entry_.length;
		}

		unsigned VorbisComment::Entry::get_field_name_length() const
		{
			FLAC__ASSERT(is_valid());
			return field_name_length_;
		}

		unsigned VorbisComment::Entry::get_field_value_length() const
		{
			FLAC__ASSERT(is_valid());
			return field_value_length_;
		}

		::FLAC__StreamMetadata_VorbisComment_Entry VorbisComment::Entry::get_entry() const
		{
			FLAC__ASSERT(is_valid());
			return entry_;
		}

		const char *VorbisComment::Entry::get_field() const
		{
			FLAC__ASSERT(is_valid());
			return (const char *)entry_.entry;
		}

		const char *VorbisComment::Entry::get_field_name() const
		{
			FLAC__ASSERT(is_valid());
			return field_name_;
		}

		const char *VorbisComment::Entry::get_field_value() const
		{
			FLAC__ASSERT(is_valid());
			return field_value_;
		}

		bool VorbisComment::Entry::set_field(const char *field, unsigned field_length)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(0 != field);

			if(!::FLAC__format_vorbiscomment_entry_is_legal((const ::FLAC__byte*)field, field_length))
				return is_valid_ = false;

			clear_entry();

			if(0 == (entry_.entry = (FLAC__byte*)safe_malloc_add_2op_(field_length, /*+*/1))) {
				is_valid_ = false;
			}
			else {
				entry_.length = field_length;
				memcpy(entry_.entry, field, field_length);
				entry_.entry[field_length] = '\0';
				(void) parse_field();
			}

			return is_valid_;
		}

		bool VorbisComment::Entry::set_field(const char *field)
		{
			return set_field(field, strlen(field));
		}

		bool VorbisComment::Entry::set_field_name(const char *field_name)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(0 != field_name);

			if(!::FLAC__format_vorbiscomment_entry_name_is_legal(field_name))
				return is_valid_ = false;

			clear_field_name();

			if(0 == (field_name_ = strdup(field_name))) {
				is_valid_ = false;
			}
			else {
				field_name_length_ = strlen(field_name_);
				compose_field();
			}

			return is_valid_;
		}

		bool VorbisComment::Entry::set_field_value(const char *field_value, unsigned field_value_length)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(0 != field_value);

			if(!::FLAC__format_vorbiscomment_entry_value_is_legal((const FLAC__byte*)field_value, field_value_length))
				return is_valid_ = false;

			clear_field_value();

			if(0 == (field_value_ = (char *)safe_malloc_add_2op_(field_value_length, /*+*/1))) {
				is_valid_ = false;
			}
			else {
				field_value_length_ = field_value_length;
				memcpy(field_value_, field_value, field_value_length);
				field_value_[field_value_length] = '\0';
				compose_field();
			}

			return is_valid_;
		}

		bool VorbisComment::Entry::set_field_value(const char *field_value)
		{
			return set_field_value(field_value, strlen(field_value));
		}

		void VorbisComment::Entry::zero()
		{
			is_valid_ = true;
			entry_.length = 0;
			entry_.entry = 0;
			field_name_ = 0;
			field_name_length_ = 0;
			field_value_ = 0;
			field_value_length_ = 0;
		}

		void VorbisComment::Entry::clear()
		{
			clear_entry();
			clear_field_name();
			clear_field_value();
			is_valid_ = true;
		}

		void VorbisComment::Entry::clear_entry()
		{
			if(0 != entry_.entry) {
				free(entry_.entry);
				entry_.entry = 0;
				entry_.length = 0;
			}
		}

		void VorbisComment::Entry::clear_field_name()
		{
			if(0 != field_name_) {
				free(field_name_);
				field_name_ = 0;
				field_name_length_ = 0;
			}
		}

		void VorbisComment::Entry::clear_field_value()
		{
			if(0 != field_value_) {
				free(field_value_);
				field_value_ = 0;
				field_value_length_ = 0;
			}
		}

		void VorbisComment::Entry::construct(const char *field, unsigned field_length)
		{
			if(set_field(field, field_length))
				parse_field();
		}

		void VorbisComment::Entry::construct(const char *field)
		{
			construct(field, strlen(field));
		}

		void VorbisComment::Entry::construct(const char *field_name, const char *field_value, unsigned field_value_length)
		{
			if(set_field_name(field_name) && set_field_value(field_value, field_value_length))
				compose_field();
		}

		void VorbisComment::Entry::construct(const char *field_name, const char *field_value)
		{
			construct(field_name, field_value, strlen(field_value));
		}

		void VorbisComment::Entry::compose_field()
		{
			clear_entry();

			if(0 == (entry_.entry = (FLAC__byte*)safe_malloc_add_4op_(field_name_length_, /*+*/1, /*+*/field_value_length_, /*+*/1))) {
				is_valid_ = false;
			}
			else {
				memcpy(entry_.entry, field_name_, field_name_length_);
				entry_.length += field_name_length_;
				memcpy(entry_.entry + entry_.length, "=", 1);
				entry_.length += 1;
				memcpy(entry_.entry + entry_.length, field_value_, field_value_length_);
				entry_.length += field_value_length_;
				entry_.entry[entry_.length] = '\0';
				is_valid_ = true;
			}
		}

		void VorbisComment::Entry::parse_field()
		{
			clear_field_name();
			clear_field_value();

			const char *p = (const char *)memchr(entry_.entry, '=', entry_.length);

			if(0 == p)
				p = (const char *)entry_.entry + entry_.length;

			field_name_length_ = (unsigned)(p - (const char *)entry_.entry);
			if(0 == (field_name_ = (char *)safe_malloc_add_2op_(field_name_length_, /*+*/1))) { // +1 for the trailing \0
				is_valid_ = false;
				return;
			}
			memcpy(field_name_, entry_.entry, field_name_length_);
			field_name_[field_name_length_] = '\0';

			if(entry_.length - field_name_length_ == 0) {
				field_value_length_ = 0;
				if(0 == (field_value_ = (char *)safe_malloc_(0))) {
					is_valid_ = false;
					return;
				}
			}
			else {
				field_value_length_ = entry_.length - field_name_length_ - 1;
				if(0 == (field_value_ = (char *)safe_malloc_add_2op_(field_value_length_, /*+*/1))) { // +1 for the trailing \0
					is_valid_ = false;
					return;
				}
				memcpy(field_value_, ++p, field_value_length_);
				field_value_[field_value_length_] = '\0';
			}

			is_valid_ = true;
		}


		//
		// VorbisComment
		//

		VorbisComment::VorbisComment():
		Prototype(FLAC__metadata_object_new(FLAC__METADATA_TYPE_VORBIS_COMMENT), /*copy=*/false)
		{ }

		VorbisComment::~VorbisComment()
		{ }

		unsigned VorbisComment::get_num_comments() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.vorbis_comment.num_comments;
		}

		const FLAC__byte *VorbisComment::get_vendor_string() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.vorbis_comment.vendor_string.entry;
		}

		VorbisComment::Entry VorbisComment::get_comment(unsigned index) const
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(index < object_->data.vorbis_comment.num_comments);
			return Entry((const char *)object_->data.vorbis_comment.comments[index].entry, object_->data.vorbis_comment.comments[index].length);
		}

		bool VorbisComment::set_vendor_string(const FLAC__byte *string)
		{
			FLAC__ASSERT(is_valid());
			// vendor_string is a special kind of entry
			const ::FLAC__StreamMetadata_VorbisComment_Entry vendor_string = { strlen((const char *)string), (FLAC__byte*)string }; // we can cheat on const-ness because we make a copy below:
			return (bool)::FLAC__metadata_object_vorbiscomment_set_vendor_string(object_, vendor_string, /*copy=*/true);
		}

		bool VorbisComment::set_comment(unsigned index, const VorbisComment::Entry &entry)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(index < object_->data.vorbis_comment.num_comments);
			return (bool)::FLAC__metadata_object_vorbiscomment_set_comment(object_, index, entry.get_entry(), /*copy=*/true);
		}

		bool VorbisComment::insert_comment(unsigned index, const VorbisComment::Entry &entry)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(index <= object_->data.vorbis_comment.num_comments);
			return (bool)::FLAC__metadata_object_vorbiscomment_insert_comment(object_, index, entry.get_entry(), /*copy=*/true);
		}

		bool VorbisComment::append_comment(const VorbisComment::Entry &entry)
		{
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_object_vorbiscomment_append_comment(object_, entry.get_entry(), /*copy=*/true);
		}

		bool VorbisComment::delete_comment(unsigned index)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(index < object_->data.vorbis_comment.num_comments);
			return (bool)::FLAC__metadata_object_vorbiscomment_delete_comment(object_, index);
		}


		//
		// CueSheet::Track
		//

		CueSheet::Track::Track():
		object_(::FLAC__metadata_object_cuesheet_track_new())
		{ }

		CueSheet::Track::Track(const ::FLAC__StreamMetadata_CueSheet_Track *track):
		object_(::FLAC__metadata_object_cuesheet_track_clone(track))
		{ }

		CueSheet::Track::Track(const Track &track):
		object_(::FLAC__metadata_object_cuesheet_track_clone(track.object_))
		{ }

		CueSheet::Track &CueSheet::Track::operator=(const Track &track)
		{
			if(0 != object_)
				::FLAC__metadata_object_cuesheet_track_delete(object_);
			object_ = ::FLAC__metadata_object_cuesheet_track_clone(track.object_);
			return *this;
		}

		CueSheet::Track::~Track()
		{
			if(0 != object_)
				::FLAC__metadata_object_cuesheet_track_delete(object_);
		}

		bool CueSheet::Track::is_valid() const
		{
			return(0 != object_);
		}

		::FLAC__StreamMetadata_CueSheet_Index CueSheet::Track::get_index(unsigned i) const
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(i < object_->num_indices);
			return object_->indices[i];
		}

		void CueSheet::Track::set_isrc(const char value[12])
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(0 != value);
			memcpy(object_->isrc, value, 12);
			object_->isrc[12] = '\0';
		}

		void CueSheet::Track::set_type(unsigned value)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(value <= 1);
			object_->type = value;
		}

		void CueSheet::Track::set_index(unsigned i, const ::FLAC__StreamMetadata_CueSheet_Index &index)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(i < object_->num_indices);
			object_->indices[i] = index;
		}


		//
		// CueSheet
		//

		CueSheet::CueSheet():
		Prototype(FLAC__metadata_object_new(FLAC__METADATA_TYPE_CUESHEET), /*copy=*/false)
		{ }

		CueSheet::~CueSheet()
		{ }

		const char *CueSheet::get_media_catalog_number() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.cue_sheet.media_catalog_number;
		}

		FLAC__uint64 CueSheet::get_lead_in() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.cue_sheet.lead_in;
		}

		bool CueSheet::get_is_cd() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.cue_sheet.is_cd? true : false;
		}

		unsigned CueSheet::get_num_tracks() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.cue_sheet.num_tracks;
		}

		CueSheet::Track CueSheet::get_track(unsigned i) const
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(i < object_->data.cue_sheet.num_tracks);
			return Track(object_->data.cue_sheet.tracks + i);
		}

		void CueSheet::set_media_catalog_number(const char value[128])
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(0 != value);
			memcpy(object_->data.cue_sheet.media_catalog_number, value, 128);
			object_->data.cue_sheet.media_catalog_number[128] = '\0';
		}

		void CueSheet::set_lead_in(FLAC__uint64 value)
		{
			FLAC__ASSERT(is_valid());
			object_->data.cue_sheet.lead_in = value;
		}

		void CueSheet::set_is_cd(bool value)
		{
			FLAC__ASSERT(is_valid());
			object_->data.cue_sheet.is_cd = value;
		}

		void CueSheet::set_index(unsigned track_num, unsigned index_num, const ::FLAC__StreamMetadata_CueSheet_Index &index)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(track_num < object_->data.cue_sheet.num_tracks);
			FLAC__ASSERT(index_num < object_->data.cue_sheet.tracks[track_num].num_indices);
			object_->data.cue_sheet.tracks[track_num].indices[index_num] = index;
		}

		bool CueSheet::insert_index(unsigned track_num, unsigned index_num, const ::FLAC__StreamMetadata_CueSheet_Index &index)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(track_num < object_->data.cue_sheet.num_tracks);
			FLAC__ASSERT(index_num <= object_->data.cue_sheet.tracks[track_num].num_indices);
			return (bool)::FLAC__metadata_object_cuesheet_track_insert_index(object_, track_num, index_num, index);
		}

		bool CueSheet::delete_index(unsigned track_num, unsigned index_num)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(track_num < object_->data.cue_sheet.num_tracks);
			FLAC__ASSERT(index_num < object_->data.cue_sheet.tracks[track_num].num_indices);
			return (bool)::FLAC__metadata_object_cuesheet_track_delete_index(object_, track_num, index_num);
		}

		bool CueSheet::set_track(unsigned i, const CueSheet::Track &track)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(i < object_->data.cue_sheet.num_tracks);
			// We can safely const_cast since copy=true
			return (bool)::FLAC__metadata_object_cuesheet_set_track(object_, i, const_cast< ::FLAC__StreamMetadata_CueSheet_Track*>(track.get_track()), /*copy=*/true);
		}

		bool CueSheet::insert_track(unsigned i, const CueSheet::Track &track)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(i <= object_->data.cue_sheet.num_tracks);
			// We can safely const_cast since copy=true
			return (bool)::FLAC__metadata_object_cuesheet_insert_track(object_, i, const_cast< ::FLAC__StreamMetadata_CueSheet_Track*>(track.get_track()), /*copy=*/true);
		}

		bool CueSheet::delete_track(unsigned i)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(i < object_->data.cue_sheet.num_tracks);
			return (bool)::FLAC__metadata_object_cuesheet_delete_track(object_, i);
		}

		bool CueSheet::is_legal(bool check_cd_da_subset, const char **violation) const
		{
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_object_cuesheet_is_legal(object_, check_cd_da_subset, violation);
		}

		FLAC__uint32 CueSheet::calculate_cddb_id() const
		{
			FLAC__ASSERT(is_valid());
			return ::FLAC__metadata_object_cuesheet_calculate_cddb_id(object_);
		}


		//
		// Picture
		//

		Picture::Picture():
		Prototype(FLAC__metadata_object_new(FLAC__METADATA_TYPE_PICTURE), /*copy=*/false)
		{ }

		Picture::~Picture()
		{ }

		::FLAC__StreamMetadata_Picture_Type Picture::get_type() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.picture.type;
		}

		const char *Picture::get_mime_type() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.picture.mime_type;
		}

		const FLAC__byte *Picture::get_description() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.picture.description;
		}

		FLAC__uint32 Picture::get_width() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.picture.width;
		}

		FLAC__uint32 Picture::get_height() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.picture.height;
		}

		FLAC__uint32 Picture::get_depth() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.picture.depth;
		}

		FLAC__uint32 Picture::get_colors() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.picture.colors;
		}

		FLAC__uint32 Picture::get_data_length() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.picture.data_length;
		}

		const FLAC__byte *Picture::get_data() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.picture.data;
		}

		void Picture::set_type(::FLAC__StreamMetadata_Picture_Type type)
		{
			FLAC__ASSERT(is_valid());
			object_->data.picture.type = type;
		}

		bool Picture::set_mime_type(const char *string)
		{
			FLAC__ASSERT(is_valid());
			// We can safely const_cast since copy=true
			return (bool)::FLAC__metadata_object_picture_set_mime_type(object_, const_cast<char*>(string), /*copy=*/true);
		}

		bool Picture::set_description(const FLAC__byte *string)
		{
			FLAC__ASSERT(is_valid());
			// We can safely const_cast since copy=true
			return (bool)::FLAC__metadata_object_picture_set_description(object_, const_cast<FLAC__byte*>(string), /*copy=*/true);
		}

		void Picture::set_width(FLAC__uint32 value) const
		{
			FLAC__ASSERT(is_valid());
			object_->data.picture.width = value;
		}

		void Picture::set_height(FLAC__uint32 value) const
		{
			FLAC__ASSERT(is_valid());
			object_->data.picture.height = value;
		}

		void Picture::set_depth(FLAC__uint32 value) const
		{
			FLAC__ASSERT(is_valid());
			object_->data.picture.depth = value;
		}

		void Picture::set_colors(FLAC__uint32 value) const
		{
			FLAC__ASSERT(is_valid());
			object_->data.picture.colors = value;
		}

		bool Picture::set_data(const FLAC__byte *data, FLAC__uint32 data_length)
		{
			FLAC__ASSERT(is_valid());
			// We can safely const_cast since copy=true
			return (bool)::FLAC__metadata_object_picture_set_data(object_, const_cast<FLAC__byte*>(data), data_length, /*copy=*/true);
		}


		//
		// Unknown
		//

		Unknown::Unknown():
		Prototype(FLAC__metadata_object_new(FLAC__METADATA_TYPE_APPLICATION), /*copy=*/false)
		{ }

		Unknown::~Unknown()
		{ }

		const FLAC__byte *Unknown::get_data() const
		{
			FLAC__ASSERT(is_valid());
			return object_->data.application.data;
		}

		bool Unknown::set_data(const FLAC__byte *data, unsigned length)
		{
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_object_application_set_data(object_, (FLAC__byte*)data, length, true);
		}

		bool Unknown::set_data(FLAC__byte *data, unsigned length, bool copy)
		{
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_object_application_set_data(object_, data, length, copy);
		}


		// ============================================================
		//
		//  Level 0
		//
		// ============================================================

		FLACPP_API bool get_streaminfo(const char *filename, StreamInfo &streaminfo)
		{
			FLAC__ASSERT(0 != filename);

			::FLAC__StreamMetadata object;

			if(::FLAC__metadata_get_streaminfo(filename, &object)) {
				streaminfo = object;
				return true;
			}
			else
				return false;
		}

		FLACPP_API bool get_tags(const char *filename, VorbisComment *&tags)
		{
			FLAC__ASSERT(0 != filename);

			::FLAC__StreamMetadata *object;

			tags = 0;

			if(::FLAC__metadata_get_tags(filename, &object)) {
				tags = new VorbisComment(object, /*copy=*/false);
				return true;
			}
			else
				return false;
		}

		FLACPP_API bool get_tags(const char *filename, VorbisComment &tags)
		{
			FLAC__ASSERT(0 != filename);

			::FLAC__StreamMetadata *object;

			if(::FLAC__metadata_get_tags(filename, &object)) {
				tags.assign(object, /*copy=*/false);
				return true;
			}
			else
				return false;
		}

		FLACPP_API bool get_cuesheet(const char *filename, CueSheet *&cuesheet)
		{
			FLAC__ASSERT(0 != filename);

			::FLAC__StreamMetadata *object;

			cuesheet = 0;

			if(::FLAC__metadata_get_cuesheet(filename, &object)) {
				cuesheet = new CueSheet(object, /*copy=*/false);
				return true;
			}
			else
				return false;
		}

		FLACPP_API bool get_cuesheet(const char *filename, CueSheet &cuesheet)
		{
			FLAC__ASSERT(0 != filename);

			::FLAC__StreamMetadata *object;

			if(::FLAC__metadata_get_cuesheet(filename, &object)) {
				cuesheet.assign(object, /*copy=*/false);
				return true;
			}
			else
				return false;
		}

		FLACPP_API bool get_picture(const char *filename, Picture *&picture, ::FLAC__StreamMetadata_Picture_Type type, const char *mime_type, const FLAC__byte *description, unsigned max_width, unsigned max_height, unsigned max_depth, unsigned max_colors)
		{
			FLAC__ASSERT(0 != filename);

			::FLAC__StreamMetadata *object;

			picture = 0;

			if(::FLAC__metadata_get_picture(filename, &object, type, mime_type, description, max_width, max_height, max_depth, max_colors)) {
				picture = new Picture(object, /*copy=*/false);
				return true;
			}
			else
				return false;
		}

		FLACPP_API bool get_picture(const char *filename, Picture &picture, ::FLAC__StreamMetadata_Picture_Type type, const char *mime_type, const FLAC__byte *description, unsigned max_width, unsigned max_height, unsigned max_depth, unsigned max_colors)
		{
			FLAC__ASSERT(0 != filename);

			::FLAC__StreamMetadata *object;

			if(::FLAC__metadata_get_picture(filename, &object, type, mime_type, description, max_width, max_height, max_depth, max_colors)) {
				picture.assign(object, /*copy=*/false);
				return true;
			}
			else
				return false;
		}


		// ============================================================
		//
		//  Level 1
		//
		// ============================================================

		SimpleIterator::SimpleIterator():
		iterator_(::FLAC__metadata_simple_iterator_new())
		{ }

		SimpleIterator::~SimpleIterator()
		{
			clear();
		}

		void SimpleIterator::clear()
		{
			if(0 != iterator_)
				FLAC__metadata_simple_iterator_delete(iterator_);
			iterator_ = 0;
		}

		bool SimpleIterator::init(const char *filename, bool read_only, bool preserve_file_stats)
		{
			FLAC__ASSERT(0 != filename);
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_simple_iterator_init(iterator_, filename, read_only, preserve_file_stats);
		}

		bool SimpleIterator::is_valid() const
		{
			return 0 != iterator_;
		}

		SimpleIterator::Status SimpleIterator::status()
		{
			FLAC__ASSERT(is_valid());
			return Status(::FLAC__metadata_simple_iterator_status(iterator_));
		}

		bool SimpleIterator::is_writable() const
		{
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_simple_iterator_is_writable(iterator_);
		}

		bool SimpleIterator::next()
		{
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_simple_iterator_next(iterator_);
		}

		bool SimpleIterator::prev()
		{
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_simple_iterator_prev(iterator_);
		}

		//@@@@ add to tests
		bool SimpleIterator::is_last() const
		{
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_simple_iterator_is_last(iterator_);
		}

		//@@@@ add to tests
		off_t SimpleIterator::get_block_offset() const
		{
			FLAC__ASSERT(is_valid());
			return ::FLAC__metadata_simple_iterator_get_block_offset(iterator_);
		}

		::FLAC__MetadataType SimpleIterator::get_block_type() const
		{
			FLAC__ASSERT(is_valid());
			return ::FLAC__metadata_simple_iterator_get_block_type(iterator_);
		}

		//@@@@ add to tests
		unsigned SimpleIterator::get_block_length() const
		{
			FLAC__ASSERT(is_valid());
			return ::FLAC__metadata_simple_iterator_get_block_length(iterator_);
		}

		//@@@@ add to tests
		bool SimpleIterator::get_application_id(FLAC__byte *id)
		{
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_simple_iterator_get_application_id(iterator_, id);
		}

		Prototype *SimpleIterator::get_block()
		{
			FLAC__ASSERT(is_valid());
			return local::construct_block(::FLAC__metadata_simple_iterator_get_block(iterator_));
		}

		bool SimpleIterator::set_block(Prototype *block, bool use_padding)
		{
			FLAC__ASSERT(0 != block);
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_simple_iterator_set_block(iterator_, block->object_, use_padding);
		}

		bool SimpleIterator::insert_block_after(Prototype *block, bool use_padding)
		{
			FLAC__ASSERT(0 != block);
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_simple_iterator_insert_block_after(iterator_, block->object_, use_padding);
		}

		bool SimpleIterator::delete_block(bool use_padding)
		{
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_simple_iterator_delete_block(iterator_, use_padding);
		}


		// ============================================================
		//
		//  Level 2
		//
		// ============================================================

		Chain::Chain():
		chain_(::FLAC__metadata_chain_new())
		{ }

		Chain::~Chain()
		{
			clear();
		}

		void Chain::clear()
		{
			if(0 != chain_)
				FLAC__metadata_chain_delete(chain_);
			chain_ = 0;
		}

		bool Chain::is_valid() const
		{
			return 0 != chain_;
		}

		Chain::Status Chain::status()
		{
			FLAC__ASSERT(is_valid());
			return Status(::FLAC__metadata_chain_status(chain_));
		}

		bool Chain::read(const char *filename, bool is_ogg)
		{
			FLAC__ASSERT(0 != filename);
			FLAC__ASSERT(is_valid());
			return is_ogg?
				(bool)::FLAC__metadata_chain_read_ogg(chain_, filename) :
			(bool)::FLAC__metadata_chain_read(chain_, filename)
				;
		}

		bool Chain::read(FLAC__IOHandle handle, ::FLAC__IOCallbacks callbacks, bool is_ogg)
		{
			FLAC__ASSERT(is_valid());
			return is_ogg?
				(bool)::FLAC__metadata_chain_read_ogg_with_callbacks(chain_, handle, callbacks) :
			(bool)::FLAC__metadata_chain_read_with_callbacks(chain_, handle, callbacks)
				;
		}

		bool Chain::check_if_tempfile_needed(bool use_padding)
		{
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_chain_check_if_tempfile_needed(chain_, use_padding);
		}

		bool Chain::write(bool use_padding, bool preserve_file_stats)
		{
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_chain_write(chain_, use_padding, preserve_file_stats);
		}

		bool Chain::write(bool use_padding, ::FLAC__IOHandle handle, ::FLAC__IOCallbacks callbacks)
		{
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_chain_write_with_callbacks(chain_, use_padding, handle, callbacks);
		}

		bool Chain::write(bool use_padding, ::FLAC__IOHandle handle, ::FLAC__IOCallbacks callbacks, ::FLAC__IOHandle temp_handle, ::FLAC__IOCallbacks temp_callbacks)
		{
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_chain_write_with_callbacks_and_tempfile(chain_, use_padding, handle, callbacks, temp_handle, temp_callbacks);
		}

		void Chain::merge_padding()
		{
			FLAC__ASSERT(is_valid());
			::FLAC__metadata_chain_merge_padding(chain_);
		}

		void Chain::sort_padding()
		{
			FLAC__ASSERT(is_valid());
			::FLAC__metadata_chain_sort_padding(chain_);
		}


		Iterator::Iterator():
		iterator_(::FLAC__metadata_iterator_new())
		{ }

		Iterator::~Iterator()
		{
			clear();
		}

		void Iterator::clear()
		{
			if(0 != iterator_)
				FLAC__metadata_iterator_delete(iterator_);
			iterator_ = 0;
		}

		bool Iterator::is_valid() const
		{
			return 0 != iterator_;
		}

		void Iterator::init(Chain &chain)
		{
			FLAC__ASSERT(is_valid());
			FLAC__ASSERT(chain.is_valid());
			::FLAC__metadata_iterator_init(iterator_, chain.chain_);
		}

		bool Iterator::next()
		{
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_iterator_next(iterator_);
		}

		bool Iterator::prev()
		{
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_iterator_prev(iterator_);
		}

		::FLAC__MetadataType Iterator::get_block_type() const
		{
			FLAC__ASSERT(is_valid());
			return ::FLAC__metadata_iterator_get_block_type(iterator_);
		}

		Prototype *Iterator::get_block()
		{
			FLAC__ASSERT(is_valid());
			Prototype *block = local::construct_block(::FLAC__metadata_iterator_get_block(iterator_));
			if(0 != block)
				block->set_reference(true);
			return block;
		}

		bool Iterator::set_block(Prototype *block)
		{
			FLAC__ASSERT(0 != block);
			FLAC__ASSERT(is_valid());
			bool ret = (bool)::FLAC__metadata_iterator_set_block(iterator_, block->object_);
			if(ret) {
				block->set_reference(true);
				delete block;
			}
			return ret;
		}

		bool Iterator::delete_block(bool replace_with_padding)
		{
			FLAC__ASSERT(is_valid());
			return (bool)::FLAC__metadata_iterator_delete_block(iterator_, replace_with_padding);
		}

		bool Iterator::insert_block_before(Prototype *block)
		{
			FLAC__ASSERT(0 != block);
			FLAC__ASSERT(is_valid());
			bool ret = (bool)::FLAC__metadata_iterator_insert_block_before(iterator_, block->object_);
			if(ret) {
				block->set_reference(true);
				delete block;
			}
			return ret;
		}

		bool Iterator::insert_block_after(Prototype *block)
		{
			FLAC__ASSERT(0 != block);
			FLAC__ASSERT(is_valid());
			bool ret = (bool)::FLAC__metadata_iterator_insert_block_after(iterator_, block->object_);
			if(ret) {
				block->set_reference(true);
				delete block;
			}
			return ret;
		}

	}

}
//++-- metadata.cpp 

//++ stream_encoder.cpp 
#ifdef _MSC_VER
// warning C4800: 'int' : forcing to bool 'true' or 'false' (performance warning)
#pragma warning ( disable : 4800 )
#endif

namespace FLAC {
	namespace Encoder {

		// ------------------------------------------------------------
		//
		// Stream
		//
		// ------------------------------------------------------------

		Stream::Stream():
	encoder_(::FLAC__stream_encoder_new())
	{ }

	Stream::~Stream()
	{
		if(0 != encoder_) {
			(void)::FLAC__stream_encoder_finish(encoder_);
			::FLAC__stream_encoder_delete(encoder_);
		}
	}

	bool Stream::is_valid() const
	{
		return 0 != encoder_;
	}

	bool Stream::set_ogg_serial_number(long value)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_set_ogg_serial_number(encoder_, value);
	}

	bool Stream::set_verify(bool value)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_set_verify(encoder_, value);
	}

	bool Stream::set_streamable_subset(bool value)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_set_streamable_subset(encoder_, value);
	}

	bool Stream::set_channels(unsigned value)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_set_channels(encoder_, value);
	}

	bool Stream::set_bits_per_sample(unsigned value)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_set_bits_per_sample(encoder_, value);
	}

	bool Stream::set_sample_rate(unsigned value)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_set_sample_rate(encoder_, value);
	}

	bool Stream::set_compression_level(unsigned value)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_set_compression_level(encoder_, value);
	}

	bool Stream::set_blocksize(unsigned value)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_set_blocksize(encoder_, value);
	}

	bool Stream::set_do_mid_side_stereo(bool value)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_set_do_mid_side_stereo(encoder_, value);
	}

	bool Stream::set_loose_mid_side_stereo(bool value)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_set_loose_mid_side_stereo(encoder_, value);
	}

	bool Stream::set_apodization(const char *specification)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_set_apodization(encoder_, specification);
	}

	bool Stream::set_max_lpc_order(unsigned value)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_set_max_lpc_order(encoder_, value);
	}

	bool Stream::set_qlp_coeff_precision(unsigned value)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_set_qlp_coeff_precision(encoder_, value);
	}

	bool Stream::set_do_qlp_coeff_prec_search(bool value)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_set_do_qlp_coeff_prec_search(encoder_, value);
	}

	bool Stream::set_do_escape_coding(bool value)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_set_do_escape_coding(encoder_, value);
	}

	bool Stream::set_do_exhaustive_model_search(bool value)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_set_do_exhaustive_model_search(encoder_, value);
	}

	bool Stream::set_min_residual_partition_order(unsigned value)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_set_min_residual_partition_order(encoder_, value);
	}

	bool Stream::set_max_residual_partition_order(unsigned value)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_set_max_residual_partition_order(encoder_, value);
	}

	bool Stream::set_rice_parameter_search_dist(unsigned value)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_set_rice_parameter_search_dist(encoder_, value);
	}

	bool Stream::set_total_samples_estimate(FLAC__uint64 value)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_set_total_samples_estimate(encoder_, value);
	}

	bool Stream::set_metadata(::FLAC__StreamMetadata **metadata, unsigned num_blocks)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_set_metadata(encoder_, metadata, num_blocks);
	}

	bool Stream::set_metadata(FLAC::Metadata::Prototype **metadata, unsigned num_blocks)
	{
		FLAC__ASSERT(is_valid());
#if (defined _MSC_VER) || (defined __BORLANDC__) || (defined __SUNPRO_CC)
		// MSVC++ can't handle:
		// ::FLAC__StreamMetadata *m[num_blocks];
		// so we do this ugly workaround
		::FLAC__StreamMetadata **m = new ::FLAC__StreamMetadata*[num_blocks];
#else
		::FLAC__StreamMetadata *m[num_blocks];
#endif
		for(unsigned i = 0; i < num_blocks; i++) {
			// we can get away with the const_cast since we know the encoder will only correct the is_last flags
			m[i] = const_cast< ::FLAC__StreamMetadata*>((const ::FLAC__StreamMetadata*)metadata[i]);
		}
#if (defined _MSC_VER) || (defined __BORLANDC__) || (defined __SUNPRO_CC)
		// complete the hack
		const bool ok = (bool)::FLAC__stream_encoder_set_metadata(encoder_, m, num_blocks);
		delete [] m;
		return ok;
#else
		return (bool)::FLAC__stream_encoder_set_metadata(encoder_, m, num_blocks);
#endif
	}

	Stream::State Stream::get_state() const
	{
		FLAC__ASSERT(is_valid());
		return State(::FLAC__stream_encoder_get_state(encoder_));
	}

	Decoder::Stream::State Stream::get_verify_decoder_state() const
	{
		FLAC__ASSERT(is_valid());
		return Decoder::Stream::State(::FLAC__stream_encoder_get_verify_decoder_state(encoder_));
	}

	void Stream::get_verify_decoder_error_stats(FLAC__uint64 *absolute_sample, unsigned *frame_number, unsigned *channel, unsigned *sample, FLAC__int32 *expected, FLAC__int32 *got)
	{
		FLAC__ASSERT(is_valid());
		::FLAC__stream_encoder_get_verify_decoder_error_stats(encoder_, absolute_sample, frame_number, channel, sample, expected, got);
	}

	bool Stream::get_verify() const
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_get_verify(encoder_);
	}

	bool Stream::get_streamable_subset() const
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_get_streamable_subset(encoder_);
	}

	bool Stream::get_do_mid_side_stereo() const
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_get_do_mid_side_stereo(encoder_);
	}

	bool Stream::get_loose_mid_side_stereo() const
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_get_loose_mid_side_stereo(encoder_);
	}

	unsigned Stream::get_channels() const
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_encoder_get_channels(encoder_);
	}

	unsigned Stream::get_bits_per_sample() const
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_encoder_get_bits_per_sample(encoder_);
	}

	unsigned Stream::get_sample_rate() const
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_encoder_get_sample_rate(encoder_);
	}

	unsigned Stream::get_blocksize() const
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_encoder_get_blocksize(encoder_);
	}

	unsigned Stream::get_max_lpc_order() const
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_encoder_get_max_lpc_order(encoder_);
	}

	unsigned Stream::get_qlp_coeff_precision() const
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_encoder_get_qlp_coeff_precision(encoder_);
	}

	bool Stream::get_do_qlp_coeff_prec_search() const
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_get_do_qlp_coeff_prec_search(encoder_);
	}

	bool Stream::get_do_escape_coding() const
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_get_do_escape_coding(encoder_);
	}

	bool Stream::get_do_exhaustive_model_search() const
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_get_do_exhaustive_model_search(encoder_);
	}

	unsigned Stream::get_min_residual_partition_order() const
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_encoder_get_min_residual_partition_order(encoder_);
	}

	unsigned Stream::get_max_residual_partition_order() const
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_encoder_get_max_residual_partition_order(encoder_);
	}

	unsigned Stream::get_rice_parameter_search_dist() const
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_encoder_get_rice_parameter_search_dist(encoder_);
	}

	FLAC__uint64 Stream::get_total_samples_estimate() const
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_encoder_get_total_samples_estimate(encoder_);
	}

	::FLAC__StreamEncoderInitStatus Stream::init()
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_encoder_init_stream(encoder_, write_callback_, seek_callback_, tell_callback_, metadata_callback_, /*client_data=*/(void*)this);
	}

	::FLAC__StreamEncoderInitStatus Stream::init_ogg()
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_encoder_init_ogg_stream(encoder_, read_callback_, write_callback_, seek_callback_, tell_callback_, metadata_callback_, /*client_data=*/(void*)this);
	}

	bool Stream::finish()
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_finish(encoder_);
	}

	bool Stream::process(const FLAC__int32 * const buffer[], unsigned samples)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_process(encoder_, buffer, samples);
	}

	bool Stream::process_interleaved(const FLAC__int32 buffer[], unsigned samples)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_encoder_process_interleaved(encoder_, buffer, samples);
	}

	::FLAC__StreamEncoderReadStatus Stream::read_callback(FLAC__byte buffer[], size_t *bytes)
	{
		(void)buffer, (void)bytes;
		return ::FLAC__STREAM_ENCODER_READ_STATUS_UNSUPPORTED;
	}

	::FLAC__StreamEncoderSeekStatus Stream::seek_callback(FLAC__uint64 absolute_byte_offset)
	{
		(void)absolute_byte_offset;
		return ::FLAC__STREAM_ENCODER_SEEK_STATUS_UNSUPPORTED;
	}

	::FLAC__StreamEncoderTellStatus Stream::tell_callback(FLAC__uint64 *absolute_byte_offset)
	{
		(void)absolute_byte_offset;
		return ::FLAC__STREAM_ENCODER_TELL_STATUS_UNSUPPORTED;
	}

	void Stream::metadata_callback(const ::FLAC__StreamMetadata *metadata)
	{
		(void)metadata;
	}

	::FLAC__StreamEncoderReadStatus Stream::read_callback_(const ::FLAC__StreamEncoder *encoder, FLAC__byte buffer[], size_t *bytes, void *client_data)
	{
		(void)encoder;
		FLAC__ASSERT(0 != client_data);
		Stream *instance = reinterpret_cast<Stream *>(client_data);
		FLAC__ASSERT(0 != instance);
		return instance->read_callback(buffer, bytes);
	}

	::FLAC__StreamEncoderWriteStatus Stream::write_callback_(const ::FLAC__StreamEncoder *encoder, const FLAC__byte buffer[], size_t bytes, unsigned samples, unsigned current_frame, void *client_data)
	{
		(void)encoder;
		FLAC__ASSERT(0 != client_data);
		Stream *instance = reinterpret_cast<Stream *>(client_data);
		FLAC__ASSERT(0 != instance);
		return instance->write_callback(buffer, bytes, samples, current_frame);
	}

	::FLAC__StreamEncoderSeekStatus Stream::seek_callback_(const ::FLAC__StreamEncoder *encoder, FLAC__uint64 absolute_byte_offset, void *client_data)
	{
		(void)encoder;
		FLAC__ASSERT(0 != client_data);
		Stream *instance = reinterpret_cast<Stream *>(client_data);
		FLAC__ASSERT(0 != instance);
		return instance->seek_callback(absolute_byte_offset);
	}

	::FLAC__StreamEncoderTellStatus Stream::tell_callback_(const ::FLAC__StreamEncoder *encoder, FLAC__uint64 *absolute_byte_offset, void *client_data)
	{
		(void)encoder;
		FLAC__ASSERT(0 != client_data);
		Stream *instance = reinterpret_cast<Stream *>(client_data);
		FLAC__ASSERT(0 != instance);
		return instance->tell_callback(absolute_byte_offset);
	}

	void Stream::metadata_callback_(const ::FLAC__StreamEncoder *encoder, const ::FLAC__StreamMetadata *metadata, void *client_data)
	{
		(void)encoder;
		FLAC__ASSERT(0 != client_data);
		Stream *instance = reinterpret_cast<Stream *>(client_data);
		FLAC__ASSERT(0 != instance);
		instance->metadata_callback(metadata);
	}

	// ------------------------------------------------------------
	//
	// File
	//
	// ------------------------------------------------------------

	File::File():
	Stream()
	{ }

	File::~File()
	{
	}

	::FLAC__StreamEncoderInitStatus File::init(FILE *file)
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_encoder_init_FILE(encoder_, file, progress_callback_, /*client_data=*/(void*)this);
	}

	::FLAC__StreamEncoderInitStatus File::init(const char *filename)
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_encoder_init_file(encoder_, filename, progress_callback_, /*client_data=*/(void*)this);
	}

	::FLAC__StreamEncoderInitStatus File::init(const std::string &filename)
	{
		return init(filename.c_str());
	}

	::FLAC__StreamEncoderInitStatus File::init_ogg(FILE *file)
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_encoder_init_ogg_FILE(encoder_, file, progress_callback_, /*client_data=*/(void*)this);
	}

	::FLAC__StreamEncoderInitStatus File::init_ogg(const char *filename)
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_encoder_init_ogg_file(encoder_, filename, progress_callback_, /*client_data=*/(void*)this);
	}

	::FLAC__StreamEncoderInitStatus File::init_ogg(const std::string &filename)
	{
		return init_ogg(filename.c_str());
	}

	// This is a dummy to satisfy the pure virtual from Stream; the
	// read callback will never be called since we are initializing
	// with FLAC__stream_decoder_init_FILE() or
	// FLAC__stream_decoder_init_file() and those supply the read
	// callback internally.
	::FLAC__StreamEncoderWriteStatus File::write_callback(const FLAC__byte buffer[], size_t bytes, unsigned samples, unsigned current_frame)
	{
		(void)buffer, (void)bytes, (void)samples, (void)current_frame;
		FLAC__ASSERT(false);
		return ::FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR; // double protection
	}

	void File::progress_callback(FLAC__uint64 bytes_written, FLAC__uint64 samples_written, unsigned frames_written, unsigned total_frames_estimate)
	{
		(void)bytes_written, (void)samples_written, (void)frames_written, (void)total_frames_estimate;
	}

	void File::progress_callback_(const ::FLAC__StreamEncoder *encoder, FLAC__uint64 bytes_written, FLAC__uint64 samples_written, unsigned frames_written, unsigned total_frames_estimate, void *client_data)
	{
		(void)encoder;
		FLAC__ASSERT(0 != client_data);
		File *instance = reinterpret_cast<File *>(client_data);
		FLAC__ASSERT(0 != instance);
		instance->progress_callback(bytes_written, samples_written, frames_written, total_frames_estimate);
	}

	}
}

//++-- stream_encoder.cpp 

//++ stream_decoder.cpp 

#ifdef _MSC_VER
// warning C4800: 'int' : forcing to bool 'true' or 'false' (performance warning)
#pragma warning ( disable : 4800 )
#endif

namespace FLAC {
	namespace Decoder {

		// ------------------------------------------------------------
		//
		// Stream
		//
		// ------------------------------------------------------------

		Stream::Stream():
	decoder_(::FLAC__stream_decoder_new())
	{ }

	Stream::~Stream()
	{
		if(0 != decoder_) {
			(void)::FLAC__stream_decoder_finish(decoder_);
			::FLAC__stream_decoder_delete(decoder_);
		}
	}

	bool Stream::is_valid() const
	{
		return 0 != decoder_;
	}

	bool Stream::set_ogg_serial_number(long value)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_decoder_set_ogg_serial_number(decoder_, value);
	}

	bool Stream::set_md5_checking(bool value)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_decoder_set_md5_checking(decoder_, value);
	}

	bool Stream::set_metadata_respond(::FLAC__MetadataType type)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_decoder_set_metadata_respond(decoder_, type);
	}

	bool Stream::set_metadata_respond_application(const FLAC__byte id[4])
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_decoder_set_metadata_respond_application(decoder_, id);
	}

	bool Stream::set_metadata_respond_all()
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_decoder_set_metadata_respond_all(decoder_);
	}

	bool Stream::set_metadata_ignore(::FLAC__MetadataType type)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_decoder_set_metadata_ignore(decoder_, type);
	}

	bool Stream::set_metadata_ignore_application(const FLAC__byte id[4])
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_decoder_set_metadata_ignore_application(decoder_, id);
	}

	bool Stream::set_metadata_ignore_all()
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_decoder_set_metadata_ignore_all(decoder_);
	}

	Stream::State Stream::get_state() const
	{
		FLAC__ASSERT(is_valid());
		return State(::FLAC__stream_decoder_get_state(decoder_));
	}

	bool Stream::get_md5_checking() const
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_decoder_get_md5_checking(decoder_);
	}

	FLAC__uint64 Stream::get_total_samples() const
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_decoder_get_total_samples(decoder_);
	}

	unsigned Stream::get_channels() const
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_decoder_get_channels(decoder_);
	}

	::FLAC__ChannelAssignment Stream::get_channel_assignment() const
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_decoder_get_channel_assignment(decoder_);
	}

	unsigned Stream::get_bits_per_sample() const
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_decoder_get_bits_per_sample(decoder_);
	}

	unsigned Stream::get_sample_rate() const
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_decoder_get_sample_rate(decoder_);
	}

	unsigned Stream::get_blocksize() const
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_decoder_get_blocksize(decoder_);
	}

	bool Stream::get_decode_position(FLAC__uint64 *position) const
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_decoder_get_decode_position(decoder_, position);
	}

	::FLAC__StreamDecoderInitStatus Stream::init()
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_decoder_init_stream(decoder_, read_callback_, seek_callback_, tell_callback_, length_callback_, eof_callback_, write_callback_, metadata_callback_, error_callback_, /*client_data=*/(void*)this);
	}

	::FLAC__StreamDecoderInitStatus Stream::init_ogg()
	{
		FLAC__ASSERT(is_valid());
		return ::FLAC__stream_decoder_init_ogg_stream(decoder_, read_callback_, seek_callback_, tell_callback_, length_callback_, eof_callback_, write_callback_, metadata_callback_, error_callback_, /*client_data=*/(void*)this);
	}

	bool Stream::finish()
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_decoder_finish(decoder_);
	}

	bool Stream::flush()
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_decoder_flush(decoder_);
	}

	bool Stream::reset()
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_decoder_reset(decoder_);
	}

	bool Stream::process_single()
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_decoder_process_single(decoder_);
	}

	bool Stream::process_until_end_of_metadata()
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_decoder_process_until_end_of_metadata(decoder_);
	}

	bool Stream::process_until_end_of_stream()
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_decoder_process_until_end_of_stream(decoder_);
	}

	bool Stream::skip_single_frame()
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_decoder_skip_single_frame(decoder_);
	}

	bool Stream::seek_absolute(FLAC__uint64 sample)
	{
		FLAC__ASSERT(is_valid());
		return (bool)::FLAC__stream_decoder_seek_absolute(decoder_, sample);
	}

	::FLAC__StreamDecoderSeekStatus Stream::seek_callback(FLAC__uint64 absolute_byte_offset)
	{
		(void)absolute_byte_offset;
		return ::FLAC__STREAM_DECODER_SEEK_STATUS_UNSUPPORTED;
	}

	::FLAC__StreamDecoderTellStatus Stream::tell_callback(FLAC__uint64 *absolute_byte_offset)
	{
		(void)absolute_byte_offset;
		return ::FLAC__STREAM_DECODER_TELL_STATUS_UNSUPPORTED;
	}

	::FLAC__StreamDecoderLengthStatus Stream::length_callback(FLAC__uint64 *stream_length)
	{
		(void)stream_length;
		return ::FLAC__STREAM_DECODER_LENGTH_STATUS_UNSUPPORTED;
	}

	bool Stream::eof_callback()
	{
		return false;
	}

	void Stream::metadata_callback(const ::FLAC__StreamMetadata *metadata)
	{
		(void)metadata;
	}

	::FLAC__StreamDecoderReadStatus Stream::read_callback_(const ::FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data)
	{
		(void)decoder;
		FLAC__ASSERT(0 != client_data);
		Stream *instance = reinterpret_cast<Stream *>(client_data);
		FLAC__ASSERT(0 != instance);
		return instance->read_callback(buffer, bytes);
	}

	::FLAC__StreamDecoderSeekStatus Stream::seek_callback_(const ::FLAC__StreamDecoder *decoder, FLAC__uint64 absolute_byte_offset, void *client_data)
	{
		(void) decoder;
		FLAC__ASSERT(0 != client_data);
		Stream *instance = reinterpret_cast<Stream *>(client_data);
		FLAC__ASSERT(0 != instance);
		return instance->seek_callback(absolute_byte_offset);
	}

	::FLAC__StreamDecoderTellStatus Stream::tell_callback_(const ::FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *client_data)
	{
		(void) decoder;
		FLAC__ASSERT(0 != client_data);
		Stream *instance = reinterpret_cast<Stream *>(client_data);
		FLAC__ASSERT(0 != instance);
		return instance->tell_callback(absolute_byte_offset);
	}

	::FLAC__StreamDecoderLengthStatus Stream::length_callback_(const ::FLAC__StreamDecoder *decoder, FLAC__uint64 *stream_length, void *client_data)
	{
		(void) decoder;
		FLAC__ASSERT(0 != client_data);
		Stream *instance = reinterpret_cast<Stream *>(client_data);
		FLAC__ASSERT(0 != instance);
		return instance->length_callback(stream_length);
	}

	FLAC__bool Stream::eof_callback_(const ::FLAC__StreamDecoder *decoder, void *client_data)
	{
		(void) decoder;
		FLAC__ASSERT(0 != client_data);
		Stream *instance = reinterpret_cast<Stream *>(client_data);
		FLAC__ASSERT(0 != instance);
		return instance->eof_callback();
	}

	::FLAC__StreamDecoderWriteStatus Stream::write_callback_(const ::FLAC__StreamDecoder *decoder, const ::FLAC__Frame *frame, const FLAC__int32 * const buffer[], void *client_data)
	{
		(void)decoder;
		FLAC__ASSERT(0 != client_data);
		Stream *instance = reinterpret_cast<Stream *>(client_data);
		FLAC__ASSERT(0 != instance);
		return instance->write_callback(frame, buffer);
	}

	void Stream::metadata_callback_(const ::FLAC__StreamDecoder *decoder, const ::FLAC__StreamMetadata *metadata, void *client_data)
	{
		(void)decoder;
		FLAC__ASSERT(0 != client_data);
		Stream *instance = reinterpret_cast<Stream *>(client_data);
		FLAC__ASSERT(0 != instance);
		instance->metadata_callback(metadata);
	}

	void Stream::error_callback_(const ::FLAC__StreamDecoder *decoder, ::FLAC__StreamDecoderErrorStatus status, void *client_data)
	{
		(void)decoder;
		FLAC__ASSERT(0 != client_data);
		Stream *instance = reinterpret_cast<Stream *>(client_data);
		FLAC__ASSERT(0 != instance);
		instance->error_callback(status);
	}

	// ------------------------------------------------------------
	//
	// File
	//
	// ------------------------------------------------------------

	File::File():
	Stream()
	{ }

	File::~File()
	{
	}

	::FLAC__StreamDecoderInitStatus File::init(FILE *file)
	{
		FLAC__ASSERT(0 != decoder_);
		return ::FLAC__stream_decoder_init_FILE(decoder_, file, write_callback_, metadata_callback_, error_callback_, /*client_data=*/(void*)this);
	}

	::FLAC__StreamDecoderInitStatus File::init(const char *filename)
	{
		FLAC__ASSERT(0 != decoder_);
		return ::FLAC__stream_decoder_init_file(decoder_, filename, write_callback_, metadata_callback_, error_callback_, /*client_data=*/(void*)this);
	}

	::FLAC__StreamDecoderInitStatus File::init(const std::string &filename)
	{
		return init(filename.c_str());
	}

	::FLAC__StreamDecoderInitStatus File::init_ogg(FILE *file)
	{
		FLAC__ASSERT(0 != decoder_);
		return ::FLAC__stream_decoder_init_ogg_FILE(decoder_, file, write_callback_, metadata_callback_, error_callback_, /*client_data=*/(void*)this);
	}

	::FLAC__StreamDecoderInitStatus File::init_ogg(const char *filename)
	{
		FLAC__ASSERT(0 != decoder_);
		return ::FLAC__stream_decoder_init_ogg_file(decoder_, filename, write_callback_, metadata_callback_, error_callback_, /*client_data=*/(void*)this);
	}

	::FLAC__StreamDecoderInitStatus File::init_ogg(const std::string &filename)
	{
		return init_ogg(filename.c_str());
	}

	// This is a dummy to satisfy the pure virtual from Stream; the
	// read callback will never be called since we are initializing
	// with FLAC__stream_decoder_init_FILE() or
	// FLAC__stream_decoder_init_file() and those supply the read
	// callback internally.
	::FLAC__StreamDecoderReadStatus File::read_callback(FLAC__byte buffer[], size_t *bytes)
	{
		(void)buffer, (void)bytes;
		FLAC__ASSERT(false);
		return ::FLAC__STREAM_DECODER_READ_STATUS_ABORT; // double protection
	}

	}
}

//++-- stream_decoder.cpp 
