#include "storage/cluster/cluster.h"
#include "storage/storage_manager.h"
#include "util/d_string.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace native_parameter_tests {
std::string_view file_contents;
size_t read_calls = 0;
size_t fail_read_at = 0;
} // namespace native_parameter_tests

size_t Cluster::size = 64;
FirmwareVersion song_firmware_version{FirmwareVersion::Type::OFFICIAL, {}};

namespace AudioEngine {
void logAudioAction(char const*, char const*, int) {
}
} // namespace AudioEngine

extern "C" void freezeWithError(char const* message) {
	throw std::runtime_error(message);
}

FileReader::FileReader() {
	fileClusterBuffer = new char[Cluster::size];
	callRoutines = false;
}
FileReader::FileReader(char* buffer, uint32_t length) {
	fileClusterBuffer = buffer;
	currentReadBufferEndPos = length;
	fileReadBufferCurrentPos = 0;
	memoryBased = true;
	callRoutines = false;
}
FileReader::~FileReader() {
	if (!memoryBased) {
		delete[] fileClusterBuffer;
	}
}
void FileReader::readDone() {
}
FRESULT FileReader::closeWriter() {
	return FR_OK;
}

FileWriter::FileWriter() {
	bufferSize = 65536;
	writeClusterBuffer = new char[bufferSize];
	callRoutines = false;
	resetWriter();
}
FileWriter::~FileWriter() {
	delete[] writeClusterBuffer;
}
void FileWriter::resetWriter() {
	fileWriteBufferCurrentPos = 0;
	fileTotalBytesWritten = 0;
	fileAccessFailedDuringWrite = false;
	indentAmount = 0;
}
void FileWriter::writeChars(char const* text) {
	size_t length = std::strlen(text);
	if (length > bufferSize - fileWriteBufferCurrentPos) {
		throw std::length_error("Native writer buffer exhausted");
	}
	std::memcpy(writeClusterBuffer + fileWriteBufferCurrentPos, text, length);
	fileWriteBufferCurrentPos += length;
}
int32_t FileWriter::bytesWritten() {
	return fileWriteBufferCurrentPos;
}
Error FileWriter::closeAfterWriting(char const*, char const*, char const*) {
	throw std::logic_error("File close outside native writer test scope");
}

extern "C" FRESULT f_read(FIL*, void* buffer, UINT requested, UINT* actual) {
	using namespace native_parameter_tests;
	++read_calls;
	if (fail_read_at && read_calls >= fail_read_at) {
		if (read_calls > fail_read_at + 8) {
			throw std::runtime_error("Parser repeatedly retried a failed read");
		}
		*actual = 0;
		return FR_DISK_ERR;
	}
	*actual = std::min<size_t>(requested, file_contents.size());
	std::memcpy(buffer, file_contents.data(), *actual);
	file_contents.remove_prefix(*actual);
	return FR_OK;
}

void String::clear(bool) {
	throw std::logic_error("String path outside native parameter test scope");
}
Error String::concatenateAtPos(char const*, int32_t, int32_t) {
	throw std::logic_error("String path outside native parameter test scope");
}
FirmwareVersion FirmwareVersion::parse(std::string_view) {
	throw std::logic_error("Firmware metadata outside native parameter test scope");
}