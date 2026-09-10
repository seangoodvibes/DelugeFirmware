#pragma once

#include <cstdint>

// Shift a parameter value, clamping to the signed 32-bit range.
int32_t shift_value(int32_t value, int32_t offset);
