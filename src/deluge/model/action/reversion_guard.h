#pragma once

// Cooperative re-entry protection, not an interrupt/thread synchronization lock.
class ReversionGuard {
public:
	explicit ReversionGuard(bool& active) : active_(active), acquired_(!active) {
		if (acquired_) {
			active_ = true;
		}
	}
	~ReversionGuard() {
		if (acquired_) {
			active_ = false;
		}
	}
	ReversionGuard(const ReversionGuard&) = delete;
	ReversionGuard& operator=(const ReversionGuard&) = delete;
	explicit operator bool() const { return acquired_; }

private:
	bool& active_;
	bool acquired_;
};
