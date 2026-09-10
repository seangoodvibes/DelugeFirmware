#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include <cstdint>
namespace recording_scroll_routing_test {
namespace session = deluge::gui::ui_session;
enum class ActionResult { DELEGATED };
struct TimelineView {
	int last_offset = 0, calls = 0;
	ActionResult horizontalEncoderAction(int32_t offset) {
		last_offset = offset;
		++calls;
		return ActionResult::DELEGATED;
	}
};
struct ClipNavigationTimelineView : TimelineView {
	static int32_t& recording_scroll_for_session();
	void focusRegained();
	ActionResult horizontalEncoderAction(int32_t offset);
};
#include "recording_scroll_routing.inc"
TEST_GROUP(RecordingScrollRouting){
    void setup() override{for (auto owner : {session::Id::Local, session::Id::Remote}){session::Scope scope(owner);
ClipNavigationTimelineView{}.focusRegained();
} // namespace recording_scroll_routing_test
}
}
;
TEST(RecordingScrollRouting, saved_positions_are_independent_and_shared_between_views_of_one_session) {
	ClipNavigationTimelineView clip, song;
	{
		session::Scope scope(session::Id::Local);
		clip.recording_scroll_for_session() = 48;
		LONGS_EQUAL(48, song.recording_scroll_for_session());
	}
	{
		session::Scope scope(session::Id::Remote);
		LONGS_EQUAL(-1, clip.recording_scroll_for_session());
		song.recording_scroll_for_session() = 96;
		LONGS_EQUAL(96, clip.recording_scroll_for_session());
	}
	{
		session::Scope scope(session::Id::Local);
		LONGS_EQUAL(48, clip.recording_scroll_for_session());
	}
}
TEST(RecordingScrollRouting, remote_focus_does_not_cancel_local_recording_follow_return_position) {
	ClipNavigationTimelineView clip, song;
	{
		session::Scope scope(session::Id::Local);
		clip.recording_scroll_for_session() = 120;
	}
	{
		session::Scope scope(session::Id::Remote);
		clip.recording_scroll_for_session() = 240;
		song.focusRegained();
		LONGS_EQUAL(-1, clip.recording_scroll_for_session());
	}
	{
		session::Scope scope(session::Id::Local);
		LONGS_EQUAL(120, clip.recording_scroll_for_session());
	}
}
TEST(RecordingScrollRouting, encoder_cancels_only_its_session_and_still_delegates_to_timeline) {
	ClipNavigationTimelineView view;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		view.recording_scroll_for_session() = 72;
	}
	{
		session::Scope scope(session::Id::Local);
		CHECK(view.horizontalEncoderAction(-3) == ActionResult::DELEGATED);
		LONGS_EQUAL(-1, view.recording_scroll_for_session());
		LONGS_EQUAL(-3, view.last_offset);
		LONGS_EQUAL(1, view.calls);
	}
	{
		session::Scope scope(session::Id::Remote);
		LONGS_EQUAL(72, view.recording_scroll_for_session());
	}
}
} // namespace recording_scroll_routing_test
