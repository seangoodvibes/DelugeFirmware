#pragma once
#include <cstdint>
class ModelStackWithAutoParam;

void set_parameter_region(ModelStackWithAutoParam* stack, int32_t value, int32_t pos, int32_t length);
