#include "processing/engines/audio_engine.h"
#include "storage/cluster/cluster.h"
#include "storage/storage_manager.h"

// Keep buffering separate from storage-manager services so native parser tests
// exercise these same refill/cursor operations with only file I/O replaced.
void FileReader::resetReader() {
	if (!memoryBased) {
		fileReadBufferCurrentPos = Cluster::size;
		currentReadBufferEndPos = Cluster::size;
	}
	else {
		fileReadBufferCurrentPos = 0;
	}
	readCount = 0;
	reachedBufferEnd = false;
}

bool FileReader::readFileClusterIfNecessary() {
	if (memoryBased) {
		if (fileReadBufferCurrentPos >= currentReadBufferEndPos) {
			reachedBufferEnd = true;
		}
		return !reachedBufferEnd;
	}
	if (fileReadBufferCurrentPos >= Cluster::size) {
		readCount = 0;
		bool result = readFileCluster();
		if (!result) {
			reachedBufferEnd = true;
		}
		return result;
	}
	if (fileReadBufferCurrentPos >= currentReadBufferEndPos) {
		reachedBufferEnd = true;
	}
	return false;
}

bool FileReader::readFileCluster() {
	AudioEngine::logAction("readFileCluster");
	if (memoryBased) {
		return true;
	}
	FRESULT result = f_read(&readFIL, (UINT*)fileClusterBuffer, Cluster::size, &currentReadBufferEndPos);
	if (result || !currentReadBufferEndPos) {
		return false;
	}
	fileReadBufferCurrentPos = 0;
	return true;
}

bool FileReader::peekChar(char* thisChar) {
	readFileClusterIfNecessary();
	if (reachedBufferEnd) {
		return false;
	}
	*thisChar = fileClusterBuffer[fileReadBufferCurrentPos];
	return true;
}

bool FileReader::readChar(char* thisChar) {
	readFileClusterIfNecessary();
	if (reachedBufferEnd) {
		return false;
	}
	*thisChar = fileClusterBuffer[fileReadBufferCurrentPos];
	fileReadBufferCurrentPos++;
	return true;
}