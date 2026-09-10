#include "CppUTest/TestHarness.h"
#include "gui/menu_item/value_scaling.h"
#include "gui/ui/ui_session.h"
#include <climits>
namespace parameter_read_routing_test {
namespace session = deluge::gui::ui_session;
enum class ParamManagerType { SOUND, GLOBAL, MIDI };
struct parameter {
	int32_t value = 0;
	int reads = 0;
	bool has_current_value(int id) { return id == 0; }
	int32_t getValue(int) {
		++reads;
		return value;
	}
};
struct manager {
	parameter* param = nullptr;
	ParamManagerType type = ParamManagerType::SOUND;
	bool matches_type(ParamManagerType expected) { return param && type == expected; }
	parameter* getPatchedParamSet() { return param; }
	parameter* getUnpatchedParamSet() { return param; }
};
struct editor {
	manager* currentParamManager = nullptr;
};
session::State<editor> editors;
editor& sound_editor_for_session() {
	return editors.active();
}
struct reader {
	session::State<int32_t> values;
	int id = 0;
	int getP() { return id; }
	void setValue(int32_t value) { values.active() = value; }
};
struct Integer : reader {
	void readCurrentValue();
};
struct UnpatchedParam : reader {
	void readCurrentValue();
};
namespace patched {
struct Pan : reader {
	void readCurrentValue();
};
#include "patched_pan_read.inc"
} // namespace patched
namespace unpatched {
struct Pan : reader {
	void readCurrentValue();
};
#include "unpatched_pan_read.inc"
} // namespace unpatched
#include "patched_integer_read.inc"
#include "unpatched_standard_read.inc"
TEST_GROUP(ParameterReadRouting){};
template <typename menu_type>
void check_rejected_reads() {
	menu_type menu;
	editors = {};
	parameter local_parameter{INT32_MAX};
	manager local_stack{&local_parameter}, empty;
	editors.for_owner(session::Id::Local).currentParamManager = &local_stack;
	menu.values.for_owner(session::Id::Local) = 11;
	menu.values.for_owner(session::Id::Remote) = 17;
	{
		session::Scope remote(session::Id::Remote);
		menu.readCurrentValue();
		LONGS_EQUAL(17, menu.values.active());
		editors.active().currentParamManager = &empty;
		menu.readCurrentValue();
		LONGS_EQUAL(17, menu.values.active());
		editors.active().currentParamManager = &local_stack;
		menu.id = -1;
		menu.readCurrentValue();
		LONGS_EQUAL(17, menu.values.active());
		menu.id = 0;
		local_stack.type = ParamManagerType::MIDI;
		menu.readCurrentValue();
		LONGS_EQUAL(17, menu.values.active());
	}
	LONGS_EQUAL(0, local_parameter.reads);
	LONGS_EQUAL(11, menu.values.for_owner(session::Id::Local));
}
template <typename menu_type>
void check_scaling(int32_t (*scale)(int32_t)) {
	menu_type menu;
	editors = {};
	parameter local_parameter{INT32_MAX}, remote_parameter{INT32_MIN};
	manager local_stack{&local_parameter}, remote_stack{&remote_parameter};
	editors.for_owner(session::Id::Local).currentParamManager = &local_stack;
	editors.for_owner(session::Id::Remote).currentParamManager = &remote_stack;
	{
		session::Scope local(session::Id::Local);
		menu.readCurrentValue();
		LONGS_EQUAL(scale(INT32_MAX), menu.values.active());
	}
	{
		session::Scope remote(session::Id::Remote);
		menu.readCurrentValue();
		LONGS_EQUAL(scale(INT32_MIN), menu.values.active());
		remote_parameter.value = 0;
		menu.readCurrentValue();
		LONGS_EQUAL(scale(0), menu.values.active());
	}
	LONGS_EQUAL(scale(INT32_MAX), menu.values.for_owner(session::Id::Local));
	LONGS_EQUAL(1, local_parameter.reads);
	LONGS_EQUAL(2, remote_parameter.reads);
}
TEST(ParameterReadRouting, all_readers_preserve_values_when_lookup_is_rejected) {
	check_rejected_reads<Integer>();
	check_rejected_reads<UnpatchedParam>();
	check_rejected_reads<patched::Pan>();
	check_rejected_reads<unpatched::Pan>();
}
TEST(ParameterReadRouting, successful_reads_keep_scaling_and_session_ownership) {
	check_scaling<Integer>(computeCurrentValueForStandardMenuItem);
	check_scaling<UnpatchedParam>(computeCurrentValueForStandardMenuItem);
	check_scaling<patched::Pan>(computeCurrentValueForPan);
	check_scaling<unpatched::Pan>(computeCurrentValueForPan);
}
TEST(ParameterReadRouting, global_manager_supports_unpatched_but_rejects_patched_reads) {
	parameter value{INT32_MAX};
	manager global_manager{&value, ParamManagerType::GLOBAL};
	session::Scope remote(session::Id::Remote);
	editors.active().currentParamManager = &global_manager;
	UnpatchedParam standard;
	unpatched::Pan pan;
	standard.readCurrentValue();
	pan.readCurrentValue();
	LONGS_EQUAL(50, standard.values.active());
	LONGS_EQUAL(25, pan.values.active());
	Integer patched_standard;
	patched::Pan patched_pan;
	patched_standard.values.active() = 13;
	patched_pan.values.active() = 7;
	patched_standard.readCurrentValue();
	patched_pan.readCurrentValue();
	LONGS_EQUAL(13, patched_standard.values.active());
	LONGS_EQUAL(7, patched_pan.values.active());
	LONGS_EQUAL(2, value.reads);
}
} // namespace parameter_read_routing_test
