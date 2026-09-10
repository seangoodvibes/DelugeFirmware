#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
namespace automation_selection_context_test {
namespace session = deluge::gui::ui_session;
namespace modulation::params {
enum class Kind { NONE, PATCH_CABLE };
}
namespace deluge {
namespace modulation = ::automation_selection_context_test::modulation;
namespace hid {
using Button = int;
namespace button {
constexpr int CLIP_VIEW = 1, SELECT_ENC = 2, BACK = 3, X_ENC = 4;
}
} // namespace hid
} // namespace deluge
using kind_type = modulation::params::Kind;
constexpr int MODEL_STACK_MAX_SIZE = 128, kKnobPosOffset = 64, kNoSelection = 255;
enum class AutomationParamType { NONE, PER_SOUND };
struct collection {
	kind_type getParamKind() { return kind_type::PATCH_CABLE; }
};
struct ModelStackWithAutoParam {
	void* autoParam = nullptr;
	collection* paramCollection = nullptr;
	int paramId = 9;
};
struct selection {
	int id = -1, output_type = -1, source = -1, x = -1, y = -1, position = -1;
	kind_type kind = kind_type::NONE;
};
struct selectable {
	int last_clip_instance_entered_start_pos_for_session() { return 0; }
	session::State<selection> state;
	int& last_selected_param_id_for_session() { return state.active().id; }
	kind_type& last_selected_param_kind_for_session() { return state.active().kind; }
	int& last_selected_output_type_for_session() { return state.active().output_type; }
	int& last_selected_patch_source_for_session() { return state.active().source; }
	int& last_selected_param_shortcut_x_for_session() { return state.active().x; }
	int& last_selected_param_shortcut_y_for_session() { return state.active().y; }
	int& last_selected_param_array_position_for_session() { return state.active().position; }
};
struct output {
	int type = 3;
};
struct Clip : selectable {
	struct output* output = nullptr;
};
session::State<Clip*> clips;
Clip* getCurrentClip() {
	return clips.active();
}
selectable* currentSong = nullptr;
enum class ActionResult { DEALT_WITH, NOT_DEALT_WITH };
struct RootUI {};
struct AutomationView : RootUI {
	void reset_navigation_for_session_startup();
	bool onMenuView = false;
	RootUI* previousUI = nullptr;
	int initialized = 0, opened = 0, led_updates = 0;
	void initializeView() { ++initialized; }
	void openedInBackground() { ++opened; }
	void resetInterpolationShortcutBlinking() {}
	void resetPadSelectionShortcutBlinking() {}
	void setKnobIndicatorLevels() {}
	void setModLedStates() { ++led_updates; }
	void buttonAction(int, bool, bool) {}
	int modPos = 0, updates = 0;
	bool onArrangerView = false;
	AutomationParamType automationParamType = AutomationParamType::NONE;
	int getAutomationParameterKnobPos(ModelStackWithAutoParam*, int) { return 0; }
	void setAutomationKnobIndicatorLevels(ModelStackWithAutoParam*, int, int) { ++updates; }
	void getLastSelectedParamShortcut(Clip*) {}
	void getLastSelectedParamArrayPosition(Clip*) {}
};
session::State<AutomationView> panels;
AutomationView& automation_view_for_session() {
	return panels.active();
}
AutomationView& view_for_session() {
	return panels.active();
}
session::State<RootUI*> roots;
session::State<int> renders, greyouts;
bool clip_minder = true;
bool rootUIIsClipMinderScreen() {
	return clip_minder;
}
RootUI* getRootUI() {
	return roots.active();
}
void swapOutRootUILowLevel(RootUI* root) {
	roots.active() = root;
}
void uiNeedsRendering(RootUI*) {
	++renders.active();
}
namespace PadLEDs {
void reassessGreyout() {
	++greyouts.active();
}
} // namespace PadLEDs
struct Automation {
	ModelStackWithAutoParam* result = nullptr;
	ModelStackWithAutoParam* getModelStackWithParam(void*) { return result; }
	int getPatchSource() { return 7; }
	bool select_automation_view_parameter(bool);
	bool restore_previous_view();
	ActionResult buttonAction(deluge::hid::Button, bool, bool);
};
#include "automation_navigation_reset.inc"
#include "automation_selection_context.inc"
TEST_GROUP(AutomationSelectionContext){void setup() override{panels = {};
clips = {};
currentSong = nullptr;
roots = {};
renders = {};
greyouts = {};
clip_minder = true;
} // namespace automation_selection_context_test
}
;
TEST(AutomationSelectionContext, incomplete_contexts_leave_selection_and_indicators_untouched) {
	Automation menu;
	Clip clip;
	output instrument;
	collection parameters;
	ModelStackWithAutoParam stack;
	session::Scope remote(session::Id::Remote);
	clips.active() = &clip;
	clip.output = &instrument;
	menu.select_automation_view_parameter(true);
	menu.result = &stack;
	menu.select_automation_view_parameter(true);
	stack.autoParam = &stack;
	menu.select_automation_view_parameter(true);
	stack.paramCollection = &parameters;
	clip.output = nullptr;
	menu.select_automation_view_parameter(true);
	clips.active() = nullptr;
	menu.select_automation_view_parameter(true);
	menu.select_automation_view_parameter(false);
	LONGS_EQUAL(0, panels.active().updates);
	LONGS_EQUAL(-1, clip.state.active().id);
	CHECK(panels.active().automationParamType == AutomationParamType::NONE);
}
TEST(AutomationSelectionContext, valid_clip_and_song_selections_remain_session_owned) {
	Automation menu;
	Clip clip;
	output instrument;
	collection parameters;
	ModelStackWithAutoParam stack{&parameters, &parameters, 9};
	selectable song;
	menu.result = &stack;
	clip.output = &instrument;
	currentSong = &song;
	session::Scope remote(session::Id::Remote);
	clips.active() = &clip;
	menu.select_automation_view_parameter(true);
	LONGS_EQUAL(9, clip.state.active().id);
	LONGS_EQUAL(3, clip.state.active().output_type);
	LONGS_EQUAL(7, clip.state.active().source);
	LONGS_EQUAL(255, clip.state.active().x);
	LONGS_EQUAL(-1, clip.state.for_owner(session::Id::Local).id);
	menu.select_automation_view_parameter(false);
	LONGS_EQUAL(9, song.state.active().id);
	LONGS_EQUAL(-1, song.state.for_owner(session::Id::Local).id);
	LONGS_EQUAL(2, panels.active().updates);
	LONGS_EQUAL(0, panels.for_owner(session::Id::Local).updates);
	CHECK(panels.active().onArrangerView);
}
TEST(AutomationSelectionContext, rejected_entry_preserves_root_and_view_state) {
	Automation menu;
	RootUI original;
	session::Scope remote(session::Id::Remote);
	roots.active() = &original;
	menu.buttonAction(deluge::hid::button::CLIP_VIEW, true, false);
	CHECK(roots.active() == &original);
	CHECK(!panels.active().onMenuView);
	CHECK(panels.active().previousUI == nullptr);
	LONGS_EQUAL(0, panels.active().initialized);
	LONGS_EQUAL(0, panels.active().led_updates);
	LONGS_EQUAL(0, greyouts.active());
	roots.active() = &panels.active();
	menu.buttonAction(deluge::hid::button::SELECT_ENC, true, false);
	LONGS_EQUAL(0, renders.active());
	LONGS_EQUAL(0, greyouts.active());
	clip_minder = false;
	CHECK(menu.buttonAction(deluge::hid::button::CLIP_VIEW, true, false) == ActionResult::NOT_DEALT_WITH);
}
TEST(AutomationSelectionContext, successful_entry_and_exit_only_switch_active_root) {
	Automation menu;
	RootUI local_root, remote_root;
	roots.for_owner(session::Id::Local) = &local_root;
	roots.for_owner(session::Id::Remote) = &remote_root;
	Clip clip;
	output instrument;
	clip.output = &instrument;
	collection parameters;
	ModelStackWithAutoParam stack{&parameters, &parameters, 9};
	menu.result = &stack;
	session::Scope remote(session::Id::Remote);
	clips.active() = &clip;
	menu.buttonAction(deluge::hid::button::CLIP_VIEW, true, false);
	CHECK(roots.active() == &panels.active());
	CHECK(panels.active().onMenuView);
	CHECK(panels.active().previousUI == &remote_root);
	LONGS_EQUAL(1, panels.active().initialized);
	LONGS_EQUAL(1, panels.active().opened);
	CHECK(roots.for_owner(session::Id::Local) == &local_root);
	menu.buttonAction(deluge::hid::button::BACK, true, false);
	CHECK(roots.active() == &remote_root);
	CHECK(!panels.active().onMenuView);
	LONGS_EQUAL(1, renders.active());
	CHECK(panels.active().previousUI == nullptr);
}
TEST(AutomationSelectionContext, invalid_return_destinations_preserve_active_root) {
	Automation menu;
	session::Scope remote(session::Id::Remote);
	roots.active() = &panels.active();
	panels.active().onMenuView = true;
	for (auto* target : {static_cast<RootUI*>(nullptr), static_cast<RootUI*>(&panels.active())}) {
		panels.active().previousUI = target;
		for (int button :
		     {deluge::hid::button::CLIP_VIEW, deluge::hid::button::BACK, deluge::hid::button::SELECT_ENC}) {
			menu.buttonAction(button, true, false);
			CHECK(roots.active() == &panels.active());
			CHECK(panels.active().onMenuView);
			CHECK(panels.active().previousUI == target);
		}
	}
	LONGS_EQUAL(0, renders.active());
	LONGS_EQUAL(0, panels.active().led_updates);
	LONGS_EQUAL(0, greyouts.active());
}
TEST(AutomationSelectionContext, clip_button_return_consumes_only_remote_destination) {
	Automation menu;
	RootUI remote_return, local_return;
	panels.for_owner(session::Id::Local).previousUI = &local_return;
	panels.for_owner(session::Id::Local).onMenuView = true;
	session::Scope remote(session::Id::Remote);
	roots.active() = &panels.active();
	panels.active().previousUI = &remote_return;
	panels.active().onMenuView = true;
	menu.buttonAction(deluge::hid::button::CLIP_VIEW, true, false);
	CHECK(roots.active() == &remote_return);
	CHECK(panels.active().previousUI == nullptr);
	CHECK(!panels.active().onMenuView);
	CHECK(panels.for_owner(session::Id::Local).previousUI == &local_return);
	CHECK(panels.for_owner(session::Id::Local).onMenuView);
	LONGS_EQUAL(1, renders.active());
}
TEST(AutomationSelectionContext, startup_clears_only_active_navigation_state) {
	RootUI previous;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		auto& view = panels.for_owner(owner);
		view.previousUI = &previous;
		view.onMenuView = view.onArrangerView = true;
	}
	session::Scope remote(session::Id::Remote);
	panels.active().reset_navigation_for_session_startup();
	panels.active().reset_navigation_for_session_startup();
	CHECK(panels.active().previousUI == nullptr);
	CHECK(!panels.active().onMenuView);
	CHECK(!panels.active().onArrangerView);
	CHECK(panels.for_owner(session::Id::Local).previousUI == &previous);
	CHECK(panels.for_owner(session::Id::Local).onMenuView);
	CHECK(panels.for_owner(session::Id::Local).onArrangerView);
}
TEST(AutomationSelectionContext, clip_selection_clears_retained_arranger_mode_only_after_validation) {
	Automation menu;
	Clip clip;
	output instrument;
	clip.output = &instrument;
	collection parameters;
	ModelStackWithAutoParam stack{&parameters, &parameters, 9};
	panels.for_owner(session::Id::Local).onArrangerView = true;
	session::Scope remote(session::Id::Remote);
	panels.active().onArrangerView = true;
	CHECK(!menu.select_automation_view_parameter(true));
	CHECK(panels.active().onArrangerView);
	menu.result = &stack;
	clips.active() = &clip;
	CHECK(menu.select_automation_view_parameter(true));
	CHECK(!panels.active().onArrangerView);
	CHECK(panels.for_owner(session::Id::Local).onArrangerView);
}
} // namespace automation_selection_context_test
