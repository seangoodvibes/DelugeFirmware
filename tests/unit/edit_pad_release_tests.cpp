#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include <vector>
namespace edit_pad_release_test {
namespace session = deluge::gui::ui_session;
constexpr int kEditPadPressBufferSize = 4, kDisplayHeight = 8;
struct stolen_nodes {
	int num = 0;
	void* nodes = nullptr;
};
struct edit_press {
	uint64_t gesture_revision = 0;
	bool isActive = false, mpeCachedYet = false;
	uint8_t yDisplay = 0;
	stolen_nodes stolenMPE[3];
};
std::vector<void*> released;
void delugeDealloc(void* allocation) {
	released.push_back(allocation);
}
struct InstrumentClipView {
	edit_press editPadPresses[kEditPadPressBufferSize];
	uint8_t numEditPadPresses = 0;
	uint8_t numEditPadPressesPerNoteRowOnScreen[kDisplayHeight]{};
	void endEditPadPress(uint8_t);
};
#include "edit_pad_release.inc"
TEST_GROUP(EditPadRelease){void setup() override{released.clear();
} // namespace edit_pad_release_test
}
;
TEST(EditPadRelease, repeat_release_frees_owned_buffers_once_and_preserves_other_session) {
	session::State<InstrumentClipView> views;
	int local_nodes, remote_nodes, remote_other_nodes;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		auto& view = views.for_owner(owner);
		view.numEditPadPresses = view.numEditPadPressesPerNoteRowOnScreen[2] = 1;
		auto& press = view.editPadPresses[0];
		press.isActive = press.mpeCachedYet = true;
		press.yDisplay = 2;
		press.stolenMPE[0] = {1, owner == session::Id::Local ? &local_nodes : &remote_nodes};
	}
	session::Scope remote(session::Id::Remote);
	auto& view = views.active();
	view.editPadPresses[0].stolenMPE[2] = {2, &remote_other_nodes};
	view.endEditPadPress(0);
	view.endEditPadPress(0);
	LONGS_EQUAL(1, view.editPadPresses[0].gesture_revision);
	LONGS_EQUAL(0, views.for_owner(session::Id::Local).editPadPresses[0].gesture_revision);
	LONGS_EQUAL(2, released.size());
	CHECK(released[0] == &remote_nodes);
	CHECK(released[1] == &remote_other_nodes);
	LONGS_EQUAL(0, view.numEditPadPresses);
	LONGS_EQUAL(0, view.numEditPadPressesPerNoteRowOnScreen[2]);
	CHECK(!view.editPadPresses[0].isActive);
	CHECK(!view.editPadPresses[0].mpeCachedYet);
	for (auto& record : view.editPadPresses[0].stolenMPE) {
		LONGS_EQUAL(0, record.num);
		CHECK(record.nodes == nullptr);
	}
	auto& local = views.for_owner(session::Id::Local);
	LONGS_EQUAL(1, local.numEditPadPresses);
	CHECK(local.editPadPresses[0].isActive);
	CHECK(local.editPadPresses[0].stolenMPE[0].nodes == &local_nodes);
}
TEST(EditPadRelease, invalid_indices_and_stale_row_do_not_underflow_counters) {
	InstrumentClipView view;
	view.endEditPadPress(kEditPadPressBufferSize);
	view.endEditPadPress(255);
	view.endEditPadPress(0);
	auto& press = view.editPadPresses[0];
	press.isActive = true;
	press.yDisplay = 255;
	int allocation;
	press.stolenMPE[0] = {1, &allocation};
	view.endEditPadPress(0);
	LONGS_EQUAL(1, released.size());
	LONGS_EQUAL(0, view.numEditPadPresses);
	for (auto count : view.numEditPadPressesPerNoteRowOnScreen)
		LONGS_EQUAL(0, count);
}
TEST(EditPadRelease, release_preserves_other_presses_on_same_row) {
	InstrumentClipView view;
	view.numEditPadPresses = view.numEditPadPressesPerNoteRowOnScreen[0] = 2;
	view.editPadPresses[0].isActive = view.editPadPresses[1].isActive = true;
	view.endEditPadPress(0);
	view.endEditPadPress(0);
	LONGS_EQUAL(1, view.numEditPadPresses);
	LONGS_EQUAL(1, view.numEditPadPressesPerNoteRowOnScreen[0]);
	CHECK(view.editPadPresses[1].isActive);
}
TEST(EditPadRelease, reusing_a_slot_advances_revision_once_per_release) {
	InstrumentClipView view;
	auto& press = view.editPadPresses[0];
	for (uint64_t revision = 1; revision <= 3; ++revision) {
		press.isActive = true;
		view.endEditPadPress(0);
		UNSIGNED_LONGS_EQUAL(revision, press.gesture_revision);
		view.endEditPadPress(0);
		UNSIGNED_LONGS_EQUAL(revision, press.gesture_revision);
	}
}
} // namespace edit_pad_release_test
