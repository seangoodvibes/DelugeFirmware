#pragma once
#include <cstdint>

namespace deluge::modulation::automation {

// Presentation caches can observe completed user edits without registering
// listeners or storing pointers into a song. Playback interpolation is separate.
inline uint64_t parameter_revision = 0;
class ParameterEditRevision final {
public:
	ParameterEditRevision() = default;
	~ParameterEditRevision() { ++parameter_revision; }
	ParameterEditRevision(const ParameterEditRevision&) = delete;
	ParameterEditRevision& operator=(const ParameterEditRevision&) = delete;
};

} // namespace deluge::modulation::automation
