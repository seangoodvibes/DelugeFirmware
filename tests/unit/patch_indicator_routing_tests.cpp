#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
namespace patch_indicator_routing_test {
namespace session = deluge::gui::ui_session;
enum class ParamManagerType { SOUND, GLOBAL };
using PatchSource = int;
struct ParamDescriptor {
	int id = -1;
	void setToHaveParamOnly(int value) { id = value; }
};
struct cable_set {
	int queries = 0, destination = 2, source = 7;
	bool isAnySourcePatchedToParamVolumeInspecific(ParamDescriptor descriptor) {
		++queries;
		return descriptor.id == destination;
	}
	bool isSourcePatchedToDestinationDescriptorVolumeInspecific(PatchSource value, ParamDescriptor descriptor) {
		++queries;
		return descriptor.id == destination && value == source;
	}
};
struct param_set {
	bool has_current_value(int id) { return id >= 0 && id < 8; }
};
struct manager {
	ParamManagerType type = ParamManagerType::SOUND;
	param_set params;
	cable_set cables;
	int cable_accesses = 0;
	bool matches_type(ParamManagerType expected) { return type == expected; }
	param_set* getPatchedParamSet() { return &params; }
	cable_set* getPatchCableSet() {
		++cable_accesses;
		return &cables;
	}
};
struct editor {
	manager* currentParamManager = nullptr;
};
session::State<editor> editors;
editor& sound_editor_for_session() {
	return editors.active();
}
struct PatchedParam {
	int id = 2;
	int getP() { return id; }
	uint8_t shouldDrawDotOnName();
	uint8_t shouldBlinkPatchingSourceShortcut(PatchSource, uint8_t*);
};
#include "patch_indicator_routing.inc"
TEST_GROUP(PatchIndicatorRouting){void setup() override{editors = {};
} // namespace patch_indicator_routing_test
}
;
TEST(PatchIndicatorRouting, rejected_contexts_never_access_patch_cables) {
	PatchedParam menu;
	manager invalid_manager;
	uint8_t colour = 91;
	session::Scope remote(session::Id::Remote);
	LONGS_EQUAL(255, menu.shouldDrawDotOnName());
	LONGS_EQUAL(255, menu.shouldBlinkPatchingSourceShortcut(7, &colour));
	editors.active().currentParamManager = &invalid_manager;
	invalid_manager.type = ParamManagerType::GLOBAL;
	LONGS_EQUAL(255, menu.shouldDrawDotOnName());
	LONGS_EQUAL(255, menu.shouldBlinkPatchingSourceShortcut(7, &colour));
	invalid_manager.type = ParamManagerType::SOUND;
	for (int id : {-1, 8, 255}) {
		menu.id = id;
		LONGS_EQUAL(255, menu.shouldDrawDotOnName());
		LONGS_EQUAL(255, menu.shouldBlinkPatchingSourceShortcut(7, &colour));
	}
	LONGS_EQUAL(0, invalid_manager.cable_accesses);
	LONGS_EQUAL(91, colour);
}
TEST(PatchIndicatorRouting, indicators_follow_active_sessions_cable_set) {
	PatchedParam menu;
	manager local_manager, remote_manager;
	remote_manager.cables.destination = 3;
	editors.for_owner(session::Id::Local).currentParamManager = &local_manager;
	editors.for_owner(session::Id::Remote).currentParamManager = &remote_manager;
	uint8_t colour = 91;
	{
		session::Scope remote(session::Id::Remote);
		LONGS_EQUAL(255, menu.shouldDrawDotOnName());
		LONGS_EQUAL(255, menu.shouldBlinkPatchingSourceShortcut(7, &colour));
	}
	session::Scope local(session::Id::Local);
	LONGS_EQUAL(3, menu.shouldDrawDotOnName());
	LONGS_EQUAL(3, menu.shouldBlinkPatchingSourceShortcut(7, &colour));
	LONGS_EQUAL(255, menu.shouldBlinkPatchingSourceShortcut(6, &colour));
	LONGS_EQUAL(3, local_manager.cables.queries);
	LONGS_EQUAL(2, remote_manager.cables.queries);
	LONGS_EQUAL(91, colour);
}
} // namespace patch_indicator_routing_test
