#include "CppUTest/TestHarness.h"
#include "model/voiced.h"
#include <functional>

namespace voiced_hibernation_test {
struct VoiceOwner : Voiced {
	int preparations = 0;
	std::function<void()> on_prepare;
	bool anyNoteIsOn() override { return false; }
	bool hasActiveVoices() const override { return false; }
	void killAllVoices() override {}
	bool allowNoteTails(ModelStackWithSoundFlags*, bool) override { return false; }
	void prepareForHibernation() override {
		++preparations;
		if (on_prepare)
			on_prepare();
	}
};
} // namespace voiced_hibernation_test
TEST_GROUP(VoicedHibernation){};
TEST(VoicedHibernation, default_adapter_preserves_normal_virtual_dispatch) {
	voiced_hibernation_test::VoiceOwner owner;
	CHECK(owner.try_prepare_for_hibernation());
	LONGS_EQUAL(1, owner.preparations);
}
TEST(VoicedHibernation, default_adapter_rejects_invalid_entry_and_callback_cancellation) {
	voiced_hibernation_test::VoiceOwner owner;
	bool valid = false;
	auto check = [&] { return valid; };
	deluge::lifetime::callback_validation validation(check);
	CHECK_FALSE(owner.try_prepare_for_hibernation(&validation));
	LONGS_EQUAL(0, owner.preparations);
	valid = true;
	owner.on_prepare = [&] { valid = false; };
	CHECK_FALSE(owner.try_prepare_for_hibernation(&validation));
	LONGS_EQUAL(1, owner.preparations);
}
