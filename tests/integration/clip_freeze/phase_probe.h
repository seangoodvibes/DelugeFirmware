// SPDX-License-Identifier: GPL-3.0-or-later
// Emulator experiment only: never included by the normal firmware build.
#include "definitions.h"
#include <algorithm>
#include <cmath>

struct FreezeProbe {
	static constexpr int capacity = 34 * 44100;
	struct Row {
		int32_t endSample, cacheSample, matchingRun, rejectionMask;
		float powerRatio, bandRatio[4], correlation;
	};
	StereoSample live[capacity], cache[capacity];
	Row rows[20000];
	uint8_t blockSizes[capacity];
	int32_t blockCount, observedSamples, rejectedSamples[5], passedSamples, longestMatch, lastRowSample;
	int32_t count, rowCount, nativeStartRepeat, recordedStartRepeat, periodTicks, clipTicks;
	int32_t firstCacheSample, lastCacheSample, fadeSamples;
	void reset() {
		count = rowCount = blockCount = observedSamples = passedSamples = longestMatch = lastRowSample = 0;
		std::fill_n(rejectedSamples, 5, 0);
		nativeStartRepeat = firstCacheSample = -1;
		fadeSamples = 0;
	}
	void capture(std::span<StereoSample> a, std::span<StereoSample> b, int32_t position) {
		if (!count)
			firstCacheSample = position;
		lastCacheSample = position;
		int n = std::min<int>(a.size(), capacity - count);
		std::copy_n(a.begin(), n, live + count);
		std::copy_n(b.begin(), n, cache + count);
		count += n;
		if (n)
			blockSizes[blockCount++] = n;
	}
	void observe(int32_t matchingRun, float livePower, float cachePower, float (&bands)[2][4],
	             std::span<StereoSample> a, std::span<StereoSample> b) {
		if (rowCount == 20000)
			return;
		Row row{};
		row.endSample = count;
		row.cacheSample = lastCacheSample;
		row.matchingRun = matchingRun;
		row.rejectionMask = (livePower < cachePower * .81f || livePower > cachePower * 1.235f) ? 1 : 0;
		row.powerRatio = livePower / std::max(1.f, cachePower);
		float totals[2]{};
		for (int stream = 0; stream < 2; ++stream)
			for (float band : bands[stream])
				totals[stream] += band;
		for (int band = 0; band < 4; ++band) {
			float target = bands[1][band] / std::max(1.f, totals[1]);
			float actual = bands[0][band] / std::max(1.f, totals[0]);
			row.bandRatio[band] = actual / std::max(1e-12f, target);
			if (bands[1][band] > totals[1] * .0001f && (actual < target * .63f || actual > target * 1.585f))
				row.rejectionMask |= 2 << band;
		}
		observedSamples += a.size();
		longestMatch = std::max(longestMatch, matchingRun);
		for (int bit = 0; bit < 5; ++bit)
			if (row.rejectionMask & (1 << bit))
				rejectedSamples[bit] += a.size();
		if (!row.rejectionMask)
			passedSamples += a.size();
		if (count - lastRowSample < 441 && count != 30 * 44100)
			return;
		lastRowSample = count;
		float cross = 0, pa = 0, pb = 0;
		for (size_t i = 0; i < a.size(); ++i) {
			cross += float(a[i].l) * b[i].l + float(a[i].r) * b[i].r;
			pa += float(a[i].l) * a[i].l + float(a[i].r) * a[i].r;
			pb += float(b[i].l) * b[i].l + float(b[i].r) * b[i].r;
		}
		row.correlation = cross / (std::sqrt(std::max(1.f, pa)) * std::sqrt(std::max(1.f, pb)));
		rows[rowCount++] = row;
	}
};
PLACE_SDRAM_BSS FreezeProbe freezeProbe;
