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

inline bool is_channel_cc(uint32_t packed) {
	// Channel-CC status family is 0xBn (high nibble 0x0B).
	return (status_byte(packed) >> 4) == 0x0B;
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
	for (auto& queue_lane : sendDataRingBuf) {
		queue_lane.fill(0);
	}
	ringBufWriteIdx.fill(0);
	ringBufReadIdx.fill(0);
}

/// Returns queued USB packet count for one lane via monotonic write/read counters.
uint16_t ConnectedUSBMIDIDevice::queue_count(QueuePriority priority) {
	// Monotonic write/read counters: occupancy is their difference for each lane.
	uint8_t p = static_cast<uint8_t>(priority);
	return static_cast<uint16_t>(ringBufWriteIdx[p] - ringBufReadIdx[p]);
}

/// Returns total queued USB packet count across all priority lanes.
uint32_t ConnectedUSBMIDIDevice::total_queued_messages() {
	// Aggregate backlog across all USB priority lanes.
	uint32_t queued = 0;
	for (uint8_t p = 0; p < QUEUE_PRIORITY_COUNT; p++) {
		queued += queue_count(static_cast<QueuePriority>(p));
	}
	return queued;
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
	// For channel-CC, prefer updating an already-queued matching controller value over appending another packet.
	if (priority == QUEUE_PRIORITY_CC && coalesce_queued_cc(message)) {
		return;
	}

	// Power-of-two mask wraps index without modulo cost.
	uint8_t p = static_cast<uint8_t>(priority);
	sendDataRingBuf[p][ringBufWriteIdx[p] & MIDI_SEND_RING_MASK] = message;
	ringBufWriteIdx[p]++;

	if (priority == QUEUE_PRIORITY_CC && is_channel_cc(message)) {
		// Extract controller number from data1 for fairness/debt accounting.
		uint8_t controller = data_1(message);
		if (controller <= kMaxMIDIValue) {
			// Enqueued CC increases this controller's pressure in fair selection.
			MIDIQueueManager::bump_controller_debt(usb_cc_fair_controller_debt.data(), controller);
		}
	}
}

/// Coalesces queued USB channel-CC packets by controller/status.
///
/// Searches the USB CC lane for the newest pending packet with the same status
/// byte and controller number, then updates only that packet's value byte.
/// Returns `true` when an in-queue replacement was applied.
bool ConnectedUSBMIDIDevice::coalesce_queued_cc(uint32_t message) {
	// Coalescing is defined only for channel-CC packets; other message types must enqueue normally.
	if (!is_channel_cc(message)) {
		return false;
	}

	constexpr uint8_t p = QUEUE_PRIORITY_CC;
	uint16_t queue_size = queue_count(QUEUE_PRIORITY_CC);
	if (!queue_size) {
		// No queued CC packets means there is nothing to coalesce in-place.
		return false;
	}

	uint8_t wanted_status = status_byte(message);
	uint8_t wanted_controller = data_1(message);
	int32_t latest_offset = -1;

	// Walk the queued CC lane and remember the newest matching status/controller.
	for (uint16_t offset = 0; offset < queue_size; offset++) {
		// Ring read index + logical offset gives this packet's current queue position.
		uint32_t queued = sendDataRingBuf[p][(ringBufReadIdx[p] + offset) & MIDI_SEND_RING_MASK];
		if (is_channel_cc(queued) && status_byte(queued) == wanted_status && data_1(queued) == wanted_controller) {
			// Keep updating so the final match is the latest pending packet.
			latest_offset = offset;
		}
	}

	if (latest_offset < 0) {
		// No matching queued status/controller pair was found; caller should enqueue a new packet.
		return false;
	}

	// Replace value byte in-place while preserving queue order for all packets.
	uint16_t target_idx = (ringBufReadIdx[p] + latest_offset) & MIDI_SEND_RING_MASK;
	// Keep CIN/status/data1 (low 24 bits) and overwrite only data2 (high byte).
	sendDataRingBuf[p][target_idx] =
	    (sendDataRingBuf[p][target_idx] & 0x00FFFFFFu) | (static_cast<uint32_t>(data_2(message)) << 24);
	// Treat this coalesced write as fresh controller pressure for fair dequeue.
	MIDIQueueManager::bump_controller_debt(usb_cc_fair_controller_debt.data(), wanted_controller);
	return true;
}

/// Removes one queued USB CC packet at a logical offset, atomically.
///
/// Fair dequeue may target a packet that is not at the lane head. This helper
/// copies the selected packet out, rebuilds the remaining CC-lane order, and
/// resets lane cursors to the rebuilt image.
bool ConnectedUSBMIDIDevice::remove_queued_cc_message_at_offset(uint16_t target_offset, uint32_t& message_out) {
	constexpr uint8_t p = QUEUE_PRIORITY_CC;
	uint16_t queue_size = queue_count(QUEUE_PRIORITY_CC);
	if (target_offset >= queue_size) {
		// Selected logical offset is outside current queue snapshot; cannot remove safely.
		return false;
	}

	// Translate logical queue offset into the wrapped physical ring index.
	uint16_t target_idx = (ringBufReadIdx[p] + target_offset) & MIDI_SEND_RING_MASK;
	// Return the selected packet so caller can emit/process it after atomic removal.
	message_out = sendDataRingBuf[p][target_idx];

	uint16_t scratch_size = 0;
	// Rebuild a compact queue image by copying every packet except the selected one.
	for (uint16_t i = 0; i < queue_size; i++) {
		if (i == target_offset) {
			// Skip the target packet; it has already been captured in message_out.
			continue;
		}
		// Preserve logical queue order while writing survivors into scratch storage.
		usb_cc_reorder_scratch[scratch_size++] = sendDataRingBuf[p][(ringBufReadIdx[p] + i) & MIDI_SEND_RING_MASK];
	}

	// Rebuild lane content without the selected packet to keep order deterministic.
	ringBufReadIdx[p] = 0;
	ringBufWriteIdx[p] = 0;
	for (uint16_t i = 0; i < scratch_size; i++) {
		// Replay compacted packets back into the lane in preserved logical order.
		sendDataRingBuf[p][ringBufWriteIdx[p] & MIDI_SEND_RING_MASK] = usb_cc_reorder_scratch[i];
		// Advance write cursor after each restored packet.
		ringBufWriteIdx[p]++;
	}

	return true;
}

/// Pops one USB packet using strict priority ordering, with fair CC selection.
bool ConnectedUSBMIDIDevice::pop_priority_message(uint32_t& message_out, int32_t& cc_budget_packets_remaining) {
	for (uint8_t p = QUEUE_PRIORITY_CLOCK; p < QUEUE_PRIORITY_COUNT; p++) {
		QueuePriority priority = static_cast<QueuePriority>(p);
		if (!queue_count(priority)) {
			continue;
		}

		if (priority == QUEUE_PRIORITY_CC) {
			// Inspect the CC-lane head packet to decide whether CC fairness rules apply.
			uint16_t head_idx = ringBufReadIdx[p] & MIDI_SEND_RING_MASK;
			uint32_t head_message = sendDataRingBuf[p][head_idx];

			if (is_channel_cc(head_message)) {
				// Per-transfer CC cap prevents low-priority bursts from dominating the batch.
				if (cc_budget_packets_remaining <= 0) {
					// Skip CC for now and keep scanning lower lanes (e.g. SysEx) this pass.
					continue;
				}
				// Pop one fair-selected CC packet (RR baseline + debt preference).
				if (pop_fair_queued_cc_message(message_out)) {
					// Charge one CC slot so the cap is enforced across this transfer assembly.
					cc_budget_packets_remaining--;
					return true;
				}
				// If fair pop failed, do not dequeue arbitrary CC head data in this call.
				continue;
			}
			// Non-CC packets living in the CC lane are handled by the generic dequeue path below.
		}
		// Power-of-two mask wraps index without modulo cost.
		message_out = sendDataRingBuf[p][ringBufReadIdx[p] & MIDI_SEND_RING_MASK];
		ringBufReadIdx[p]++;
		return true;
	}

	return false;
}

/// Pops one queued USB channel-CC packet using controller fairness.
///
/// Selection flow:
/// 1. Capture each controller's first queued CC offset.
/// 2. Establish RR baseline from rotating controller cursor.
/// 3. Prefer highest-debt controller when debt is non-zero.
/// 4. Remove selected packet atomically and commit fairness state.
bool ConnectedUSBMIDIDevice::pop_fair_queued_cc_message(uint32_t& message_out) {
	constexpr uint8_t p = QUEUE_PRIORITY_CC;
	uint16_t queue_size = queue_count(QUEUE_PRIORITY_CC);
	if (!queue_size) {
		// No queued CC packets means there is nothing eligible for fair dequeue.
		return false;
	}

	// Initialize this scan snapshot to "no queued packet found yet" for each controller.
	auto& first_offsets = MIDIQueueManager::initialize_first_controller_offsets(usb_cc_fair_first_offsets);
	// Tracks whether this queue snapshot contains any channel-CC packets at all.
	bool saw_any_cc = false;

	// Scan the current CC queue snapshot to collect first-seen offsets per controller.
	for (uint16_t offset = 0; offset < queue_size; offset++) {
		// Map logical scan offset to wrapped ring index, then inspect that queued packet.
		uint32_t queued = sendDataRingBuf[p][(ringBufReadIdx[p] + offset) & MIDI_SEND_RING_MASK];
		// Fair selection in this pass only considers channel-CC packets.
		if (!is_channel_cc(queued)) {
			continue;
		}
		saw_any_cc = true;
		// For channel-CC packets, data1 is the controller number used as fairness key.
		uint8_t controller = data_1(queued);
		// Record only the first queued packet offset for each controller in this snapshot.
		MIDIQueueManager::record_first_controller_offset(first_offsets, controller, offset);
	}

	if (!saw_any_cc) {
		// Without any channel-CC candidates in this snapshot, fair dequeue cannot select a packet.
		return false;
	}

	// Candidate selection uses shared RR+debt policy logic.
	uint16_t selected_offset = 0;
	uint8_t selected_controller = 0;
	if (!MIDIQueueManager::select_fair_controller_candidate(first_offsets, usb_cc_fair_next_controller,
	                                                        usb_cc_fair_controller_debt.data(), selected_offset,
	                                                        selected_controller)) {
		// No eligible controller was discovered, so fair dequeue has nothing to emit this pass.
		return false;
	}

	// Do not commit fairness bookkeeping unless the selected packet is removed atomically.
	if (!remove_queued_cc_message_at_offset(selected_offset, message_out)) {
		return false;
	}

	// Commit post-dequeue fairness state for the serviced controller, then rotate RR start.
	MIDIQueueManager::commit_fair_controller_service(usb_cc_fair_controller_debt, usb_cc_fair_next_controller,
	                                                 selected_controller);
	// Selection and removal succeeded; caller can emit the selected packet.
	return true;
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

/// Resets serial pacing state so the next flush starts from a known baseline.
void ConnectedDINMIDIDevice::reset_serial_state(uint32_t now_sample_timer) {
	// Start pacing from "now" and with zero carry-over send budget.
	serial_budget_last_update_ = now_sample_timer;
	serial_budget_Q8_ = 0;
}

/// Pushes one byte into a serial-priority ring buffer lane.
bool ConnectedDINMIDIDevice::SerialByteQueue::push(uint8_t byte) {
	// Ring wraps with a mask because capacity is a power of two.
	uint16_t next = (write_pos + 1) & (k_capacity - 1);
	// Keep one slot open so full vs empty remains distinguishable.
	if (next == read_pos) {
		return false;
	}
	data[write_pos] = byte;
	write_pos = next;
	return true;
}

/// Pops one byte from a serial-priority ring buffer lane.
bool ConnectedDINMIDIDevice::SerialByteQueue::pop(uint8_t& out) {
	// read_pos == write_pos means queue is empty.
	if (read_pos == write_pos) {
		return false;
	}
	out = data[read_pos];
	// Consume one byte and wrap cursor within ring capacity.
	read_pos = (read_pos + 1) & (k_capacity - 1);
	return true;
}

/// Pops `count` bytes atomically from a serial-priority ring buffer lane.
bool ConnectedDINMIDIDevice::SerialByteQueue::pop_many(uint8_t* out, uint16_t count) {
	// All-or-nothing pop to preserve complete MIDI message boundaries.
	if (size() < count) {
		return false;
	}
	// Copy the logical span out of the ring, wrapping with the capacity mask.
	for (uint16_t i = 0; i < count; i++) {
		out[i] = data[(read_pos + i) & (k_capacity - 1)];
	}
	// Consume the copied span and wrap the cursor within ring capacity.
	read_pos = (read_pos + count) & (k_capacity - 1);
	return true;
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
	for (auto const& queue : serial_priority_queues_) {
		if (!queue.empty()) {
			// One populated lane is enough to indicate pending serial output work.
			return true;
		}
	}
	return false;
}

/// Removes a 3-byte CC message from an arbitrary byte offset in the CC ring.
///
/// Fair dequeue may select a controller whose earliest queued message is not at
/// the ring head. To preserve message order and atomicity, this function copies
/// out the selected message, rebuilds the remaining bytes in-order, and resets
/// ring cursors to the rebuilt layout.
bool ConnectedDINMIDIDevice::remove_queued_cc_message_at_offset(uint16_t target_offset, uint8_t* out_bytes) {
	SerialByteQueue& queue = serial_priority_queues_[QUEUE_PRIORITY_CC];
	uint16_t queue_size = queue.size();
	if (target_offset + 3 > queue_size) {
		// Selected CC span must be fully in-bounds of the current queue snapshot.
		return false;
	}

	// Fair selection can target a CC message in the middle of the byte ring,
	// so remove atomically by rebuilding the remaining bytes in-order.
	for (uint16_t i = 0; i < 3; i++) {
		out_bytes[i] = queue.peek(target_offset + i);
	}

	// Repack queue contents minus the selected 3-byte CC span.
	uint16_t scratch_size = 0;
	for (uint16_t i = 0; i < queue_size; i++) {
		if (i >= target_offset && i < target_offset + 3) {
			continue;
		}
		// Preserve byte order for all surviving queued messages.
		cc_reorder_scratch_[scratch_size++] = queue.peek(i);
	}

	// Reinitialize ring cursors, then replay compacted bytes as the new queue image.
	queue.read_pos = 0;
	queue.write_pos = 0;
	for (uint16_t i = 0; i < scratch_size; i++) {
		queue.push(cc_reorder_scratch_[i]);
	}

	return true;
}

/// Pops one queued CC message using controller-aware fairness.
///
/// Selection policy:
/// 1. Collect each controller's first queued CC offset.
/// 2. Start from `cc_fair_next_controller_` for round-robin ordering.
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

	SerialByteQueue& queue = serial_priority_queues_[QUEUE_PRIORITY_CC];
	uint16_t queue_size = queue.size();
	if (queue_size < 3) {
		// No possible CC candidate if fewer than 3 bytes are queued.
		return false;
	}

	// Initialize this scan snapshot to "no queued packet for this controller".
	auto& first_offsets = MIDIQueueManager::initialize_first_controller_offsets(cc_fair_first_offsets_);
	// Tracks whether this queue snapshot contains any channel-CC packets at all.
	bool saw_any_cc = false;

	// Walk message-by-message so offsets always align to parsed MIDI frames, then
	// capture the first queued CC packet per controller for fair candidate selection.
	for (uint16_t offset = 0; offset < queue_size;) {
		uint8_t status = queue.peek(offset);
		int32_t message_len = bytesPerStatusMessage(status);
		if (message_len <= 0 || offset + message_len > queue_size) {
			// Abort instead of consuming bytes from malformed/partial message boundaries.
			return false;
		}

		// Consider only canonical 3-byte channel-CC frames when building per-
		// controller dequeue candidates for fairness selection.
		if (message_len == 3 && (status >> 4) == 0x0B) {
			saw_any_cc = true;
			uint8_t controller = queue.peek(offset + 1);
			// Capture only the first offset per controller; later occurrences remain behind it in-order.
			MIDIQueueManager::record_first_controller_offset(first_offsets, controller, offset);
		}

		// Step by one parsed MIDI frame to keep subsequent reads aligned.
		offset += static_cast<uint16_t>(message_len);
	}

	if (!saw_any_cc) {
		// No eligible CC frames were discovered during the scan, so fairness
		// selection has no candidates to dequeue this pass.
		return false;
	}

	// Candidate selection uses shared RR+debt policy logic.
	uint16_t selected_offset = 0;
	uint8_t selected_controller = 0;
	if (!MIDIQueueManager::select_fair_controller_candidate(first_offsets, cc_fair_next_controller_,
	                                                        cc_fair_controller_debt_.data(), selected_offset,
	                                                        selected_controller)) {
		// Sweep found no eligible controller candidate to dequeue this pass.
		return false;
	}

	// Commit nothing unless the selected CC can be removed atomically.
	if (!remove_queued_cc_message_at_offset(selected_offset, out_bytes)) {
		return false;
	}

	// Successful dequeue commits fairness bookkeeping and rotates RR start cursor.
	MIDIQueueManager::commit_fair_controller_service(cc_fair_controller_debt_, cc_fair_next_controller_,
	                                                 selected_controller);
	popped_priority = QUEUE_PRIORITY_CC;
	return true;
}

/// Coalesces a queued CC by replacing the newest matching pending value.
bool ConnectedDINMIDIDevice::coalesce_queued_cc(MIDIMessage message) {
	if (message.statusType != 0x0B) {
		// Only channel CC messages are eligible for in-queue value replacement.
		return false;
	}

	SerialByteQueue& queue = serial_priority_queues_[QUEUE_PRIORITY_CC];
	uint16_t queue_size = queue.size();
	if (queue_size < 3) {
		// A complete CC frame is 3 bytes, so shorter queues cannot contain one.
		return false;
	}

	// Match only the same channel+status byte; sentinel -1 means no queued
	// packet for this controller/channel pair has been found yet.
	uint8_t wanted_status = message.channel | (message.statusType << 4);
	int32_t latest_match_offset = -1;

	for (uint16_t offset = 0; offset < queue_size;) {
		uint8_t status = queue.peek(offset);
		int32_t message_len = bytesPerStatusMessage(status);
		if (message_len <= 0 || offset + message_len > queue_size) {
			// Stop coalescing scan on malformed boundary rather than mutating unknown bytes.
			break;
		}

		// Match only full 3-byte CC packets for the same status/channel and
		// controller number; those are eligible for in-place value replacement.
		if (message_len == 3 && status == wanted_status && queue.peek(offset + 1) == message.data1) {
			// Keep walking to coalesce the newest pending value for this controller.
			latest_match_offset = offset;
		}

		offset += message_len;
	}

	if (latest_match_offset < 0) {
		// No pending packet for this controller/channel pair; caller may enqueue normally.
		return false;
	}

	// Patch only the value byte of the newest matching queued packet.
	uint16_t value_index = (queue.read_pos + latest_match_offset + 2) & (SerialByteQueue::k_capacity - 1);
	queue.data[value_index] = message.data2;
	// Treat coalesce as fresh pressure so fairness can compensate relative enqueue rate.
	MIDIQueueManager::bump_controller_debt(cc_fair_controller_debt_.data(), message.data1);
	return true;
}

/// Enqueues a complete byte sequence into one serial-priority lane.
bool ConnectedDINMIDIDevice::enqueue_serial_bytes(QueuePriority priority, uint8_t const* bytes, int32_t len) {
	SerialByteQueue& queue = serial_priority_queues_[static_cast<uint8_t>(priority)];
	if (len <= 0) {
		// Empty payload is a successful no-op; callers can treat it as enqueued.
		return true;
	}
	if (queue.space() < len) {
		// Reject atomically if lane is full; never enqueue partial messages.
		return false;
	}
	// Space is guaranteed above, so this loop commits the whole message payload.
	for (int32_t i = 0; i < len; i++) {
		queue.push(bytes[i]);
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

	// Scan lanes in strict priority order (clock -> notes -> expression -> CC)
	// and stop at the first lane that can produce a full eligible payload.
	for (size_t idx = k_clock_idx; idx <= k_cc_idx; idx++) {
		SerialByteQueue& queue = serial_priority_queues_[idx];
		if (queue.empty()) {
			// This lane has no work; advance to the next priority lane in-order.
			continue;
		}

		if (idx == k_clock_idx) {
			// Realtime/clock lane is byte-oriented: emit at most one byte per call.
			if (budget_bytes < 1 || uart_space < 1 || max_len < 1) {
				return 0;
			}
			queue.pop(out_bytes[0]);
			// Report the lane and exact byte count so caller accounting stays correct.
			popped_priority = static_cast<QueuePriority>(idx);
			return 1;
		}

		if (idx == k_cc_idx) {
			// Parse the queue-head message once so we can gate CC dequeue safely.
			uint8_t status = queue.peek();
			int32_t message_len = bytesPerStatusMessage(status);
			if (message_len <= 0) {
				// Unknown head status: defer and try again on a later scheduler pass.
				return 0;
			}

			// Never dequeue a CC message if the staged-CC UART budget is exhausted.
			if ((status >> 4) == 0x0B && message_len == 3 && cc_uart_budget < 3) {
				return 0;
			}

			// The CC-priority lane may contain non-CC channel messages (e.g. program change).
			// Only run fair dequeue for actual 3-byte CC messages.
			if ((status >> 4) == 0x0B && message_len == 3) {
				if (pop_fair_queued_cc_message(out_bytes, budget_bytes, uart_space, max_len, popped_priority)) {
					return 3;
				}
			}
		}

		// For channel messages, prefer popping whole messages to avoid fragmentation.
		uint8_t status = queue.peek();
		int32_t message_len = bytesPerStatusMessage(status);
		// Unknown status (or malformed queue content) is skipped safely this pass.
		if (message_len <= 0) {
			return 0;
		}
		// Require full message fit in queue/budget/space for atomic send.
		if (queue.size() < message_len || budget_bytes < message_len || uart_space < message_len
		    || max_len < message_len) {
			return 0;
		}
		// Keep this final guard even after fit checks: pop_many is the atomic boundary.
		if (!queue.pop_many(out_bytes, message_len)) {
			return 0;
		}
		// Report both the source lane and actual byte count so caller accounting stays correct.
		popped_priority = static_cast<QueuePriority>(idx);
		return message_len;
	}

	return 0;
}

/// Encodes and enqueues one channel/system MIDI message into serial-priority lanes.
void ConnectedDINMIDIDevice::enqueue_serial_message(MIDIMessage message) {
	// Convert message to wire bytes and queue by shared priority policy.
	uint8_t status_byte = message.channel | (message.statusType << 4);
	uint8_t raw_bytes[3] = {status_byte, message.data1, message.data2};
	int32_t message_length = bytesPerStatusMessage(status_byte);
	QueuePriority priority = MIDIQueueManager::classify_message(message);
	if (priority == QUEUE_PRIORITY_CC && coalesce_queued_cc(message)) {
		// For dense CC streams, replace pending controller value instead of appending another packet.
		return;
	}

	// Atomic lane enqueue; false means lane full and message is intentionally dropped.
	bool queued_ok = enqueue_serial_bytes(priority, raw_bytes, message_length);
	if (priority == QUEUE_PRIORITY_CC) {
		if (!queued_ok) {
			// Do not update fairness state for data that never entered the queue.
			return;
		}
		// Debt tracks relative enqueue/coalesce pressure so dequeue can compensate fairly.
		uint8_t controller = message.data1;
		if (controller <= kMaxMIDIValue) {
			// Mark this controller as newly backlogged so fair dequeue can compensate.
			MIDIQueueManager::bump_controller_debt(cc_fair_controller_debt_.data(), controller);
		}
	}
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
	if (send_allowance_bytes <= 0 && serial_priority_queues_[k_clock_idx].empty()) {
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
		if (is_cc_message && bytes_popped == 3 && (bytes_to_send[0] >> 4) == 0x0B
		    && bytes_to_send[1] <= kMaxMIDIValue) {
			uint8_t dequeued_controller = bytes_to_send[1];
			// Successful transmit repays that controller's pressure.
			cc_fair_controller_debt_[dequeued_controller] = 0;
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
