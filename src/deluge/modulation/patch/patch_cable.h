/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "modulation/automation/auto_param.h"
#include "modulation/params/param_descriptor.h"
#include "util/fixedpoint.h"
#include <cstdint>

class Sound;
class ParamManagerForTimeline;
class InstrumentClip;

enum class Polarity : uint8_t {
	UNIPOLAR = 0,
	BIPOLAR = 1,
};

Polarity stringToPolarity(std::string_view string);
std::string_view polarityToString(Polarity polarity);
std::string_view polarityToStringShort(const Polarity polarity);

class PatchCable {
public:
	PatchCable() = default;
	~PatchCable();
	PatchCable(const PatchCable&) = delete;
	PatchCable& operator=(const PatchCable&) = delete;
	PatchCable(PatchCable&& other) noexcept;
	PatchCable& operator=(PatchCable&& other) noexcept;
	int32_t get_current_value() const { return current_value_; }
	void set_current_value(int32_t value) { current_value_ = value; }
	bool is_automated() const { return automation_ && automation_->isAutomated(); }
	AutoParam* get_auto_param(bool allow_creation = false);
	void release_automation();
	void release_unautomated();
	void rebind_automation();
	void clone_automation(bool copy_automation, int32_t reverse_length);
	Error take_automation_from(AutoParam& source);
	void write_amount(Serializer& writer, bool write_automation);
	void setDefaultPolarity();
	static bool hasPolarity(PatchSource source);
	static Polarity getDefaultPolarity(PatchSource source);
	void setup(PatchSource newFrom, uint8_t newTo, int32_t newAmount);
	bool isActive();
	void initAmount(int32_t value);
	void makeUnusable();
	/// @brief Converts a patch cable source to the correct polarity.
	/// The source is required because some sources are stored unipolar
	int32_t toPolarity(int32_t value) {
		// aftertouch is stored unipolar, all others are stored bipolar
		if (from == PatchSource::AFTERTOUCH) {
			if (polarity == Polarity::UNIPOLAR) {
				return value;
			}
			return (value - (std::numeric_limits<int32_t>::max() / 2)) * 2; // Convert from unipolar to bipolar
		}
		// Because unipolar mod wheel and bipolar MPE y share the same mod source we can't convert :(
		if (from == PatchSource::Y || polarity == Polarity::BIPOLAR) {
			return value;
		}
		return (value / 2) + (std::numeric_limits<int32_t>::max() / 2); // Convert from bipolar to unipolar
	}
	[[gnu::always_inline]] [[nodiscard]] int32_t applyRangeAdjustment(int32_t value) const {
		int32_t small = multiply_32x32_rshift32(value, *this->rangeAdjustmentPointer);
		return signed_saturate<32 - 5>(small) << 3; // Not sure if these limits are as wide as they could be...
	}

	PatchSource from{PatchSource::NONE};
	Polarity polarity{Polarity::BIPOLAR};
	ParamDescriptor destinationParamDescriptor;

private:
	friend class PatchCableSet;
	int32_t current_value_ = 0; // Amounts are within +1073741824 and -1073741824.
	AutoParam* automation_ = nullptr;

public:
	int32_t const* rangeAdjustmentPointer = nullptr;
};
