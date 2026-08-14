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

#include "io/midi/midi_device_manager.h"
#include "definitions_cxx.hpp"
#include "gui/menu_item/mpe/zone_num_member_channels.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "io/midi/cable_types/din.h"
#include "io/midi/cable_types/usb_device_cable.h"
#include "io/midi/device_specific/specific_midi_device.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_queue_manager.h"
#include "mem_functions.h"
#include "memory/general_memory_allocator.h"
#include "storage/storage_manager.h"
#include "util/container/vector/named_thing_vector.h"
#include "util/misc.h"

struct ConnectedUSBMIDIDevice::USBPriorityPopContext {
	uint32_t& message_out;
	int32_t& cc_budget_packets_remaining;
};

struct ConnectedDINMIDIDevice::SerialPriorityPopContext {
	uint8_t* out_bytes;
	int32_t budget_bytes;
	int32_t uart_space;
	int32_t max_len;
	int32_t cc_uart_budget;
	QueuePriority& popped_priority;
};

extern "C" {
#include "RZA1/uart/sio_char.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_basic_define.h"
#include "drivers/uart/uart.h"

extern uint8_t anyUSBSendingStillHappening[];
}
#pragma GCC diagnostic push
// This is supported by GCC and other compilers should error (not warn), so turn off for this file
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#define SETTINGS_FOLDER "SETTINGS"
#define MIDI_DEVICES_XML "SETTINGS/MIDIDevices.XML"

PLACE_SDRAM_BSS ConnectedUSBMIDIDevice connectedUSBMIDIDevices[USB_NUM_USBIP][MAX_NUM_USB_MIDI_DEVICES];
PLACE_SDRAM_BSS ConnectedDINMIDIDevice connectedDINMIDIDevice{};

namespace MIDIDeviceManager {

NamedThingVector hostedMIDIDevices{__builtin_offsetof(MIDICableUSBHosted, name)};

bool differentiatingInputsByDevice = true;

struct USBDev {
	String name{};
	uint16_t vendorId;
	uint16_t productId;
};
std::array<USBDev, USB_NUM_USBIP> usbDeviceCurrentlyBeingSetUp{};

// This class represents a thing you can send midi too,
// the virtual cable is an implementation detail
PLACE_SDRAM_BSS MIDICableUSBUpstream upstreamUSBMIDICable1{0, false, true};
PLACE_SDRAM_BSS MIDICableUSBUpstream upstreamUSBMIDICable2{1, true, false};
PLACE_SDRAM_BSS MIDICableUSBUpstream upstreamUSBMIDICable3{2, false, false};
PLACE_SDRAM_BSS MIDICableDINPorts dinMIDIPorts{};

uint8_t lowestLastMemberChannelOfLowerZoneOnConnectedOutput = 15;
uint8_t highestLastMemberChannelOfUpperZoneOnConnectedOutput = 0;

bool anyChangesToSave = false;

// Gets called within UITimerManager, which may get called during SD card routine.
void slowRoutine() {
	upstreamUSBMIDICable1.sendMCMsNowIfNeeded();
	upstreamUSBMIDICable2.sendMCMsNowIfNeeded();
	// port3 is not used for channel data

	for (int32_t d = 0; d < hostedMIDIDevices.getNumElements(); d++) {
		MIDICableUSBHosted* device = (MIDICableUSBHosted*)hostedMIDIDevices.getElement(d);
		device->sendMCMsNowIfNeeded();

		// This routine placed here because for whatever reason we can't send sysex from hostedDeviceConfigured
		if (device->freshly_connected) {
			device->hookOnConnected();
			device->freshly_connected = false; // Must be set to false here or the hook will run forever
		}
	}
}

extern "C" void giveDetailsOfDeviceBeingSetUp(int32_t ip, char const* name, uint16_t vendorId, uint16_t productId) {

	usbDeviceCurrentlyBeingSetUp[ip].name.set(name); // If that fails, it'll just have a 0-length name
	usbDeviceCurrentlyBeingSetUp[ip].vendorId = vendorId;
	usbDeviceCurrentlyBeingSetUp[ip].productId = productId;

	uartPrint("name: ");
	uartPrintln(name);
	uartPrint("vendor: ");
	uartPrintNumber(vendorId);
	uartPrint("product: ");
	uartPrintNumber(productId);
}

// name can be NULL, or an empty String
MIDICableUSBHosted* getOrCreateHostedMIDIDeviceFromDetails(String* name, uint16_t vendorId, uint16_t productId) {

	// Do we know any details about this device already?

	bool gotAName = (name && !name->isEmpty());
	int32_t i = 0; // Need default value for below if we skip first bit because !gotAName

	if (gotAName) {
		// Search by name first
		bool foundExact;
		i = hostedMIDIDevices.search(name->get(), GREATER_OR_EQUAL, &foundExact);

		// If we'd already seen it before...
		if (foundExact) {
			auto* device = static_cast<MIDICableUSBHosted*>(hostedMIDIDevices.getElement(i));

			// Update vendor and product id, if we have those
			if (vendorId) {
				device->vendorId = vendorId;
				device->productId = productId;
			}

			return device;
		}
	}

	// Ok, try searching by vendor / product id
	for (int32_t i = 0; i < hostedMIDIDevices.getNumElements(); i++) {
		auto* candidate = static_cast<MIDICableUSBHosted*>(hostedMIDIDevices.getElement(i));

		if (candidate->vendorId == vendorId && candidate->productId == productId) {
			// Update its name - if we got one and it's different
			if (gotAName && !candidate->name.equals(name)) {
				hostedMIDIDevices.renameMember(i, name);
			}
			return candidate;
		}
	}

	bool success = hostedMIDIDevices.ensureEnoughSpaceAllocated(1);
	if (!success) {
		return nullptr;
	}

	MIDICableUSBHosted* device = nullptr;

	SpecificMidiDeviceType devType = getSpecificMidiDeviceType(vendorId, productId);
	if (devType == SpecificMidiDeviceType::LUMI_KEYS) {
		void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(MIDIDeviceLumiKeys));
		if (!memory) {
			return nullptr;
		}

		MIDIDeviceLumiKeys* instDevice = new (memory) MIDIDeviceLumiKeys();
		device = instDevice;
	}
	else {
		void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(MIDICableUSBHosted));
		if (!memory) {
			return nullptr;
		}

		MIDICableUSBHosted* instDevice = new (memory) MIDICableUSBHosted();
		device = instDevice;
	}

	if (gotAName) {
		device->name.set(name);
	}
	device->vendorId = vendorId;
	device->productId = productId;

	// Store record of this device
	Error error = hostedMIDIDevices.insertElement(device, i); // We made sure, above, that there's space
#if ALPHA_OR_BETA_VERSION
	if (error != Error::NONE) {
		FREEZE_WITH_ERROR("E405");
	}
#endif

	return device;
}

void recountSmallestMPEZonesForCable(MIDICable& cable) {
	if (!cable.connectionFlags) {
		return;
	}

	if (cable.ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeLowerZoneLastMemberChannel
	    && cable.ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeLowerZoneLastMemberChannel
	           < lowestLastMemberChannelOfLowerZoneOnConnectedOutput) {

		lowestLastMemberChannelOfLowerZoneOnConnectedOutput =
		    cable.ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeLowerZoneLastMemberChannel;
	}

	if (cable.ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeUpperZoneLastMemberChannel != 15
	    && cable.ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeUpperZoneLastMemberChannel
	           > highestLastMemberChannelOfUpperZoneOnConnectedOutput) {

		highestLastMemberChannelOfUpperZoneOnConnectedOutput =
		    cable.ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeUpperZoneLastMemberChannel;
	}
}

void recountSmallestMPEZones() {
	lowestLastMemberChannelOfLowerZoneOnConnectedOutput = 15;
	highestLastMemberChannelOfUpperZoneOnConnectedOutput = 0;

	recountSmallestMPEZonesForCable(upstreamUSBMIDICable1);
	recountSmallestMPEZonesForCable(upstreamUSBMIDICable2);
	recountSmallestMPEZonesForCable(dinMIDIPorts);

	for (int32_t d = 0; d < hostedMIDIDevices.getNumElements(); d++) {
		MIDICableUSBHosted* cable = (MIDICableUSBHosted*)hostedMIDIDevices.getElement(d);
		recountSmallestMPEZonesForCable(*cable);
	}
}

// Create the midi device configuration and add to the USB midi array
extern "C" void hostedDeviceConfigured(int32_t ip, int32_t midiDeviceNum) {
	MIDICableUSBHosted* device = getOrCreateHostedMIDIDeviceFromDetails(&usbDeviceCurrentlyBeingSetUp[ip].name,
	                                                                    usbDeviceCurrentlyBeingSetUp[ip].vendorId,
	                                                                    usbDeviceCurrentlyBeingSetUp[ip].productId);

	usbDeviceCurrentlyBeingSetUp[ip].name.clear(); // Save some memory. Not strictly necessary

	if (!device) {
		return; // Only if ran out of RAM - i.e. very unlikely.
	}

	// Associate with USB port
	ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][midiDeviceNum];

	connectedDevice->setup();
	int32_t ports = connectedDevice->maxPortConnected;
	for (int32_t i = 0; i <= ports; i++) {
		connectedDevice->cable[i] = device;
	}

	connectedDevice->sq = 0;
	connectedDevice->canHaveMIDISent = (bool)strcmp(device->name.get(), "Synthstrom MIDI Foot Controller");
	connectedDevice->canHaveMIDISent = (bool)strcmp(device->name.get(), "LUMI Keys BLOCK");

	device->connectedNow(midiDeviceNum);
	recountSmallestMPEZones(); // Must be called after setting device->connectionFlags

	device->freshly_connected = true; // Used to trigger hookOnConnected from the input loop

	if (display->haveOLED()) {
		String text;
		text.set(&device->name);
		Error error = text.concatenate(" attached");
		if (error == Error::NONE) {
			consoleTextIfAllBootedUp(text.get());
		}
	}
	else {
		consoleTextIfAllBootedUp("MIDI");
	}
}

extern "C" void hostedDeviceDetached(int32_t ip, int32_t midiDeviceNum) {

#if ALPHA_OR_BETA_VERSION
	if (midiDeviceNum == MAX_NUM_USB_MIDI_DEVICES) {
		FREEZE_WITH_ERROR("E367");
	}
#endif

	uartPrint("detached MIDI device: ");
	uartPrintNumber(midiDeviceNum);
	ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][midiDeviceNum];
	int32_t ports = connectedDevice->maxPortConnected;
	for (int32_t i = 0; i <= ports; i++) {
		MIDICableUSB* device = connectedDevice->cable[i];
		if (device) { // Surely always has one?
			device->connectionFlags &= ~(1 << midiDeviceNum);
		}
		connectedDevice->cable[i] = nullptr;
	}
	recountSmallestMPEZones();
}

// called by USB setup
extern "C" void configuredAsPeripheral(int32_t ip) {
	// Leave this - we'll use this device for all upstream ports
	ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][0];

	// add second port here
	connectedDevice->setup();
	connectedDevice->cable[0] = &upstreamUSBMIDICable1;
	connectedDevice->cable[1] = &upstreamUSBMIDICable2;
	connectedDevice->cable[2] = &upstreamUSBMIDICable3;
	connectedDevice->maxPortConnected = 2;
	connectedDevice->canHaveMIDISent = 1;

	anyUSBSendingStillHappening[ip] = 0; // Initialize this. There's obviously nothing sending yet right now.

	upstreamUSBMIDICable1.connectedNow(0);
	upstreamUSBMIDICable2.connectedNow(0);
	upstreamUSBMIDICable3.connectedNow(0);
	recountSmallestMPEZones();
}

extern "C" void detachedAsPeripheral(int32_t ip) {
	// will need to reset all devices if more are added
	int32_t ports = connectedUSBMIDIDevices[ip][0].maxPortConnected;
	for (int32_t i = 0; i <= ports; i++) {
		connectedUSBMIDIDevices[ip][0].cable[i] = nullptr;
	}
	upstreamUSBMIDICable1.connectionFlags = 0;
	upstreamUSBMIDICable2.connectionFlags = 0;
	upstreamUSBMIDICable3.connectionFlags = 0;
	anyUSBSendingStillHappening[ip] = 0; // Reset this again. Been meaning to do this, and can no longer quite remember
	                                     // reason or whether technically essential, but adds to safety at least.

	recountSmallestMPEZones();
}

// Returns NULL if insufficient details found, or not enough RAM to create
MIDICable* readDeviceReferenceFromFile(Deserializer& reader) {

	uint16_t vendorId = 0;
	uint16_t productId = 0;
	String name;
	MIDICable* device = nullptr;

	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "vendorId")) {
			vendorId = reader.readTagOrAttributeValueHex(0);
		}
		else if (!strcmp(tagName, "productId")) {
			productId = reader.readTagOrAttributeValueHex(0);
		}
		else if (!strcmp(tagName, "name")) {
			reader.readTagOrAttributeValueString(&name);
		}
		else if (!strcmp(tagName, "port")) {
			char const* port = reader.readTagOrAttributeValue();
			if (!strcmp(port, "upstreamUSB")) {
				device = &upstreamUSBMIDICable1;
			}
			else if (!strcmp(port, "upstreamUSB2")) {
				device = &upstreamUSBMIDICable2;
			}
			else if (!strcmp(port, "upstreamUSB3")) {
				device = &upstreamUSBMIDICable3;
			}
			else if (!strcmp(port, "din")) {
				device = &dinMIDIPorts;
			}
		}

		reader.exitTag();
	}

	if (device) {
		return device;
	}

	// If we got something, go use it
	if (!name.isEmpty() || vendorId) {
		return getOrCreateHostedMIDIDeviceFromDetails(&name, vendorId, productId); // Will return NULL if error.
	}

	return nullptr;
}

static MIDICable* readCableFromFlash(uint8_t const* memory) {
	uint16_t vendorId = *(uint16_t const*)memory;

	MIDICable* cable;

	if (vendorId == VENDOR_ID_NONE) {
		cable = nullptr;
	}
	else if (vendorId == VENDOR_ID_UPSTREAM_USB) {
		cable = &upstreamUSBMIDICable1;
	}
	else if (vendorId == VENDOR_ID_UPSTREAM_USB2) {
		cable = &upstreamUSBMIDICable2;
	}
	else if (vendorId == VENDOR_ID_UPSTREAM_USB3) {
		cable = &upstreamUSBMIDICable3;
	}
	else if (vendorId == VENDOR_ID_DIN) {
		cable = &dinMIDIPorts;
	}
	else {
		uint16_t productId = *(uint16_t const*)(memory + 2);
		cable = getOrCreateHostedMIDIDeviceFromDetails(nullptr, vendorId, productId);
	}

	return cable;
}

void readDeviceReferenceFromFlash(GlobalMIDICommand whichCommand, uint8_t const* memory) {
	midiEngine.globalMIDICommands[util::to_underlying(whichCommand)].cable = readCableFromFlash(memory);
}

void writeDeviceReferenceToFlash(GlobalMIDICommand whichCommand, uint8_t* memory) {
	if (midiEngine.globalMIDICommands[util::to_underlying(whichCommand)].cable) {
		midiEngine.globalMIDICommands[util::to_underlying(whichCommand)].cable->writeToFlash(memory);
	}
}

void writeDevicesToFile() {
	if (!anyChangesToSave) {
		return;
	}
	anyChangesToSave = false;

	bool anyWorthWritting = dinMIDIPorts.worthWritingToFile() || upstreamUSBMIDICable1.worthWritingToFile()
	                        || upstreamUSBMIDICable2.worthWritingToFile();
	if (!anyWorthWritting) {
		for (int32_t d = 0; d < hostedMIDIDevices.getNumElements(); d++) {
			MIDICableUSBHosted* device = (MIDICableUSBHosted*)hostedMIDIDevices.getElement(d);
			if (device->worthWritingToFile()) {
				anyWorthWritting = true;
				break;
			}
		}
	}

	if (!anyWorthWritting) {
		// If still here, nothing worth writing. Delete the file if there was one.
		f_unlink(MIDI_DEVICES_XML); // May give error, but no real consequence from that.
		return;
	}

	Error error = StorageManager::createXMLFile(MIDI_DEVICES_XML, smSerializer, true);
	if (error != Error::NONE) {
		return;
	}

	Serializer& writer = GetSerializer();
	writer.writeOpeningTagBeginning("midiDevices");
	writer.writeFirmwareVersion();
	writer.writeEarliestCompatibleFirmwareVersion("4.0.0");
	writer.writeOpeningTagEnd();

	if (dinMIDIPorts.worthWritingToFile()) {
		dinMIDIPorts.writeToFile(writer, "dinPorts");
	}
	if (upstreamUSBMIDICable1.worthWritingToFile()) {
		upstreamUSBMIDICable1.writeToFile(writer, "upstreamUSBDevice");
	}
	if (upstreamUSBMIDICable2.worthWritingToFile()) {
		upstreamUSBMIDICable2.writeToFile(writer, "upstreamUSBDevice2");
	}

	for (int32_t d = 0; d < hostedMIDIDevices.getNumElements(); d++) {
		MIDICableUSBHosted* device = (MIDICableUSBHosted*)hostedMIDIDevices.getElement(d);
		if (device->worthWritingToFile()) {
			device->writeToFile(writer, "hostedUSBDevice");
		}
		// Stow this for the hook  point later
		device->hookOnWriteHostedDeviceToFile();
	}

	writer.writeClosingTag("midiDevices");

	writer.closeFileAfterWriting();
}

bool successfullyReadDevicesFromFile = false; // We'll only do this one time

void readDevicesFromFile() {
	if (successfullyReadDevicesFromFile) {
		return; // Yup, we only want to do this once
	}

	FilePointer fp;
	bool success = StorageManager::fileExists(MIDI_DEVICES_XML, &fp);
	if (!success) {
		// since we changed the file path for the MIDIDevices.XML in c1.3, it's possible
		// that a MIDIDevice file may exists in the root of the SD card
		// if so, let's move it to the new SETTINGS folder (but first make sure folder exists)
		FRESULT result = f_mkdir(SETTINGS_FOLDER);
		if (result == FR_OK || result == FR_EXIST) {
			result = f_rename("MIDIDevices.XML", MIDI_DEVICES_XML);
			if (result == FR_OK) {
				// this means we moved it
				// now let's open it
				success = StorageManager::fileExists(MIDI_DEVICES_XML, &fp);
			}
		}
		if (!success) {
			return;
		}
	}

	Error error = StorageManager::openXMLFile(&fp, smDeserializer, "midiDevices");
	if (error != Error::NONE) {
		return;
	}
	Deserializer& reader = *activeDeserializer;
	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "dinPorts")) {
			dinMIDIPorts.readFromFile(reader);
		}
		else if (!strcmp(tagName, "upstreamUSBDevice")) {
			upstreamUSBMIDICable1.readFromFile(reader);
		}
		else if (!strcmp(tagName, "upstreamUSBDevice2")) {
			upstreamUSBMIDICable2.readFromFile(reader);
		}
		else if (!strcmp(tagName, "upstreamUSBDevice3")) {
			upstreamUSBMIDICable3.readFromFile(reader);
		}
		else if (!strcmp(tagName, "hostedUSBDevice")) {
			readAHostedDeviceFromFile(reader);
		}

		reader.exitTag();
	}

	activeDeserializer->closeWriter();

	recountSmallestMPEZones();

	soundEditor.mpeZonesPotentiallyUpdated();

	successfullyReadDevicesFromFile = true;
}

void readAHostedDeviceFromFile(Deserializer& reader) {
	MIDICableUSBHosted* device = nullptr;

	String name;
	uint16_t vendorId;
	uint16_t productId;

	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {

		int32_t whichPort;

		if (!strcmp(tagName, "vendorId")) {
			vendorId = reader.readTagOrAttributeValueHex(0);
		}
		else if (!strcmp(tagName, "productId")) {
			productId = reader.readTagOrAttributeValueHex(0);
		}
		else if (!strcmp(tagName, "name")) {
			reader.readTagOrAttributeValueString(&name);
		}
		else if (!strcmp(tagName, "input")) {
			whichPort = MIDI_DIRECTION_INPUT_TO_DELUGE;
checkDevice:
			if (!device) {
				if (!name.isEmpty() || vendorId) {
					device = getOrCreateHostedMIDIDeviceFromDetails(&name, vendorId,
					                                                productId); // Will return NULL if error.
				}
			}

			if (device) {
				device->ports[whichPort].readFromFile(
				    reader, (whichPort == MIDI_DIRECTION_OUTPUT_FROM_DELUGE) ? device : nullptr);
			}
		}
		else if (!strcmp(tagName, "output")) {
			whichPort = MIDI_DIRECTION_OUTPUT_FROM_DELUGE;
			goto checkDevice;
		}
		else if (!strcmp(tagName, "defaultVolumeVelocitySensitivity")) {

			// Sorry, I cloned this code from above.
			if (!device) {
				if (!name.isEmpty() || vendorId) {
					device = getOrCreateHostedMIDIDeviceFromDetails(&name, vendorId,
					                                                productId); // Will return NULL if error.
				}
			}

			if (device) {
				device->defaultVelocityToLevel = reader.readTagOrAttributeValueInt();
			}
		}
		else if (!strcmp(tagName, "sendClock")) {
			// this is actually not much duplicated code, just checks for nulls and then an attempt to create a device
			if (!device) {
				if (!name.isEmpty() || vendorId) {
					device = getOrCreateHostedMIDIDeviceFromDetails(&name, vendorId,
					                                                productId); // Will return NULL if error.
				}
			}

			if (device) {
				device->sendClock = reader.readTagOrAttributeValueInt();
			}
		}
		else if (!strcmp(tagName, "receiveClock")) {
			// this is actually not much duplicated code, just checks for nulls and then an attempt to create a device
			if (!device) {
				if (!name.isEmpty() || vendorId) {
					device = getOrCreateHostedMIDIDeviceFromDetails(&name, vendorId,
					                                                productId); // Will return NULL if error.
				}
			}

			if (device) {
				device->receiveClock = reader.readTagOrAttributeValueInt();
			}
		}
		else if (!strcmp(tagName, "is_relative")) {
			// this is actually not much duplicated code, just checks for nulls and then an attempt to create a device
			if (!device) {
				if (!name.isEmpty() || vendorId) {
					device = getOrCreateHostedMIDIDeviceFromDetails(&name, vendorId,
					                                                productId); // Will return NULL if error.
				}
			}

			if (device) {
				device->is_relative = reader.readTagOrAttributeValueInt();
			}
		}

		reader.exitTag();
	}

	// Hook point!
	if (device) {}
}

} // namespace MIDIDeviceManager

namespace {
inline uint8_t status_byte(uint32_t packed) {
	// USB-MIDI event packets store CIN in byte 0 and status in byte 1.
	return static_cast<uint8_t>((packed >> 8) & 0xFF);
}

inline uint8_t data_1(uint32_t packed) {
	// Byte 2 is MIDI data1 for channel/system messages.
	return static_cast<uint8_t>((packed >> 16) & 0xFF);
}

inline uint8_t data_2(uint32_t packed) {
	// Byte 3 is MIDI data2 (for CC this is the value byte).
	return static_cast<uint8_t>((packed >> 24) & 0xFF);
}

inline bool is_packed_channel_cc(uint32_t packed) {
	// Channel-CC status family is 0xBn (high nibble 0x0B).
	return MIDIQueueManager::is_channel_cc_status_byte(status_byte(packed));
}

// DIN link throughput in Q8 fixed-point bytes/second (31.25 kbps ~= 3125 bytes/s).
constexpr int32_t k_serial_bytes_per_second_Q8 = 3125 * 256;
// Maximum accumulated send budget (Q8 bytes) allowed for one burst after idle time.
constexpr int32_t k_serial_queue_budget_max_Q8 = MIDI_TX_BUFFER_SIZE * 256;
// Reserve some UART TX space so we do not fill the hardware buffer to the edge.
constexpr int32_t k_serial_uart_headroom_bytes = 16;
// Limit how much lowest-priority CC traffic can be staged ahead in the DIN UART buffer.
constexpr int32_t k_serial_buffered_cc_bytes_cap = 24;
} // namespace

/// Initializes all USB device send/receive buffers and per-priority queue cursors.
ConnectedUSBMIDIDevice::ConnectedUSBMIDIDevice() {
	sq = 0;
	canHaveMIDISent = false;
	setup();
	memset(receiveData, 0, 64);
	memset(dataSendingNow, 0, MIDI_SEND_BUFFER_LEN_INNER * 4);
	// Start with an empty per-priority ring state.
	reset_queue_storage();
}

/// Resets volatile USB transfer state for a newly connected/initialized device.
void ConnectedUSBMIDIDevice::setup() {
	numBytesSendingNow = 0;
	currentlyWaitingToReceive = false;
	numBytesReceived = 0;

	// default to only a single port
	maxPortConnected = 0;
}

/// Resets all USB per-priority queues and read/write cursors.
void ConnectedUSBMIDIDevice::reset_queue_storage() {
	// storage cleared for deterministic startup, and read/write cursors reset to zero.
	queue_manager_.clear_all();
}

/// Returns queued USB packet count for one lane via monotonic write/read counters.
uint16_t ConnectedUSBMIDIDevice::queue_count(QueuePriority priority) const {
	uint8_t p = static_cast<uint8_t>(priority);
	return queue_manager_.queue_count(p);
}

/// Returns total queued USB packet count across all priority lanes.
uint32_t ConnectedUSBMIDIDevice::total_queued_messages() {
	return queue_manager_.total_queued_messages();
}

/// Queues one USB-MIDI packet into the selected priority lane with backpressure handling.
void ConnectedUSBMIDIDevice::bufferMessage(uint32_t fullMessage, QueuePriority priority) {
	// Defensive fallback: unknown priority values are treated as regular CC lane.
	if (priority >= QUEUE_PRIORITY_COUNT) {
		priority = QUEUE_PRIORITY_CC;
	}

	// Total packets currently queued across all priority lanes for this device.
	uint32_t queued = total_queued_messages();
	// If backlog grows, opportunistically kick a flush to keep latency bounded.
	if (queued > 16) {
		// Only trigger a new flush when a send transaction is not already active.
		if (anyUSBSendingStillHappening[0] == 0) {
			midiEngine.flushUSBMIDIOutput();
		}
	}

	// Occupancy of just the selected priority lane we are about to enqueue into.
	uint16_t queue_size = queue_count(priority);
	// Keep one slot free in a ring buffer so full/empty states stay distinguishable.
	if (queue_size >= (MIDI_SEND_BUFFER_LEN_RING - 1)) {
		// If nothing is currently transmitting, try flushing now to free space.
		if (anyUSBSendingStillHappening[0] == 0) {
			midiEngine.flushUSBMIDIOutput();
		}
		// Re-check after opportunistic flush.
		queue_size = queue_count(priority);
		if (queue_size >= (MIDI_SEND_BUFFER_LEN_RING - 1)) {
			// Still full: drop this message rather than overwrite unread queued data.
			// TODO: show some error message
			return;
		}
	}

	// Write the packet through shared queue-state helper.
	push_priority_message(priority, fullMessage);

	// Signal that at least one USB packet is waiting so flush logic can schedule transmission.
	anythingInUSBOutputBuffer = true;
}

/// Pushes one USB packet onto a selected priority lane.
void ConnectedUSBMIDIDevice::push_priority_message(QueuePriority priority, uint32_t message) {
	queue_manager_.enqueue_with_cc_policy(*this, priority, message,
	                                      /*allow_coalesce=*/true,
	                                      /*track_debt=*/is_packed_channel_cc(message), data_1(message),
	                                      &ConnectedUSBMIDIDevice::coalesce_queued_cc,
	                                      &ConnectedUSBMIDIDevice::enqueue_priority_message);
}

bool ConnectedUSBMIDIDevice::enqueue_priority_message(QueuePriority priority, uint32_t message) {
	append_priority_queue_message(priority, message);
	return true;
}

uint32_t ConnectedUSBMIDIDevice::read_priority_queue_message_at(QueuePriority priority, uint16_t logical_offset) const {
	uint8_t p = static_cast<uint8_t>(priority);
	return queue_manager_.read_at(p, logical_offset);
}

uint32_t ConnectedUSBMIDIDevice::read_priority_queue_head(QueuePriority priority) const {
	return read_priority_queue_message_at(priority, 0);
}

bool ConnectedUSBMIDIDevice::pop_priority_queue_head(QueuePriority priority, uint32_t& message_out) {
	if (!queue_count(priority)) {
		return false;
	}

	uint8_t p = static_cast<uint8_t>(priority);
	return queue_manager_.pop_head(p, message_out);
}

void ConnectedUSBMIDIDevice::append_priority_queue_message(QueuePriority priority, uint32_t message) {
	uint8_t p = static_cast<uint8_t>(priority);
	queue_manager_.push(p, message);
}

/// Coalesces queued USB channel-CC packets by controller/status.
///
/// Searches the USB CC lane for the newest pending packet with the same status
/// byte and controller number, then updates only that packet's value byte.
/// Returns `true` when an in-queue replacement was applied.
bool ConnectedUSBMIDIDevice::coalesce_queued_cc(uint32_t message) {
	// Coalescing is defined only for channel-CC packets; other message types must enqueue normally.
	if (!is_packed_channel_cc(message)) {
		return false;
	}

	uint8_t wanted_status = status_byte(message);
	uint8_t wanted_controller = data_1(message);
	int32_t latest_offset = queue_manager_.find_latest_matching_cc_offset(
	    *this, wanted_status, wanted_controller, &ConnectedUSBMIDIDevice::begin_coalesce_cc_scan,
	    &ConnectedUSBMIDIDevice::next_coalesce_cc_scan_step);

	if (latest_offset < 0) {
		// No matching queued status/controller pair was found; caller should enqueue a new packet.
		return false;
	}

	// Replace value byte in-place while preserving queue order for all packets.
	constexpr uint8_t p = QUEUE_PRIORITY_CC;
	// Keep CIN/status/data1 (low 24 bits) and overwrite only data2 (high byte).
	uint32_t queued = queue_manager_.read_at(p, static_cast<uint16_t>(latest_offset));
	queue_manager_.overwrite_at(p, static_cast<uint16_t>(latest_offset),
	                            (queued & 0x00FFFFFFu) | (static_cast<uint32_t>(data_2(message)) << 24));
	// Treat this coalesced write as fresh controller pressure for fair dequeue.
	queue_manager_.bump_controller_debt(wanted_controller);
	return true;
}

bool ConnectedUSBMIDIDevice::begin_coalesce_cc_scan(uint16_t& cursor, uint16_t& limit) const {
	cursor = 0;
	limit = queue_manager_.queue_count(QUEUE_PRIORITY_CC);
	return limit > 0;
}

MIDIQueueManager::CoalesceScanResult
ConnectedUSBMIDIDevice::next_coalesce_cc_scan_step(uint16_t& cursor, uint16_t limit, uint16_t& candidate_offset,
                                                   uint8_t& status, uint8_t& controller) const {
	if (cursor >= limit) {
		return MIDIQueueManager::CoalesceScanResult::NoMore;
	}

	uint16_t offset = cursor;
	cursor++;
	uint32_t queued = read_cc_queue_message_at(offset);
	if (!is_packed_channel_cc(queued)) {
		return MIDIQueueManager::CoalesceScanResult::Skip;
	}

	status = status_byte(queued);
	controller = data_1(queued);
	candidate_offset = offset;
	return MIDIQueueManager::CoalesceScanResult::Matchable;
}

uint32_t ConnectedUSBMIDIDevice::read_cc_queue_message_at(uint16_t logical_offset) const {
	return read_priority_queue_message_at(QUEUE_PRIORITY_CC, logical_offset);
}

void ConnectedUSBMIDIDevice::reset_cc_queue() {
	queue_manager_.clear(QUEUE_PRIORITY_CC);
}

void ConnectedUSBMIDIDevice::append_cc_queue_message(uint32_t message) {
	queue_manager_.push(QUEUE_PRIORITY_CC, message);
}

/// Removes one queued USB CC packet at a logical offset, atomically.
///
/// Fair dequeue may target a packet that is not at the lane head. This helper
/// copies the selected packet out, rebuilds the remaining CC-lane order, and
/// resets lane cursors to the rebuilt image.
bool ConnectedUSBMIDIDevice::remove_queued_cc_message_at_offset(uint16_t target_offset, uint32_t& message_out) {
	uint16_t queue_size = queue_manager_.queue_count(QUEUE_PRIORITY_CC);
	uint32_t removed_message[1] = {0};
	if (!MIDIQueueManager::remove_logical_span_and_repack(
	        queue_size, target_offset, 1, *this, removed_message, cc_reorder_scratch.data(),
	        &ConnectedUSBMIDIDevice::read_cc_queue_message_at, &ConnectedUSBMIDIDevice::reset_cc_queue,
	        &ConnectedUSBMIDIDevice::append_cc_queue_message)) {
		return false;
	}

	message_out = removed_message[0];
	return true;
}

/// Pops one USB packet using strict priority ordering, with fair CC selection.
bool ConnectedUSBMIDIDevice::pop_priority_message(uint32_t& message_out, int32_t& cc_budget_packets_remaining) {
	USBPriorityPopContext context{message_out, cc_budget_packets_remaining};
	return MIDIQueueManager::pop_priority_lanes_with_cc_fairness(
	    *this, QUEUE_PRIORITY_CLOCK, static_cast<QueuePriority>(QUEUE_PRIORITY_COUNT - 1),
	    &ConnectedUSBMIDIDevice::queue_count, &ConnectedUSBMIDIDevice::handle_priority_lane_pop,
	    &ConnectedUSBMIDIDevice::pop_priority_lane_message, context);
}

MIDIQueueManager::PriorityLaneTraversalResult
ConnectedUSBMIDIDevice::handle_priority_lane_pop(QueuePriority priority, USBPriorityPopContext& context) {
	uint32_t head_message = read_priority_queue_head(priority);
	auto cc_result = MIDIQueueManager::try_fair_pop_cc(
	    *this, is_packed_channel_cc(head_message), context.cc_budget_packets_remaining > 0,
	    &ConnectedUSBMIDIDevice::pop_fair_queued_cc_message, context.message_out);
	if (cc_result == MIDIQueueManager::CCFairPopResult::Popped) {
		// Charge one CC slot so the cap is enforced across this transfer assembly.
		context.cc_budget_packets_remaining--;
		return MIDIQueueManager::PriorityLaneTraversalResult::Popped;
	}
	if (cc_result == MIDIQueueManager::CCFairPopResult::NotCC) {
		return MIDIQueueManager::PriorityLaneTraversalResult::PopLane;
	}
	return MIDIQueueManager::PriorityLaneTraversalResult::SkipLane;
}

bool ConnectedUSBMIDIDevice::pop_priority_lane_message(QueuePriority priority, USBPriorityPopContext& context) {
	return pop_priority_queue_head(priority, context.message_out);
}

/// Pops one queued USB channel-CC packet using controller fairness.
///
/// Selection flow:
/// 1. Capture each controller's first queued CC offset.
/// 2. Establish RR baseline from rotating controller cursor.
/// 3. Prefer highest-debt controller when debt is non-zero.
/// 4. Remove selected packet atomically and commit fairness state.
bool ConnectedUSBMIDIDevice::pop_fair_queued_cc_message(uint32_t& message_out) {
	return queue_manager_.pop_fair_cc_candidate(*this, &ConnectedUSBMIDIDevice::collect_fair_cc_candidates,
	                                            &ConnectedUSBMIDIDevice::remove_queued_cc_message_at_offset,
	                                            message_out);
}

bool ConnectedUSBMIDIDevice::collect_fair_cc_candidates(std::array<uint16_t, kMaxMIDIValue + 1>& first_offsets) {
	(void)first_offsets;
	return queue_manager_.collect_first_controller_offsets_from_scan(
	    *this, &ConnectedUSBMIDIDevice::begin_fair_cc_candidate_scan,
	    &ConnectedUSBMIDIDevice::next_fair_cc_candidate_scan_step);
}

bool ConnectedUSBMIDIDevice::begin_fair_cc_candidate_scan(uint16_t& cursor, uint16_t& limit) const {
	cursor = 0;
	limit = queue_manager_.queue_count(QUEUE_PRIORITY_CC);
	return limit > 0;
}

MIDIQueueManager::CandidateScanResult
ConnectedUSBMIDIDevice::next_fair_cc_candidate_scan_step(uint16_t& cursor, uint16_t limit, uint16_t& candidate_offset,
                                                         uint8_t& controller) const {
	if (cursor >= limit) {
		return MIDIQueueManager::CandidateScanResult::NoMore;
	}

	uint16_t offset = cursor;
	cursor++;
	uint32_t queued = read_cc_queue_message_at(offset);
	if (!is_packed_channel_cc(queued)) {
		return MIDIQueueManager::CandidateScanResult::Skip;
	}

	controller = data_1(queued);
	candidate_offset = offset;
	return MIDIQueueManager::CandidateScanResult::Candidate;
}

/// Returns whether this device has at least one queued USB-MIDI packet to send.
bool ConnectedUSBMIDIDevice::hasBufferedSendData() {
	// True when at least one queued USB-MIDI packet exists across any priority lane.
	return total_queued_messages() > 0;
}

/// Reports remaining send capacity in serial-MIDI-byte units across all USB priority lanes.
int ConnectedUSBMIDIDevice::sendBufferSpace() {
	// Total queued USB-MIDI packets currently buffered across all priority lanes.
	uint32_t queued = total_queued_messages();
	// Maximum packets we can queue: usable slots per lane (ring-1) times number of lanes.
	uint32_t total_capacity = (MIDI_SEND_BUFFER_LEN_RING - 1) * QUEUE_PRIORITY_COUNT;
	// each 4-byte MIDI-USB message contains 3 bytes of serial MIDI data
	// Report remaining space in serial-MIDI-byte units to match caller expectations.
	return (total_capacity - queued) * 3;
}

/// Moves queued USB packets into the contiguous transfer buffer for the next hardware send.
bool ConnectedUSBMIDIDevice::consumeSendData() {
	// Snapshot total queued packets across all priority lanes.
	uint32_t queued = total_queued_messages();
	if (queued == 0) {
		// Nothing pending: caller should not start a USB send transfer.
		return false;
	}

	int32_t i = 0;
	uint32_t max_size = MIDI_SEND_BUFFER_LEN_INNER;
	if (g_usb_usbmode == USB_HOST) {
		// many devices do not accept more than 64 bytes of data at a time
		// likely this can be inferred from the device metadata somehow?

		// some seem to take even less, especially with hubs involved. The hydrasynth seems to only respond to a max of
		// 2 messages per transfer, the third gets blocked. For MPE this leads to ignoring note ons as the x and y
		// resets are sent before the note on
		max_size = MIDI_SEND_BUFFER_LEN_INNER_HOST;
	}

	// Build at most one USB transfer worth of packets: no more than queued, and no more than the mode/device cap.
	int32_t to_send = std::min<uint32_t>(queued, max_size);
	// Keep CC bursts bounded per transfer so fresh clock/notes can preempt sooner.
	int32_t cc_budget_packets_remaining = 8;
	// Serialize `to_send` prioritized 4-byte USB-MIDI packets into dataSendingNow.
	for (i = 0; i < to_send; i++) {
		uint32_t message = 0;
		// Pull one prioritized USB-MIDI packet (4 bytes) from the multi-lane ring queues.
		if (!pop_priority_message(message, cc_budget_packets_remaining)) {
			// Queue state changed unexpectedly (or became empty); stop assembling this transfer.
			break;
		}
		// Pack each 32-bit USB-MIDI event contiguously for the driver DMA/transfer buffer.
		memcpy(dataSendingNow + (i * 4), &message, 4);
	}

	// `i` is the number of USB-MIDI event packets queued, each packet is 4 bytes.
	numBytesSendingNow = i * 4;
	// Tell caller whether we assembled at least one packet to transmit.
	return i > 0;
}

uint16_t ConnectedDINMIDIDevice::queue_count(QueuePriority priority) const {
	return queue_manager_.queue_count(static_cast<uint8_t>(priority));
}

/// Resets serial pacing state so the next flush starts from a known baseline.
void ConnectedDINMIDIDevice::reset_serial_state(uint32_t now_sample_timer) {
	// Start pacing from "now" and with zero carry-over send budget.
	serial_budget_last_update_ = now_sample_timer;
	serial_budget_Q8_ = 0;
}

/// Refills DIN serial pacing tokens from elapsed sample time.
void ConnectedDINMIDIDevice::update_serial_budget(uint32_t now_sample_timer) {
	uint32_t delta_samples = now_sample_timer - serial_budget_last_update_;
	if (!delta_samples) {
		// No elapsed sample time means no new transmit budget to accrue.
		return;
	}
	serial_budget_last_update_ = now_sample_timer;

	// Q8 token bucket accumulation at classic DIN throughput.
	serial_budget_Q8_ +=
	    static_cast<int32_t>((static_cast<uint64_t>(delta_samples) * k_serial_bytes_per_second_Q8) / kSampleRate);
	if (serial_budget_Q8_ > k_serial_queue_budget_max_Q8) {
		// Cap idle-time bursts so one flush cannot monopolize UART.
		serial_budget_Q8_ = k_serial_queue_budget_max_Q8;
	}
}

/// Returns whether any serial-priority lane currently has data pending.
bool ConnectedDINMIDIDevice::has_serial_data() const {
	// Fast pre-check before attempting a paced drain pass.
	return queue_manager_.has_any_data();
}

/// Removes a 3-byte CC message from an arbitrary byte offset in the CC ring.
///
/// Fair dequeue may select a controller whose earliest queued message is not at
/// the ring head. To preserve message order and atomicity, this function copies
/// out the selected message, rebuilds the remaining bytes in-order, and resets
/// ring cursors to the rebuilt layout.
bool ConnectedDINMIDIDevice::remove_queued_cc_message_at_offset(uint16_t target_offset, uint8_t* out_bytes) {
	uint16_t queue_size = queue_manager_.queue_count(QUEUE_PRIORITY_CC);
	return MIDIQueueManager::remove_logical_span_and_repack(
	    queue_size, target_offset, 3, *this, out_bytes, cc_reorder_scratch_.data(),
	    &ConnectedDINMIDIDevice::read_cc_queue_byte_at, &ConnectedDINMIDIDevice::reset_cc_queue,
	    &ConnectedDINMIDIDevice::append_cc_queue_byte);
}

uint8_t ConnectedDINMIDIDevice::read_cc_queue_byte_at(uint16_t logical_offset) const {
	return queue_manager_.read_at(QUEUE_PRIORITY_CC, logical_offset);
}

void ConnectedDINMIDIDevice::reset_cc_queue() {
	queue_manager_.clear(QUEUE_PRIORITY_CC);
}

void ConnectedDINMIDIDevice::append_cc_queue_byte(uint8_t byte) {
	queue_manager_.push(QUEUE_PRIORITY_CC, byte);
}

/// Pops one queued CC message using controller-aware fairness.
///
/// Selection policy:
/// 1. Collect each controller's first queued CC offset.
/// 2. Start from the queue manager's round-robin cursor for ordering.
/// 3. Prefer highest controller debt; use RR order as tie-break.
/// 4. Remove the selected message atomically via offset-based removal.
///
/// Returns `false` when budgets/space do not allow a full 3-byte CC message,
/// when queue data is malformed, or when no eligible CC is present.
bool ConnectedDINMIDIDevice::pop_fair_queued_cc_message(uint8_t* out_bytes, int32_t budget_bytes, int32_t uart_space,
                                                        int32_t max_len, QueuePriority& popped_priority) {
	// Fair CC dequeue is atomic on one full 3-byte message; bail out if any limiter
	// (DIN budget, UART space, or output span) cannot accommodate that minimum.
	if (budget_bytes < 3 || uart_space < 3 || max_len < 3) {
		return false;
	}

	if (queue_manager_.queue_count(QUEUE_PRIORITY_CC) < 3) {
		// No possible CC candidate if fewer than 3 bytes are queued.
		return false;
	}

	bool popped =
	    queue_manager_.pop_fair_cc_candidate(*this, &ConnectedDINMIDIDevice::collect_fair_cc_candidates,
	                                         &ConnectedDINMIDIDevice::remove_queued_cc_message_at_offset, out_bytes);

	if (popped) {
		popped_priority = QUEUE_PRIORITY_CC;
	}
	return popped;
}

bool ConnectedDINMIDIDevice::collect_fair_cc_candidates(std::array<uint16_t, kMaxMIDIValue + 1>& first_offsets) {
	(void)first_offsets;
	return queue_manager_.collect_first_controller_offsets_from_scan(
	    *this, &ConnectedDINMIDIDevice::begin_fair_cc_candidate_scan,
	    &ConnectedDINMIDIDevice::next_fair_cc_candidate_scan_step);
}

bool ConnectedDINMIDIDevice::begin_fair_cc_candidate_scan(uint16_t& cursor, uint16_t& limit) const {
	cursor = 0;
	limit = queue_manager_.queue_count(QUEUE_PRIORITY_CC);
	return limit >= 3;
}

MIDIQueueManager::CoalesceScanResult ConnectedDINMIDIDevice::next_din_cc_scan_step(uint16_t& cursor, uint16_t limit,
                                                                                   uint16_t& candidate_offset,
                                                                                   uint8_t& status,
                                                                                   uint8_t& controller) const {
	if (cursor >= limit) {
		return MIDIQueueManager::CoalesceScanResult::NoMore;
	}

	status = read_cc_queue_byte_at(cursor);
	int32_t message_len = bytesPerStatusMessage(status);
	if (message_len <= 0 || cursor + message_len > limit) {
		return MIDIQueueManager::CoalesceScanResult::Invalid;
	}

	uint16_t offset = cursor;
	cursor = static_cast<uint16_t>(cursor + message_len);
	if (MIDIQueueManager::is_three_byte_channel_cc(status, message_len)) {
		controller = read_cc_queue_byte_at(static_cast<uint16_t>(offset + 1));
		candidate_offset = offset;
		return MIDIQueueManager::CoalesceScanResult::Matchable;
	}

	return MIDIQueueManager::CoalesceScanResult::Skip;
}

MIDIQueueManager::CandidateScanResult
ConnectedDINMIDIDevice::next_fair_cc_candidate_scan_step(uint16_t& cursor, uint16_t limit, uint16_t& candidate_offset,
                                                         uint8_t& controller) const {
	uint8_t status = 0;
	auto step = next_din_cc_scan_step(cursor, limit, candidate_offset, status, controller);
	if (step == MIDIQueueManager::CoalesceScanResult::Matchable) {
		return MIDIQueueManager::CandidateScanResult::Candidate;
	}
	if (step == MIDIQueueManager::CoalesceScanResult::NoMore) {
		return MIDIQueueManager::CandidateScanResult::NoMore;
	}
	if (step == MIDIQueueManager::CoalesceScanResult::Invalid) {
		return MIDIQueueManager::CandidateScanResult::Invalid;
	}
	return MIDIQueueManager::CandidateScanResult::Skip;
}

/// Coalesces a queued CC by replacing the newest matching pending value.
bool ConnectedDINMIDIDevice::coalesce_queued_cc(MIDIMessage message) {
	if (!MIDIQueueManager::is_channel_cc_status_type(message.statusType)) {
		// Only channel CC messages are eligible for in-queue value replacement.
		return false;
	}

	// Match only the same channel+status byte; sentinel -1 means no queued
	// packet for this controller/channel pair has been found yet.
	uint8_t wanted_status = message.channel | (message.statusType << 4);
	int32_t latest_match_offset = queue_manager_.find_latest_matching_cc_offset(
	    *this, wanted_status, message.data1, &ConnectedDINMIDIDevice::begin_coalesce_cc_scan,
	    &ConnectedDINMIDIDevice::next_coalesce_cc_scan_step);

	if (latest_match_offset < 0) {
		// No pending packet for this controller/channel pair; caller may enqueue normally.
		return false;
	}

	// Patch only the value byte of the newest matching queued packet.
	queue_manager_.overwrite_at(QUEUE_PRIORITY_CC, static_cast<uint16_t>(latest_match_offset + 2), message.data2);
	// Treat coalesce as fresh pressure so fairness can compensate relative enqueue rate.
	queue_manager_.bump_controller_debt(message.data1);
	return true;
}

bool ConnectedDINMIDIDevice::begin_coalesce_cc_scan(uint16_t& cursor, uint16_t& limit) const {
	cursor = 0;
	limit = queue_manager_.queue_count(QUEUE_PRIORITY_CC);
	return limit >= 3;
}

MIDIQueueManager::CoalesceScanResult
ConnectedDINMIDIDevice::next_coalesce_cc_scan_step(uint16_t& cursor, uint16_t limit, uint16_t& candidate_offset,
                                                   uint8_t& status, uint8_t& controller) const {
	return next_din_cc_scan_step(cursor, limit, candidate_offset, status, controller);
}

/// Enqueues a complete byte sequence into one serial-priority lane.
bool ConnectedDINMIDIDevice::enqueue_serial_bytes(QueuePriority priority, uint8_t const* bytes, int32_t len) {
	uint8_t lane = static_cast<uint8_t>(priority);
	if (len <= 0) {
		// Empty payload is a successful no-op; callers can treat it as enqueued.
		return true;
	}
	if (queue_manager_.space(lane) < len) {
		// Reject atomically if lane is full; never enqueue partial messages.
		return false;
	}
	// Space is guaranteed above, so this loop commits the whole message payload.
	for (int32_t i = 0; i < len; i++) {
		queue_manager_.push(lane, bytes[i]);
	}
	return true;
}

/// Pops one realtime byte or one full MIDI message under budget and UART-space limits.
int32_t ConnectedDINMIDIDevice::pop_next_prioritized_bytes(uint8_t* out_bytes, int32_t max_len, int32_t budget_bytes,
                                                           int32_t uart_space, int32_t cc_uart_budget,
                                                           QueuePriority& popped_priority) {
	// Nothing can be emitted if caller buffer room, DIN token budget, or UART
	// space is already exhausted for this scheduling pass.
	if (max_len <= 0 || budget_bytes <= 0 || uart_space <= 0) {
		return 0;
	}

	constexpr size_t k_clock_idx = QUEUE_PRIORITY_CLOCK;
	constexpr size_t k_cc_idx = QUEUE_PRIORITY_CC;
	SerialPriorityPopContext context{out_bytes, budget_bytes, uart_space, max_len, cc_uart_budget, popped_priority};
	return MIDIQueueManager::pop_priority_lanes_with_cc_fairness(
	    *this, static_cast<QueuePriority>(k_clock_idx), static_cast<QueuePriority>(k_cc_idx),
	    &ConnectedDINMIDIDevice::queue_count, &ConnectedDINMIDIDevice::handle_priority_lane_pop,
	    &ConnectedDINMIDIDevice::pop_priority_lane_message, context);
}

MIDIQueueManager::PriorityLaneTraversalResult
ConnectedDINMIDIDevice::handle_priority_lane_pop(QueuePriority priority, SerialPriorityPopContext& context) {
	uint8_t status = queue_manager_.head(static_cast<uint8_t>(priority));
	int32_t message_len = 0;
	auto head_check = MIDIQueueManager::validate_head_message_pop(
	    status, queue_manager_.queue_count(static_cast<uint8_t>(priority)), context.budget_bytes, context.uart_space,
	    context.max_len, message_len);
	if (head_check != MIDIQueueManager::HeadMessageCheckResult::Ready) {
		return MIDIQueueManager::PriorityLaneTraversalResult::Abort;
	}

	bool head_is_cc = MIDIQueueManager::is_three_byte_channel_cc(status, message_len);
	auto cc_result = MIDIQueueManager::try_fair_pop_cc(
	    *this, head_is_cc, context.cc_uart_budget >= 3, &ConnectedDINMIDIDevice::pop_fair_queued_cc_message,
	    context.out_bytes, context.budget_bytes, context.uart_space, context.max_len, context.popped_priority);
	if (cc_result == MIDIQueueManager::CCFairPopResult::Popped) {
		return MIDIQueueManager::PriorityLaneTraversalResult::Popped;
	}
	if (cc_result == MIDIQueueManager::CCFairPopResult::NotCC) {
		return MIDIQueueManager::PriorityLaneTraversalResult::PopLane;
	}

	// Budget-blocked or failed CC fairness should stop this drain pass, matching the previous behavior.
	return MIDIQueueManager::PriorityLaneTraversalResult::Abort;
}

bool ConnectedDINMIDIDevice::pop_priority_lane_message(QueuePriority priority, SerialPriorityPopContext& context) {
	if (priority == QUEUE_PRIORITY_CLOCK) {
		// Realtime/clock lane is byte-oriented: emit at most one byte per call.
		if (context.budget_bytes < 1 || context.uart_space < 1 || context.max_len < 1) {
			return false;
		}
		queue_manager_.pop_head(static_cast<uint8_t>(priority), context.out_bytes[0]);
		context.popped_priority = priority;
		return true;
	}

	uint8_t status = queue_manager_.head(static_cast<uint8_t>(priority));
	int32_t message_len = 0;
	auto head_check = MIDIQueueManager::validate_head_message_pop(
	    status, queue_manager_.queue_count(static_cast<uint8_t>(priority)), context.budget_bytes, context.uart_space,
	    context.max_len, message_len);
	if (head_check != MIDIQueueManager::HeadMessageCheckResult::Ready) {
		return false;
	}

	// For channel messages, prefer popping whole messages to avoid fragmentation.
	// Keep this final guard even after fit checks: pop_many is the atomic boundary.
	if (!queue_manager_.pop_many(static_cast<uint8_t>(priority), context.out_bytes, message_len)) {
		return false;
	}
	context.popped_priority = priority;
	return true;
}

/// Encodes and enqueues one channel/system MIDI message into serial-priority lanes.
void ConnectedDINMIDIDevice::enqueue_serial_message(MIDIMessage message) {
	QueuePriority priority = MIDIQueueManager::classify_message(message);
	queue_manager_.enqueue_with_cc_policy(*this, priority, message,
	                                      /*allow_coalesce=*/true,
	                                      /*track_debt=*/true, message.data1,
	                                      &ConnectedDINMIDIDevice::coalesce_queued_cc,
	                                      &ConnectedDINMIDIDevice::enqueue_priority_message);
}

bool ConnectedDINMIDIDevice::enqueue_priority_message(QueuePriority priority, MIDIMessage message) {
	// Convert message to wire bytes and enqueue atomically into the chosen DIN lane.
	uint8_t status_byte = message.channel | (message.statusType << 4);
	uint8_t raw_bytes[3] = {status_byte, message.data1, message.data2};
	int32_t message_length = bytesPerStatusMessage(status_byte);
	return enqueue_serial_bytes(priority, raw_bytes, message_length);
}

/// Drains serial-priority queues into UART while enforcing DIN pacing and strict priority gates.
void ConnectedDINMIDIDevice::flush_serial_output(uint32_t now_sample_timer) {
	if (!has_serial_data()) {
		// Fast exit when all lanes are empty; avoids pacing/space calculations.
		return;
	}

	// Apply DIN pacing before deciding this iteration's send allowance.
	update_serial_budget(now_sample_timer);

	int32_t raw_uart_space = uartGetTxBufferSpace(UART_ITEM_MIDI);
	int32_t uart_space = raw_uart_space - k_serial_uart_headroom_bytes;
	if (uart_space <= 0) {
		// Preserve a little headroom so other UART activity is not starved.
		return;
	}
	// This counts CC bytes already staged in the UART path and limits additional
	// queued CC so later clock/notes can still preempt promptly.
	int32_t cc_uart_budget =
	    std::max<int32_t>(0, k_serial_buffered_cc_bytes_cap - (MIDI_TX_BUFFER_SIZE - raw_uart_space));

	// Convert Q8 token budget to whole bytes available for this drain pass.
	int32_t send_allowance_bytes = serial_budget_Q8_ >> 8;
	constexpr size_t k_clock_idx = QUEUE_PRIORITY_CLOCK;
	// If no budget exists and no realtime clock is waiting, defer this pass.
	if (send_allowance_bytes <= 0 && queue_manager_.empty(static_cast<uint8_t>(k_clock_idx))) {
		return;
	}

	if (send_allowance_bytes <= 0) {
		// Allow one realtime byte to pass when budget is depleted.
		send_allowance_bytes = 1;
	}

	int32_t sent = 0;
	// Keep draining while both UART capacity and token budget remain.
	while (uart_space > 0 && send_allowance_bytes > 0) {
		uint8_t bytes_to_send[3] = {0, 0, 0};
		QueuePriority popped_priority = QUEUE_PRIORITY_CC;
		int32_t bytes_popped = pop_next_prioritized_bytes(bytes_to_send, 3, send_allowance_bytes, uart_space,
		                                                  cc_uart_budget, popped_priority);
		if (bytes_popped <= 0) {
			break;
		}

		for (int32_t i = 0; i < bytes_popped; i++) {
			// Push selected bytes into the UART MIDI TX buffer.
			bufferMIDIUart(bytes_to_send[i]);
		}
		sent += bytes_popped;

		bool is_cc_message = (popped_priority == QUEUE_PRIORITY_CC);
		if (is_cc_message) {
			// Decrement staged-CC budget only for actual CC-lane output.
			cc_uart_budget -= bytes_popped;
		}
		// Only commit fairness state when a full 3-byte channel-CC frame with a
		// valid controller number has actually been emitted to UART.
		if (is_cc_message && MIDIQueueManager::is_three_byte_channel_cc(bytes_to_send[0], bytes_popped)
		    && bytes_to_send[1] <= kMaxMIDIValue) {
			uint8_t dequeued_controller = bytes_to_send[1];
			// Successful transmit repays that controller's pressure.
			queue_manager_.clear_controller_debt(dequeued_controller);
		}
		uart_space -= bytes_popped;
		send_allowance_bytes -= bytes_popped;
	}

	if (sent > 0) {
		// Convert whole bytes back to Q8 units and debit pacing bucket.
		serial_budget_Q8_ = std::max<int32_t>(0, serial_budget_Q8_ - sent * 256);
	}
}

/*
    for (int32_t d = 0; d < hostedMIDIDevices.getNumElements(); d++) {
        MIDIDeviceUSBHosted* device = (MIDIDeviceUSBHosted*)hostedMIDIDevices.getElement(d);

    }
 */
#pragma GCC diagnostic pop
