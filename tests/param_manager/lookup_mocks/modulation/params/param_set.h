#pragma once
#include "modulation/params/param_collection.h"
#include <vector>
class AutoParam {
public:
	int32_t value = 0;
};
class ParamSet : public ParamCollection {
public:
	ParamSet(deluge::modulation::params::Kind kind, int32_t count)
	    : ParamCollection(kind), numParams_(count), storage(count), params(storage.data()) {}
	ModelStackWithAutoParam* getAutoParamFromId(ModelStackWithParamId*, bool = true) final;
	AutoParam* getParam(int32_t id, bool) { return &params[id]; }
	int32_t numParams_;
	std::vector<AutoParam> storage;
	AutoParam* params;
};