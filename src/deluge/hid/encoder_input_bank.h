#pragma once
#include "hid/encoders.h"

namespace deluge::hid::encoders {

// Injected UI input never shares the physical IRQ counters. One outstanding
// movement preserves its position relative to subsequent button/pad events.
struct EncoderInputBank {
	// A handler can yield after taking its ticks. Keep the bank occupied until
	// it returns, including when it restores those ticks for a card-routine retry.
	class Dispatch {
	public:
		explicit Dispatch(EncoderInputBank& bank) : bank_(bank.dispatching_ ? nullptr : &bank) {
			if (bank_)
				bank_->dispatching_ = true;
		}
		Dispatch(const Dispatch&) = delete;
		Dispatch& operator=(const Dispatch&) = delete;
		~Dispatch() {
			if (!bank_)
				return;
			bank_->dispatching_ = false;
			if (bank_->clear_after_dispatch_) {
				bank_->clear_after_dispatch_ = false;
				bank_->clear();
			}
		}
		explicit operator bool() const { return bank_ != nullptr; }

	private:
		EncoderInputBank* bank_;
	};

	DetentedEncoder functions[4];
	ContinuousEncoder mods[2];
	bool pending() const {
		if (dispatching_)
			return true;
		for (const auto& encoder : functions)
			if (encoder.pending())
				return true;
		for (const auto& encoder : mods)
			if (encoder.pending())
				return true;
		return false;
	}
	bool queue(uint8_t index, int32_t delta) {
		if (index >= 6 || delta == 0 || delta < -127 || delta > 127 || pending())
			return false;
		if (index < 4)
			functions[index].restore(delta);
		else
			mods[index - 4].applyEdges(static_cast<int8_t>(delta));
		return true;
	}
	void clear() {
		if (dispatching_) {
			// A returning handler may restore its input. Clear after that restore
			// so teardown cannot leave an old movement in a new session.
			clear_after_dispatch_ = true;
			return;
		}
		for (auto& encoder : functions)
			encoder.take();
		for (auto& encoder : mods) {
			encoder.take();
			encoder.reset_speed_for_session();
		}
	}

private:
	bool dispatching_ = false;
	bool clear_after_dispatch_ = false;
};

} // namespace deluge::hid::encoders
