/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "memory/memory_region.h"

#include <array>

#define MEMORY_REGION_STEALABLE 0
#define MEMORY_REGION_INTERNAL 1
#define MEMORY_REGION_EXTERNAL 2
#define MEMORY_REGION_EXTERNAL_SMALL 3
#define MEMORY_REGION_INTERNAL_SMALL 4
#define NUM_MEMORY_REGIONS 5
constexpr uint32_t RESERVED_EXTERNAL_ALLOCATOR = 0x00200000;       // 2 MiB
constexpr uint32_t RESERVED_EXTERNAL_SMALL_ALLOCATOR = 0x00020000; // 200k
constexpr uint32_t RESERVED_INTERNAL_SMALL = 0x00010000;           // 200k
class Stealable;

/*
 * ======================= MEMORY ALLOCATION ========================
 *
 * The Deluge codebase uses a custom memory allocation system, largely necessitated by the fact that
 * the Deluge’s CPU has 3MB ram, plus the Deluge has an external 64MB SDRAM IC, and both of these
 * need to have dynamic memory allocation as part of the same system.
 *
 * The internal RAM on the CPU is a bit faster, so is allocated first when available.
 * But huge blocks of data like cached Clusters of audio data from the SD card are always
 * placed on the external RAM IC because they would overwhelm the internal RAM too quickly,
 * preventing potentially thousands of small objects which need to be accessed all the time
 * from being placed in that fast internal RAM.
 *
 * Various objects or pieces of data remain loaded (cached) in RAM even when they are no longer necessarily needed.
 * The main example of this is audio data in Clusters, discussed above. The base class for all such
 * objects is Stealable, and as the name suggests, their memory may usually be “stolen” when needed.
 *
 * Most Stealables store a “numReasonsToBeLoaded”, which counts how many “things” are requiring
 * that object to be retained in RAM. E.g. a Cluster of audio data would have a “reason” to
 * remain loaded in RAM if it is currently being played back. If that numReasons goes down to 0,
 * then that Stealable object is usually free to have its memory stolen.
 *
 * Stealables which in fact are eligible to be stolen at a given moment are stored in a queue which
 * prioritises stealing of the audio data which is less likely to be needed, e.g. if it belongs to a
 * Song that’s no longer loaded. But, to avoid overcomplication, this queue is not adhered to in the
 * case where a neighbouring region of memory is chosen for allocation (or itself being stolen) when
 * the allocation requires that the object in question have its memory stolen too in order to make
 * up a large enough allocation.
 */

enum class AllocationTag : uint8_t {
	GENERIC = 0,
	WAVETABLE_TEMP,
	SAMPLE_PERC_CACHE,
	VOICE,
	VOICE_POOL,
	VOICE_SAMPLE_POOL,
	TIME_STRETCHER_POOL,
	TIME_STRETCHER_BUFFER,
	DX7_VOICE_POOL,
	SONG_LOAD_TEMP,
	DX7,
	DX7_ENGINE,
	DX7_PATCH,
	AUDIO_CLIP,
	CLIP_ARRAY,
	CLIP_INSTANCE,
	SAMPLE_BROWSER_SAMPLE,
	SAMPLE_BROWSER_SOUND_DRUM,
	INSTRUMENT_CLIP,
	SOUND_DRUM,
	SLICER_SOUND_DRUM,
	DRUM_CREATOR_SOUND_DRUM,
	MIDI_DRUM,
	GATE_DRUM,
	SONG,
	LOAD_SONG,
	CLEAR_SONG,
	AUDIO_FILE,
	FATFS,
	SAMPLE,
	SAMPLE_CLUSTER_ARRAY,
	SOUND_ARRAY,
	SOUND_INSTRUMENT,
	MIDI_INSTRUMENT,
	CV_INSTRUMENT,
	KIT,
	AUDIO_OUTPUT,
	NOTE,
	NOTE_ROW,
	ARPEGGIATOR_NOTE,
	ARPEGGIATOR_NOTE_AS_PLAYED,
	ARPEGGIATOR_NOTE_BY_PATTERN,
	RUNTIME_FEATURE_SETTING,
	MULTI_RANGE,
	POINTER_ARRAY,
	SAMPLE_RECORDER,
	LIVE_INPUT_BUFFER,
	INPUT_REPITCHED_BUFFER,
	MOD_FX_BUFFER,
	BROWSER_FILE_ITEMS,
	GRAIN_BUFFER,
	GRANULAR_PROCESSOR,
	RESIZEABLE_ARRAY,
	OUTPUT_HASH_TABLE,
	MIDI_PARAM_COLLECTION,
	UNPATCHED_PARAM_SET,
	PATCHED_PARAM_SET,
	PATCH_CABLE_SET,
	PARAM_NODE,
	PATCH_CABLE_DESTINATION,
	LIVE_PITCH_SHIFTER,
	MIDI_DEVICE_LUMI_KEYS,
	MIDI_CABLE_USB_HOSTED,
	ACTION_NEW,
	ACTION_CLIP_STATE,
	CONSEQUENCE_RECORD_SWING_CHANGE,
	CONSEQUENCE_RECORD_TEMPO_CHANGE,
	CONSEQUENCE_RECORD_PERFORMANCE_VIEW_PRESS,
	CONSEQUENCE_CLIP_HORIZONTAL_SHIFT,
	CONSEQUENCE_CLIP_INSTANCE_CHANGE,
	CONSEQUENCE_CREATE_OUTPUT_LINEAR_RECORD,
	CONSEQUENCE_CLIP_BEGIN_LINEAR_RECORD,
	CONSEQUENCE_NOTE_ROW_MUTE,
	CONSEQUENCE_SCALE_ADD_NOTE,
	CONSEQUENCE_PARAM_CHANGE,
	CONSEQUENCE_NOTE_ARRAY_CHANGE,
	CONSEQUENCE_NOTE_EXISTENCE,
	CONSEQUENCE_CLIP_INSTANCE_EXISTENCE,
	CONSEQUENCE_CLIP_LENGTH,
	CONSEQUENCE_CLIP_EXISTENCE,
	CONSEQUENCE_AUDIO_CLIP_SET_SAMPLE,
	CONSEQUENCE,
	REGION_STEALABLE,
	REGION_INTERNAL,
	REGION_EXTERNAL,
	REGION_EXTERNAL_SMALL,
	REGION_INTERNAL_SMALL,
	DELAY_BUFFER,
	OTHER,
	NUM_TAGS,
};

static constexpr const char* allocationTagName(AllocationTag tag) {
	switch (tag) {
	case AllocationTag::GENERIC:
		return "generic";
	case AllocationTag::WAVETABLE_TEMP:
		return "wavetable_temp";
	case AllocationTag::SAMPLE_PERC_CACHE:
		return "sample_perc_cache";
	case AllocationTag::VOICE:
		return "voice";
	case AllocationTag::VOICE_POOL:
		return "voice_pool";
	case AllocationTag::VOICE_SAMPLE_POOL:
		return "voice_sample_pool";
	case AllocationTag::TIME_STRETCHER_POOL:
		return "time_stretcher_pool";
	case AllocationTag::TIME_STRETCHER_BUFFER:
		return "time_stretcher_buffer";
	case AllocationTag::DX7_VOICE_POOL:
		return "dx7_voice_pool";
	case AllocationTag::SONG_LOAD_TEMP:
		return "song_load_temp";
	case AllocationTag::DX7:
		return "dx7";
	case AllocationTag::DX7_ENGINE:
		return "dx7_engine";
	case AllocationTag::DX7_PATCH:
		return "dx7_patch";
	case AllocationTag::AUDIO_CLIP:
		return "audio_clip";
	case AllocationTag::CLIP_ARRAY:
		return "clip_array";
	case AllocationTag::CLIP_INSTANCE:
		return "clip_instance";
	case AllocationTag::INSTRUMENT_CLIP:
		return "instrument_clip";
	case AllocationTag::SOUND_DRUM:
		return "sound_drum";
	case AllocationTag::SLICER_SOUND_DRUM:
		return "slicer_sound_drum";
	case AllocationTag::DRUM_CREATOR_SOUND_DRUM:
		return "drum_creator_sound_drum";
	case AllocationTag::MIDI_DRUM:
		return "midi_drum";
	case AllocationTag::GATE_DRUM:
		return "gate_drum";
	case AllocationTag::SONG:
		return "song";
	case AllocationTag::LOAD_SONG:
		return "load_song";
	case AllocationTag::CLEAR_SONG:
		return "clear_song";
	case AllocationTag::AUDIO_FILE:
		return "audio_file";
	case AllocationTag::FATFS:
		return "fatfs";
	case AllocationTag::SAMPLE:
		return "sample";
	case AllocationTag::SAMPLE_CLUSTER_ARRAY:
		return "sample_cluster_array";
	case AllocationTag::SAMPLE_BROWSER_SAMPLE:
		return "sample_browser_sample";
	case AllocationTag::SAMPLE_BROWSER_SOUND_DRUM:
		return "sample_browser_sound_drum";
	case AllocationTag::SOUND_ARRAY:
		return "sound_array";
	case AllocationTag::SOUND_INSTRUMENT:
		return "sound_instrument";
	case AllocationTag::MIDI_INSTRUMENT:
		return "midi_instrument";
	case AllocationTag::CV_INSTRUMENT:
		return "cv_instrument";
	case AllocationTag::KIT:
		return "kit";
	case AllocationTag::AUDIO_OUTPUT:
		return "audio_output";
	case AllocationTag::NOTE:
		return "note";
	case AllocationTag::NOTE_ROW:
		return "note_row";
	case AllocationTag::ARPEGGIATOR_NOTE:
		return "arpeggiator_note";
	case AllocationTag::ARPEGGIATOR_NOTE_AS_PLAYED:
		return "arpeggiator_note_as_played";
	case AllocationTag::ARPEGGIATOR_NOTE_BY_PATTERN:
		return "arpeggiator_note_by_pattern";
	case AllocationTag::RUNTIME_FEATURE_SETTING:
		return "runtime_feature_setting";
	case AllocationTag::MULTI_RANGE:
		return "multi_range";
	case AllocationTag::POINTER_ARRAY:
		return "pointer_array";
	case AllocationTag::SAMPLE_RECORDER:
		return "sample_recorder";
	case AllocationTag::LIVE_INPUT_BUFFER:
		return "live_input_buffer";
	case AllocationTag::INPUT_REPITCHED_BUFFER:
		return "input_repitched_buffer";
	case AllocationTag::MOD_FX_BUFFER:
		return "mod_fx_buffer";
	case AllocationTag::BROWSER_FILE_ITEMS:
		return "browser_file_items";
	case AllocationTag::GRAIN_BUFFER:
		return "grain_buffer";
	case AllocationTag::GRANULAR_PROCESSOR:
		return "granular_processor";
	case AllocationTag::RESIZEABLE_ARRAY:
		return "resizeable_array";
	case AllocationTag::OUTPUT_HASH_TABLE:
		return "output_hash_table";
	case AllocationTag::MIDI_PARAM_COLLECTION:
		return "midi_param_collection";
	case AllocationTag::UNPATCHED_PARAM_SET:
		return "unpatched_param_set";
	case AllocationTag::PATCHED_PARAM_SET:
		return "patched_param_set";
	case AllocationTag::PATCH_CABLE_SET:
		return "patch_cable_set";
	case AllocationTag::PARAM_NODE:
		return "param_node";
	case AllocationTag::PATCH_CABLE_DESTINATION:
		return "patch_cable_destination";
	case AllocationTag::LIVE_PITCH_SHIFTER:
		return "live_pitch_shifter";
	case AllocationTag::MIDI_DEVICE_LUMI_KEYS:
		return "midi_device_lumi_keys";
	case AllocationTag::MIDI_CABLE_USB_HOSTED:
		return "midi_cable_usb_hosted";
	case AllocationTag::ACTION_NEW:
		return "action_new";
	case AllocationTag::ACTION_CLIP_STATE:
		return "action_clip_state";
	case AllocationTag::CONSEQUENCE_RECORD_SWING_CHANGE:
		return "consequence_record_swing_change";
	case AllocationTag::CONSEQUENCE_RECORD_TEMPO_CHANGE:
		return "consequence_record_tempo_change";
	case AllocationTag::CONSEQUENCE_RECORD_PERFORMANCE_VIEW_PRESS:
		return "consequence_record_performance_view_press";
	case AllocationTag::CONSEQUENCE_CLIP_HORIZONTAL_SHIFT:
		return "consequence_clip_horizontal_shift";
	case AllocationTag::CONSEQUENCE_CLIP_INSTANCE_CHANGE:
		return "consequence_clip_instance_change";
	case AllocationTag::CONSEQUENCE_CREATE_OUTPUT_LINEAR_RECORD:
		return "consequence_create_output_linear_record";
	case AllocationTag::CONSEQUENCE_CLIP_BEGIN_LINEAR_RECORD:
		return "consequence_clip_begin_linear_record";
	case AllocationTag::CONSEQUENCE_NOTE_ROW_MUTE:
		return "consequence_note_row_mute";
	case AllocationTag::CONSEQUENCE_SCALE_ADD_NOTE:
		return "consequence_scale_add_note";
	case AllocationTag::CONSEQUENCE_PARAM_CHANGE:
		return "consequence_param_change";
	case AllocationTag::CONSEQUENCE_NOTE_ARRAY_CHANGE:
		return "consequence_note_array_change";
	case AllocationTag::CONSEQUENCE_NOTE_EXISTENCE:
		return "consequence_note_existence";
	case AllocationTag::CONSEQUENCE_CLIP_INSTANCE_EXISTENCE:
		return "consequence_clip_instance_existence";
	case AllocationTag::CONSEQUENCE_CLIP_LENGTH:
		return "consequence_clip_length";
	case AllocationTag::CONSEQUENCE_CLIP_EXISTENCE:
		return "consequence_clip_existence";
	case AllocationTag::CONSEQUENCE_AUDIO_CLIP_SET_SAMPLE:
		return "consequence_audio_clip_set_sample";
	case AllocationTag::CONSEQUENCE:
		return "consequence";
	case AllocationTag::REGION_STEALABLE:
		return "region_stealable";
	case AllocationTag::REGION_INTERNAL:
		return "region_internal";
	case AllocationTag::REGION_EXTERNAL:
		return "region_external";
	case AllocationTag::REGION_EXTERNAL_SMALL:
		return "region_external_small";
	case AllocationTag::REGION_INTERNAL_SMALL:
		return "region_internal_small";
	case AllocationTag::DELAY_BUFFER:
		return "delay_buffer";
	case AllocationTag::OTHER:
		return "other";
	case AllocationTag::NUM_TAGS:
		return "invalid";
	default:
		return "unknown";
	}
}

struct AllocationRecord {
	void* address = nullptr;
	uint32_t size = 0;
	uint8_t region = 0;
	uint8_t tag = 0;
};

class GeneralMemoryAllocator {
public:
	GeneralMemoryAllocator();
	[[gnu::always_inline]] void* allocMaxSpeed(uint32_t requiredSize, void* thingNotToStealFrom = nullptr) {
		return alloc(requiredSize, true, false, thingNotToStealFrom, AllocationTag::GENERIC);
	}

	[[gnu::always_inline]] void* allocMaxSpeedTagged(uint32_t requiredSize, AllocationTag tag,
	                                                 void* thingNotToStealFrom = nullptr) {
		return alloc(requiredSize, true, false, thingNotToStealFrom, tag);
	}

	[[gnu::always_inline]] void* allocLowSpeed(uint32_t requiredSize, void* thingNotToStealFrom = nullptr) {
		return alloc(requiredSize, false, false, thingNotToStealFrom, AllocationTag::GENERIC);
	}

	[[gnu::always_inline]] void* allocLowSpeedTagged(uint32_t requiredSize, AllocationTag tag,
	                                                 void* thingNotToStealFrom = nullptr) {
		return alloc(requiredSize, false, false, thingNotToStealFrom, tag);
	}

	[[gnu::always_inline]] void* allocStealable(uint32_t requiredSize, void* thingNotToStealFrom = nullptr) {
		return alloc(requiredSize, false, true, thingNotToStealFrom, AllocationTag::GENERIC);
	}

	[[gnu::always_inline]] void* allocStealableTagged(uint32_t requiredSize, AllocationTag tag,
	                                                  void* thingNotToStealFrom = nullptr) {
		return alloc(requiredSize, false, true, thingNotToStealFrom, tag);
	}

	void* alloc(uint32_t requiredSize, bool mayUseOnChipRam, bool makeStealable, void* thingNotToStealFrom,
	            AllocationTag tag);
	void dealloc(void* address);
	void* allocExternal(uint32_t requiredSize);
	void* allocInternal(uint32_t requiredSize);
	void deallocExternal(void* address);
	uint32_t shortenRight(void* address, uint32_t newSize);
	uint32_t shortenLeft(void* address, uint32_t amountToShorten, uint32_t numBytesToMoveRightIfSuccessful = 0);
	void extend(void* address, uint32_t minAmountToExtend, uint32_t idealAmountToExtend,
	            uint32_t* getAmountExtendedLeft, uint32_t* getAmountExtendedRight, void* thingNotToStealFrom = nullptr);
	uint32_t extendRightAsMuchAsEasilyPossible(void* address);
	void test();
	uint32_t getAllocatedSize(void* address);
#if ALPHA_OR_BETA_VERSION
	void debugPrintMemoryUsage(char const* label);
	void debugPrintTagUsage(char const* label);
#endif
	void checkStack(char const* caller);
	void testShorten(int32_t i);
	int32_t getRegion(void* address);
	void testMemoryDeallocated(void* address);

	void putStealableInQueue(Stealable* stealable, StealableQueue q);
	void putStealableInAppropriateQueue(Stealable* stealable);

	MemoryRegion regions[NUM_MEMORY_REGIONS];
	// only used for managing stealables (audio files that we could deallocate and re load from sd later if needed)
	CacheManager cacheManager;
	bool lock;

#if ALPHA_OR_BETA_VERSION
	std::array<AllocationRecord, 1024> trackedAllocations{};
	uint32_t trackedAllocationCount = 0;
	void trackAllocation(void* address, uint32_t size, int32_t region, AllocationTag tag);
	void untrackAllocation(void* address);
#endif

	static GeneralMemoryAllocator& get() {
		static GeneralMemoryAllocator generalMemoryAllocator;
		return generalMemoryAllocator;
	}

private:
	void checkEverythingOk(char const* errorString);
};

extern "C" {
void* delugeAlloc(unsigned int requiredSize, bool mayUseOnChipRam = true);
void* delugeAllocTagged(unsigned int requiredSize, bool mayUseOnChipRam, AllocationTag tag);
void* delugeAllocFatFs(unsigned int requiredSize, bool mayUseOnChipRam = true);
void delugeDealloc(void* address);
}
