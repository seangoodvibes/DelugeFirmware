#pragma once
#include "modulation/params/param.h"
class ModelStackWithParamCollection;
class ParamCollectionSummary;

// Host tests exercise ParamManager without the DSP/automation engine.
// Keep the virtual dtor and virtual getParamKind() so this matches the real class's shape.
class ParamCollection {
public:
	explicit ParamCollection(deluge::modulation::params::Kind kind) : kind_(kind) {}
	virtual ~ParamCollection() {
		if (cloned_for_test_)
			++clone_destructor_count;
	}
	inline static int clone_calls_before_failure = -1;
	inline static int clone_destructor_count = 0;
	virtual deluge::modulation::params::Kind getParamKind() { return kind_; }
	virtual void tickTicks(int32_t ticks, ModelStackWithParamCollection* stack) {}
	virtual void processCurrentPos(ModelStackWithParamCollection* stack, int32_t ticks, bool reversed, bool pingpong,
	                               bool interpolate) {}
	int32_t ticksTilNextEvent = 0;
	Error beenCloned(bool copyAutomation, int32_t reverseLength, ParamCollectionSummary* = nullptr) {
		cloned_for_test_ = true;
		if (clone_calls_before_failure == 0)
			return Error::INSUFFICIENT_RAM;
		if (clone_calls_before_failure > 0)
			--clone_calls_before_failure;
		clonedAutomation = copyAutomation;
		clonedReverseLength = reverseLength;
		return Error::NONE;
	}
	uint32_t objectSize = sizeof(ParamCollection);
	bool clonedAutomation = false;
	int32_t clonedReverseLength = 0;

private:
	bool cloned_for_test_ = false;
	deluge::modulation::params::Kind kind_;
};
