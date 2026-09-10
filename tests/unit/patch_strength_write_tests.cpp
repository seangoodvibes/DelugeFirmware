#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
namespace patch_strength_write_test {
namespace session = deluge::gui::ui_session;
constexpr int MODEL_STACK_MAX_SIZE = 128, kMaxMenuPatchCableValue = 5000;
namespace modulation::params {
enum class Kind { PATCH_CABLE };
}
struct ModelStackWithAutoParam;
struct parameter {
	int writes = 0;
	int32_t value = 0;
	void setCurrentValueInResponseToUserInput(int32_t next, ModelStackWithAutoParam*) {
		++writes;
		value = next;
	}
};
struct collection {
	auto getParamKind() { return modulation::params::Kind::PATCH_CABLE; }
};
struct ModelStackWithAutoParam {
	parameter* autoParam = nullptr;
	int paramId = 42;
	collection* paramCollection = nullptr;
};
struct view {
	int refreshes = 0, refreshed_id = -1;
	void possiblyRefreshAutomationEditorGrid(void*, modulation::params::Kind, int id) {
		++refreshes;
		refreshed_id = id;
	}
};
session::State<view> views;
view& automation_view_for_session() {
	return views.active();
}
void* getRootUI() {
	return &views.active();
}
void* getCurrentClip() {
	return nullptr;
}
struct PatchCableStrength {
	session::State<ModelStackWithAutoParam*> results;
	bool creation_requested = false;
	ModelStackWithAutoParam* getModelStack(void*, bool create) {
		creation_requested = create;
		return results.active();
	}
	int getValue() { return 2500; }
	void writeCurrentValue();
};
#include "patch_strength_write.inc"
TEST_GROUP(PatchStrengthWrite){void setup() override{views = {};
} // namespace patch_strength_write_test
}
;
TEST(PatchStrengthWrite, missing_stack_or_parameter_does_not_write_or_refresh) {
	PatchCableStrength menu;
	ModelStackWithAutoParam empty;
	session::Scope remote(session::Id::Remote);
	menu.writeCurrentValue();
	CHECK(menu.creation_requested);
	LONGS_EQUAL(0, views.active().refreshes);
	menu.results.active() = &empty;
	menu.writeCurrentValue();
	LONGS_EQUAL(0, views.active().refreshes);
}
TEST(PatchStrengthWrite, rejected_remote_edit_preserves_valid_local_edit) {
	PatchCableStrength menu;
	parameter param;
	collection parameters;
	ModelStackWithAutoParam stack{&param, 42, &parameters};
	menu.results.for_owner(session::Id::Local) = &stack;
	{
		session::Scope remote(session::Id::Remote);
		menu.writeCurrentValue();
		LONGS_EQUAL(0, param.writes);
	}
	session::Scope local(session::Id::Local);
	menu.writeCurrentValue();
	LONGS_EQUAL(1, param.writes);
	CHECK(param.value >= (1 << 29) - 1 && param.value <= (1 << 29));
	LONGS_EQUAL(1, views.active().refreshes);
	LONGS_EQUAL(42, views.active().refreshed_id);
	LONGS_EQUAL(0, views.for_owner(session::Id::Remote).refreshes);
}
} // namespace patch_strength_write_test
