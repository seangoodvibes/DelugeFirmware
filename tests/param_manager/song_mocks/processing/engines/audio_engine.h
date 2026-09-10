#pragma once
#include <functional>
namespace AudioEngine {
inline std::function<void()> on_routine;
inline void logAction(const char*) {
}
inline void routineWithClusterLoading() {
	auto callback = on_routine;
	if (callback)
		callback();
}
} // namespace AudioEngine