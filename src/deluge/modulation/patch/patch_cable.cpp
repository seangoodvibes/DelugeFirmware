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

#include "modulation/patch/patch_cable.h"
#include "definitions_cxx.hpp"
#include "modulation/automation/auto_param_pool.h"
#include "util/fixedpoint.h"
#include <cstring>
#include <utility>

#include <storage/flash_storage.h>

Polarity stringToPolarity(const std::string_view string) {
	if (string == "unipolar") {
		return Polarity::UNIPOLAR;
	}
	if (string == "bipolar") {
		return Polarity::BIPOLAR;
	}
	return Polarity::BIPOLAR; // Default to bipolar
}
std::string_view polarityToString(const Polarity polarity) {
	switch (polarity) {
	case Polarity::UNIPOLAR:
		return "unipolar";
	case Polarity::BIPOLAR:
		return "bipolar";
	default:
		return "bipolar";
	}
}
std::string_view polarityToStringShort(const Polarity polarity) {
	switch (polarity) {
	case Polarity::UNIPOLAR:
		return "UPLR";
	case Polarity::BIPOLAR:
		return "BPLR";
	default:
		return "BPLR";
	}
}

bool PatchCable::hasPolarity(PatchSource source) {
	if (source == PatchSource::Y || source == PatchSource::X) {
		// these can't be converted so they ignore the actual setting
		return false;
	}
	return true;
}

Polarity PatchCable::getDefaultPolarity(PatchSource source) {
	if (source == PatchSource::AFTERTOUCH) {
		// Aftertouch is stored unipolar, using bipolar here causes near zero volume with the default patch to level
		return Polarity::UNIPOLAR;
	}
	if (source == PatchSource::Y || source == PatchSource::X || source == PatchSource::SIDECHAIN) {
		// mod wheel is stored unipolar but MPE Y is bipolar, so stuck using bipolar
		return Polarity::BIPOLAR;
	}
	return FlashStorage::defaultPatchCablePolarity; // Use the default polarity from flash storage
}

void PatchCable::setDefaultPolarity() {
	polarity = getDefaultPolarity(from);
}

void PatchCable::setup(PatchSource newFrom, uint8_t newTo, int32_t newAmount) {
	from = newFrom;
	destinationParamDescriptor.setToHaveParamOnly(newTo);
	initAmount(newAmount);
	setDefaultPolarity();
}

bool PatchCable::isActive() {
	return current_value_ != 0 || is_automated();
}

void PatchCable::initAmount(int32_t value) {
	release_automation();
	current_value_ = value;
}

void PatchCable::makeUnusable() {
	destinationParamDescriptor.setToNull();
}

PatchCable::~PatchCable() {
	release_automation();
}

PatchCable::PatchCable(PatchCable&& other) noexcept {
	*this = std::move(other);
}

PatchCable& PatchCable::operator=(PatchCable&& other) noexcept {
	if (this == &other)
		return *this;
	release_automation();
	from = other.from;
	polarity = other.polarity;
	destinationParamDescriptor = other.destinationParamDescriptor;
	rangeAdjustmentPointer = other.rangeAdjustmentPointer;
	current_value_ = other.current_value_;
	automation_ = std::exchange(other.automation_, nullptr);
	rebind_automation();
	other.current_value_ = 0;
	other.from = PatchSource::NONE;
	other.makeUnusable();
	other.rangeAdjustmentPointer = nullptr;
	return *this;
}

AutoParam* PatchCable::get_auto_param(bool allow_creation) {
	if (!automation_ && allow_creation) {
		automation_ = auto_param_pool::get().acquire();
		rebind_automation();
	}
	return automation_;
}

void PatchCable::release_automation() {
	auto_param_pool::get().release(std::exchange(automation_, nullptr));
}

void PatchCable::release_unautomated() {
	if (!is_automated())
		release_automation();
}

void PatchCable::rebind_automation() {
	if (automation_)
		automation_->bind_current_value(current_value_);
}

void PatchCable::clone_automation(bool copy_automation, int32_t reverse_length) {
	// ParamManager makes a raw collection copy. Its pointers still belong to the source.
	auto* source = std::exchange(automation_, nullptr);
	if (copy_automation && source && source->isAutomated()) {
		if (auto* destination = get_auto_param(true)) {
			memcpy(destination, source, sizeof(AutoParam));
			rebind_automation();
			destination->beenCloned(true, reverse_length);
			release_unautomated();
		}
	}
}

Error PatchCable::take_automation_from(AutoParam& source) {
	initAmount(source.getCurrentValue());
	if (!source.isAutomated())
		return Error::NONE;
	auto* destination = get_auto_param(true);
	if (!destination)
		return Error::INSUFFICIENT_RAM;
	destination->nodes.swapStateWith(&source.nodes);
	return Error::NONE;
}

void PatchCable::write_amount(Serializer& writer, bool write_automation) {
	if (automation_)
		automation_->writeToFile(writer, write_automation);
	else {
		AutoParam scalar;
		scalar.setCurrentValueBasicForSetup(current_value_);
		scalar.writeToFile(writer, false);
	}
}
