#pragma once

#include "gui/ui/ui_session.h"

namespace deluge::gui::ui_session {

// One pending command, preserving the existing latest-command-wins policy.
// A callback may yield and queue another command, but must not execute it recursively.
template <typename Command>
class DeferredSessionCommand {
public:
	void pend(Command command) {
		if (!suspensions_) {
			pending_ = Entry{command, current()};
		}
	}
	class Suspension {
	public:
		explicit Suspension(DeferredSessionCommand& commands) : commands_(commands) {
			++commands_.suspensions_;
			commands_.cancel();
		}
		~Suspension() { --commands_.suspensions_; }
		Suspension(const Suspension&) = delete;
		Suspension& operator=(const Suspension&) = delete;

	private:
		DeferredSessionCommand& commands_;
	};
	[[nodiscard]] Suspension suspend() { return Suspension(*this); }
	// Cancel pending work only. An active callback still owns the execution guard.
	void cancel() { pending_.reset(); }

	template <typename Callback>
	void service(Callback&& callback) {
		if (suspensions_ || executing_ || !pending_) {
			return;
		}
		const auto entry = *pending_;
		pending_.reset();
		executing_ = true;
		struct Reset {
			bool& flag;
			~Reset() { flag = false; }
		} reset{executing_};
		Scope owner(entry.owner);
		callback(entry.command);
	}

private:
	struct Entry {
		Command command;
		Id owner;
	};
	std::optional<Entry> pending_;
	bool executing_ = false;
	uint32_t suspensions_ = 0;
};

} // namespace deluge::gui::ui_session
