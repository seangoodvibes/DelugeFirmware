#include "CppUTest/TestHarness.h"
#include "util/lifetime.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>

#define ALPHA_OR_BETA_VERSION 0
#define FREEZE_WITH_ERROR(code) FAIL(code)
#define D_PRINTLN(...)                                                                                                 \
	do {                                                                                                               \
	} while (0)
namespace sample_assignment_test {
constexpr int CLUSTER_ENQUEUE = 1, kNumClustersLoadedAhead = 2, kMaxNumClustersLoadedAhead = 4;
constexpr int kMarkerSamplesBeforeToClaim = 150;
struct String {
	bool isEmpty() const { return true; }
	void set(const String*) {}
};
struct AudioFile {
	int reasons = 0;
	int numChannels = 1;
	void addReason() { ++reasons; }
	void removeReason(const char*) {
		CHECK(reasons > 0);
		--reasons;
	}
};
struct Cluster {
	static constexpr int size_magnitude = 8, size = 256;
	int reasons = 0;
	bool loaded = true;
};
std::function<void(int)> on_cluster;
std::function<void()> on_sample_set;
int acquisitions = 0, recalculations = 0, failed_cluster = -1;
struct Sample;
struct SampleCluster {
	Cluster value;
	Cluster* getCluster(Sample* sample, int index, int);
};
struct Sample : AudioFile {
	struct {
		std::array<SampleCluster, 4> slots;
		SampleCluster* getElement(int index) { return &slots.at(index); }
		int getNumElements() { return slots.size(); }
	} clusters;
	String tempFilePathForRecording;
	uint32_t lengthInSamples = 1024, audioDataStartPosBytes = 0;
	int byteDepth = 1;
	int getFirstClusterIndexWithAudioData() { return 0; }
	int getFirstClusterIndexWithNoAudioData() { return 4; }
};
Cluster* SampleCluster::getCluster(Sample* sample, int index, int) {
	CHECK(sample->reasons >= 2); // Holder plus temporary sample pin.
	++acquisitions;
	if (index == failed_cluster)
		return nullptr;
	auto* result = &value;
	++result->reasons;
	if (on_cluster)
		on_cluster(index);
	CHECK(sample->reasons >= 1); // The holder may now be gone; the pin must remain.
	return result;
}
struct {
	void removeReasonFromCluster(Cluster& cluster, const char*) {
		CHECK(cluster.reasons > 0);
		--cluster.reasons;
	}
} audioFileManager;
struct {
	String lastFilePathLoaded;
} browser;
auto& sample_browser_for_session() {
	return browser;
}
struct AudioFileHolder {
	AudioFile* audioFile = nullptr;
	String filePath;
	virtual ~AudioFileHolder() {
		if (audioFile)
			audioFile->removeReason("test");
	}
	virtual void unassignAllClusterReasons(bool = false) {}
	virtual bool setAudioFile(AudioFile*, bool = false, bool = false, int = CLUSTER_ENQUEUE,
	                          const deluge::lifetime::callback_validation* = nullptr);
};
#include "audio_file_assignment.inc"
struct SampleHolder : AudioFileHolder {
	deluge::lifetime::lifetime_source lifetime;
	uint64_t startPos = 0, endPos = 1024;
	int32_t startPlaybackAtSample = 0;
	uint32_t startPlaybackAtByte = 0;
	Cluster* clustersForStart[kNumClustersLoadedAhead]{};
	~SampleHolder() override {
		lifetime.retire();
		unassignAllClusterReasons();
	}
	void unassignAllClusterReasons(bool = false) override {
		for (auto*& cluster : clustersForStart) {
			if (cluster)
				audioFileManager.removeReasonFromCluster(*cluster, "test");
			cluster = nullptr;
		}
	}
	int64_t getEndPos() { return endPos; }
	uint32_t getDurationInSamples() { return endPos - startPos; }
	void sampleBeenSet(bool, bool) {
		if (on_sample_set)
			on_sample_set();
	}
	bool setAudioFile(AudioFile*, bool, bool, int, const deluge::lifetime::callback_validation*) override;
	virtual bool claimClusterReasons(bool, int, const deluge::lifetime::callback_validation*);
	bool claimClusterReasonsForMarker(Cluster**, uint32_t, int32_t, int32_t, int32_t,
	                                  const deluge::lifetime::callback_validation*);
};
#include "sample_assignment.inc"
struct SampleHolderForClip : SampleHolder {
	bool setAudioFile(AudioFile*, bool, bool, int, const deluge::lifetime::callback_validation*) override;
	void recalculateNeutralPhaseIncrement() { ++recalculations; }
};
#include "clip_sample_assignment.inc"
struct SampleHolderForVoice : SampleHolder {
	int loopEndPos = 0, loopStartPos = 0;
	Cluster* clustersForLoopStart[kMaxNumClustersLoadedAhead]{};
	~SampleHolderForVoice() override {
		for (auto* cluster : clustersForLoopStart)
			if (cluster)
				audioFileManager.removeReasonFromCluster(*cluster, "test");
	}
	bool claimClusterReasons(bool, int, const deluge::lifetime::callback_validation*) override;
};
#include "voice_sample_assignment.inc"
} // namespace sample_assignment_test
#undef ALPHA_OR_BETA_VERSION
#undef FREEZE_WITH_ERROR
#undef D_PRINTLN
using namespace sample_assignment_test;
TEST_GROUP(SampleAssignment) {
	Sample sample;
	void setup() override {
		on_cluster = {};
		on_sample_set = {};
		acquisitions = recalculations = 0;
		failed_cluster = -1;
	}
	void teardown() override {
		on_cluster = {};
		on_sample_set = {};
	}
	void check_released() {
		LONGS_EQUAL(0, sample.reasons);
		for (auto& slot : sample.clusters.slots)
			LONGS_EQUAL(0, slot.value.reasons);
	}
};

TEST(SampleAssignment, destroyed_clip_holder_releases_temporary_cluster_and_sample_reasons) {
	auto holder = std::make_unique<SampleHolderForClip>();
	deluge::lifetime::lifetime_watch watch(holder->lifetime);
	auto valid = [&] { return watch.alive(); };
	deluge::lifetime::callback_validation validation(valid);
	on_cluster = [&](int) { holder.reset(); };
	CHECK_FALSE(holder->setAudioFile(&sample, false, false, CLUSTER_ENQUEUE, &validation));
	LONGS_EQUAL(1, acquisitions);
	LONGS_EQUAL(0, recalculations);
	check_released();
}

TEST(SampleAssignment, second_cluster_cancellation_releases_both_temporary_reasons) {
	auto holder = std::make_unique<SampleHolderForClip>();
	deluge::lifetime::lifetime_watch watch(holder->lifetime);
	auto valid = [&] { return watch.alive(); };
	deluge::lifetime::callback_validation validation(valid);
	on_cluster = [&](int index) {
		if (index == 1)
			holder.reset();
	};
	CHECK_FALSE(holder->setAudioFile(&sample, false, false, CLUSTER_ENQUEUE, &validation));
	LONGS_EQUAL(2, acquisitions);
	check_released();
}

TEST(SampleAssignment, changed_markers_preserve_existing_cluster_reasons) {
	SampleHolderForClip holder;
	auto valid = [] { return true; };
	deluge::lifetime::callback_validation validation(valid);
	CHECK(holder.setAudioFile(&sample, false, false, CLUSTER_ENQUEUE, &validation));
	auto* original = holder.clustersForStart[0];
	on_cluster = [&](int) { ++holder.endPos; };
	CHECK_FALSE(holder.claimClusterReasons(false, CLUSTER_ENQUEUE, &validation));
	POINTERS_EQUAL(original, holder.clustersForStart[0]);
	LONGS_EQUAL(1, original->reasons);
	LONGS_EQUAL(1, sample.reasons);
}

TEST(SampleAssignment, successful_clip_assignment_transfers_cluster_reasons_and_recalculates) {
	{
		SampleHolderForClip holder;
		CHECK(holder.setAudioFile(&sample, false, false, CLUSTER_ENQUEUE, nullptr));
		LONGS_EQUAL(1, sample.reasons);
		LONGS_EQUAL(1, recalculations);
		for (int index = 0; index < 2; ++index)
			LONGS_EQUAL(1, sample.clusters.slots[index].value.reasons);
	}
	check_released();
}

TEST(SampleAssignment, voice_loop_acquisition_cancellation_releases_published_and_temporary_reasons) {
	auto holder = std::make_unique<SampleHolderForVoice>();
	deluge::lifetime::lifetime_watch watch(holder->lifetime);
	auto valid = [&] { return watch.alive(); };
	deluge::lifetime::callback_validation validation(valid);
	on_cluster = [&](int index) {
		if (index == 2)
			holder.reset();
	};
	CHECK_FALSE(holder->setAudioFile(&sample, false, false, CLUSTER_ENQUEUE, &validation));
	LONGS_EQUAL(3, acquisitions);
	check_released();
}

TEST(SampleAssignment, sample_configuration_cancellation_skips_cluster_acquisition) {
	auto holder = std::make_unique<SampleHolderForClip>();
	deluge::lifetime::lifetime_watch watch(holder->lifetime);
	auto valid = [&] { return watch.alive(); };
	deluge::lifetime::callback_validation validation(valid);
	on_sample_set = [&] { holder.reset(); };
	CHECK_FALSE(holder->setAudioFile(&sample, false, false, CLUSTER_ENQUEUE, &validation));
	LONGS_EQUAL(0, acquisitions);
	LONGS_EQUAL(0, recalculations);
	check_released();
}

TEST(SampleAssignment, nested_sample_assignment_preserves_new_sample_and_releases_old_temporaries) {
	Sample replacement;
	{
		SampleHolderForClip holder;
		auto valid = [] { return true; };
		deluge::lifetime::callback_validation validation(valid);
		bool nested = false;
		on_cluster = [&](int) {
			if (!nested) {
				nested = true;
				CHECK(holder.setAudioFile(&replacement, false, false, CLUSTER_ENQUEUE, &validation));
			}
		};
		CHECK_FALSE(holder.setAudioFile(&sample, false, false, CLUSTER_ENQUEUE, &validation));
		POINTERS_EQUAL(&replacement, holder.audioFile);
		LONGS_EQUAL(1, replacement.reasons);
		check_released();
	}
	LONGS_EQUAL(0, replacement.reasons);
	for (auto& slot : replacement.clusters.slots)
		LONGS_EQUAL(0, slot.value.reasons);
}

TEST(SampleAssignment, nested_cluster_replacement_is_not_overwritten_by_outer_acquisition) {
	SampleHolderForClip holder;
	auto valid = [] { return true; };
	deluge::lifetime::callback_validation validation(valid);
	CHECK(holder.setAudioFile(&sample, false, false, CLUSTER_ENQUEUE, &validation));
	auto* replacement = &sample.clusters.slots[3].value;
	on_cluster = [&](int) {
		audioFileManager.removeReasonFromCluster(*holder.clustersForStart[0], "test");
		++replacement->reasons;
		holder.clustersForStart[0] = replacement;
	};
	CHECK_FALSE(holder.claimClusterReasons(false, CLUSTER_ENQUEUE, &validation));
	POINTERS_EQUAL(replacement, holder.clustersForStart[0]);
	LONGS_EQUAL(1, replacement->reasons);
	LONGS_EQUAL(0, sample.clusters.slots[0].value.reasons);
}

TEST(SampleAssignment, invalid_assignment_does_not_acquire_sample_or_clusters) {
	SampleHolderForClip holder;
	auto valid = [] { return false; };
	deluge::lifetime::callback_validation validation(valid);
	CHECK_FALSE(holder.setAudioFile(&sample, false, false, CLUSTER_ENQUEUE, &validation));
	LONGS_EQUAL(0, acquisitions);
	LONGS_EQUAL(0, recalculations);
	check_released();
}

TEST(SampleAssignment, successful_voice_assignment_transfers_start_and_loop_reasons) {
	{
		SampleHolderForVoice holder;
		CHECK(holder.setAudioFile(&sample, false, false, CLUSTER_ENQUEUE, nullptr));
		LONGS_EQUAL(4, acquisitions);
		LONGS_EQUAL(1, sample.reasons);
		for (auto& slot : sample.clusters.slots)
			LONGS_EQUAL(1, slot.value.reasons);
	}
	check_released();
}

TEST(SampleAssignment, changed_voice_loop_marker_cancels_without_publishing_loop_clusters) {
	for (int changed_at : {0, 2}) {
		{
			SampleHolderForVoice holder;
			on_cluster = [&](int index) {
				if (index == changed_at)
					++holder.loopEndPos;
			};
			auto valid = [] { return true; };
			deluge::lifetime::callback_validation validation(valid);
			CHECK_FALSE(holder.setAudioFile(&sample, false, false, CLUSTER_ENQUEUE, &validation));
			for (auto* cluster : holder.clustersForLoopStart)
				POINTERS_EQUAL(nullptr, cluster);
			LONGS_EQUAL(changed_at == 0 ? 0 : 1, sample.clusters.slots[0].value.reasons);
		}
		check_released();
	}
}

TEST(SampleAssignment, unavailable_second_cluster_preserves_existing_partial_load_behavior) {
	failed_cluster = 1;
	{
		SampleHolderForClip holder;
		CHECK(holder.setAudioFile(&sample, false, false, CLUSTER_ENQUEUE, nullptr));
		POINTERS_EQUAL(&sample.clusters.slots[0].value, holder.clustersForStart[0]);
		POINTERS_EQUAL(nullptr, holder.clustersForStart[1]);
		LONGS_EQUAL(1, sample.reasons);
	}
	check_released();
}
