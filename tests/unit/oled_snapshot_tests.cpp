#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "hid/display/oled_frame_state.h"
#include <array>

// Only the surrounding hardware UI is omitted. Both the frame implementation
// and accessor body are production code.
namespace deluge::hid::display {
class OLED {
public:
	static void invalidate_remote_frame();
	static std::optional<uint32_t> copy_remote_frame(std::span<uint8_t> destination);
};
static deluge::gui::ui_session::State<OLEDFrameState> frame_states;

#include "oled_snapshot_method.inc"
} // namespace deluge::hid::display

namespace session = deluge::gui::ui_session;
namespace display = deluge::hid::display;

TEST_GROUP(OLEDSnapshot) {
	std::array<uint8_t, sizeof(display::oled_canvas::Canvas::ImageStore)> snapshot;
	void setup() override {
		session::detail::active = session::Id::Local;
		snapshot.fill(0xCC);
		for (auto owner : {session::Id::Local, session::Id::Remote}) {
			auto& frame = display::frame_states.for_owner(owner);
			frame.current_image = nullptr;
			frame.published = false;
			frame.revision = 0;
		}
	}
	void publish(session::Id owner, uint8_t value) {
		auto& frame = display::frame_states.for_owner(owner);
		memset(frame.main.hackGetImageStore(), value, snapshot.size());
		frame.current_image = frame.main.hackGetImageStore();
		frame.publish();
	}
};

TEST(OLEDSnapshot, unpublished_and_wrong_size_do_not_modify_destination) {
	CHECK_FALSE(display::OLED::copy_remote_frame(snapshot));
	publish(session::Id::Remote, 7);
	CHECK_FALSE(display::OLED::copy_remote_frame(std::span(snapshot).first(snapshot.size() - 1)));
	CHECK_FALSE(display::OLED::copy_remote_frame({}));
	for (auto value : snapshot)
		LONGS_EQUAL(0xCC, value);
}

TEST(OLEDSnapshot, always_copies_remote_without_switching_caller_ownership) {
	publish(session::Id::Local, 5);
	publish(session::Id::Remote, 9);
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		auto revision = display::OLED::copy_remote_frame(snapshot);
		CHECK_TRUE(revision.has_value());
		UNSIGNED_LONGS_EQUAL(1, *revision);
		CHECK_TRUE(session::current() == owner);
		for (auto value : snapshot)
			LONGS_EQUAL(9, value);
	}
	LONGS_EQUAL(5, display::frame_states.for_owner(session::Id::Local).current_image[0][0]);
}

TEST(OLEDSnapshot, working_edits_and_later_publications_cannot_tear_transport_copy) {
	publish(session::Id::Remote, 7);
	auto& frame = display::frame_states.for_owner(session::Id::Remote);
	memset(frame.main.hackGetImageStore(), 8, snapshot.size());
	frame.current_image = frame.main.hackGetImageStore();
	CHECK_TRUE(display::OLED::copy_remote_frame(snapshot));
	for (auto value : snapshot)
		LONGS_EQUAL(7, value);
	frame.publish();
	for (auto value : snapshot)
		LONGS_EQUAL(7, value);
	auto revision = display::OLED::copy_remote_frame(snapshot);
	CHECK_TRUE(revision.has_value());
	UNSIGNED_LONGS_EQUAL(2, *revision);
	for (auto value : snapshot)
		LONGS_EQUAL(8, value);
}

TEST(OLEDSnapshot, revision_wrap_does_not_hide_published_frame) {
	display::frame_states.for_owner(session::Id::Remote).revision = UINT32_MAX;
	publish(session::Id::Remote, 4);
	auto revision = display::OLED::copy_remote_frame(snapshot);
	CHECK_TRUE(revision.has_value());
	UNSIGNED_LONGS_EQUAL(0, *revision);
	for (auto value : snapshot)
		LONGS_EQUAL(4, value);
}

TEST(OLEDSnapshot, invalidation_requires_new_render_and_preserves_local_frame) {
	publish(session::Id::Local, 5);
	publish(session::Id::Remote, 9);
	display::OLED::invalidate_remote_frame();
	CHECK_TRUE(session::current() == session::Id::Local);
	CHECK_FALSE(display::OLED::copy_remote_frame(snapshot));
	auto& remote = display::frame_states.for_owner(session::Id::Remote);
	remote.publish();
	CHECK_FALSE(display::OLED::copy_remote_frame(snapshot));
	CHECK_TRUE(display::frame_states.for_owner(session::Id::Local).published);
	LONGS_EQUAL(5, display::frame_states.for_owner(session::Id::Local).current_image[0][0]);
	publish(session::Id::Remote, 3);
	CHECK_TRUE(display::OLED::copy_remote_frame(snapshot));
	for (auto value : snapshot)
		LONGS_EQUAL(3, value);
}
