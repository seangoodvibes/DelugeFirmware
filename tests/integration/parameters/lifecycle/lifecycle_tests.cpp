#include "CppUTest/TestHarness.h"
#include "gui/views/automation/editor_layout/mod_controllable/parameter_edit.h"
#include "gui/views/view.h"
#include "memory/general_memory_allocator.h"
#include "model/consequence/consequence_param_change.h"
#include "model/mod_controllable/mod_controllable.h"
#include "model/timeline_counter.h"
#include "modulation/automation/auto_param_pool.h"
#include "modulation/automation/copied_param_automation.h"
#include "modulation/midi/midi_param.h"
#include "modulation/midi/midi_param_collection.h"
#include "modulation/midi/midi_param_move.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_node.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_pool.h"
#include "modulation/patch/patch_cable_set.h"
#include "platform.h"
#include "playback/playback_handler.h"
#include "storage/cluster/cluster.h"
#include "storage/storage_manager.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {
struct fixture {
	ParamManagerForTimeline manager;
	alignas(ModelStackWithAutoParam) char stack_memory[MODEL_STACK_MAX_SIZE]{};
	fixture() { CHECK(manager.setupUnpatched() == Error::NONE); }
	UnpatchedParamSet& set() { return *manager.getUnpatchedParamSet(); }
	ParamCollectionSummary& summary() { return *manager.getUnpatchedParamSetSummary(); }
	ModelStackWithParamCollection* stack() {
		return setupModelStackWithSong(stack_memory, nullptr)
		    ->addTimelineCounter(nullptr)
		    ->addOtherTwoThingsButNoNoteRow(nullptr, &manager)
		    ->addParamCollection(&set(), &summary());
	}
	ModelStackWithAutoParam* param(int32_t id) { return set().getAutoParamFromId(stack()->addParamId(id), true); }
	void add_node(int32_t id, int32_t pos, int32_t value, bool interpolated = false) {
		auto* context = param(id);
		bool was_automated = set().isAutomated(id);
		CHECK(context->autoParam->setNodeAtPos(pos, value, interpolated) >= 0);
		set().notifyParamModifiedInSomeWay(context, set().getValue(id), true, was_automated, true);
	}
};

void check_flag(ParamCollectionSummary& summary, int32_t id, bool automated, bool interpolating = false) {
	auto mask = uint32_t{1} << (id & 31);
	CHECK_EQUAL(automated, bool(summary.whichParamsAreAutomated[id >> 5] & mask));
	CHECK_EQUAL(interpolating, bool(summary.whichParamsAreInterpolating[id >> 5] & mask));
}
void check_node(AutoParam& param, int32_t index, int32_t pos, int32_t value, bool interpolated) {
	auto* node = param.nodes.getElement(index);
	CHECK(node);
	LONGS_EQUAL(pos, node->pos);
	LONGS_EQUAL(value, node->value);
	CHECK_EQUAL(interpolated, bool(node->interpolated));
}

template <class Set>
void check_initial_values() {
	ParamCollectionSummary summary{};
	Set set(&summary);
	for (int32_t id = 0; id < set.getNumParams(); ++id) {
		LONGS_EQUAL(0, set.getValue(id));
		CHECK_FALSE(set.isAutomated(id));
		CHECK_FALSE(set.containsSomething(id));
	}
	for (int32_t id : {0, set.getNumParams() - 1}) {
		set.setCurrentValueBasicForSetup(id, INT32_MIN);
		LONGS_EQUAL(INT32_MIN, set.getValue(id));
		CHECK_FALSE(set.containsSomething(id, uint32_t{1} << 31));
		set.setCurrentValueBasicForSetup(id, INT32_MAX);
		CHECK(set.containsSomething(id));
		LONGS_EQUAL(INT32_MAX, set.getValue(id));
	}
	for (int32_t id = 1; id < set.getNumParams() - 1; ++id)
		LONGS_EQUAL(0, set.getValue(id));
	CHECK_FALSE(summary.containsAutomation());
}
} // namespace

// clang-format off
TEST_GROUP(parameter_lifecycle) {
    void setup() override {
        patch_cable_pool::get().clear_unused();
        auto_param_pool::get().clear_unused();
        parameter_test::reset();
        LONGS_EQUAL(0, parameter_test::outstanding_allocations());
    }
    void teardown() override {
        LONGS_EQUAL(0, patch_cable_pool::get().active_count());
        LONGS_EQUAL(0, auto_param_pool::get().active_count());
        patch_cable_pool::get().clear_unused();
        auto_param_pool::get().clear_unused();
        LONGS_EQUAL(0, parameter_test::outstanding_allocations());
    }
};
// clang-format on

TEST(parameter_lifecycle, all_parameter_sets_start_with_independent_neutral_scalars) {
	check_initial_values<UnpatchedParamSet>();
	check_initial_values<PatchedParamSet>();
	check_initial_values<ExpressionParamSet>();
}

TEST(parameter_lifecycle, scalar_edits_notify_only_when_value_changes) {
	fixture f;
	for (int32_t id : {0, 31, 32, f.set().getNumParams() - 1}) {
		auto* context = f.param(id);
		size_t before = parameter_test::notifications;
		context->autoParam->setCurrentValueWithNoReversionOrRecording(context, 123);
		LONGS_EQUAL(123, f.set().getValue(id));
		LONGS_EQUAL(before + 1, parameter_test::notifications);
		context = f.param(id);
		context->autoParam->setCurrentValueWithNoReversionOrRecording(context, 123);
		LONGS_EQUAL(before + 1, parameter_test::notifications);
		CHECK_FALSE(f.set().isAutomated(id));
		check_flag(f.summary(), id, false);
	}
}

TEST(parameter_lifecycle, scalar_consequence_repeatedly_undoes_and_redoes_without_automation) {
	fixture f;
	f.set().setCurrentValueBasicForSetup(32, 17);
	ConsequenceParamChange undo(f.param(32), false);
	auto* context = f.param(32);
	context->autoParam->setCurrentValueWithNoReversionOrRecording(context, -42);
	for (int i = 0; i < 3; ++i) {
		CHECK(undo.revert(TimeType::BEFORE, nullptr) == Error::NONE);
		LONGS_EQUAL(17, f.set().getValue(32));
		check_flag(f.summary(), 32, false);
		CHECK(undo.revert(TimeType::AFTER, nullptr) == Error::NONE);
		LONGS_EQUAL(-42, f.set().getValue(32));
		check_flag(f.summary(), 32, false);
	}
	LONGS_EQUAL(7, parameter_test::notifications);
}

TEST(parameter_lifecycle, first_node_creation_undo_redo_preserves_scalar_and_neighbor_flags) {
	fixture f;
	f.add_node(31, 0, 99);
	f.set().setCurrentValueBasicForSetup(32, 17);
	ConsequenceParamChange undo(f.param(32), false);
	f.add_node(32, 4, 800, true);
	for (int i = 0; i < 3; ++i) {
		CHECK(undo.revert(TimeType::BEFORE, nullptr) == Error::NONE);
		LONGS_EQUAL(17, f.set().getValue(32));
		CHECK_FALSE(f.set().isAutomated(32));
		check_flag(f.summary(), 32, false);
		check_flag(f.summary(), 31, true);
		CHECK(undo.revert(TimeType::AFTER, nullptr) == Error::NONE);
		LONGS_EQUAL(17, f.set().getValue(32));
		check_flag(f.summary(), 32, true);
		check_node(*f.param(32)->autoParam, 0, 4, 800, true);
	}
}

TEST(parameter_lifecycle, last_node_deletion_undo_redo_restores_nodes_and_clears_interpolation) {
	fixture f;
	f.set().setCurrentValueBasicForSetup(31, -123);
	f.add_node(31, 4, 800, true);
	f.add_node(32, 8, 900);
	ConsequenceParamChange undo(f.param(31), false);
	f.summary().whichParamsAreInterpolating[0] = uint32_t{1} << 31;
	f.param(31)->autoParam->valueIncrementPerHalfTick = 16;
	auto* context = f.param(31);
	context->autoParam->deleteAutomation(nullptr, context);
	LONGS_EQUAL(-123, f.set().getValue(31));
	CHECK_FALSE(f.set().isAutomated(31));
	check_flag(f.summary(), 31, false);
	for (int i = 0; i < 3; ++i) {
		CHECK(undo.revert(TimeType::BEFORE, nullptr) == Error::NONE);
		check_node(*f.param(31)->autoParam, 0, 4, 800, true);
		check_flag(f.summary(), 31, true);
		CHECK(undo.revert(TimeType::AFTER, nullptr) == Error::NONE);
		check_flag(f.summary(), 31, false);
		CHECK_FALSE(f.set().isAutomated(31));
		LONGS_EQUAL(-123, f.set().getValue(31));
		check_flag(f.summary(), 32, true);
	}
}

TEST(parameter_lifecycle, manager_clone_survives_source_destruction_with_and_without_automation) {
	for (bool copy_automation : {false, true}) {
		ParamManagerForTimeline clone;
		{
			fixture source;
			source.set().setCurrentValueBasicForSetup(32, 17);
			source.add_node(32, 0, 100, true);
			source.add_node(32, 16, 500, true);
			source.summary().whichParamsAreInterpolating[1] = 1;
			source.param(32)->autoParam->valueIncrementPerHalfTick = 16;
			auto* expression = source.manager.getOrCreateExpressionParamSet();
			CHECK(expression);
			expression->setCurrentValueBasicForSetup(2, 61);
			CHECK(expression->getParam(2)->setNodeAtPos(8, 72, false) >= 0);
			expression->paramHasAutomationNow(source.manager.getExpressionParamSetSummary(), 2);
			CHECK(clone.cloneParamCollectionsFrom(&source.manager, copy_automation, true) == Error::NONE);
			source.set().setCurrentValueBasicForSetup(32, 99);
			source.param(32)->autoParam->setNodeAtPos(0, -99, false);
			expression->setCurrentValueBasicForSetup(2, 0);
		}
		auto* set = clone.getUnpatchedParamSet();
		LONGS_EQUAL(17, set->getValue(32));
		CHECK_EQUAL(copy_automation, set->isAutomated(32));
		check_flag(*clone.getUnpatchedParamSetSummary(), 32, copy_automation, copy_automation);
		LONGS_EQUAL(61, clone.getExpressionParamSet()->getValue(2));
		CHECK_EQUAL(copy_automation, clone.getExpressionParamSet()->isAutomated(2));
		check_flag(*clone.getExpressionParamSetSummary(), 2, copy_automation);
		if (copy_automation) {
			check_node(*set->getParam(32), 0, 0, 100, true);
			check_node(*set->getParam(32), 1, 16, 500, true);
			check_node(*clone.getExpressionParamSet()->getParam(2), 0, 8, 72, false);
		}
		set->setCurrentValueBasicForSetup(32, 123);
		LONGS_EQUAL(123, set->getValue(32));
	}
}

TEST(parameter_lifecycle, clear_all_automation_preserves_scalars_and_allows_new_automation) {
	fixture f;
	for (int32_t id : {0, 31, 32, f.set().getNumParams() - 1}) {
		f.set().setCurrentValueBasicForSetup(id, id + 100);
		f.add_node(id, 4, 800);
	}
	f.set().deleteAllAutomation(nullptr, f.stack());
	for (int32_t id = 0; id < f.set().getNumParams(); ++id) {
		CHECK_FALSE(f.set().isAutomated(id));
		check_flag(f.summary(), id, false);
	}
	for (int32_t id : {0, 31, 32, f.set().getNumParams() - 1}) {
		LONGS_EQUAL(id + 100, f.set().getValue(id));
		f.add_node(id, 12, -800);
		check_node(*f.param(id)->autoParam, 0, 12, -800, false);
	}
}

TEST(parameter_lifecycle, seeking_and_tick_interpolation_update_owner_scalar_and_stop_clears_state) {
	fixture f;
	f.set().setCurrentValueBasicForSetup(31, 99);
	f.add_node(32, 0, 0, true);
	f.add_node(32, 16, 1600, true);
	f.set().setPlayPos(4, f.stack(), false);
	LONGS_EQUAL(400, f.set().getValue(32));
	check_flag(f.summary(), 32, true, true);
	f.set().tickTicks(2, f.stack());
	LONGS_EQUAL(600, f.set().getValue(32));
	LONGS_EQUAL(99, f.set().getValue(31));
	f.set().playbackHasEnded(f.stack());
	check_flag(f.summary(), 32, true, false);
	f.set().tickTicks(2, f.stack());
	LONGS_EQUAL(600, f.set().getValue(32));
}

TEST(parameter_lifecycle, user_input_records_old_scalar_and_real_consequence_restores_it) {
	fixture f;
	f.set().setCurrentValueBasicForSetup(32, 17);
	auto* context = f.param(32);
	context->autoParam->setCurrentValueInResponseToUserInput(-42, context);
	LONGS_EQUAL(-42, f.set().getValue(32));
	LONGS_EQUAL(1, parameter_test::notifications);
	auto undo = parameter_test::take_snapshot();
	CHECK(undo != nullptr);
	LONGS_EQUAL(17, undo->state.value);
	CHECK(undo->revert(TimeType::BEFORE, nullptr) == Error::NONE);
	LONGS_EQUAL(17, f.set().getValue(32));
	CHECK(undo->revert(TimeType::AFTER, nullptr) == Error::NONE);
	LONGS_EQUAL(-42, f.set().getValue(32));
	CHECK_FALSE(f.set().isAutomated(32));
	check_flag(f.summary(), 32, false);
}

TEST(parameter_lifecycle, deleting_time_removes_last_nodes_and_clears_only_target_flags) {
	fixture f;
	f.add_node(31, 0, 100);
	f.add_node(31, 31, 110);
	f.add_node(32, 8, 200);
	f.add_node(32, 12, 400);
	f.set().setCurrentValueBasicForSetup(32, 123);
	f.summary().whichParamsAreInterpolating[1] = 1;
	f.set().deleteTime(f.stack(), 8, 8);
	CHECK_FALSE(f.set().isAutomated(32));
	check_flag(f.summary(), 32, false);
	LONGS_EQUAL(123, f.set().getValue(32));
	CHECK(f.set().isAutomated(31));
	check_flag(f.summary(), 31, true);
	check_node(*f.param(31)->autoParam, 0, 0, 100, false);
	check_node(*f.param(31)->autoParam, 1, 23, 110, false);
}

TEST(parameter_lifecycle, reverse_seek_and_ticks_follow_the_same_envelope_backwards) {
	fixture f;
	f.add_node(32, 0, 0, true);
	f.add_node(32, 16, 1600, true);
	f.set().setPlayPos(12, f.stack(), true);
	LONGS_EQUAL(1200, f.set().getValue(32));
	check_flag(f.summary(), 32, true, true);
	f.set().tickTicks(2, f.stack());
	LONGS_EQUAL(1000, f.set().getValue(32));
}

TEST(parameter_lifecycle, reversed_manager_clone_owns_independent_reversed_nodes) {
	ParamManagerForTimeline clone;
	{
		fixture source;
		source.set().setCurrentValueBasicForSetup(32, 123);
		source.add_node(32, 0, 0, true);
		source.add_node(32, 8, 800, true);
		CHECK(clone.cloneParamCollectionsFrom(&source.manager, true, false, 32) == Error::NONE);
		source.param(32)->autoParam->setNodeAtPos(8, -99, false);
	}
	auto* set = clone.getUnpatchedParamSet();
	LONGS_EQUAL(123, set->getValue(32));
	check_node(*set->getParam(32), 0, 0, 0, true);
	check_node(*set->getParam(32), 1, 24, 800, true);
	check_flag(*clone.getUnpatchedParamSetSummary(), 32, true);
}

TEST(parameter_lifecycle, clone_from_keeps_destination_scalar_binding) {
	ParamCollectionSummary source_summary{}, destination_summary{};
	PatchedParamSet source(&source_summary), destination(&destination_summary);
	source.setCurrentValueBasicForSetup(32, 17);
	destination.setCurrentValueBasicForSetup(32, 99);
	CHECK(source.getParam(32)->setNodeAtPos(4, 400, true) >= 0);
	destination.getParam(32)->cloneFrom(source.getParam(32), true);
	LONGS_EQUAL(17, destination.getValue(32));
	destination.getParam(32)->setCurrentValueBasicForSetup(-42);
	LONGS_EQUAL(-42, destination.getValue(32));
	LONGS_EQUAL(17, source.getValue(32));
	source.getParam(32)->deleteAutomationBasicForSetup();
	check_node(*destination.getParam(32), 0, 4, 400, true);
}

TEST(parameter_lifecycle, stolen_undo_snapshot_owns_nodes_until_restored) {
	fixture f;
	f.set().setCurrentValueBasicForSetup(32, 17);
	f.add_node(32, 0, 100, true);
	f.add_node(32, 16, 500, true);
	{
		ConsequenceParamChange undo(f.param(32), true);
		// This mirrors Action's steal-data snapshot: its caller clears automation flags.
		f.set().paramHasNoAutomationNow(f.stack(), 32);
		CHECK_FALSE(f.set().isAutomated(32));
		LONGS_EQUAL(2, undo.state.nodes.getNumElements());
		f.set().setCurrentValueBasicForSetup(32, 99);
		CHECK(undo.revert(TimeType::BEFORE, nullptr) == Error::NONE);
		LONGS_EQUAL(17, f.set().getValue(32));
		check_flag(f.summary(), 32, true);
		check_node(*f.param(32)->autoParam, 1, 16, 500, true);
		LONGS_EQUAL(0, undo.state.nodes.getNumElements());
	}
	// Destroying the undo consequence must not destroy the now-restored nodes.
	check_node(*f.param(32)->autoParam, 0, 0, 100, true);
}

TEST(parameter_lifecycle, cloning_over_existing_manager_releases_old_nodes_and_retains_its_expression) {
	fixture source, destination;
	source.set().setCurrentValueBasicForSetup(32, 17);
	source.add_node(32, 4, 400, true);
	destination.set().setCurrentValueBasicForSetup(32, 99);
	destination.add_node(32, 8, 800);
	auto* expression = destination.manager.getOrCreateExpressionParamSet();
	CHECK(expression);
	expression->setCurrentValueBasicForSetup(2, 61);
	CHECK(expression->getParam(2)->setNodeAtPos(12, 1200, true) >= 0);
	expression->paramHasAutomationNow(destination.manager.getExpressionParamSetSummary(), 2);
	CHECK(destination.manager.cloneParamCollectionsFrom(&source.manager, true, true) == Error::NONE);
	LONGS_EQUAL(17, destination.set().getValue(32));
	check_node(*destination.param(32)->autoParam, 0, 4, 400, true);
	LONGS_EQUAL(61, destination.manager.getExpressionParamSet()->getValue(2));
	check_node(*destination.manager.getExpressionParamSet()->getParam(2), 0, 12, 1200, true);
	source.set().setCurrentValueBasicForSetup(32, -42);
	LONGS_EQUAL(17, destination.set().getValue(32));
}

TEST(parameter_lifecycle, patched_collection_clone_rebinds_owner_after_source_destruction) {
	for (bool copy_automation : {false, true}) {
		auto dispose = [](PatchedParamSet* set) {
			if (set) {
				set->~PatchedParamSet();
				delugeDealloc(set);
			}
		};
		std::unique_ptr<PatchedParamSet, decltype(dispose)> clone(nullptr, dispose);
		{
			ParamCollectionSummary source_summary{};
			PatchedParamSet source(&source_summary);
			source.setCurrentValueBasicForSetup(32, 17);
			CHECK(source.getParam(32)->setNodeAtPos(4, 400, true) >= 0);
			// Exercise the collection's real beenCloned hook using the same raw-copy
			// contract as ParamManager. No patch-cable or audio-rendering fixture is needed.
			void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(PatchedParamSet));
			CHECK(memory);
			std::memcpy(memory, static_cast<void const*>(&source), sizeof(PatchedParamSet));
			clone.reset(static_cast<PatchedParamSet*>(memory));
			clone->beenCloned(copy_automation, 0);
			source.setCurrentValueBasicForSetup(32, 99);
		}
		LONGS_EQUAL(17, clone->getValue(32));
		CHECK_EQUAL(copy_automation, clone->isAutomated(32));
		if (copy_automation) {
			check_node(*clone->getParam(32), 0, 4, 400, true);
		}
		clone->getParam(32)->setCurrentValueBasicForSetup(-42);
		LONGS_EQUAL(-42, clone->getValue(32));
	}
}

namespace native_parameter_tests {
extern std::string_view file_contents;
}
namespace {
struct fail_allocations {
	explicit fail_allocations(int after = 0, bool clear_cache = true) {
		if (clear_cache) {
			patch_cable_pool::get().clear_unused();
			auto_param_pool::get().clear_unused();
		}
		parameter_test::allocations_before_failure = after;
	}
	~fail_allocations() { parameter_test::allocations_before_failure = -1; }
};

template <class Writer, class Reader>
void check_real_persistence(bool json) {
	fixture source, destination;
	source.set().setCurrentValueBasicForSetup(32, 17);
	source.add_node(32, 0, 100, false);
	source.add_node(32, 16, 500, true);
	destination.add_node(31, 0, 1234);
	for (bool save_nodes : {true, false}) {
		Writer writer;
		writer.writeOpeningTagBeginning(json ? nullptr : "params");
		source.set().writeParamAsAttribute(writer, "value", 32, save_nodes, false);
		writer.writeAttribute("sentinel", 73);
		writer.closeTag();
		std::string encoded(writer.getBufferPtr(), writer.bytesWritten());
		for (bool load_nodes : {true, false}) {
			for (bool had_automation : {false, true}) {
				for (size_t padding = 0; padding < Cluster::size; ++padding) {
					destination.set().setCurrentValueBasicForSetup(32, -99);
					destination.set().deleteAutomationForParamBasicForSetup(destination.stack(), 32);
					if (had_automation)
						destination.add_node(32, 8, 888, true);
					destination.summary().whichParamsAreInterpolating[1] = 1;
					destination.param(32)->autoParam->valueIncrementPerHalfTick = 10;
					std::string document = std::string(padding, ' ') + encoded;
					native_parameter_tests::file_contents = document;
					Reader reader;
					if (json)
						CHECK(reader.match('{'));
					else
						STRCMP_EQUAL("params", reader.readNextTagOrAttributeName());
					STRCMP_EQUAL("value", reader.readNextTagOrAttributeName());
					destination.set().readParam(reader, &destination.summary(), 32, load_nodes ? 32 : 0);
					check_flag(destination.summary(), 31, true);
					check_node(*destination.param(31)->autoParam, 0, 0, 1234, false);
					LONGS_EQUAL(17, destination.set().getValue(32));
					check_flag(destination.summary(), 32, save_nodes && load_nodes);
					CHECK_EQUAL(save_nodes && load_nodes, destination.set().isAutomated(32));
					if (save_nodes && load_nodes) {
						LONGS_EQUAL(2, destination.param(32)->autoParam->nodes.getNumElements());
						check_node(*destination.param(32)->autoParam, 0, 0, 100, false);
						check_node(*destination.param(32)->autoParam, 1, 16, 500, true);
					}
					reader.exitTag("value");
					STRCMP_EQUAL("sentinel", reader.readNextTagOrAttributeName());
					LONGS_EQUAL(73, reader.readTagOrAttributeValueInt());
					auto* loaded = destination.set().getParam(32, false);
					CHECK_FALSE(loaded && loaded->hasInterpolationIncrement());
					if (!save_nodes || !load_nodes)
						POINTERS_EQUAL(nullptr, loaded);
					LONGS_EQUAL(17, source.set().getValue(32));
					check_node(*source.param(32)->autoParam, 1, 16, 500, true);
				}
			}
		}
	}
	native_parameter_tests::file_contents = {};
}
} // namespace
TEST(parameter_lifecycle, real_xml_save_reload_replaces_automation_and_preserves_source) {
	check_real_persistence<XMLSerializer, XMLDeserializer>(false);
}
TEST(parameter_lifecycle, real_json_save_reload_replaces_automation_and_preserves_source) {
	check_real_persistence<JsonSerializer, JsonDeserializer>(true);
}

TEST(parameter_lifecycle, failed_first_node_allocation_preserves_scalar_and_flags_then_retries) {
	fixture f;
	f.set().setCurrentValueBasicForSetup(32, 17);
	f.param(32); // Acquire the object before testing node allocation failure.
	size_t before = parameter_test::outstanding_allocations();
	{
		fail_allocations failure;
		LONGS_EQUAL(-1, f.param(32)->autoParam->setNodeAtPos(4, 400, true));
		LONGS_EQUAL(17, f.set().getValue(32));
		check_flag(f.summary(), 32, false);
		CHECK_FALSE(f.set().isAutomated(32));
		LONGS_EQUAL(before, parameter_test::outstanding_allocations());
	}
	f.add_node(32, 4, 400, true);
	check_node(*f.param(32)->autoParam, 0, 4, 400, true);
}

TEST(parameter_lifecycle, failed_collection_clone_keeps_source_destination_and_expression) {
	for (int fail_at : {0, 1}) {
		fixture source, destination;
		source.add_node(32, 4, 400, true);
		CHECK(source.manager.getOrCreateExpressionParamSet());
		destination.add_node(32, 8, 800, false);
		destination.set().setCurrentValueBasicForSetup(32, 99);
		size_t before = parameter_test::outstanding_allocations();
		{
			fail_allocations failure(fail_at);
			CHECK(destination.manager.cloneParamCollectionsFrom(&source.manager, true, true)
			      == Error::INSUFFICIENT_RAM);
		}
		LONGS_EQUAL(before, parameter_test::outstanding_allocations());
		LONGS_EQUAL(99, destination.set().getValue(32));
		check_node(*destination.param(32)->autoParam, 0, 8, 800, false);
		check_node(*source.param(32)->autoParam, 0, 4, 400, true);
		CHECK(source.manager.has_valid_layout());
		CHECK(destination.manager.has_valid_layout());
	}
}

TEST(parameter_lifecycle, failed_node_clone_preserves_source_and_clone_scalar_without_aliasing) {
	fixture source;
	source.set().setCurrentValueBasicForSetup(32, 17);
	source.add_node(32, 4, 400, true);
	ParamManagerForTimeline clone;
	{
		// Collection allocation succeeds; cloning its node storage fails.
		fail_allocations failure(1);
		CHECK(clone.cloneParamCollectionsFrom(&source.manager, true) == Error::NONE);
	}
	LONGS_EQUAL(17, clone.getUnpatchedParamSet()->getValue(32));
	CHECK_FALSE(clone.getUnpatchedParamSet()->isAutomated(32));
	check_flag(*clone.getUnpatchedParamSetSummary(), 32, false);
	check_node(*source.param(32)->autoParam, 0, 4, 400, true);
	clone.destructAndForgetParamCollections();
	check_node(*source.param(32)->autoParam, 0, 4, 400, true);
}

TEST(parameter_lifecycle, failed_undo_snapshot_refuses_undo_and_redo_without_changing_owner) {
	fixture f;
	f.set().setCurrentValueBasicForSetup(32, 17);
	f.add_node(32, 4, 400, true);
	f.add_node(31, 8, 900);
	std::unique_ptr<ConsequenceParamChange> undo;
	{
		fail_allocations failure;
		undo = std::make_unique<ConsequenceParamChange>(f.param(32), false);
		check_node(*f.param(32)->autoParam, 0, 4, 400, true);
	}
	// The edit proceeds, but its incomplete snapshot must never replace this state.
	f.set().setCurrentValueBasicForSetup(32, 27);
	f.param(32)->autoParam->setNodeAtPos(4, 500, false);
	f.add_node(32, 12, 1200, true);
	f.summary().whichParamsAreInterpolating[1] = 1;
	f.param(32)->autoParam->valueIncrementPerHalfTick = 16;
	const auto allocations = parameter_test::outstanding_allocations();
	const auto notifications = parameter_test::notifications;
	for (TimeType time : {TimeType::BEFORE, TimeType::AFTER, TimeType::BEFORE}) {
		CHECK(undo->revert(time, nullptr) == Error::INSUFFICIENT_RAM);
		LONGS_EQUAL(27, f.set().getValue(32));
		LONGS_EQUAL(2, f.param(32)->autoParam->nodes.getNumElements());
		check_node(*f.param(32)->autoParam, 0, 4, 500, false);
		check_node(*f.param(32)->autoParam, 1, 12, 1200, true);
		check_flag(f.summary(), 32, true, true);
		check_node(*f.param(31)->autoParam, 0, 8, 900, false);
		LONGS_EQUAL(notifications, parameter_test::notifications);
		LONGS_EQUAL(allocations, parameter_test::outstanding_allocations());
	}
	undo.reset();
	check_node(*f.param(32)->autoParam, 1, 12, 1200, true);
	// A fresh, complete snapshot works once memory is available.
	ConsequenceParamChange retry(f.param(32), false);
	f.set().setCurrentValueBasicForSetup(32, 37);
	CHECK(retry.revert(TimeType::BEFORE, nullptr) == Error::NONE);
	LONGS_EQUAL(27, f.set().getValue(32));
}

TEST(parameter_lifecycle, trimming_away_automation_round_trips_through_undo) {
	fixture f;
	f.set().setCurrentValueBasicForSetup(32, 17);
	f.add_node(32, 16, 400, true);
	f.add_node(32, 24, 800, false);
	f.add_node(31, 0, 100);
	f.add_node(31, 4, 200);
	ConsequenceParamChange undo(f.param(32), false);
	f.set().trimToLength(8, f.stack(), nullptr, false);
	CHECK_FALSE(f.set().isAutomated(32));
	check_flag(f.summary(), 32, false);
	LONGS_EQUAL(17, f.set().getValue(32));
	check_flag(f.summary(), 31, true);
	for (int i = 0; i < 3; ++i) {
		CHECK(undo.revert(TimeType::BEFORE, nullptr) == Error::NONE);
		check_node(*f.param(32)->autoParam, 0, 16, 400, true);
		check_node(*f.param(32)->autoParam, 1, 24, 800, false);
		CHECK(undo.revert(TimeType::AFTER, nullptr) == Error::NONE);
		CHECK_FALSE(f.set().isAutomated(32));
		check_flag(f.summary(), 32, false);
	}
}

TEST(parameter_lifecycle, deleting_last_nodes_in_region_resets_scalar_and_is_undoable) {
	fixture f;
	f.set().setCurrentValueBasicForSetup(32, 17);
	f.add_node(32, 8, 400, true);
	f.add_node(32, 12, 800, false);
	ConsequenceParamChange undo(f.param(32), false);
	parameter_test::allow_no_action = true;
	auto* context = f.param(32);
	context->autoParam->deleteNodesWithinRegion(context, 8, 8);
	CHECK_FALSE(f.set().isAutomated(32));
	check_flag(f.summary(), 32, false);
	// Region deletion intentionally clears the scalar for MPE safety.
	LONGS_EQUAL(0, f.set().getValue(32));
	CHECK(undo.revert(TimeType::BEFORE, nullptr) == Error::NONE);
	LONGS_EQUAL(17, f.set().getValue(32));
	check_node(*f.param(32)->autoParam, 1, 12, 800, false);
	CHECK(undo.revert(TimeType::AFTER, nullptr) == Error::NONE);
	CHECK_FALSE(f.set().isAutomated(32));
	LONGS_EQUAL(0, f.set().getValue(32));
}

TEST(parameter_lifecycle, wrapping_region_deletion_removes_both_ends_and_retains_middle_nodes) {
	fixture f;
	for (int pos : {0, 8, 16, 24})
		f.add_node(32, pos, pos * 100);
	parameter_test::allow_no_action = true;
	auto* context = f.param(32);
	context->autoParam->deleteNodesWithinRegion(context, 24, 16);
	LONGS_EQUAL(2, f.param(32)->autoParam->nodes.getNumElements());
	check_node(*f.param(32)->autoParam, 0, 8, 800, false);
	check_node(*f.param(32)->autoParam, 1, 16, 1600, false);
	check_flag(f.summary(), 32, true);
}

namespace {
void paste_replacement(fixture& f) {
	ParamNode nodes[2];
	nodes[0].pos = 0;
	nodes[0].value = -100;
	nodes[0].interpolated = false;
	nodes[1].pos = 16;
	nodes[1].value = -500;
	nodes[1].interpolated = true;
	CopiedParamAutomation copy;
	copy.width = 32;
	copy.numNodes = 2;
	copy.nodes = nodes;
	auto* context = f.param(32);
	context->autoParam->paste(0, 32, 1.0f, context, &copy, false);
}
} // namespace
TEST(parameter_lifecycle, paste_replaces_all_nodes_and_undo_redo_restores_both_versions) {
	fixture f;
	f.set().setCurrentValueBasicForSetup(32, 17);
	f.add_node(32, 4, 400, true);
	f.add_node(32, 24, 800, false);
	ConsequenceParamChange undo(f.param(32), false);
	paste_replacement(f);
	LONGS_EQUAL(17, f.set().getValue(32));
	for (int i = 0; i < 3; ++i) {
		check_node(*f.param(32)->autoParam, 0, 0, -100, false);
		check_node(*f.param(32)->autoParam, 1, 16, -500, true);
		CHECK(undo.revert(TimeType::BEFORE, nullptr) == Error::NONE);
		check_node(*f.param(32)->autoParam, 0, 4, 400, true);
		check_node(*f.param(32)->autoParam, 1, 24, 800, false);
		CHECK(undo.revert(TimeType::AFTER, nullptr) == Error::NONE);
		check_flag(f.summary(), 32, true);
	}
}
TEST(parameter_lifecycle, failed_paste_reports_removed_automation_without_losing_scalar) {
	fixture f;
	f.set().setCurrentValueBasicForSetup(32, 17);
	f.add_node(32, 4, 400, true);
	f.summary().whichParamsAreInterpolating[1] = 1;
	{
		fail_allocations failure;
		paste_replacement(f);
	}
	// Existing paste is not transactional: old nodes are removed before insertion.
	CHECK_FALSE(f.set().isAutomated(32));
	LONGS_EQUAL(17, f.set().getValue(32));
	check_flag(f.summary(), 32, false);
	paste_replacement(f);
	check_node(*f.param(32)->autoParam, 1, 16, -500, true);
	check_flag(f.summary(), 32, true);
}

namespace {
class notification_observer : public ModControllable {
public:
	ParamSet* set = nullptr;
	int comparisons = 0;
	int mono_events = 0;
	int32_t last_value = 0;
	int32_t last_dimension = -1;
	bool last_automated = false;
	bool forward_expression = false;
	ParamManagerType required_param_manager_type() const override { return ParamManagerType::ANY; }
	bool valueChangedEnoughToMatter(int32_t old_value, int32_t new_value, deluge::modulation::params::Kind,
	                                uint32_t id) override {
		++comparisons;
		LONGS_EQUAL(new_value, set->getValue(id));
		last_value = new_value;
		last_automated = set->isAutomated(id);
		// Patched tests exercise the consumer threshold gate without rendering sound.
		return forward_expression && old_value != new_value;
	}
	void monophonicExpressionEvent(int32_t value, int32_t dimension) override {
		++mono_events;
		last_value = value;
		last_dimension = dimension;
	}
};

template <class Set>
void check_notification_lifetime(bool expression) {
	ParamManagerForTimeline manager;
	ParamCollectionSummary summary{};
	Set set(&summary);
	summary.paramCollection = &set;
	notification_observer observer;
	observer.set = &set;
	observer.forward_expression = expression;
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE]{};
	auto* stack = setupModelStackWithThreeMainThingsButNoNoteRow(memory, nullptr, &observer, nullptr, &manager)
	                  ->addParamCollection(&set, &summary);
	int id = expression ? 1 : deluge::modulation::params::LOCAL_PAN;
	auto lookup = [&] { return set.getAutoParamFromId(stack->addParamId(id), true); };
	auto* context = lookup();
	context->autoParam->setCurrentValueWithNoReversionOrRecording(context, 123);
	LONGS_EQUAL(1, observer.comparisons);
	LONGS_EQUAL(123, observer.last_value);
	LONGS_EQUAL(expression ? 1 : 0, observer.mono_events);
	LONGS_EQUAL(1, parameter_test::notifications);
	context = lookup();
	CHECK(context->autoParam->setNodeAtPos(4, 400, true) >= 0);
	set.paramHasAutomationNow(&summary, id);
	observer.comparisons = 0;
	parameter_test::allow_no_action = true;
	context = lookup();
	context->autoParam->deleteNodesWithinRegion(context, 0, 32);
	CHECK_FALSE(set.isAutomated(id));
	check_flag(summary, id, false);
	LONGS_EQUAL(123, set.getValue(id));
	LONGS_EQUAL(1, observer.comparisons);
	CHECK_FALSE(observer.last_automated);
	LONGS_EQUAL(123, observer.last_value);
}
} // namespace
TEST(parameter_lifecycle, patched_deletion_notifies_consumer_once_with_final_scalar_and_automation_state) {
	check_notification_lifetime<PatchedParamSet>(false);
}
TEST(parameter_lifecycle, expression_edit_and_deletion_preserve_monophonic_notification_contract) {
	check_notification_lifetime<ExpressionParamSet>(true);
}

TEST(parameter_lifecycle, partial_node_clone_preserves_successful_parameter_and_clears_only_failed_flags) {
	fixture source;
	source.add_node(31, 4, 400, true);
	source.add_node(32, 8, 800, false);
	source.summary().whichParamsAreInterpolating[0] = uint32_t{1} << 31;
	source.summary().whichParamsAreInterpolating[1] = 1;
	source.param(31)->autoParam->valueIncrementPerHalfTick = 10;
	ParamManagerForTimeline clone;
	{
		fail_allocations failure(3); // Collection + parameter 31 succeed; parameter 32 fails.
		CHECK(clone.cloneParamCollectionsFrom(&source.manager, true) == Error::NONE);
	}
	check_node(*clone.getUnpatchedParamSet()->getParam(31), 0, 4, 400, true);
	check_flag(*clone.getUnpatchedParamSetSummary(), 31, true, true);
	CHECK_FALSE(clone.getUnpatchedParamSet()->isAutomated(32));
	check_flag(*clone.getUnpatchedParamSetSummary(), 32, false);
	check_node(*source.param(32)->autoParam, 0, 8, 800, false);
}

TEST(parameter_lifecycle, real_reload_allocation_failure_preserves_loaded_scalar_and_clears_old_nodes) {
	fixture f;
	f.add_node(31, 0, 1234);
	f.add_node(32, 8, 888, true);
	f.summary().whichParamsAreInterpolating[1] = 1;
	std::string document = "{\"value\":\"0x000000110000019080000004\"}";
	native_parameter_tests::file_contents = document;
	JsonDeserializer reader;
	CHECK(reader.match('{'));
	STRCMP_EQUAL("value", reader.readNextTagOrAttributeName());
	{
		fail_allocations failure;
		f.set().readParam(reader, &f.summary(), 32, 32);
	}
	LONGS_EQUAL(17, f.set().getValue(32));
	CHECK_FALSE(f.set().isAutomated(32));
	check_flag(f.summary(), 32, false);
	check_flag(f.summary(), 31, true);
	check_node(*f.param(31)->autoParam, 0, 0, 1234, false);
	native_parameter_tests::file_contents = {};
}

TEST(parameter_lifecycle, append_handles_all_source_destination_automation_combinations) {
	for (bool via_manager : {false, true}) {
		for (bool source_automated : {false, true}) {
			for (bool destination_automated : {false, true}) {
				for (bool reversed : {false, true}) {
					fixture destination;
					destination.set().setCurrentValueBasicForSetup(32, 17);
					if (destination_automated)
						destination.add_node(32, 4, 40, true);
					{
						fixture source;
						source.set().setCurrentValueBasicForSetup(32, 99);
						if (source_automated) {
							source.add_node(32, 0, 100, true);
							source.add_node(32, 8, 800, true);
						}
						if (via_manager) {
							destination.manager.appendParamManager(destination.stack(), source.stack(), 32,
							                                       reversed ? 32 : 0, reversed);
						}
						else {
							destination.set().appendParamCollection(destination.stack(), source.stack(), 32,
							                                        reversed ? 32 : 0, reversed);
						}
						LONGS_EQUAL(99, source.set().getValue(32));
						if (source_automated) {
							check_node(*source.param(32)->autoParam, 1, 8, 800, true);
							source.param(32)->autoParam->setNodeAtPos(8, -1, false);
						}
					}
					LONGS_EQUAL(17, destination.set().getValue(32));
					check_flag(destination.summary(), 32, source_automated || destination_automated);
					LONGS_EQUAL(int(destination_automated) + 2 * int(source_automated),
					            destination.param(32)->autoParam->nodes.getNumElements());
					if (destination_automated)
						check_node(*destination.param(32)->autoParam, 0, 4, 40, true);
					if (source_automated) {
						check_node(*destination.param(32)->autoParam, int(destination_automated), 32, 100, true);
						check_node(*destination.param(32)->autoParam, int(destination_automated) + 1,
						           reversed ? 56 : 40, 800, true);
					}
				}
			}
		}
	}
}

TEST(parameter_lifecycle, forward_and_pingpong_repeats_preserve_values_and_partial_final_repeat) {
	for (bool pingpong : {false, true}) {
		fixture f;
		f.set().setCurrentValueBasicForSetup(32, 17);
		f.set().setCurrentValueBasicForSetup(31, 99);
		f.add_node(32, 0, 100, true);
		f.add_node(32, 8, 800, true);
		f.set().generateRepeats(f.stack(), 32, 70, pingpong);
		LONGS_EQUAL(5, f.param(32)->autoParam->nodes.getNumElements());
		check_node(*f.param(32)->autoParam, 0, 0, 100, true);
		check_node(*f.param(32)->autoParam, 1, 8, 800, true);
		check_node(*f.param(32)->autoParam, 2, 32, 100, true);
		check_node(*f.param(32)->autoParam, 3, pingpong ? 56 : 40, 800, true);
		check_node(*f.param(32)->autoParam, 4, 64, 100, true);
		LONGS_EQUAL(17, f.set().getValue(32));
		LONGS_EQUAL(99, f.set().getValue(31));
		check_flag(f.summary(), 32, true);
		check_flag(f.summary(), 31, false);
	}
}

TEST(parameter_lifecycle, discarding_stolen_snapshot_releases_only_snapshot_nodes) {
	fixture f;
	f.add_node(31, 8, 900);
	const auto baseline = parameter_test::outstanding_allocations();
	f.set().setCurrentValueBasicForSetup(32, 17);
	f.add_node(32, 0, 100, true);
	f.add_node(32, 16, 500, true);
	{
		ConsequenceParamChange undo(f.param(32), true);
		f.set().paramHasNoAutomationNow(f.stack(), 32);
		LONGS_EQUAL(2, undo.state.nodes.getNumElements());
		CHECK_FALSE(f.set().isAutomated(32));
	}
	f.set().release_unautomated(32);
	patch_cable_pool::get().clear_unused();
	auto_param_pool::get().clear_unused();
	LONGS_EQUAL(baseline, parameter_test::outstanding_allocations());
	LONGS_EQUAL(17, f.set().getValue(32));
	check_flag(f.summary(), 32, false);
	check_flag(f.summary(), 31, true);
	check_node(*f.param(31)->autoParam, 0, 8, 900, false);
	f.add_node(32, 12, 1200);
	check_node(*f.param(32)->autoParam, 0, 12, 1200, false);
	check_node(*f.param(31)->autoParam, 0, 8, 900, false);
}

TEST(parameter_lifecycle, sparse_collection_operations_preserve_every_unautomated_scalar) {
	fixture f;
	const int last = f.set().getNumParams() - 1;
	auto automated = [last](int id) { return id == 0 || id == 32 || id == last; };
	for (int id = 0; id <= last; ++id)
		f.set().setCurrentValueBasicForSetup(id, -1000 - id);
	for (int id : {0, 32, last}) {
		f.add_node(id, 0, id, true);
		f.add_node(id, 16, 1600 + id, true);
	}
	auto check_scalars = [&] {
		for (int id = 0; id <= last; ++id) {
			if (!automated(id)) {
				LONGS_EQUAL(-1000 - id, f.set().getValue(id));
				CHECK_FALSE(f.set().isAutomated(id));
				check_flag(f.summary(), id, false);
			}
		}
	};
	f.set().setPlayPos(4, f.stack(), false);
	for (int id : {0, 32, last})
		LONGS_EQUAL(400 + id, f.set().getValue(id));
	check_scalars();
	f.set().tickTicks(2, f.stack());
	for (int id : {0, 32, last})
		LONGS_EQUAL(600 + id, f.set().getValue(id));
	check_scalars();
	f.set().playbackHasEnded(f.stack());
	f.set().insertTime(f.stack(), 8, 4);
	for (int id : {0, 32, last}) {
		check_node(*f.param(id)->autoParam, 1, 20, 1600 + id, true);
		check_flag(f.summary(), id, true);
	}
	check_scalars();
	ParamManagerForTimeline clone;
	CHECK(clone.cloneParamCollectionsFrom(&f.manager, true, false) == Error::NONE);
	f.set().deleteAllAutomation(nullptr, f.stack());
	check_scalars();
	for (int id = 0; id <= last; ++id) {
		check_flag(f.summary(), id, false);
		CHECK_FALSE(f.set().isAutomated(id));
		LONGS_EQUAL(automated(id) ? 600 + id : -1000 - id, f.set().getValue(id));
		LONGS_EQUAL(f.set().getValue(id), clone.getUnpatchedParamSet()->getValue(id));
		check_flag(*clone.getUnpatchedParamSetSummary(), id, automated(id));
		if (automated(id))
			check_node(*clone.getUnpatchedParamSet()->getParam(id), 1, 20, 1600 + id, true);
	}
}

TEST(parameter_lifecycle, pingpong_repeat_inserts_interpolated_loop_boundary) {
	fixture f;
	f.add_node(32, 8, 800, true);
	f.add_node(32, 24, 2400, true);
	f.set().generateRepeats(f.stack(), 32, 64, true);
	LONGS_EQUAL(6, f.param(32)->autoParam->nodes.getNumElements());
	check_node(*f.param(32)->autoParam, 0, 0, 1600, true);
	check_node(*f.param(32)->autoParam, 1, 8, 800, true);
	check_node(*f.param(32)->autoParam, 2, 24, 2400, true);
	check_node(*f.param(32)->autoParam, 3, 32, 1600, true);
	check_node(*f.param(32)->autoParam, 4, 40, 2400, true);
	check_node(*f.param(32)->autoParam, 5, 56, 800, true);
	check_flag(f.summary(), 32, true);
}

TEST(parameter_lifecycle, reversed_append_preserves_step_segments) {
	fixture source, destination;
	source.add_node(32, 0, 100);
	source.add_node(32, 8, 800);
	destination.manager.appendParamManager(destination.stack(), source.stack(), 32, 32, true);
	LONGS_EQUAL(2, destination.param(32)->autoParam->nodes.getNumElements());
	check_node(*destination.param(32)->autoParam, 0, 32, 800, false);
	check_node(*destination.param(32)->autoParam, 1, 56, 100, false);
	check_flag(destination.summary(), 32, true);
	check_node(*source.param(32)->autoParam, 0, 0, 100, false);
	check_node(*source.param(32)->autoParam, 1, 8, 800, false);
}

namespace {
struct stolen_nodes {
	StolenParamNodes record{};
	~stolen_nodes() { delugeDealloc(record.nodes); }
};
void check_same_nodes(AutoParam& expected, AutoParam& actual) {
	LONGS_EQUAL(expected.nodes.getNumElements(), actual.nodes.getNumElements());
	for (int i = 0; i < expected.nodes.getNumElements(); ++i) {
		auto* node = expected.nodes.getElement(i);
		check_node(actual, i, node->pos, node->value, node->interpolated);
	}
}
void check_ordered_nodes(AutoParam& param, int length) {
	int previous = -1;
	for (int i = 0; i < param.nodes.getNumElements(); ++i) {
		auto* node = param.nodes.getElement(i);
		CHECK(node->pos > previous);
		CHECK(node->pos < length);
		previous = node->pos;
	}
}
} // namespace

TEST(parameter_lifecycle, stealing_and_reinserting_wrapped_nodes_preserves_ownership_and_undo) {
	for (bool keep_middle : {false, true}) {
		fixture f;
		f.set().setCurrentValueBasicForSetup(32, 17);
		f.add_node(31, 8, 900);
		f.add_node(32, 2, 200, true);
		if (keep_middle)
			f.add_node(32, 16, 1600);
		f.add_node(32, 28, 2800, true);
		ConsequenceParamChange undo(f.param(32), false);
		if (!keep_middle) {
			f.summary().whichParamsAreInterpolating[1] = 1;
			f.param(32)->autoParam->valueIncrementPerHalfTick = 16;
		}
		{
			stolen_nodes stolen;
			auto* context = f.param(32);
			context->autoParam->stealNodes(context, 24, 12, 32, nullptr, &stolen.record);
			LONGS_EQUAL(2, stolen.record.num);
			LONGS_EQUAL(4, stolen.record.nodes[0].pos);
			LONGS_EQUAL(2800, stolen.record.nodes[0].value);
			LONGS_EQUAL(10, stolen.record.nodes[1].pos);
			LONGS_EQUAL(200, stolen.record.nodes[1].value);
			check_flag(f.summary(), 32, keep_middle);
			LONGS_EQUAL(17, f.set().getValue(32));
			context = f.param(32);
			context->autoParam->insertStolenNodes(context, 24, 12, 32, nullptr, &stolen.record);
			check_flag(f.summary(), 32, true);
			// Reinsertion copies nodes; disposing the temporary record must leave them alive.
			stolen.record.nodes[0].value = -1;
		}
		check_node(*f.param(32)->autoParam, 0, 2, 200, true);
		check_node(*f.param(32)->autoParam, keep_middle ? 2 : 1, 28, 2800, true);
		CHECK(undo.revert(TimeType::BEFORE, nullptr) == Error::NONE);
		check_node(*f.param(32)->autoParam, 0, 2, 200, true);
		check_node(*f.param(31)->autoParam, 0, 8, 900, false);
		check_flag(f.summary(), 31, true);
	}
}

TEST(parameter_lifecycle, inserting_stolen_nodes_replaces_destination_and_truncates_to_region) {
	fixture f;
	f.set().setCurrentValueBasicForSetup(32, 17);
	f.add_node(32, 0, 100);
	f.add_node(32, 8, 800);
	f.add_node(32, 20, 2000);
	ConsequenceParamChange undo(f.param(32), false);
	ParamNode nodes[3];
	for (int i = 0; i < 3; ++i) {
		nodes[i].pos = i == 2 ? 12 : i * 4;
		nodes[i].value = (i + 1) * 400;
		nodes[i].interpolated = i != 1;
	}
	StolenParamNodes record{3, nodes};
	auto* context = f.param(32);
	context->autoParam->insertStolenNodes(context, 28, 8, 32, nullptr, &record);
	LONGS_EQUAL(4, context->autoParam->nodes.getNumElements());
	check_node(*context->autoParam, 0, 0, 800, false);
	check_node(*context->autoParam, 1, 8, 800, false);
	check_node(*context->autoParam, 2, 20, 2000, false);
	check_node(*context->autoParam, 3, 28, 400, true);
	LONGS_EQUAL(17, f.set().getValue(32));
	CHECK(undo.revert(TimeType::BEFORE, nullptr) == Error::NONE);
	check_node(*f.param(32)->autoParam, 0, 0, 100, false);
	CHECK(undo.revert(TimeType::AFTER, nullptr) == Error::NONE);
	check_node(*f.param(32)->autoParam, 3, 28, 400, true);
	check_flag(f.summary(), 32, true);
}

TEST(parameter_lifecycle, expression_region_moves_wrap_both_directions_and_undo) {
	for (int offset : {-1, 1}) {
		fixture f;
		auto* set = f.manager.getOrCreateExpressionParamSet();
		auto* summary = f.manager.getExpressionParamSetSummary();
		notification_observer observer;
		observer.set = set;
		auto stack = [&] {
			return setupModelStackWithThreeMainThingsButNoNoteRow(f.stack_memory, nullptr, &observer, nullptr,
			                                                      &f.manager)
			    ->addParamCollection(set, summary);
		};
		auto param = [&](int id) { return set->getAutoParamFromId(stack()->addParamId(id), true); };
		for (int id = 0; id < 3; ++id)
			set->setCurrentValueBasicForSetup(id, 100 + id);
		for (int pos : {0, 2, 16, 28, 31})
			CHECK(param(0)->autoParam->setNodeAtPos(pos, pos * 100, true) >= 0);
		set->paramHasAutomationNow(summary, 0);
		ConsequenceParamChange undo(param(0), false);
		set->moveRegionHorizontally(stack(), 24, 12, offset, 32, nullptr);
		const std::array<int, 5> positions =
		    offset == 1 ? std::array<int, 5>{0, 1, 3, 16, 29} : std::array<int, 5>{1, 16, 27, 30, 31};
		const std::array<int, 5> values =
		    offset == 1 ? std::array<int, 5>{3100, 0, 200, 1600, 2800} : std::array<int, 5>{200, 1600, 2800, 3100, 0};
		for (int i = 0; i < 5; ++i)
			check_node(*param(0)->autoParam, i, positions[i], values[i], true);
		for (int id = 0; id < 3; ++id) {
			LONGS_EQUAL(100 + id, set->getValue(id));
			check_flag(*summary, id, id == 0);
		}
		CHECK(undo.revert(TimeType::BEFORE, nullptr) == Error::NONE);
		check_node(*param(0)->autoParam, 4, 31, 3100, true);
		CHECK(undo.revert(TimeType::AFTER, nullptr) == Error::NONE);
		for (int i = 0; i < 5; ++i)
			check_node(*param(0)->autoParam, i, positions[i], values[i], true);
	}
}

TEST(parameter_lifecycle, multiple_undo_snapshots_keep_independent_states_when_discarded) {
	fixture f;
	f.add_node(31, 8, 900);
	f.set().setCurrentValueBasicForSetup(32, 17);
	f.add_node(32, 0, 100, true);
	auto first = std::make_unique<ConsequenceParamChange>(f.param(32), false);
	f.set().setCurrentValueBasicForSetup(32, 27);
	f.param(32)->autoParam->setNodeAtPos(0, 200, false);
	auto second = std::make_unique<ConsequenceParamChange>(f.param(32), false);
	f.set().setCurrentValueBasicForSetup(32, 37);
	f.param(32)->autoParam->setNodeAtPos(0, 300, true);
	for (int i = 0; i < 3; ++i) {
		CHECK(second->revert(TimeType::BEFORE, nullptr) == Error::NONE);
		LONGS_EQUAL(27, f.set().getValue(32));
		check_node(*f.param(32)->autoParam, 0, 0, 200, false);
		CHECK(first->revert(TimeType::BEFORE, nullptr) == Error::NONE);
		LONGS_EQUAL(17, f.set().getValue(32));
		check_node(*f.param(32)->autoParam, 0, 0, 100, true);
		CHECK(first->revert(TimeType::AFTER, nullptr) == Error::NONE);
		CHECK(second->revert(TimeType::AFTER, nullptr) == Error::NONE);
		LONGS_EQUAL(37, f.set().getValue(32));
		check_node(*f.param(32)->autoParam, 0, 0, 300, true);
	}
	first.reset();
	CHECK(second->revert(TimeType::BEFORE, nullptr) == Error::NONE);
	check_node(*f.param(32)->autoParam, 0, 0, 200, false);
	second.reset();
	LONGS_EQUAL(27, f.set().getValue(32));
	check_node(*f.param(32)->autoParam, 0, 0, 200, false);
	check_node(*f.param(31)->autoParam, 0, 8, 900, false);
	check_flag(f.summary(), 31, true);
	check_flag(f.summary(), 32, true);
}

TEST(parameter_lifecycle, append_allocation_failures_keep_valid_nodes_and_allow_retry) {
	for (bool pingpong : {false, true}) {
		bool reached_success = false;
		bool saw_failure = false;
		for (int budget = 0; budget < 16 && !reached_success; ++budget) {
			fixture source, destination, expected;
			for (int pos = 4; pos < 64; pos += 8)
				source.add_node(32, pos, pos * 100, true);
			for (fixture* f : {&destination, &expected}) {
				f->set().setCurrentValueBasicForSetup(32, 17);
				f->add_node(32, 0, 99);
				f->add_node(31, 8, 900);
			}
			expected.set().appendParamCollection(expected.stack(), source.stack(), 64, pingpong ? 64 : 0, pingpong);
			const auto failures_before = parameter_test::allocation_failures;
			{
				fail_allocations failure(budget);
				destination.set().appendParamCollection(destination.stack(), source.stack(), 64, pingpong ? 64 : 0,
				                                        pingpong);
			}
			reached_success = failures_before == parameter_test::allocation_failures;
			saw_failure |= !reached_success;
			check_ordered_nodes(*destination.param(32)->autoParam, 128);
			LONGS_EQUAL(17, destination.set().getValue(32));
			check_flag(destination.summary(), 32, destination.set().isAutomated(32));
			check_node(*destination.param(31)->autoParam, 0, 8, 900, false);
			if (!reached_success)
				destination.set().appendParamCollection(destination.stack(), source.stack(), 64, pingpong ? 64 : 0,
				                                        pingpong);
			check_same_nodes(*expected.param(32)->autoParam, *destination.param(32)->autoParam);
			for (int i = 0; i < 8; ++i)
				check_node(*source.param(32)->autoParam, i, 4 + i * 8, (4 + i * 8) * 100, true);
		}
		CHECK(reached_success);
		CHECK(saw_failure);
	}
}

TEST(parameter_lifecycle, repeat_allocation_failures_keep_valid_nodes_and_allow_retry) {
	for (bool pingpong : {false, true}) {
		bool reached_success = false;
		bool saw_failure = false;
		for (int budget = 0; budget < 16 && !reached_success; ++budget) {
			fixture f, expected;
			for (fixture* target : {&f, &expected}) {
				target->set().setCurrentValueBasicForSetup(32, 17);
				target->set().setCurrentValueBasicForSetup(31, 900);
				for (int pos = 4; pos < 64; pos += 8)
					target->add_node(32, pos, pos * 100, true);
			}
			expected.set().generateRepeats(expected.stack(), 64, 256, pingpong);
			const auto failures_before = parameter_test::allocation_failures;
			{
				fail_allocations failure(budget);
				f.set().generateRepeats(f.stack(), 64, 256, pingpong);
			}
			reached_success = failures_before == parameter_test::allocation_failures;
			saw_failure |= !reached_success;
			check_ordered_nodes(*f.param(32)->autoParam, 256);
			LONGS_EQUAL(17, f.set().getValue(32));
			check_flag(f.summary(), 32, f.set().isAutomated(32));
			LONGS_EQUAL(900, f.set().getValue(31));
			check_flag(f.summary(), 31, false);
			if (!reached_success)
				f.set().generateRepeats(f.stack(), 64, 256, pingpong);
			check_same_nodes(*expected.param(32)->autoParam, *f.param(32)->autoParam);
		}
		CHECK(reached_success);
		CHECK(saw_failure);
	}
}

TEST(parameter_lifecycle, inserting_empty_record_removes_last_nodes_and_undo_restores_them) {
	fixture f;
	f.set().setCurrentValueBasicForSetup(32, 17);
	f.add_node(32, 0, 100, true);
	f.add_node(31, 8, 900);
	ConsequenceParamChange undo(f.param(32), false);
	f.summary().whichParamsAreInterpolating[1] = 1;
	f.param(32)->autoParam->valueIncrementPerHalfTick = 16;
	StolenParamNodes record{};
	auto* context = f.param(32);
	context->autoParam->insertStolenNodes(context, 0, 32, 32, nullptr, &record);
	CHECK_FALSE(f.set().isAutomated(32));
	check_flag(f.summary(), 32, false);
	LONGS_EQUAL(17, f.set().getValue(32));
	CHECK(undo.revert(TimeType::BEFORE, nullptr) == Error::NONE);
	check_node(*f.param(32)->autoParam, 0, 0, 100, true);
	CHECK(undo.revert(TimeType::AFTER, nullptr) == Error::NONE);
	check_flag(f.summary(), 32, false);
	check_node(*f.param(31)->autoParam, 0, 8, 900, false);
	check_flag(f.summary(), 31, true);
}

TEST(parameter_lifecycle, scalar_and_stolen_undo_snapshots_work_without_node_allocations) {
	fixture f;
	f.set().setCurrentValueBasicForSetup(32, 17);
	{
		fail_allocations failure;
		ConsequenceParamChange scalar(f.stack()->addAutoParam(32, nullptr), false);
		f.set().setCurrentValueBasicForSetup(32, 27);
		CHECK(scalar.revert(TimeType::BEFORE, nullptr) == Error::NONE);
		LONGS_EQUAL(17, f.set().getValue(32));
	}
	f.add_node(32, 4, 400, true);
	{
		fail_allocations failure;
		ConsequenceParamChange stolen(f.param(32), true);
		f.set().paramHasNoAutomationNow(f.stack(), 32);
		CHECK_FALSE(f.set().isAutomated(32));
		CHECK(stolen.revert(TimeType::BEFORE, nullptr) == Error::NONE);
		check_node(*f.param(32)->autoParam, 0, 4, 400, true);
		check_flag(f.summary(), 32, true);
	}
	LONGS_EQUAL(0, parameter_test::allocation_failures);
}

TEST(parameter_lifecycle, failed_node_capture_preserves_source_and_allows_retry) {
	for (bool wrapping : {false, true}) {
		fixture f;
		f.set().setCurrentValueBasicForSetup(32, 17);
		f.add_node(31, 8, 900);
		f.add_node(32, 2, 200, true);
		f.add_node(32, 28, 2800, true);
		stolen_nodes stolen;
		const auto allocations = parameter_test::outstanding_allocations();
		{
			fail_allocations failure;
			auto* context = f.param(32);
			CHECK(context->autoParam->stealNodes(context, wrapping ? 24 : 0, wrapping ? 12 : 32, 32, nullptr,
			                                     &stolen.record)
			      == Error::INSUFFICIENT_RAM);
		}
		POINTERS_EQUAL(nullptr, stolen.record.nodes);
		LONGS_EQUAL(0, stolen.record.num);
		LONGS_EQUAL(allocations, parameter_test::outstanding_allocations());
		LONGS_EQUAL(17, f.set().getValue(32));
		check_node(*f.param(32)->autoParam, 0, 2, 200, true);
		check_node(*f.param(32)->autoParam, 1, 28, 2800, true);
		check_flag(f.summary(), 32, true);
		auto* context = f.param(32);
		CHECK(
		    context->autoParam->stealNodes(context, wrapping ? 24 : 0, wrapping ? 12 : 32, 32, nullptr, &stolen.record)
		    == Error::NONE);
		LONGS_EQUAL(2, stolen.record.num);
		check_flag(f.summary(), 32, false);
		context = f.param(32);
		CHECK(context->autoParam->insertStolenNodes(context, wrapping ? 24 : 0, wrapping ? 12 : 32, 32, nullptr,
		                                            &stolen.record)
		      == Error::NONE);
		check_node(*f.param(32)->autoParam, 0, 2, 200, true);
		check_node(*f.param(32)->autoParam, 1, 28, 2800, true);
		check_node(*f.param(31)->autoParam, 0, 8, 900, false);
	}
}

TEST(parameter_lifecycle, partial_node_insertion_reports_failure_and_retries_without_consuming_record) {
	bool reached_success = false, saw_partial = false, saw_failure = false;
	for (int budget = 0; budget < 16 && !reached_success; ++budget) {
		fixture f;
		f.set().setCurrentValueBasicForSetup(32, 17);
		f.add_node(32, 8, -800);
		f.add_node(32, 96, 9600);
		f.add_node(32, 116, -11600);
		f.add_node(31, 8, 900);
		std::array<ParamNode, 32> nodes;
		for (int i = 0; i < 32; ++i) {
			nodes[i].pos = i * 3;
			nodes[i].value = i * 100;
			nodes[i].interpolated = i % 2;
		}
		StolenParamNodes record{32, nodes.data()};
		Error result;
		{
			fail_allocations failure(budget);
			auto* context = f.param(32);
			result = context->autoParam->insertStolenNodes(context, 112, 96, 128, nullptr, &record);
		}
		reached_success = result == Error::NONE;
		CHECK(reached_success || result == Error::INSUFFICIENT_RAM);
		saw_failure |= !reached_success;
		const int count = f.param(32)->autoParam->nodes.getNumElements();
		saw_partial |= !reached_success && count > 1 && count < 33;
		check_ordered_nodes(*f.param(32)->autoParam, 128);
		check_flag(f.summary(), 32, f.set().isAutomated(32));
		LONGS_EQUAL(17, f.set().getValue(32));
		check_node(*f.param(31)->autoParam, 0, 8, 900, false);
		LONGS_EQUAL(32, record.num);
		for (int i = 0; i < 32; ++i) {
			LONGS_EQUAL(i * 3, nodes[i].pos);
			LONGS_EQUAL(i * 100, nodes[i].value);
		}
		if (!reached_success) {
			auto* context = f.param(32);
			CHECK(context->autoParam->insertStolenNodes(context, 112, 96, 128, nullptr, &record) == Error::NONE);
		}
		LONGS_EQUAL(33, f.param(32)->autoParam->nodes.getNumElements());
		for (int i = 0; i < 32; ++i) {
			const int pos = (112 + i * 3) % 128;
			const int index = f.param(32)->autoParam->nodes.searchExact(pos);
			CHECK(index >= 0);
			check_node(*f.param(32)->autoParam, index, pos, i * 100, i % 2);
		}
		const int index = f.param(32)->autoParam->nodes.searchExact(96);
		check_node(*f.param(32)->autoParam, index, 96, 9600, false);
	}
	CHECK(reached_success);
	CHECK(saw_failure);
	CHECK(saw_partial);
}

TEST(parameter_lifecycle, failed_first_replacement_node_clears_flags_and_keeps_record_for_retry) {
	fixture f;
	f.set().setCurrentValueBasicForSetup(32, 17);
	f.add_node(32, 4, 400, true);
	f.summary().whichParamsAreInterpolating[1] = 1;
	f.param(32)->autoParam->valueIncrementPerHalfTick = 16;
	ParamNode node;
	node.pos = 8;
	node.value = 800;
	StolenParamNodes record{1, &node};
	{
		fail_allocations failure;
		auto* context = f.param(32);
		CHECK(context->autoParam->insertStolenNodes(context, 0, 32, 32, nullptr, &record) == Error::INSUFFICIENT_RAM);
	}
	LONGS_EQUAL(17, f.set().getValue(32));
	check_flag(f.summary(), 32, false);
	CHECK_FALSE(f.set().isAutomated(32));
	LONGS_EQUAL(1, record.num);
	auto* context = f.param(32);
	CHECK(context->autoParam->insertStolenNodes(context, 0, 32, 32, nullptr, &record) == Error::NONE);
	check_node(*f.param(32)->autoParam, 0, 8, 800, false);
	check_flag(f.summary(), 32, true);
}

TEST(parameter_lifecycle, deterministic_edit_delete_clone_and_undo_sequence_preserves_ownership) {
	struct expected_node {
		int32_t position;
		int32_t value;
		bool interpolated;
	};
	struct expected_param {
		int32_t value = 0;
		std::vector<expected_node> nodes;
	};
	struct snapshot {
		std::unique_ptr<ConsequenceParamChange> actual;
		int owner = 0;
		int param = 0;
		expected_param expected;
	};
	enum class operation {
		scalar,
		node,
		capture,
		capture_stolen,
		erase,
		undo,
		redo,
		clone,
		discard,
		destroy,
		create,
		clear
	};
	struct sequence_step {
		char const* label;
		operation action;
		int owner;
		int param = 31;
		int value = 0;
		int position = 0;
		int slot = 0;
		bool interpolated = false;
	};
	// Clone's owner is its source; the other manager is its destination.
	// Expected state is maintained independently of production values and node storage.
	const sequence_step steps[] = {
	    {"set original scalar", operation::scalar, 0, 31, 17},
	    {"create first node", operation::node, 0, 31, 400, 4, 0, true},
	    {"capture first edit", operation::capture, 0},
	    {"extend envelope", operation::node, 0, 31, 1200, 12, 0, true},
	    {"change original scalar", operation::scalar, 0, 31, -42},
	    {"set neighboring scalar", operation::scalar, 0, 32, 99},
	    {"automate neighboring parameter", operation::node, 0, 32, 800, 8},
	    {"clone both automated parameters", operation::clone, 0},
	    {"capture neighbor", operation::capture, 0, 32, 0, 0, 1},
	    {"delete original envelope", operation::erase, 0},
	    {"undo original edit", operation::undo, 0},
	    {"redo original deletion", operation::redo, 0},
	    {"restore original again", operation::undo, 0},
	    {"delete neighboring envelope", operation::erase, 0, 32},
	    {"undo neighbor deletion", operation::undo, 0, 32, 0, 0, 1},
	    {"redo neighbor deletion", operation::redo, 0, 32, 0, 0, 1},
	    {"capture clone independently", operation::capture, 1, 31, 0, 0, 2},
	    {"edit clone scalar", operation::scalar, 1, 31, 123},
	    {"replace clone node", operation::node, 1, 31, -400, 4},
	    {"delete clone neighbor", operation::erase, 1, 32},
	    {"discard neighbor undo owning nodes", operation::discard, 0, 32, 0, 0, 1},
	    {"discard original undo", operation::discard, 0},
	    {"destroy source while clone and its snapshot survive", operation::destroy, 0},
	    {"undo clone after source destruction", operation::undo, 1, 31, 0, 0, 2},
	    {"redo clone after source destruction", operation::redo, 1, 31, 0, 0, 2},
	    {"discard clone snapshot owning historical nodes", operation::discard, 1, 31, 0, 0, 2},
	    {"create replacement source", operation::create, 0},
	    {"clone back into replacement source", operation::clone, 1},
	    {"steal replacement envelope into snapshot", operation::capture_stolen, 0},
	    {"create envelope while snapshot owns previous nodes", operation::node, 0, 31, 2000, 20},
	    {"swap stolen envelope back", operation::undo, 0},
	    {"discard snapshot owning replaced envelope", operation::discard, 0},
	    {"clear surviving clone automation", operation::clear, 1},
	    {"recreate clone automation", operation::node, 1, 31, 2400, 24},
	    {"destroy replacement source", operation::destroy, 0},
	    {"destroy final owner", operation::destroy, 1},
	};
	std::array<std::unique_ptr<fixture>, 2> owners{std::make_unique<fixture>(), std::make_unique<fixture>()};
	const int num_params = owners[0]->set().getNumParams();
	std::array<std::vector<expected_param>, 2> expected{std::vector<expected_param>(num_params),
	                                                    std::vector<expected_param>(num_params)};
	std::array<snapshot, 3> snapshots;

	auto check_all = [&](char const* label) {
		std::set<ParamNode*> owned_nodes;
		auto check_nodes = [&](ParamNodeVector& actual, std::vector<expected_node> const& model) {
			CHECK_TEXT(actual.getNumElements() == static_cast<int>(model.size()), label);
			for (size_t index = 0; index < model.size(); ++index) {
				auto* node = actual.getElement(index);
				CHECK_TEXT(node != nullptr, label);
				CHECK_TEXT(node->pos == model[index].position, label);
				CHECK_TEXT(node->value == model[index].value, label);
				CHECK_TEXT(node->interpolated == model[index].interpolated, label);
				// Every node belongs to exactly one live parameter or undo snapshot.
				CHECK_TEXT(owned_nodes.insert(node).second, label);
			}
		};
		for (int owner = 0; owner < 2; ++owner) {
			if (!owners[owner])
				continue;
			auto& f = *owners[owner];
			for (int id = 0; id < num_params; ++id) {
				auto const& model = expected[owner][id];
				const bool automated = !model.nodes.empty();
				CHECK_TEXT(f.set().getValue(id) == model.value, label);
				CHECK_TEXT(f.set().isAutomated(id) == automated, label);
				const uint32_t mask = uint32_t{1} << (id & 31);
				CHECK_TEXT(bool(f.summary().whichParamsAreAutomated[id >> 5] & mask) == automated, label);
				CHECK_TEXT(!(f.summary().whichParamsAreInterpolating[id >> 5] & mask), label);
				if (automated)
					check_nodes(f.param(id)->autoParam->nodes, model.nodes);
			}
		}
		for (auto& saved : snapshots) {
			if (!saved.actual)
				continue;
			CHECK_TEXT(owners[saved.owner] != nullptr, label);
			CHECK_TEXT(saved.actual->state.value == saved.expected.value, label);
			check_nodes(saved.actual->state.nodes, saved.expected.nodes);
		}
	};

	check_all("initial state");
	for (auto const& step : steps) {
		auto& owner = owners[step.owner];
		auto& model = expected[step.owner][step.param];
		auto& saved = snapshots[step.slot];
		switch (step.action) {
		case operation::scalar: {
			auto* context = owner->param(step.param);
			context->autoParam->setCurrentValueWithNoReversionOrRecording(context, step.value);
			model.value = step.value;
			break;
		}
		case operation::node: {
			owner->add_node(step.param, step.position, step.value, step.interpolated);
			auto it = std::lower_bound(model.nodes.begin(), model.nodes.end(), step.position,
			                           [](expected_node const& node, int pos) { return node.position < pos; });
			expected_node node{step.position, step.value, step.interpolated};
			if (it != model.nodes.end() && it->position == step.position)
				*it = node;
			else
				model.nodes.insert(it, node);
			break;
		}
		case operation::capture:
		case operation::capture_stolen: {
			CHECK_TEXT(!saved.actual, step.label);
			const bool steal = step.action == operation::capture_stolen;
			saved.owner = step.owner;
			saved.param = step.param;
			saved.expected = model;
			saved.actual = std::make_unique<ConsequenceParamChange>(owner->param(step.param), steal);
			if (steal) {
				owner->set().paramHasNoAutomationNow(owner->stack(), step.param);
				model.nodes.clear();
			}
			break;
		}
		case operation::erase: {
			auto* context = owner->param(step.param);
			context->autoParam->deleteAutomation(nullptr, context);
			model.nodes.clear();
			break;
		}
		case operation::undo:
		case operation::redo:
			CHECK_TEXT(saved.actual && saved.owner == step.owner && saved.param == step.param, step.label);
			CHECK_TEXT(
			    saved.actual->revert(step.action == operation::undo ? TimeType::BEFORE : TimeType::AFTER, nullptr)
			        == Error::NONE,
			    step.label);
			std::swap(model, saved.expected);
			break;
		case operation::clone: {
			const int destination = 1 - step.owner;
			// Replacing a collection invalidates saved contexts; never clone over live history.
			for (auto const& snapshot : snapshots)
				CHECK_TEXT(!snapshot.actual || snapshot.owner != destination, step.label);
			CHECK_TEXT(owners[destination]->manager.cloneParamCollectionsFrom(&owner->manager, true, false)
			               == Error::NONE,
			           step.label);
			expected[destination] = expected[step.owner];
			break;
		}
		case operation::discard:
			CHECK_TEXT(saved.actual != nullptr, step.label);
			saved.actual.reset();
			break;
		case operation::destroy:
			for (auto const& snapshot : snapshots)
				CHECK_TEXT(!snapshot.actual || snapshot.owner != step.owner, step.label);
			owner.reset();
			break;
		case operation::create:
			CHECK_TEXT(!owner, step.label);
			owner = std::make_unique<fixture>();
			expected[step.owner].assign(num_params, expected_param{});
			break;
		case operation::clear:
			owner->set().deleteAllAutomation(nullptr, owner->stack());
			for (auto& param : expected[step.owner])
				param.nodes.clear();
			break;
		}
		check_all(step.label);
	}
	patch_cable_pool::get().clear_unused();
	auto_param_pool::get().clear_unused();
	LONGS_EQUAL(0, parameter_test::outstanding_allocations());
}

TEST(parameter_lifecycle, sparse_scalar_access_and_notifications_do_not_acquire_objects) {
	fixture f;
	fail_allocations failure;
	for (int32_t id = 0; id < f.set().getNumParams(); ++id) {
		POINTERS_EQUAL(nullptr, f.set().getParam(id, false));
		CHECK(f.set().has_current_value(id));
		f.set().set_current_value(f.stack(), id, id + 1);
		LONGS_EQUAL(id + 1, f.set().getValue(id));
		LONGS_EQUAL(id + 1, f.set().get_current_value(id));
		POINTERS_EQUAL(nullptr, f.set().getParam(id, false));
	}
	LONGS_EQUAL(0, auto_param_pool::get().active_count());
	LONGS_EQUAL(0, parameter_test::allocation_failures);
}

TEST(parameter_lifecycle, shared_pool_reuses_storage_across_all_three_parameter_sets) {
	ParamCollectionSummary unpatched_summary{}, patched_summary{}, expression_summary{};
	UnpatchedParamSet unpatched(&unpatched_summary);
	PatchedParamSet patched(&patched_summary);
	ExpressionParamSet expression(&expression_summary);
	auto& pool = auto_param_pool::get();
	AutoParam* previous = nullptr;
	for (ParamSet* set :
	     {static_cast<ParamSet*>(&unpatched), static_cast<ParamSet*>(&patched), static_cast<ParamSet*>(&expression)}) {
		set->setCurrentValueBasicForSetup(0, 17);
		auto* param = set->getParam(0);
		CHECK(param);
		if (previous)
			POINTERS_EQUAL(previous, param);
		LONGS_EQUAL(17, param->getCurrentValue());
		CHECK_FALSE(param->isAutomated());
		CHECK_FALSE(param->hasInterpolationIncrement());
		LONGS_EQUAL(0, param->renewedOverridingAtTime);
		CHECK(param->setNodeAtPos(4, 400, true) >= 0);
		param->valueIncrementPerHalfTick = 123;
		param->renewedOverridingAtTime = 456;
		LONGS_EQUAL(1, pool.active_count());
		param->deleteAutomationBasicForSetup();
		set->release_unautomated(0);
		POINTERS_EQUAL(nullptr, set->getParam(0, false));
		LONGS_EQUAL(17, set->getValue(0));
		LONGS_EQUAL(0, pool.active_count());
		LONGS_EQUAL(1, pool.cached_count());
		previous = param;
	}
}

TEST(parameter_lifecycle, pool_acquisition_failure_preserves_scalar_flags_and_allows_retry) {
	fixture f;
	f.set().setCurrentValueBasicForSetup(32, 17);
	{
		fail_allocations failure;
		POINTERS_EQUAL(nullptr, f.param(32)->autoParam);
		LONGS_EQUAL(17, f.set().getValue(32));
		check_flag(f.summary(), 32, false);
		LONGS_EQUAL(0, auto_param_pool::get().active_count());
	}
	f.add_node(32, 4, 400, true);
	LONGS_EQUAL(1, auto_param_pool::get().active_count());
	check_node(*f.param(32)->autoParam, 0, 4, 400, true);
}

TEST(parameter_lifecycle, deleting_last_node_returns_object_and_undo_reacquires_without_aliasing) {
	fixture f;
	f.set().setCurrentValueBasicForSetup(32, 17);
	f.add_node(32, 4, 400, true);
	ConsequenceParamChange undo(f.param(32), false);
	auto* context = f.param(32);
	context->autoParam->deleteAutomation(nullptr, context);
	POINTERS_EQUAL(nullptr, f.set().getParam(32, false));
	LONGS_EQUAL(0, auto_param_pool::get().active_count());
	// Occupy the returned block with another parameter before restoring the snapshot.
	f.add_node(31, 8, 900);
	{
		fail_allocations failure;
		CHECK(undo.revert(TimeType::BEFORE, nullptr) == Error::INSUFFICIENT_RAM);
		LONGS_EQUAL(17, f.set().getValue(32));
		POINTERS_EQUAL(nullptr, f.set().getParam(32, false));
		check_node(*f.param(31)->autoParam, 0, 8, 900, false);
		LONGS_EQUAL(1, undo.state.nodes.getNumElements());
		LONGS_EQUAL(400, undo.state.nodes.getElement(0)->value);
	}
	CHECK(undo.revert(TimeType::BEFORE, nullptr) == Error::NONE);
	check_node(*f.param(32)->autoParam, 0, 4, 400, true);
	CHECK(f.param(31)->autoParam != f.set().getParam(32, false));
	CHECK(undo.revert(TimeType::AFTER, nullptr) == Error::NONE);
	POINTERS_EQUAL(nullptr, f.set().getParam(32, false));
	check_node(*f.param(31)->autoParam, 0, 8, 900, false);
}

TEST(parameter_lifecycle, whole_loop_edit_releases_automation_after_updating_scalar_and_can_undo) {
	fixture f;
	f.set().setCurrentValueBasicForSetup(32, 17);
	f.add_node(32, 4, 400, true);
	ConsequenceParamChange undo(f.param(32), false);
	parameter_test::allow_no_action = true;
	auto* context = f.param(32);
	const auto notifications = parameter_test::notifications;
	context->autoParam->setValueForRegion(0, parameter_test::loop_length, 99, context);
	LONGS_EQUAL(99, f.set().getValue(32));
	POINTERS_EQUAL(nullptr, f.set().getParam(32, false));
	check_flag(f.summary(), 32, false);
	LONGS_EQUAL(notifications + 1, parameter_test::notifications);
	CHECK(undo.revert(TimeType::BEFORE, nullptr) == Error::NONE);
	LONGS_EQUAL(17, f.set().getValue(32));
	check_node(*f.param(32)->autoParam, 0, 4, 400, true);
}

TEST(parameter_lifecycle, pool_grows_beyond_idle_cache_limit_and_releases_excess_storage) {
	auto& pool = auto_param_pool::get();
	std::array<AutoParam*, 80> objects{};
	for (auto& object : objects) {
		object = pool.acquire();
		CHECK(object);
	}
	LONGS_EQUAL(objects.size(), pool.active_count());
	std::set<AutoParam*> unique(objects.begin(), objects.end());
	LONGS_EQUAL(objects.size(), unique.size());
	for (auto* object : objects)
		pool.release(object);
	LONGS_EQUAL(0, pool.active_count());
	CHECK(pool.cached_count() < objects.size());
	LONGS_EQUAL(pool.cached_count(), parameter_test::outstanding_allocations());
	pool.clear_unused();
	LONGS_EQUAL(0, parameter_test::outstanding_allocations());
}

TEST(parameter_lifecycle, cached_object_is_available_when_heap_allocation_fails) {
	fixture f;
	auto* previous = f.set().getParam(31);
	CHECK(previous);
	f.set().setCurrentValueBasicForSetup(31, 31);
	f.set().release_unautomated(31);
	f.set().setCurrentValueBasicForSetup(32, 32);
	fail_allocations failure(0, false);
	auto* reused = f.set().getParam(32);
	POINTERS_EQUAL(previous, reused);
	LONGS_EQUAL(32, reused->getCurrentValue());
	reused->setCurrentValueBasicForSetup(99);
	LONGS_EQUAL(31, f.set().getValue(31));
	LONGS_EQUAL(99, f.set().getValue(32));
	LONGS_EQUAL(0, parameter_test::allocation_failures);
}

TEST(parameter_lifecycle, failed_node_clone_returns_new_object_without_releasing_source) {
	for (int32_t reverse_length : {0, 32}) {
		fixture source;
		source.set().setCurrentValueBasicForSetup(32, 17);
		source.add_node(32, 4, 400, true);
		ParamManagerForTimeline clone;
		{
			// Collection and AutoParam succeed; node allocation fails.
			fail_allocations failure(2);
			CHECK(clone.cloneParamCollectionsFrom(&source.manager, true, false, reverse_length) == Error::NONE);
		}
		LONGS_EQUAL(17, clone.getUnpatchedParamSet()->getValue(32));
		POINTERS_EQUAL(nullptr, clone.getUnpatchedParamSet()->getParam(32, false));
		check_flag(*clone.getUnpatchedParamSetSummary(), 32, false);
		LONGS_EQUAL(1, auto_param_pool::get().active_count());
		check_node(*source.param(32)->autoParam, 0, 4, 400, true);
	}
}

TEST(parameter_lifecycle, failed_first_region_edit_returns_empty_object_and_retries) {
	fixture f;
	f.set().setCurrentValueBasicForSetup(32, 17);
	auto* context = f.param(32);
	parameter_test::allow_no_action = true;
	{
		fail_allocations failure;
		context->autoParam->setValueForRegion(4, 4, 99, context);
	}
	LONGS_EQUAL(17, f.set().getValue(32));
	POINTERS_EQUAL(nullptr, f.set().getParam(32, false));
	check_flag(f.summary(), 32, false);
	context = f.param(32);
	context->autoParam->setValueForRegion(4, 4, 99, context);
	CHECK(f.set().isAutomated(32));
	check_flag(f.summary(), 32, true);
}

namespace {
class test_timeline final : public TimelineCounter {
public:
	int32_t getLastProcessedPos() const override { return 0; }
	uint32_t getLivePos() const override { return 0; }
	int32_t getLoopLength() const override { return parameter_test::loop_length; }
	bool isPlayingAutomationNow() const override { return true; }
	bool backtrackingCouldLoopBackToEnd() const override { return true; }
	int32_t getPosAtWhichPlaybackWillCut(ModelStackWithTimelineCounter const*) const override { return INT32_MAX; }
	void getActiveModControllable(ModelStackWithTimelineCounter*) override { FAIL("Unexpected timeline lookup"); }
	void expectEvent() override {}
	TimelineCounter* getTimelineCounterToRecordTo() override { return this; }
};

class knob_lookup final : public ModControllable {
public:
	fixture& owner;
	int32_t param_id = 0;
	int lookups = 0;
	explicit knob_lookup(fixture& owner) : owner(owner) {}
	ParamManagerType required_param_manager_type() const override { return ParamManagerType::GLOBAL; }
	ModelStackWithAutoParam* getParamFromModEncoder(int32_t knob, ModelStackWithThreeMainThings*,
	                                                bool allow_creation) override {
		LONGS_EQUAL(1, knob);
		CHECK_FALSE(allow_creation);
		++lookups;
		return owner.set().getAutoParamFromId(owner.stack()->addParamId(param_id), allow_creation);
	}
};

template <class Reader>
void check_loading_pool_failure(bool json) {
	fixture f;
	f.set().setCurrentValueBasicForSetup(32, -99);
	f.summary().whichParamsAreInterpolating[1] = 1;
	f.add_node(31, 0, 1234);
	const auto baseline = parameter_test::outstanding_allocations();
	const std::string document = json ? "{\"value\":\"0x000000110000019080000004\",\"sentinel\":73}"
	                                  : "<params value=\"0x000000110000019080000004\" sentinel=\"73\" />";
	for (bool fail : {true, false}) {
		native_parameter_tests::file_contents = document;
		Reader reader;
		if (json)
			CHECK(reader.match('{'));
		else
			STRCMP_EQUAL("params", reader.readNextTagOrAttributeName());
		STRCMP_EQUAL("value", reader.readNextTagOrAttributeName());
		if (fail) {
			// The temporary node vector succeeds; acquiring its persistent owner fails.
			fail_allocations failure(1);
			f.set().readParam(reader, &f.summary(), 32, 32);
			LONGS_EQUAL(1, parameter_test::allocation_failures);
			LONGS_EQUAL(sizeof(AutoParam), parameter_test::last_failed_allocation_size);
			POINTERS_EQUAL(nullptr, f.set().getParam(32, false));
			LONGS_EQUAL(baseline, parameter_test::outstanding_allocations());
		}
		else {
			f.set().readParam(reader, &f.summary(), 32, 32);
			check_node(*f.set().getParam(32, false), 0, 4, 400, true);
		}
		LONGS_EQUAL(17, f.set().getValue(32));
		check_flag(f.summary(), 32, !fail);
		check_flag(f.summary(), 31, true);
		check_node(*f.set().getParam(31, false), 0, 0, 1234, false);
		reader.exitTag("value");
		STRCMP_EQUAL("sentinel", reader.readNextTagOrAttributeName());
		LONGS_EQUAL(73, reader.readTagOrAttributeValueInt());
	}
	native_parameter_tests::file_contents = {};
}
} // namespace

TEST(parameter_lifecycle, editor_reacquires_after_whole_loop_deletion_and_cross_parameter_reuse) {
	fixture f;
	test_timeline timeline;
	f.add_node(32, 4, 400, true);
	auto* previous = f.set().getParam(32, false);
	auto* context = f.param(32);
	context->setTimelineCounter(&timeline);
	parameter_test::allow_no_action = true;
	set_parameter_region(context, 17, 0, 32);
	POINTERS_EQUAL(nullptr, context->autoParam);
	POINTERS_EQUAL(nullptr, f.set().getParam(32, false));
	LONGS_EQUAL(17, f.set().getValue(32));
	check_flag(f.summary(), 32, false);

	// Deliberately use a separate stack so the editor retains its original context.
	auto* neighbor = f.set().getParam(31);
	POINTERS_EQUAL(previous, neighbor);
	neighbor->setCurrentValueBasicForSetup(91);
	CHECK(neighbor->setNodeAtPos(8, 900, false) >= 0);
	f.set().paramHasAutomationNow(&f.summary(), 31);
	set_parameter_region(context, 99, 4, 4);
	CHECK(context->autoParam);
	CHECK(context->autoParam != neighbor);
	LONGS_EQUAL(99, f.set().getValue(32));
	check_flag(f.summary(), 32, true);
	set_parameter_region(context, 27, 0, 32);
	POINTERS_EQUAL(nullptr, context->autoParam);
	set_parameter_region(context, 37, 0, 32); // Same sequence as the editor's repeated writes.
	POINTERS_EQUAL(nullptr, context->autoParam);
	LONGS_EQUAL(37, f.set().getValue(32));
	check_flag(f.summary(), 32, false);
	LONGS_EQUAL(91, f.set().getValue(31));
	check_node(*neighbor, 0, 8, 900, false);
	check_flag(f.summary(), 31, true);
}

TEST(parameter_lifecycle, short_loop_recording_releases_after_scalar_update_and_notifies_once) {
	for (int32_t loop_length : {32, 88}) {
		fixture f;
		test_timeline timeline;
		parameter_test::loop_length = loop_length;
		f.set().setCurrentValueBasicForSetup(32, 17);
		f.add_node(32, 4, 400, true);
		f.add_node(31, 8, 900);
		auto* context = f.param(32);
		context->setTimelineCounter(&timeline);
		context->autoParam->valueIncrementPerHalfTick = 16;
		context->autoParam->renewedOverridingAtTime = 123;
		f.summary().whichParamsAreInterpolating[1] = 1;
		parameter_test::allow_no_action = true;
		parameter_test::allow_recording_controls = true;
		playbackHandler.playbackState = PLAYBACK_CLOCK_EITHER_ACTIVE;
		playbackHandler.recording = RecordingMode::NORMAL;
		const auto notifications = parameter_test::notifications;
		{
			fail_allocations failure;
			context->autoParam->setCurrentValueInResponseToUserInput(99, context);
		}
		LONGS_EQUAL(notifications + 1, parameter_test::notifications);
		LONGS_EQUAL(0, parameter_test::allocation_failures);
		LONGS_EQUAL(99, f.set().getValue(32));
		POINTERS_EQUAL(nullptr, f.set().getParam(32, false));
		check_flag(f.summary(), 32, false);
		check_node(*f.set().getParam(31, false), 0, 8, 900, false);
		check_flag(f.summary(), 31, true);
		playbackHandler.playbackState = 0;
		playbackHandler.recording = RecordingMode::OFF;
	}
}

TEST(parameter_lifecycle, xml_pool_failure_after_parsing_frees_nodes_and_preserves_reader_position) {
	check_loading_pool_failure<XMLDeserializer>(false);
}
TEST(parameter_lifecycle, json_pool_failure_after_parsing_frees_nodes_and_preserves_reader_position) {
	check_loading_pool_failure<JsonDeserializer>(true);
}

TEST(parameter_lifecycle, scalar_knob_indicator_uses_noncreating_lookup_and_correct_bipolar_display) {
	fixture f;
	knob_lookup controls(f);
	view_for_session().activeModControllableModelStack.modControllable = &controls;
	view_for_session().modPos = 0;
	for (int32_t id :
	     {int32_t(deluge::modulation::params::UNPATCHED_PAN), int32_t(deluge::modulation::params::UNPATCHED_VOLUME)}) {
		controls.param_id = id;
		for (int32_t value : {INT32_MIN, 0, INT32_MAX}) {
			f.set().setCurrentValueBasicForSetup(id, value);
			fail_allocations failure;
			const auto calls = parameter_test::indicator_calls;
			view_for_session().setKnobIndicatorLevel(1);
			LONGS_EQUAL(calls + 1, parameter_test::indicator_calls);
			LONGS_EQUAL(1, parameter_test::indicator_knob);
			LONGS_EQUAL(value == INT32_MIN ? 0 : value == 0 ? 64 : 128, parameter_test::indicator_level);
			CHECK_EQUAL(id == deluge::modulation::params::UNPATCHED_PAN, parameter_test::indicator_bipolar);
			POINTERS_EQUAL(nullptr, f.set().getParam(id, false));
			LONGS_EQUAL(value, f.set().getValue(id));
			LONGS_EQUAL(0, auto_param_pool::get().active_count());
			LONGS_EQUAL(0, parameter_test::allocation_failures);
		}
	}
	LONGS_EQUAL(6, controls.lookups);
	view_for_session().activeModControllableModelStack.modControllable = nullptr;
}

TEST(parameter_lifecycle, clearing_full_idle_cache_preserves_active_automation_and_allows_more_acquisitions) {
	auto& pool = auto_param_pool::get();
	std::array<AutoParam*, 80> objects{};
	for (auto& object : objects) {
		object = pool.acquire();
		CHECK(object);
	}
	auto* active = objects[0];
	active->setCurrentValueBasicForSetup(17);
	CHECK(active->setNodeAtPos(4, 400, true) >= 0);
	for (size_t index = 1; index < objects.size(); ++index)
		pool.release(objects[index]);
	LONGS_EQUAL(32, pool.cached_count());
	LONGS_EQUAL(1, pool.active_count());
	pool.clear_unused();
	pool.clear_unused(); // Draining an empty cache is harmless.
	LONGS_EQUAL(0, pool.cached_count());
	LONGS_EQUAL(1, pool.active_count());
	LONGS_EQUAL(2, parameter_test::outstanding_allocations()); // Active object and its nodes.
	LONGS_EQUAL(17, active->getCurrentValue());
	check_node(*active, 0, 4, 400, true);
	auto* another = pool.acquire();
	CHECK(another);
	CHECK(another != active);
	CHECK_FALSE(another->isAutomated());
	pool.release(another);
	check_node(*active, 0, 4, 400, true);
	pool.release(active);
	LONGS_EQUAL(0, pool.active_count());
}

namespace {
struct patch_fixture {
	ParamManagerForTimeline manager;
	alignas(ModelStackWithAutoParam) char stack_memory[MODEL_STACK_MAX_SIZE]{};
	patch_fixture() {
		parameter_test::allow_patch_cables = true;
		CHECK(manager.setupWithPatching() == Error::NONE);
	}
	PatchCableSet& set() { return *manager.getPatchCableSet(); }
	ParamCollectionSummary& summary() { return *manager.getPatchCableSetSummary(); }
	ModelStackWithParamCollection* stack() {
		return setupModelStackWithSong(stack_memory, nullptr)
		    ->addTimelineCounter(nullptr)
		    ->addOtherTwoThingsButNoNoteRow(nullptr, &manager)
		    ->addParamCollection(&set(), &summary());
	}
	int32_t add_cable(PatchSource source, int32_t destination, int32_t value) {
		ParamDescriptor descriptor;
		descriptor.setToHaveParamOnly(destination);
		auto index = set().getPatchCableIndex(source, descriptor, nullptr, true);
		CHECK(index != 255);
		set().patch_cables_[index]->set_current_value(value);
		set().setupPatching(stack());
		return PatchCableSet::getParamId(descriptor, source);
	}
	PatchCable& cable(int32_t id) {
		ParamDescriptor descriptor;
		PatchSource source;
		PatchCableSet::dissectParamId(id, &descriptor, &source);
		auto index = set().getPatchCableIndex(source, descriptor);
		CHECK(index != 255);
		return *set().patch_cables_[index];
	}
	ModelStackWithAutoParam* param(int32_t id, bool create = true) {
		return set().getAutoParamFromId(stack()->addParamId(id), create);
	}
	void add_node(int32_t id, int32_t pos, int32_t value) {
		auto* context = param(id);
		CHECK(context->autoParam);
		bool was_automated = context->autoParam->isAutomated();
		CHECK(context->autoParam->setNodeAtPos(pos, value, false) >= 0);
		set().notifyParamModifiedInSomeWay(context, set().get_current_value(id), true, was_automated, true);
	}
	void check_ownership() {
		size_t automated = 0;
		std::set<AutoParam*> pointers;
		std::set<PatchCable*> cable_pointers;
		for (int32_t index = 0; index < set().numPatchCables; ++index) {
			CHECK(set().patch_cables_[index]);
			CHECK(cable_pointers.insert(set().patch_cables_[index]).second);
			auto& cable = *set().patch_cables_[index];
			auto* param = cable.get_auto_param();
			check_flag(summary(), index, index < set().numUsablePatchCables && cable.is_automated());
			if (param) {
				CHECK(pointers.insert(param).second);
				LONGS_EQUAL(cable.get_current_value(), param->getCurrentValue());
				++automated;
			}
		}
		for (int32_t index = set().numPatchCables; index < kMaxNumPatchCables; ++index) {
			POINTERS_EQUAL(nullptr, set().patch_cables_[index]);
			check_flag(summary(), index, false);
		}
		CHECK(automated <= auto_param_pool::get().active_count());
	}
};
} // namespace

TEST(parameter_lifecycle, patch_cable_scalars_do_not_allocate_automation) {
	patch_fixture f;
	for (int32_t index = 0; index < kMaxNumPatchCables; ++index) {
		auto id = f.add_cable(PatchSource::VELOCITY, index, index + 1);
		LONGS_EQUAL(index + 1, f.set().get_current_value(id));
		CHECK(f.set().has_current_value(id));
		POINTERS_EQUAL(nullptr, f.param(id, false)->autoParam);
	}
	LONGS_EQUAL(0, auto_param_pool::get().active_count());
	f.check_ownership();
}

TEST(parameter_lifecycle, patch_cable_reordering_and_compaction_rebind_scalar_owners) {
	patch_fixture f;
	auto first = f.add_cable(PatchSource::VELOCITY, 0, 11);
	auto middle = f.add_cable(PatchSource::NOTE, 1, 22);
	auto last = f.add_cable(PatchSource::AFTERTOUCH, 0, 33);
	f.add_node(first, 0, 101);
	f.add_node(middle, 4, 202);
	f.add_node(last, 8, 303);
	auto* first_param = f.param(first)->autoParam;
	auto* last_param = f.param(last)->autoParam;
	f.set().setupPatching(f.stack());
	f.check_ownership();
	POINTERS_EQUAL(first_param, f.param(first)->autoParam);
	POINTERS_EQUAL(last_param, f.param(last)->autoParam);
	auto middle_index = std::find(f.set().patch_cables_.begin(), f.set().patch_cables_.end(), &f.cable(middle))
	                    - f.set().patch_cables_.begin();
	f.set().deletePatchCable(f.stack(), middle_index);
	f.check_ownership();
	LONGS_EQUAL(2, auto_param_pool::get().active_count());
	first_param->setCurrentValueBasicForSetup(111);
	last_param->setCurrentValueBasicForSetup(333);
	LONGS_EQUAL(111, f.set().get_current_value(first));
	LONGS_EQUAL(333, f.set().get_current_value(last));
	auto replacement = f.add_cable(PatchSource::RANDOM, 2, 44);
	f.add_node(replacement, 12, 404);
	f.check_ownership();
	check_node(*first_param, 0, 0, 101, false);
	check_node(*last_param, 0, 8, 303, false);
}

TEST(parameter_lifecycle, patch_cable_final_automation_removal_keeps_nonzero_scalar) {
	patch_fixture f;
	auto id = f.add_cable(PatchSource::VELOCITY, 0, 42);
	f.add_node(id, 0, 99);
	auto* context = f.param(id);
	context->autoParam->deleteAutomation(nullptr, context);
	LONGS_EQUAL(42, f.set().get_current_value(id));
	LONGS_EQUAL(1, f.set().numPatchCables);
	POINTERS_EQUAL(nullptr, f.param(id, false)->autoParam);
	LONGS_EQUAL(0, auto_param_pool::get().active_count());
	f.check_ownership();
}

TEST(parameter_lifecycle, patch_cable_zero_value_deletion_releases_and_compacts) {
	patch_fixture f;
	auto id = f.add_cable(PatchSource::VELOCITY, 0, 0);
	auto survivor = f.add_cable(PatchSource::NOTE, 1, 42);
	f.add_node(id, 0, 99);
	f.add_node(survivor, 4, 123);
	auto* survivor_param = f.param(survivor)->autoParam;
	auto* context = f.param(id);
	context->autoParam->deleteAutomation(nullptr, context);
	CHECK_FALSE(f.set().has_current_value(id));
	POINTERS_EQUAL(survivor_param, f.param(survivor)->autoParam);
	survivor_param->setCurrentValueBasicForSetup(100);
	LONGS_EQUAL(100, f.set().get_current_value(survivor));
	LONGS_EQUAL(1, auto_param_pool::get().active_count());
	f.check_ownership();
}

TEST(parameter_lifecycle, patch_cable_undo_reacquires_automation_and_preserves_polarity) {
	patch_fixture f;
	auto id = f.add_cable(PatchSource::VELOCITY, 0, 42);
	f.cable(id).polarity = Polarity::UNIPOLAR;
	f.add_node(id, 4, 99);
	ConsequenceParamChange undo(f.param(id), false);
	auto* context = f.param(id);
	context->autoParam->deleteAutomation(nullptr, context);
	POINTERS_EQUAL(nullptr, f.param(id, false)->autoParam);
	patch_cable_pool::get().clear_unused();
	auto_param_pool::get().clear_unused();
	parameter_test::allocations_before_failure = 0;
	CHECK(undo.revert(BEFORE, nullptr) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(42, f.set().get_current_value(id));
	parameter_test::allocations_before_failure = -1;
	CHECK(undo.revert(BEFORE, nullptr) == Error::NONE);
	check_node(*f.param(id)->autoParam, 0, 4, 99, false);
	CHECK(f.cable(id).polarity == Polarity::UNIPOLAR);
	f.check_ownership();
	CHECK(undo.revert(AFTER, nullptr) == Error::NONE);
	POINTERS_EQUAL(nullptr, f.param(id, false)->autoParam);
	f.check_ownership();
}

TEST(parameter_lifecycle, patch_cable_pool_failure_does_not_create_a_route) {
	patch_fixture f;
	ParamDescriptor descriptor;
	descriptor.setToHaveParamOnly(0);
	auto id = PatchCableSet::getParamId(descriptor, PatchSource::VELOCITY);
	parameter_test::allocations_before_failure = 0;
	POINTERS_EQUAL(nullptr, f.param(id)->autoParam);
	CHECK_FALSE(f.set().has_current_value(id));
	LONGS_EQUAL(0, f.set().numPatchCables);
	parameter_test::allocations_before_failure = -1;
	CHECK(f.param(id)->autoParam);
	f.check_ownership();
}

TEST(parameter_lifecycle, patch_cable_clones_own_distinct_automation_and_scalars) {
	patch_fixture source;
	auto id = source.add_cable(PatchSource::VELOCITY, 0, 42);
	source.cable(id).polarity = Polarity::UNIPOLAR;
	source.add_node(id, 4, 99);
	for (bool copy_automation : {false, true}) {
		ParamManagerForTimeline clone;
		CHECK(clone.cloneParamCollectionsFrom(&source.manager, copy_automation, false) == Error::NONE);
		auto* set = clone.getPatchCableSet();
		LONGS_EQUAL(42, set->get_current_value(id));
		CHECK(set->patch_cables_[0]->polarity == Polarity::UNIPOLAR);
		auto* cloned_param = set->patch_cables_[0]->get_auto_param();
		CHECK_EQUAL(copy_automation, cloned_param != nullptr);
		if (cloned_param) {
			CHECK(cloned_param != source.param(id)->autoParam);
			check_node(*cloned_param, 0, 4, 99, false);
			cloned_param->setCurrentValueBasicForSetup(77);
			LONGS_EQUAL(77, set->get_current_value(id));
		}
		LONGS_EQUAL(42, source.set().get_current_value(id));
	}
	source.check_ownership();
}

TEST(parameter_lifecycle, patch_cable_delete_all_releases_each_automated_owner) {
	patch_fixture f;
	for (int32_t index = 0; index < 6; ++index) {
		auto id = f.add_cable(PatchSource::VELOCITY, index, 10 + index);
		f.add_node(id, index, index * 10);
	}
	f.set().deleteAllAutomation(nullptr, f.stack());
	LONGS_EQUAL(0, auto_param_pool::get().active_count());
	LONGS_EQUAL(6, f.set().numPatchCables);
	f.check_ownership();
}

TEST(parameter_lifecycle, patch_cable_append_allocates_first_automation_and_updates_flags) {
	patch_fixture source, destination;
	auto id = source.add_cable(PatchSource::VELOCITY, 0, 42);
	destination.add_cable(PatchSource::VELOCITY, 0, 17);
	source.add_node(id, 4, 99);
	destination.set().appendParamCollection(destination.stack(), source.stack(), 32, 0, false);
	CHECK(destination.cable(id).is_automated());
	check_node(*destination.param(id)->autoParam, 0, 36, 99, false);
	LONGS_EQUAL(17, destination.set().get_current_value(id));
	destination.check_ownership();
	source.check_ownership();
}

TEST(parameter_lifecycle, patch_cable_clone_allocation_failures_do_not_alias_source) {
	patch_fixture source;
	auto id = source.add_cable(PatchSource::VELOCITY, 0, 42);
	source.add_node(id, 4, 99);
	auto* source_param = source.param(id)->autoParam;
	for (int budget = 0; budget < 12; ++budget) {
		patch_cable_pool::get().clear_unused();
		auto_param_pool::get().clear_unused();
		size_t baseline = parameter_test::outstanding_allocations();
		{
			ParamManagerForTimeline clone;
			fail_allocations failure(budget, false);
			auto result = clone.cloneParamCollectionsFrom(&source.manager, true, false);
			if (result == Error::NONE) {
				auto* set = clone.getPatchCableSet();
				LONGS_EQUAL(42, set->get_current_value(id));
				auto* param = set->patch_cables_[0]->get_auto_param();
				if (param) {
					CHECK(param != source_param);
					check_node(*param, 0, 4, 99, false);
					param->setCurrentValueBasicForSetup(88);
				}
				check_flag(*clone.getPatchCableSetSummary(), 0, param != nullptr);
			}
		}
		patch_cable_pool::get().clear_unused();
		auto_param_pool::get().clear_unused();
		LONGS_EQUAL(baseline, parameter_test::outstanding_allocations());
		LONGS_EQUAL(42, source.set().get_current_value(id));
		check_node(*source_param, 0, 4, 99, false);
		source.check_ownership();
	}
}

TEST(parameter_lifecycle, patch_cable_setup_allocation_failure_is_safe_and_retryable) {
	patch_fixture f;
	auto id = f.add_cable(PatchSource::VELOCITY, 0, 42);
	f.add_node(id, 4, 99);
	auto* param = f.param(id)->autoParam;
	for (int budget : {0, 1}) {
		{
			fail_allocations failure(budget);
			f.set().setupPatching(f.stack());
		}
		LONGS_EQUAL(0, f.set().numUsablePatchCables);
		CHECK_FALSE(f.summary().containsAutomation());
		CHECK_FALSE(f.set().isSourcePatchedToSomething(PatchSource::VELOCITY));
		POINTERS_EQUAL(param, f.param(id)->autoParam);
		f.set().setupPatching(f.stack());
		LONGS_EQUAL(1, f.set().numUsablePatchCables);
		f.check_ownership();
	}
}

TEST(parameter_lifecycle, patch_cable_inactive_range_automation_clones_without_sharing) {
	patch_fixture source;
	ParamDescriptor descriptor;
	descriptor.setToHaveParamAndSource(0, PatchSource::VELOCITY);
	auto id = PatchCableSet::getParamId(descriptor, PatchSource::NOTE);
	source.add_node(id, 4, 99);
	LONGS_EQUAL(0, source.set().numUsablePatchCables);
	CHECK_FALSE(source.summary().containsAutomation());
	ParamManagerForTimeline clone;
	CHECK(clone.cloneParamCollectionsFrom(&source.manager, true, false) == Error::NONE);
	auto* cloned_param = clone.getPatchCableSet()->patch_cables_[0]->get_auto_param();
	CHECK(cloned_param);
	CHECK(cloned_param != source.param(id)->autoParam);
	check_node(*cloned_param, 0, 4, 99, false);
	source.add_cable(PatchSource::VELOCITY, 0, 42);
	LONGS_EQUAL(2, source.set().numUsablePatchCables);
	source.check_ownership();
}

namespace {
template <class Writer, class Reader>
void check_patch_persistence(bool json) {
	patch_fixture source, destination;
	auto id = source.add_cable(PatchSource::VELOCITY, 0, 42);
	source.cable(id).polarity = Polarity::UNIPOLAR;
	source.add_node(id, 4, 99);
	auto scalar_id = source.add_cable(PatchSource::NOTE, 1, 123);
	ParamDescriptor descriptor;
	descriptor.setToHaveParamAndSource(0, PatchSource::VELOCITY);
	auto range_id = PatchCableSet::getParamId(descriptor, PatchSource::RANDOM);
	source.add_node(range_id, 8, 77);
	source.cable(range_id).set_current_value(55);
	for (bool save_nodes : {true, false}) {
		Writer writer;
		writer.writeOpeningTagBeginning(json ? nullptr : "params");
		writer.writeOpeningTagEnd();
		source.set().writePatchCablesToFile(writer, save_nodes);
		writer.writeTag("sentinel", 73);
		writer.writeClosingTag(json ? nullptr : "params");
		std::string document(writer.getBufferPtr(), writer.bytesWritten());
		for (bool load_nodes : {true, false}) {
			auto replaced = destination.add_cable(PatchSource::AFTERTOUCH, 2, 200);
			destination.add_node(replaced, 0, 444);
			native_parameter_tests::file_contents = document;
			Reader reader;
			if (json)
				CHECK(reader.match('{'));
			else
				STRCMP_EQUAL("params", reader.readNextTagOrAttributeName());
			STRCMP_EQUAL("patchCables", reader.readNextTagOrAttributeName());
			destination.set().readPatchCablesFromFile(reader, load_nodes ? 32 : 0);
			reader.exitTag("patchCables");
			STRCMP_EQUAL("sentinel", reader.readNextTagOrAttributeName());
			LONGS_EQUAL(73, reader.readTagOrAttributeValueInt());
			destination.set().setupPatching(destination.stack());
			LONGS_EQUAL(3, destination.set().numPatchCables);
			LONGS_EQUAL(42, destination.set().get_current_value(id));
			LONGS_EQUAL(123, destination.set().get_current_value(scalar_id));
			LONGS_EQUAL(55, destination.set().get_current_value(range_id));
			CHECK(destination.cable(id).polarity == Polarity::UNIPOLAR);
			CHECK(destination.cable(range_id).polarity == source.cable(range_id).polarity);
			CHECK_EQUAL(save_nodes && load_nodes, destination.cable(id).is_automated());
			CHECK_EQUAL(save_nodes && load_nodes, destination.cable(range_id).is_automated());
			POINTERS_EQUAL(nullptr, destination.param(scalar_id, false)->autoParam);
			if (save_nodes && load_nodes) {
				check_node(*destination.param(id)->autoParam, 0, 4, 99, false);
				check_node(*destination.param(range_id)->autoParam, 0, 8, 77, false);
			}
			destination.check_ownership();
			source.check_ownership();
		}
	}
	native_parameter_tests::file_contents = {};
}
} // namespace

TEST(parameter_lifecycle, patch_cable_xml_round_trip_preserves_scalars_ranges_and_automation) {
	check_patch_persistence<XMLSerializer, XMLDeserializer>(false);
}
TEST(parameter_lifecycle, patch_cable_json_round_trip_preserves_scalars_ranges_and_automation) {
	check_patch_persistence<JsonSerializer, JsonDeserializer>(true);
}

TEST(parameter_lifecycle, patch_cable_trim_and_nudge_release_all_empty_owners) {
	for (bool trim : {true, false}) {
		patch_fixture f;
		std::array<int32_t, 3> ids;
		for (int32_t index = 0; index < 3; ++index) {
			ids[index] = f.add_cable(PatchSource::VELOCITY, index, 10 + index);
			f.add_node(ids[index], 0, 100 + index);
			f.add_node(ids[index], 31, 200 + index);
		}
		if (trim)
			f.set().trimToLength(8, f.stack(), nullptr, true);
		else
			f.set().nudgeNonInterpolatingNodesAtPos(31, 1, 32, nullptr, f.stack());
		LONGS_EQUAL(0, auto_param_pool::get().active_count());
		for (auto id : ids)
			POINTERS_EQUAL(nullptr, f.param(id, false)->autoParam);
		f.check_ownership();
	}
}

TEST(parameter_lifecycle, patch_cable_removing_destination_does_not_skip_reordered_cables) {
	patch_fixture f;
	auto first = f.add_cable(PatchSource::VELOCITY, 0, 10);
	auto survivor = f.add_cable(PatchSource::NOTE, 1, 20);
	auto last = f.add_cable(PatchSource::RANDOM, 0, 30);
	for (auto id : {first, survivor, last})
		f.add_node(id, 4, 100);
	auto* survivor_param = f.param(survivor)->autoParam;
	f.set().removeAllPatchingToParam(f.stack(), 0);
	LONGS_EQUAL(1, f.set().numPatchCables);
	POINTERS_EQUAL(survivor_param, f.param(survivor)->autoParam);
	LONGS_EQUAL(1, auto_param_pool::get().active_count());
	f.check_ownership();
}

TEST(parameter_lifecycle, patch_cable_scalar_undo_uses_no_pool_object) {
	patch_fixture f;
	auto id = f.add_cable(PatchSource::VELOCITY, 0, 42);
	ConsequenceParamChange undo(f.param(id, false), false);
	f.cable(id).set_current_value(99);
	{
		fail_allocations failure;
		CHECK(undo.revert(BEFORE, nullptr) == Error::NONE);
		LONGS_EQUAL(42, f.set().get_current_value(id));
		CHECK(undo.revert(AFTER, nullptr) == Error::NONE);
		LONGS_EQUAL(99, f.set().get_current_value(id));
		LONGS_EQUAL(0, parameter_test::allocation_failures);
	}
	LONGS_EQUAL(0, auto_param_pool::get().active_count());
	f.check_ownership();
}

TEST(parameter_lifecycle, patch_cable_pool_storage_can_be_reused_by_parameter_sets) {
	patch_fixture f;
	fixture other;
	auto id = f.add_cable(PatchSource::VELOCITY, 0, 42);
	f.add_node(id, 4, 99);
	auto* context = f.param(id);
	auto* recycled = context->autoParam;
	context->autoParam->deleteAutomation(nullptr, context);
	other.add_node(32, 8, 123);
	POINTERS_EQUAL(recycled, other.param(32)->autoParam);
	other.param(32)->autoParam->setCurrentValueBasicForSetup(17);
	LONGS_EQUAL(42, f.set().get_current_value(id));
	LONGS_EQUAL(17, other.set().getValue(32));
	f.add_node(id, 12, 444);
	CHECK(recycled != f.param(id)->autoParam);
	check_node(*recycled, 0, 8, 123, false);
	f.check_ownership();
}

TEST(parameter_lifecycle, patch_cable_pool_empty_sets_have_no_cable_allocations) {
	patch_fixture f;
	LONGS_EQUAL(0, patch_cable_pool::get().active_count());
	for (auto* cable : f.set().patch_cables_)
		POINTERS_EQUAL(nullptr, cable);
	ParamDescriptor descriptor;
	descriptor.setToHaveParamOnly(0);
	{
		fail_allocations failure;
		LONGS_EQUAL(255, f.set().getPatchCableIndex(PatchSource::VELOCITY, descriptor, nullptr, true));
		CHECK(f.set().setup_cable(PatchSource::VELOCITY, 0, 42) == Error::INSUFFICIENT_RAM);
	}
	LONGS_EQUAL(0, f.set().numPatchCables);
	f.check_ownership();
	CHECK(f.set().setup_cable(PatchSource::VELOCITY, 0, 42) == Error::NONE);
	LONGS_EQUAL(1, patch_cable_pool::get().active_count());
	LONGS_EQUAL(42, f.set().patch_cables_[0]->get_current_value());
	LONGS_EQUAL(0, auto_param_pool::get().active_count());
}

TEST(parameter_lifecycle, patch_cable_pool_failure_releases_reserved_automation) {
	patch_fixture f;
	ParamDescriptor descriptor;
	descriptor.setToHaveParamOnly(0);
	auto id = PatchCableSet::getParamId(descriptor, PatchSource::VELOCITY);
	{
		fail_allocations failure(1);
		// The AutoParam reservation succeeds, then the cable allocation fails.
		POINTERS_EQUAL(nullptr, f.param(id)->autoParam);
		LONGS_EQUAL(sizeof(PatchCable), parameter_test::last_failed_allocation_size);
	}
	LONGS_EQUAL(0, patch_cable_pool::get().active_count());
	LONGS_EQUAL(0, auto_param_pool::get().active_count());
	LONGS_EQUAL(0, f.set().numPatchCables);
	f.add_node(id, 4, 99);
	f.check_ownership();
}

TEST(parameter_lifecycle, pooled_cables_keep_addresses_through_sorting_compaction_and_reuse) {
	patch_fixture f;
	auto first = f.add_cable(PatchSource::VELOCITY, 0, 11);
	auto middle = f.add_cable(PatchSource::NOTE, 1, 22);
	auto* first_address = &f.cable(first);
	auto* middle_address = &f.cable(middle);
	f.add_node(first, 4, 111);
	auto last = f.add_cable(PatchSource::RANDOM, 0, 33);
	auto* last_address = &f.cable(last);
	f.add_node(last, 8, 333);
	for (int repeat = 0; repeat < 3; ++repeat) {
		f.set().setupPatching(f.stack());
		POINTERS_EQUAL(first_address, &f.cable(first));
		POINTERS_EQUAL(middle_address, &f.cable(middle));
		POINTERS_EQUAL(last_address, &f.cable(last));
	}
	f.set().removeAllPatchingToParam(f.stack(), 1);
	POINTERS_EQUAL(first_address, &f.cable(first));
	POINTERS_EQUAL(last_address, &f.cable(last));
	auto replacement = f.add_cable(PatchSource::AFTERTOUCH, 2, 44);
	POINTERS_EQUAL(middle_address, &f.cable(replacement));
	first_address->get_auto_param()->setCurrentValueBasicForSetup(1111);
	LONGS_EQUAL(1111, f.set().get_current_value(first));
	LONGS_EQUAL(44, f.set().get_current_value(replacement));
	f.check_ownership();
}

TEST(parameter_lifecycle, patch_cable_pool_clear_releases_inactive_cables_and_automation) {
	patch_fixture f;
	ParamDescriptor descriptor;
	descriptor.setToHaveParamAndSource(0, PatchSource::VELOCITY);
	auto id = PatchCableSet::getParamId(descriptor, PatchSource::NOTE);
	f.add_node(id, 4, 99);
	LONGS_EQUAL(1, patch_cable_pool::get().active_count());
	LONGS_EQUAL(0, f.set().numUsablePatchCables);
	f.set().clear_cables(&f.summary());
	f.set().clear_cables(&f.summary());
	LONGS_EQUAL(0, patch_cable_pool::get().active_count());
	LONGS_EQUAL(0, auto_param_pool::get().active_count());
	f.check_ownership();
}

TEST(parameter_lifecycle, patch_cable_pool_cache_is_bounded_and_reuse_resets_every_owner) {
	auto& pool = patch_cable_pool::get();
	std::array<PatchCable*, 40> cables;
	int32_t range_value = 16;
	for (auto*& cable : cables) {
		cable = pool.acquire();
		CHECK(cable);
		cable->setup(PatchSource::AFTERTOUCH, 0, 123);
		cable->rangeAdjustmentPointer = &range_value;
		auto* param = cable->get_auto_param(true);
		CHECK(param);
		CHECK(param->setNodeAtPos(4, 99, false) >= 0);
	}
	LONGS_EQUAL(40, pool.active_count());
	for (auto* cable : cables)
		pool.release(cable);
	LONGS_EQUAL(0, pool.active_count());
	LONGS_EQUAL(32, pool.cached_count());
	LONGS_EQUAL(0, auto_param_pool::get().active_count());
	{
		fail_allocations failure(0, false);
		auto* reused = pool.acquire();
		CHECK(reused);
		CHECK(reused->from == PatchSource::NONE);
		CHECK(reused->polarity == Polarity::BIPOLAR);
		LONGS_EQUAL(0, reused->destinationParamDescriptor.data);
		LONGS_EQUAL(0, reused->get_current_value());
		POINTERS_EQUAL(nullptr, reused->get_auto_param());
		POINTERS_EQUAL(nullptr, reused->rangeAdjustmentPointer);
		pool.release(reused);
		LONGS_EQUAL(0, parameter_test::allocation_failures);
	}
	auto* active = pool.acquire();
	active->setup(PatchSource::VELOCITY, 1, 77);
	pool.clear_unused();
	pool.clear_unused();
	LONGS_EQUAL(1, pool.active_count());
	LONGS_EQUAL(77, active->get_current_value());
	pool.release(active);
}

TEST(parameter_lifecycle, patch_cable_pool_full_set_failure_preserves_all_routes) {
	patch_fixture f;
	for (int32_t index = 0; index < kMaxNumPatchCables; ++index)
		CHECK(f.set().setup_cable(PatchSource::VELOCITY, index, index + 1) == Error::NONE);
	auto pointers = f.set().patch_cables_;
	CHECK(f.set().setup_cable(PatchSource::NOTE, 0, 55) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(kMaxNumPatchCables, patch_cable_pool::get().active_count());
	for (int32_t index = 0; index < kMaxNumPatchCables; ++index) {
		POINTERS_EQUAL(pointers[index], f.set().patch_cables_[index]);
		LONGS_EQUAL(index + 1, pointers[index]->get_current_value());
	}
	f.check_ownership();
}

TEST(parameter_lifecycle, patch_cable_clone_failure_preserves_populated_destination_and_expression) {
	patch_fixture source, destination;
	auto source_id = source.add_cable(PatchSource::VELOCITY, 0, 42);
	auto destination_id = destination.add_cable(PatchSource::NOTE, 1, 17);
	source.add_node(source_id, 4, 99);
	destination.add_node(destination_id, 8, 88);
	auto* retained_cable = &destination.cable(destination_id);
	auto* retained_expression = destination.manager.getOrCreateExpressionParamSet();
	CHECK(retained_expression);
	bool reached_success = false;
	for (int budget = 0; budget < 16 && !reached_success; ++budget) {
		Error error;
		{
			fail_allocations failure(budget);
			error = destination.manager.cloneParamCollectionsFrom(&source.manager, true, true);
		}
		if (error != Error::NONE) {
			CHECK(error == Error::INSUFFICIENT_RAM);
			POINTERS_EQUAL(retained_cable, &destination.cable(destination_id));
			POINTERS_EQUAL(retained_expression, destination.manager.getExpressionParamSet());
			check_node(*destination.param(destination_id)->autoParam, 0, 8, 88, false);
			LONGS_EQUAL(2, patch_cable_pool::get().active_count());
		}
		else {
			reached_success = true;
			CHECK(&destination.cable(source_id) != &source.cable(source_id));
			check_node(*destination.param(source_id)->autoParam, 0, 4, 99, false);
		}
		source.check_ownership();
		destination.check_ownership();
		check_node(*source.param(source_id)->autoParam, 0, 4, 99, false);
	}
	CHECK(reached_success);
}

TEST(parameter_lifecycle, patch_cable_shallow_manager_clone_failure_never_releases_source) {
	patch_fixture source;
	auto id = source.add_cable(PatchSource::VELOCITY, 0, 42);
	source.add_node(id, 4, 99);
	CHECK(source.manager.getOrCreateExpressionParamSet());
	auto* source_cable = &source.cable(id);
	bool reached_success = false;
	for (int budget = 0; budget < 16 && !reached_success; ++budget) {
		{
			ParamManagerForTimeline clone;
			memcpy(&clone, &source.manager, sizeof(clone));
			Error error;
			{
				fail_allocations failure(budget);
				error = clone.beenCloned(32);
			}
			if (error == Error::NONE) {
				reached_success = true;
				auto* cable = clone.getPatchCableSet()->patch_cables_[0];
				CHECK(cable != source_cable);
				LONGS_EQUAL(42, cable->get_current_value());
				check_node(*cable->get_auto_param(), 0, 28, 99, false);
			}
			else {
				CHECK(error == Error::INSUFFICIENT_RAM);
				CHECK(clone.has_valid_layout());
				POINTERS_EQUAL(nullptr, clone.summaries[0].paramCollection);
			}
		}
		POINTERS_EQUAL(source_cable, &source.cable(id));
		LONGS_EQUAL(1, patch_cable_pool::get().active_count());
		check_node(*source.param(id)->autoParam, 0, 4, 99, false);
	}
	CHECK(reached_success);
}

namespace {
template <class Writer, class Reader>
void check_cable_allocation_failure_on_load(bool json) {
	patch_fixture source, destination;
	auto id = source.add_cable(PatchSource::VELOCITY, 0, 42);
	ParamDescriptor descriptor;
	descriptor.setToHaveParamAndSource(0, PatchSource::VELOCITY);
	auto range_id = PatchCableSet::getParamId(descriptor, PatchSource::NOTE);
	auto index = source.set().getPatchCableIndex(PatchSource::NOTE, descriptor, nullptr, true);
	CHECK(index != 255);
	source.cable(range_id).set_current_value(17);
	Writer writer;
	writer.writeOpeningTagBeginning(json ? nullptr : "params");
	writer.writeOpeningTagEnd();
	source.set().writePatchCablesToFile(writer, false);
	writer.writeTag("sentinel", 73);
	writer.writeClosingTag(json ? nullptr : "params");
	std::string document(writer.getBufferPtr(), writer.bytesWritten());
	for (int budget : {0, 1, 2}) {
		destination.set().clear_cables(&destination.summary());
		native_parameter_tests::file_contents = document;
		Reader reader;
		if (json)
			CHECK(reader.match('{'));
		else
			STRCMP_EQUAL("params", reader.readNextTagOrAttributeName());
		STRCMP_EQUAL("patchCables", reader.readNextTagOrAttributeName());
		{
			fail_allocations failure(budget);
			destination.set().readPatchCablesFromFile(reader, 32);
		}
		reader.exitTag("patchCables");
		STRCMP_EQUAL("sentinel", reader.readNextTagOrAttributeName());
		LONGS_EQUAL(73, reader.readTagOrAttributeValueInt());
		destination.set().setupPatching(destination.stack());
		LONGS_EQUAL(budget == 2 ? 2 : 0, destination.set().numPatchCables);
		LONGS_EQUAL(budget == 2 ? 4 : 2, patch_cable_pool::get().active_count());
		if (budget == 2) {
			LONGS_EQUAL(42, destination.set().get_current_value(id));
			LONGS_EQUAL(17, destination.set().get_current_value(range_id));
		}
		destination.check_ownership();
		source.check_ownership();
	}
	native_parameter_tests::file_contents = {};
}
} // namespace
TEST(parameter_lifecycle, patch_cable_pool_xml_load_failure_discards_orphan_ranges) {
	check_cable_allocation_failure_on_load<XMLSerializer, XMLDeserializer>(false);
}
TEST(parameter_lifecycle, patch_cable_pool_json_load_failure_discards_orphan_ranges) {
	check_cable_allocation_failure_on_load<JsonSerializer, JsonDeserializer>(true);
}

TEST(parameter_lifecycle, pooled_cable_clone_outlives_source_and_source_storage_reuse) {
	ParamManagerForTimeline clone;
	PatchCable* source_address;
	{
		patch_fixture source;
		auto id = source.add_cable(PatchSource::VELOCITY, 0, 42);
		source.add_node(id, 4, 99);
		source_address = &source.cable(id);
		CHECK(clone.cloneParamCollectionsFrom(&source.manager, true) == Error::NONE);
		CHECK(clone.getPatchCableSet()->patch_cables_[0] != source_address);
	}
	auto* reused = patch_cable_pool::get().acquire();
	POINTERS_EQUAL(source_address, reused);
	reused->setup(PatchSource::NOTE, 1, 17);
	auto* cloned_cable = clone.getPatchCableSet()->patch_cables_[0];
	LONGS_EQUAL(42, cloned_cable->get_current_value());
	check_node(*cloned_cable->get_auto_param(), 0, 4, 99, false);
	patch_cable_pool::get().release(reused);
}

TEST(parameter_lifecycle, collection_clone_hook_failure_keeps_existing_destination) {
	patch_fixture source, destination;
	auto source_id = source.add_cable(PatchSource::VELOCITY, 0, 42);
	auto destination_id = destination.add_cable(PatchSource::NOTE, 1, 17);
	source.add_node(source_id, 4, 99);
	destination.add_node(destination_id, 8, 88);
	auto* original_set = &destination.set();
	auto* original_expression = destination.manager.getOrCreateExpressionParamSet();
	CHECK(original_expression);
	{
		// All three collection allocations succeed. A later allocation inside
		// the patch-cable clone hook fails, before the manager commits the clone.
		fail_allocations failure(5);
		CHECK(destination.manager.cloneParamCollectionsFrom(&source.manager, true, true) == Error::INSUFFICIENT_RAM);
	}
	POINTERS_EQUAL(original_set, &destination.set());
	POINTERS_EQUAL(original_expression, destination.manager.getExpressionParamSet());
	LONGS_EQUAL(17, destination.set().get_current_value(destination_id));
	check_node(*destination.param(destination_id)->autoParam, 0, 8, 88, false);
	LONGS_EQUAL(42, source.set().get_current_value(source_id));
	check_node(*source.param(source_id)->autoParam, 0, 4, 99, false);
	source.check_ownership();
	destination.check_ownership();
}

namespace {
struct midi_fixture {
	ParamManagerForTimeline manager;
	alignas(ModelStackWithAutoParam) char stack_memory[MODEL_STACK_MAX_SIZE]{};
	midi_fixture() {
		parameter_test::allow_midi_params = true;
		CHECK(manager.setupMIDI() == Error::NONE);
	}
	MIDIParamCollection& set() { return *manager.getMIDIParamCollection(); }
	ParamCollectionSummary& summary() { return *manager.getMIDIParamCollectionSummary(); }
	ModelStackWithParamCollection* stack() {
		return setupModelStackWithSong(stack_memory, nullptr)
		    ->addTimelineCounter(nullptr)
		    ->addOtherTwoThingsButNoNoteRow(nullptr, &manager)
		    ->addParamCollection(&set(), &summary());
	}
	MIDIParam& scalar(int32_t cc, int32_t value) {
		auto* owner = set().params.getOrCreateParamFromCC(cc);
		CHECK(owner);
		owner->set_current_value(value);
		return *owner;
	}
	ModelStackWithAutoParam* param(int32_t cc, bool create = true) {
		return set().getAutoParamFromId(stack()->addParamId(cc), create);
	}
	void add_node(int32_t cc, int32_t pos, int32_t value) {
		auto* context = param(cc);
		CHECK(context->autoParam);
		bool was_automated = context->autoParam->isAutomated();
		CHECK(context->autoParam->setNodeAtPos(pos, value, false) >= 0);
		set().notifyParamModifiedInSomeWay(context, set().get_current_value(cc), true, was_automated, true);
	}
	void check_ownership() {
		std::set<AutoParam*> pointers;
		int last_cc = -1;
		for (int32_t index = 0; index < set().params.getNumElements(); ++index) {
			auto* owner = set().params.getElement(index);
			CHECK(owner->cc > last_cc);
			last_cc = owner->cc;
			if (auto* param = owner->get_auto_param()) {
				CHECK(pointers.insert(param).second);
				LONGS_EQUAL(owner->get_current_value(), param->getCurrentValue());
			}
		}
	}
};
} // namespace

TEST(parameter_lifecycle, midi_scalars_do_not_reserve_auto_params) {
	midi_fixture f;
	for (int cc : {119, 0, 64, 1, 74}) {
		f.scalar(cc, cc * 100);
		CHECK(f.set().has_current_value(cc));
		LONGS_EQUAL(cc * 100, f.set().get_current_value(cc));
		POINTERS_EQUAL(nullptr, f.param(cc, false)->autoParam);
	}
	LONGS_EQUAL(0, auto_param_pool::get().active_count());
	auto count = f.set().params.getNumElements();
	POINTERS_EQUAL(nullptr, f.param(42, false)->autoParam);
	LONGS_EQUAL(count, f.set().params.getNumElements());
	f.check_ownership();
}

TEST(parameter_lifecycle, midi_vector_insertion_growth_and_deletion_rebind_scalar_owners) {
	midi_fixture f;
	f.scalar(119, 1190);
	f.add_node(119, 4, 11900);
	auto* retained = f.param(119)->autoParam;
	for (int cc = 118; cc >= 0; --cc) {
		f.scalar(cc, cc * 10);
		if (cc % 20 == 0)
			f.add_node(cc, 8, cc * 100);
		retained->setCurrentValueBasicForSetup(10000 + cc);
		LONGS_EQUAL(10000 + cc, f.set().get_current_value(119));
		f.check_ownership();
	}
	for (int cc = 0; cc < 119; cc += 2) {
		f.set().params.deleteAtKey(cc);
		CHECK_FALSE(f.set().has_current_value(cc));
		retained->setCurrentValueBasicForSetup(20000 + cc);
		LONGS_EQUAL(20000 + cc, f.set().get_current_value(119));
		f.check_ownership();
	}
	POINTERS_EQUAL(retained, f.param(119, false)->autoParam);
	check_node(*retained, 0, 4, 11900, false);
	LONGS_EQUAL(1, auto_param_pool::get().active_count());
}

TEST(parameter_lifecycle, midi_scalar_edit_and_final_node_deletion_release_automation) {
	midi_fixture f;
	f.scalar(7, 42);
	auto* context = f.param(7);
	context->autoParam->setCurrentValueWithNoReversionOrRecording(context, 99);
	LONGS_EQUAL(99, f.set().get_current_value(7));
	POINTERS_EQUAL(nullptr, f.param(7, false)->autoParam);
	CHECK_FALSE(parameter_test::midi_notifications.empty());
	LONGS_EQUAL(7, parameter_test::midi_notifications.back().cc);
	LONGS_EQUAL(99, parameter_test::midi_notifications.back().new_value);
	f.add_node(7, 4, 123);
	context = f.param(7);
	context->autoParam->valueIncrementPerHalfTick = 10;
	f.summary().whichParamsAreInterpolating[0] = 1;
	context->autoParam->deleteAutomation(nullptr, context);
	POINTERS_EQUAL(nullptr, f.param(7, false)->autoParam);
	LONGS_EQUAL(99, f.set().get_current_value(7));
	LONGS_EQUAL(0, f.summary().whichParamsAreInterpolating[0]);
	LONGS_EQUAL(0, auto_param_pool::get().active_count());
	LONGS_EQUAL(1, f.set().params.getNumElements());
}

TEST(parameter_lifecycle, midi_creation_failures_leave_no_partial_cc_entry) {
	midi_fixture f;
	for (int budget : {0, 1}) {
		{
			fail_allocations failure(budget);
			POINTERS_EQUAL(nullptr, f.param(7)->autoParam);
		}
		LONGS_EQUAL(0, f.set().params.getNumElements());
		LONGS_EQUAL(0, auto_param_pool::get().active_count());
	}
	f.scalar(7, 42);
	{
		fail_allocations failure;
		POINTERS_EQUAL(nullptr, f.param(7)->autoParam);
	}
	LONGS_EQUAL(42, f.set().get_current_value(7));
	f.add_node(7, 4, 123);
	f.check_ownership();
}

TEST(parameter_lifecycle, midi_scalar_and_automation_undo_preserve_owners_after_relocation) {
	midi_fixture f;
	f.scalar(100, 42);
	ConsequenceParamChange scalar_undo(f.param(100, false), false);
	f.scalar(100, 99);
	{
		fail_allocations failure;
		CHECK(scalar_undo.revert(BEFORE, nullptr) == Error::NONE);
		LONGS_EQUAL(42, f.set().get_current_value(100));
		CHECK(scalar_undo.revert(AFTER, nullptr) == Error::NONE);
		LONGS_EQUAL(99, f.set().get_current_value(100));
		LONGS_EQUAL(0, parameter_test::allocation_failures);
	}
	f.add_node(100, 4, 123);
	ConsequenceParamChange automation_undo(f.param(100), false);
	auto* context = f.param(100);
	context->autoParam->deleteAutomation(nullptr, context);
	for (int cc = 0; cc < 80; ++cc)
		f.scalar(cc, cc);
	{
		fail_allocations failure;
		CHECK(automation_undo.revert(BEFORE, nullptr) == Error::INSUFFICIENT_RAM);
	}
	CHECK(automation_undo.revert(BEFORE, nullptr) == Error::NONE);
	check_node(*f.param(100)->autoParam, 0, 4, 123, false);
	CHECK(automation_undo.revert(AFTER, nullptr) == Error::NONE);
	POINTERS_EQUAL(nullptr, f.param(100, false)->autoParam);
	LONGS_EQUAL(99, f.set().get_current_value(100));
	f.check_ownership();
}

TEST(parameter_lifecycle, midi_clones_own_nodes_and_scalars_after_source_destruction) {
	for (bool copy_automation : {false, true}) {
		ParamManagerForTimeline clone;
		{
			midi_fixture source;
			source.scalar(7, 42);
			source.scalar(10, 99);
			source.add_node(7, 4, 123);
			CHECK(clone.cloneParamCollectionsFrom(&source.manager, copy_automation, false, 32) == Error::NONE);
			if (copy_automation)
				CHECK(clone.getMIDIParamCollection()->params.getParamFromCC(7)->get_auto_param()
				      != source.param(7)->autoParam);
		}
		auto* owner = clone.getMIDIParamCollection()->params.getParamFromCC(7);
		LONGS_EQUAL(42, owner->get_current_value());
		CHECK_EQUAL(copy_automation, owner->is_automated());
		if (copy_automation) {
			check_node(*owner->get_auto_param(), 0, 28, 123, false);
			owner->get_auto_param()->setCurrentValueBasicForSetup(77);
			LONGS_EQUAL(77, owner->get_current_value());
		}
		LONGS_EQUAL(99, clone.getMIDIParamCollection()->get_current_value(10));
	}
}

TEST(parameter_lifecycle, midi_clone_allocation_failures_preserve_source_and_destination) {
	for (bool shallow : {false, true}) {
		midi_fixture source;
		source.scalar(7, 42);
		source.add_node(7, 4, 123);
		source.scalar(10, 99);
		source.add_node(10, 8, 456);
		CHECK(source.manager.getOrCreateExpressionParamSet());
		bool succeeded = false;
		for (int budget = 0; budget < 20 && !succeeded; ++budget) {
			ParamManagerForTimeline clone;
			if (shallow)
				memcpy(&clone, &source.manager, sizeof(clone));
			else
				CHECK(clone.setupUnpatched() == Error::NONE);
			auto* original_collection = clone.summaries[0].paramCollection;
			Error error;
			{
				fail_allocations failure(budget);
				error = shallow ? clone.beenCloned() : clone.cloneParamCollectionsFrom(&source.manager, true, true);
			}
			if (error == Error::NONE) {
				succeeded = true;
				check_node(*clone.getMIDIParamCollection()->params.getParamFromCC(10)->get_auto_param(), 0, 8, 456,
				           false);
			}
			else {
				CHECK(error == Error::INSUFFICIENT_RAM);
				if (shallow)
					POINTERS_EQUAL(nullptr, clone.summaries[0].paramCollection);
				else
					POINTERS_EQUAL(original_collection, clone.summaries[0].paramCollection);
			}
			source.check_ownership();
			check_node(*source.param(7)->autoParam, 0, 4, 123, false);
			check_node(*source.param(10)->autoParam, 0, 8, 456, false);
		}
		CHECK(succeeded);
	}
}

TEST(parameter_lifecycle, midi_append_trim_nudge_and_delete_all_release_empty_objects) {
	midi_fixture source, destination;
	source.scalar(7, 42);
	source.add_node(7, 0, 100);
	source.add_node(7, 31, 200);
	destination.scalar(7, 99);
	destination.set().appendParamCollection(destination.stack(), source.stack(), 32, 0, false);
	check_node(*destination.param(7)->autoParam, 0, 32, 100, false);
	check_node(*destination.param(7)->autoParam, 1, 63, 200, false);
	destination.set().trimToLength(8, destination.stack(), nullptr, false);
	POINTERS_EQUAL(nullptr, destination.param(7, false)->autoParam);
	LONGS_EQUAL(99, destination.set().get_current_value(7));
	source.set().nudgeNonInterpolatingNodesAtPos(31, 1, 32, nullptr, source.stack());
	POINTERS_EQUAL(nullptr, source.param(7, false)->autoParam);
	source.add_node(7, 0, 123);
	source.set().deleteAllAutomation(nullptr, source.stack());
	POINTERS_EQUAL(nullptr, source.param(7, false)->autoParam);
	LONGS_EQUAL(0, auto_param_pool::get().active_count());
}

TEST(parameter_lifecycle, midi_playback_skips_scalars_and_updates_the_automated_cc) {
	midi_fixture f;
	f.scalar(7, 42);
	f.scalar(10, 99);
	f.add_node(7, 0, 100);
	f.add_node(7, 8, 200);
	parameter_test::midi_notifications.clear();
	f.set().setPlayPos(8, f.stack(), false);
	LONGS_EQUAL(200, f.set().get_current_value(7));
	LONGS_EQUAL(99, f.set().get_current_value(10));
	CHECK_FALSE(parameter_test::midi_notifications.empty());
	LONGS_EQUAL(7, parameter_test::midi_notifications.back().cc);
	f.set().processCurrentPos(f.stack(), 0, false, false, false);
	f.set().tickTicks(1, f.stack());
	f.set().notifyPingpongOccurred(f.stack());
	POINTERS_EQUAL(nullptr, f.param(10, false)->autoParam);
	f.check_ownership();
}

TEST(parameter_lifecycle, midi_cc_value_conversion_preserves_boundary_rounding) {
	LONGS_EQUAL(-64, MIDIParamCollection::autoparamValueToCC(INT32_MIN));
	LONGS_EQUAL(0, MIDIParamCollection::autoparamValueToCC(0));
	LONGS_EQUAL(63, MIDIParamCollection::autoparamValueToCC(INT32_MAX));
	LONGS_EQUAL(0, MIDIParamCollection::autoparamValueToCC((1 << 24) - 1));
	LONGS_EQUAL(1, MIDIParamCollection::autoparamValueToCC(1 << 24));
}

namespace {
template <class Writer, class Reader>
void check_midi_value_persistence(bool json) {
	midi_fixture source, destination;
	source.scalar(7, 42);
	source.add_node(7, 4, 123);
	for (bool automated : {false, true}) {
		if (!automated)
			source.set().deleteAllAutomation(nullptr, source.stack());
		else
			source.add_node(7, 4, 123);
		Writer writer;
		writer.writeOpeningTagBeginning(json ? nullptr : "params");
		writer.write(" ");
		writer.writeTagNameAndSeperator("value");
		writer.write("\"");
		source.set().params.getParamFromCC(7)->write_to_file(writer);
		writer.write("\"");
		writer.writeAttribute("sentinel", 73);
		writer.closeTag();
		std::string document(writer.getBufferPtr(), writer.bytesWritten());
		for (bool load_nodes : {false, true}) {
			destination.scalar(7, 99);
			destination.add_node(7, 0, 777);
			native_parameter_tests::file_contents = document;
			Reader reader;
			if (json)
				CHECK(reader.match('{'));
			else
				STRCMP_EQUAL("params", reader.readNextTagOrAttributeName());
			STRCMP_EQUAL("value", reader.readNextTagOrAttributeName());
			auto* owner = destination.set().params.getParamFromCC(7);
			CHECK(owner->read_from_file(reader, load_nodes ? 32 : 0) == Error::NONE);
			LONGS_EQUAL(42, owner->get_current_value());
			CHECK_EQUAL(automated && load_nodes, owner->is_automated());
			if (owner->is_automated())
				check_node(*owner->get_auto_param(), 0, 4, 123, false);
			else
				POINTERS_EQUAL(nullptr, owner->get_auto_param());
			reader.exitTag("value");
			STRCMP_EQUAL("sentinel", reader.readNextTagOrAttributeName());
			LONGS_EQUAL(73, reader.readTagOrAttributeValueInt());
			destination.check_ownership();
		}
	}
	native_parameter_tests::file_contents = {};
}
} // namespace

TEST(parameter_lifecycle, midi_xml_value_round_trip_preserves_scalar_and_optional_nodes) {
	check_midi_value_persistence<XMLSerializer, XMLDeserializer>(false);
}
TEST(parameter_lifecycle, midi_json_value_round_trip_preserves_scalar_and_optional_nodes) {
	check_midi_value_persistence<JsonSerializer, JsonDeserializer>(true);
}

TEST(parameter_lifecycle, midi_cc_reassignment_reserves_destination_before_removing_source) {
	midi_fixture f;
	f.scalar(100, 42);
	f.add_node(100, 4, 123);
	alignas(ModelStackWithAutoParam) char source_memory[MODEL_STACK_MAX_SIZE];
	copyModelStack(source_memory, f.param(100), sizeof(ModelStackWithAutoParam));
	auto* source = reinterpret_cast<ModelStackWithAutoParam*>(source_memory);
	{
		fail_allocations failure;
		auto* destination = f.param(7);
		CHECK(move_midi_parameter_state(source, destination) == Error::INSUFFICIENT_RAM);
	}
	LONGS_EQUAL(42, f.set().get_current_value(100));
	check_node(*f.param(100)->autoParam, 0, 4, 123, false);
	CHECK_FALSE(f.set().has_current_value(7));
	auto* destination = f.param(7); // Insert before source; may relocate it.
	CHECK(move_midi_parameter_state(source, destination) == Error::NONE);
	CHECK_FALSE(f.set().has_current_value(100));
	LONGS_EQUAL(42, f.set().get_current_value(7));
	check_node(*f.param(7)->autoParam, 0, 4, 123, false);
	LONGS_EQUAL(1, auto_param_pool::get().active_count());
	f.check_ownership();
}

TEST(parameter_lifecycle, midi_cc_reassignment_replaces_destination_without_sharing_nodes) {
	midi_fixture f;
	f.scalar(7, 42);
	f.add_node(7, 4, 123);
	f.scalar(100, 99);
	f.add_node(100, 8, 777);
	alignas(ModelStackWithAutoParam) char source_memory[MODEL_STACK_MAX_SIZE];
	copyModelStack(source_memory, f.param(7), sizeof(ModelStackWithAutoParam));
	auto* source = reinterpret_cast<ModelStackWithAutoParam*>(source_memory);
	CHECK(move_midi_parameter_state(source, f.param(7)) == Error::NONE);
	CHECK(move_midi_parameter_state(source, f.param(100)) == Error::NONE);
	CHECK_FALSE(f.set().has_current_value(7));
	LONGS_EQUAL(42, f.set().get_current_value(100));
	auto* param = f.param(100)->autoParam;
	LONGS_EQUAL(1, param->nodes.getNumElements());
	check_node(*param, 0, 4, 123, false);
	f.check_ownership();
}

namespace {
template <class Reader>
void check_midi_load_pool_failure(bool json) {
	midi_fixture f;
	f.scalar(7, 99);
	const std::string document = json ? R"({"value":"0x0000002A0000007B00000004","sentinel":73})"
	                                  : R"(<params value="0x0000002A0000007B00000004" sentinel="73"/>)";
	for (bool fail : {true, false}) {
		native_parameter_tests::file_contents = document;
		Reader reader;
		if (json)
			CHECK(reader.match('{'));
		else
			STRCMP_EQUAL("params", reader.readNextTagOrAttributeName());
		STRCMP_EQUAL("value", reader.readNextTagOrAttributeName());
		auto* owner = f.set().params.getParamFromCC(7);
		if (fail) {
			fail_allocations failure(1);
			CHECK(owner->read_from_file(reader, 32) == Error::INSUFFICIENT_RAM);
			LONGS_EQUAL(sizeof(AutoParam), parameter_test::last_failed_allocation_size);
			POINTERS_EQUAL(nullptr, owner->get_auto_param());
			LONGS_EQUAL(0, auto_param_pool::get().active_count());
		}
		else {
			CHECK(owner->read_from_file(reader, 32) == Error::NONE);
			check_node(*owner->get_auto_param(), 0, 4, 123, false);
		}
		LONGS_EQUAL(42, owner->get_current_value());
		reader.exitTag("value");
		STRCMP_EQUAL("sentinel", reader.readNextTagOrAttributeName());
		LONGS_EQUAL(73, reader.readTagOrAttributeValueInt());
	}
	native_parameter_tests::file_contents = {};
}
} // namespace
TEST(parameter_lifecycle, midi_xml_pool_failure_retains_scalar_and_reader_position) {
	check_midi_load_pool_failure<XMLDeserializer>(false);
}
TEST(parameter_lifecycle, midi_json_pool_failure_retains_scalar_and_reader_position) {
	check_midi_load_pool_failure<JsonDeserializer>(true);
}

TEST(parameter_lifecycle, midi_failed_vector_growth_preserves_existing_scalar_bindings) {
	midi_fixture f;
	f.scalar(119, 42);
	f.add_node(119, 4, 123);
	auto* retained = f.param(119)->autoParam;
	bool failed = false;
	for (int cc = 118; cc >= 0 && !failed; --cc) {
		auto previous_count = f.set().params.getNumElements();
		MIDIParam* inserted;
		{
			fail_allocations failure;
			inserted = f.set().params.getOrCreateParamFromCC(cc);
		}
		retained->setCurrentValueBasicForSetup(1000 + cc);
		LONGS_EQUAL(1000 + cc, f.set().get_current_value(119));
		if (!inserted) {
			failed = true;
			LONGS_EQUAL(previous_count, f.set().params.getNumElements());
			CHECK_FALSE(f.set().has_current_value(cc));
			f.scalar(cc, 99);
		}
		f.check_ownership();
		check_node(*retained, 0, 4, 123, false);
	}
	CHECK(failed);
}

TEST(parameter_lifecycle, midi_loading_scalar_while_skipping_nodes_does_not_allocate) {
	midi_fixture f;
	auto* owner = &f.scalar(7, 99);
	const std::string document = R"({"value":"0x0000002A0000007B00000004","sentinel":73})";
	native_parameter_tests::file_contents = document;
	JsonDeserializer reader;
	CHECK(reader.match('{'));
	STRCMP_EQUAL("value", reader.readNextTagOrAttributeName());
	{
		fail_allocations failure;
		CHECK(owner->read_from_file(reader, 0) == Error::NONE);
		LONGS_EQUAL(0, parameter_test::allocation_failures);
	}
	LONGS_EQUAL(42, owner->get_current_value());
	POINTERS_EQUAL(nullptr, owner->get_auto_param());
	reader.exitTag("value");
	STRCMP_EQUAL("sentinel", reader.readNextTagOrAttributeName());
	LONGS_EQUAL(73, reader.readTagOrAttributeValueInt());
	native_parameter_tests::file_contents = {};
}

TEST(parameter_lifecycle, midi_collection_xml_save_preserves_cc_order_and_scalar_only_entries) {
	midi_fixture source, destination;
	source.scalar(100, 99);
	source.scalar(7, 42);
	source.add_node(7, 4, 123);
	XMLSerializer writer;
	writer.writeOpeningTag("clip");
	source.set().writeToFile(writer);
	writer.writeTag("sentinel", 73);
	writer.writeClosingTag("clip");
	const std::string document(writer.getBufferPtr(), writer.bytesWritten());
	native_parameter_tests::file_contents = document;
	XMLDeserializer reader;
	STRCMP_EQUAL("clip", reader.readNextTagOrAttributeName());
	STRCMP_EQUAL("midiParams", reader.readNextTagOrAttributeName());
	for (int cc : {7, 100}) {
		STRCMP_EQUAL("param", reader.readNextTagOrAttributeName());
		STRCMP_EQUAL("cc", reader.readNextTagOrAttributeName());
		LONGS_EQUAL(cc, reader.readTagOrAttributeValueInt());
		reader.exitTag("cc");
		STRCMP_EQUAL("value", reader.readNextTagOrAttributeName());
		auto* owner = &destination.scalar(cc, 0);
		CHECK(owner->read_from_file(reader, 32) == Error::NONE);
		reader.exitTag("value");
		reader.exitTag("param");
	}
	reader.exitTag("midiParams");
	STRCMP_EQUAL("sentinel", reader.readNextTagOrAttributeName());
	LONGS_EQUAL(73, reader.readTagOrAttributeValueInt());
	LONGS_EQUAL(42, destination.set().get_current_value(7));
	LONGS_EQUAL(99, destination.set().get_current_value(100));
	check_node(*destination.param(7)->autoParam, 0, 4, 123, false);
	POINTERS_EQUAL(nullptr, destination.param(100, false)->autoParam);
	source.check_ownership();
	destination.check_ownership();
	native_parameter_tests::file_contents = {};
}

// Backup cleanup reuses an entry and moves it to the first key for its output.
// Exercise the real ring-array move code, not the backup-table double.
TEST(parameter_lifecycle, ring_reposition_preserves_complete_entries_without_allocation) {
	struct Entry {
		uint32_t key;
		uint32_t payload[3];
		bool operator==(const Entry&) const = default;
	};
	class Ring : public ResizeableArray {
	public:
		std::array<Entry, 8> storage{};
		Ring(int count, int start) : ResizeableArray(sizeof(Entry)) {
			setStaticMemory(storage.data(), sizeof(storage));
			numElements = count;
			memoryStart = start;
		}
		~Ring() {
			// Test-owned static storage must not reach the firmware deallocator.
			memory = nullptr;
		}
	};
	for (int count = 1; count <= 8; ++count) {
		for (int start = 0; start < 8; ++start) {
			for (int from = 0; from < count; ++from) {
				for (int to = 0; to < count; ++to) {
					Ring ring(count, start);
					std::vector<Entry> expected;
					for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
						Entry entry{i, {i + 100, i + 200, i + 300}};
						*static_cast<Entry*>(ring.getElementAddress(i)) = entry;
						expected.push_back(entry);
					}
					if (from < to)
						std::rotate(expected.begin() + from, expected.begin() + from + 1, expected.begin() + to + 1);
					else if (from > to)
						std::rotate(expected.begin() + to, expected.begin() + from, expected.begin() + from + 1);
					{
						fail_allocations failure(0, false);
						ring.repositionElement(from, to);
					}
					LONGS_EQUAL(count, ring.getNumElements());
					for (int i = 0; i < count; ++i)
						CHECK(*static_cast<Entry*>(ring.getElementAddress(i)) == expected[i]);
				}
			}
		}
	}
}

namespace {
struct RepeatEntry {
	int32_t key;
	uint32_t payload[3];
	bool operator==(const RepeatEntry&) const = default;
};
class RepeatRing : public OrderedResizeableArrayWith32bitKey {
public:
	RepeatRing(int count, int start) : OrderedResizeableArrayWith32bitKey(sizeof(RepeatEntry), 0, 0) {
		CHECK_TRUE(insertAtIndex(0, 32) == Error::NONE);
		numElements = count;
		memoryStart = start;
	}
	RepeatEntry& entry(int index) { return *static_cast<RepeatEntry*>(getElementAddress(index)); }
};
} // namespace

TEST(parameter_lifecycle, repeated_ring_entries_match_reference_for_partial_and_complete_loops) {
	const std::vector<RepeatEntry> source{{0, {11, 12, 13}}, {2, {21, 22, 23}}, {5, {31, 32, 33}}, {9, {41, 42, 43}}};
	for (int start = 0; start < 32; ++start) {
		for (int end = 0; end <= 40; ++end) {
			RepeatRing ring(source.size(), start);
			for (int i = 0; i < static_cast<int>(source.size()); ++i)
				ring.entry(i) = source[i];
			std::vector<RepeatEntry> expected;
			for (int offset = 0; offset < end; offset += 7) {
				for (auto entry : source) {
					if (entry.key >= 7 || offset + entry.key >= end)
						continue;
					entry.key += offset;
					expected.push_back(entry);
				}
			}
			{
				fail_allocations failure(0, false);
				CHECK_TRUE(ring.generateRepeats(7, end));
			}
			LONGS_EQUAL(expected.size(), ring.getNumElements());
			for (int i = 0; i < ring.getNumElements(); ++i)
				CHECK(ring.entry(i) == expected[i]);
		}
	}
}

TEST(parameter_lifecycle, repetition_failure_preserves_existing_entries) {
	RepeatRing ring(2, 31);
	ring.entry(0) = {0, {11, 12, 13}};
	ring.entry(1) = {1, {21, 22, 23}};
	const auto first = ring.entry(0), second = ring.entry(1);
	for (auto limits :
	     {std::pair{0, 64}, std::pair{-1, 64}, std::pair{2, -1}, std::pair{2, INT32_MAX}, std::pair{2, 128}}) {
		fail_allocations failure(0, false);
		CHECK_FALSE(ring.generateRepeats(limits.first, limits.second));
		LONGS_EQUAL(2, ring.getNumElements());
		CHECK(ring.entry(0) == first);
		CHECK(ring.entry(1) == second);
	}
}

TEST(parameter_lifecycle, repetition_with_no_source_entries_does_not_walk_huge_repeat_range) {
	RepeatRing empty(0, 0);
	CHECK_TRUE(empty.generateRepeats(1, INT32_MAX));
	LONGS_EQUAL(0, empty.getNumElements());
	RepeatRing outside(1, 31);
	outside.entry(0) = {3, {11, 12, 13}};
	CHECK_TRUE(outside.generateRepeats(1, INT32_MAX));
	LONGS_EQUAL(0, outside.getNumElements());
}

TEST(parameter_lifecycle, shallow_ring_clone_failure_preserves_source_through_destruction_and_reuse) {
	const std::vector<RepeatEntry> expected{{1, {11, 12, 13}}, {3, {21, 22, 23}}, {5, {31, 32, 33}}};
	for (int start = 0; start < 32; ++start) {
		RepeatRing source(expected.size(), start);
		for (int i = 0; i < 3; ++i)
			source.entry(i) = expected[i];
		{
			RepeatRing clone = source; // The shallow-copy contract used by beenCloned().
			{
				fail_allocations failure(0, false);
				CHECK_TRUE(clone.beenCloned() == Error::INSUFFICIENT_RAM);
			}
			LONGS_EQUAL(0, clone.getNumElements());
			// Reusing the failed copy must acquire its own storage.
			CHECK_TRUE(clone.insertAtIndex(0, 1) == Error::NONE);
			clone.entry(0) = {7, {41, 42, 43}};
			for (int i = 0; i < 3; ++i)
				CHECK_TRUE(source.entry(i) == expected[i]);
		}
		for (int i = 0; i < 3; ++i)
			CHECK_TRUE(source.entry(i) == expected[i]);
	}
}
TEST(parameter_lifecycle, shallow_ring_clone_success_owns_independent_payload_storage) {
	const std::vector<RepeatEntry> expected{{1, {11, 12, 13}}, {3, {21, 22, 23}}, {5, {31, 32, 33}}};
	for (int start = 0; start < 32; ++start) {
		RepeatRing source(expected.size(), start);
		for (int i = 0; i < 3; ++i)
			source.entry(i) = expected[i];
		{
			RepeatRing clone = source;
			CHECK_TRUE(clone.beenCloned() == Error::NONE);
			LONGS_EQUAL(3, clone.getNumElements());
			for (int i = 0; i < 3; ++i)
				CHECK_TRUE(clone.entry(i) == expected[i]);
			CHECK_TRUE(clone.getElementAddress(0) != source.getElementAddress(0));
			clone.entry(1).payload[0] = 999;
			CHECK_TRUE(source.entry(1) == expected[1]);
		}
		for (int i = 0; i < 3; ++i)
			CHECK_TRUE(source.entry(i) == expected[i]);
	}
}

TEST(parameter_lifecycle, array_insertion_rejects_invalid_ranges_without_allocation) {
	ResizeableArray array(sizeof(int32_t));
	parameter_test::allocations_before_failure = 0;
	CHECK_TRUE(array.insertAtIndex(-1, 1) == Error::BUG);
	CHECK_TRUE(array.insertAtIndex(1, 1) == Error::BUG);
	CHECK_TRUE(array.insertAtIndex(0, 0) == Error::BUG);
	CHECK_TRUE(array.insertAtIndex(0, -1) == Error::BUG);
	LONGS_EQUAL(0, parameter_test::allocation_failures);
	LONGS_EQUAL(0, array.getNumElements());
}
TEST(parameter_lifecycle, array_insertion_bounds_include_spare_capacity) {
	ResizeableArray array(sizeof(int32_t), 16, 15);
	parameter_test::allocations_before_failure = 0;
	CHECK_TRUE(array.insertAtIndex(0, INT32_MAX) == Error::INSUFFICIENT_RAM);
	CHECK_TRUE(array.insertAtIndex(0, INT32_MAX / sizeof(int32_t)) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(0, parameter_test::allocation_failures);
	CHECK_TRUE(array.insertAtIndex(0, INT32_MAX / sizeof(int32_t) - 15) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(1, parameter_test::allocation_failures);
	LONGS_EQUAL(0, array.getNumElements());
}
TEST(parameter_lifecycle, array_rejected_growth_preserves_elements_and_allows_retry) {
	ResizeableArray array(sizeof(int32_t));
	CHECK_TRUE(array.insertAtIndex(0, 1) == Error::NONE);
	*static_cast<int32_t*>(array.getElementAddress(0)) = 42;
	CHECK_TRUE(array.insertAtIndex(1, INT32_MAX) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(1, array.getNumElements());
	LONGS_EQUAL(42, *static_cast<int32_t*>(array.getElementAddress(0)));
	CHECK_TRUE(array.insertAtIndex(1, 1) == Error::NONE);
	LONGS_EQUAL(2, array.getNumElements());
	LONGS_EQUAL(42, *static_cast<int32_t*>(array.getElementAddress(0)));
}

TEST(parameter_lifecycle, array_reservation_rejects_invalid_and_oversized_counts_before_allocation) {
	ResizeableArray array(sizeof(int32_t));
	parameter_test::allocations_before_failure = 0;
	CHECK_FALSE(array.ensureEnoughSpaceAllocated(-1));
	CHECK_FALSE(array.ensureEnoughSpaceAllocated(INT32_MIN));
	CHECK_FALSE(array.ensureEnoughSpaceAllocated(INT32_MAX));
	CHECK_FALSE(array.ensureEnoughSpaceAllocated(INT32_MAX / sizeof(int32_t)));
	LONGS_EQUAL(0, parameter_test::allocation_failures);
	LONGS_EQUAL(0, array.getNumElements());
}
TEST(parameter_lifecycle, array_zero_reservation_succeeds_without_allocation) {
	ResizeableArray array(sizeof(int32_t));
	parameter_test::allocations_before_failure = 0;
	CHECK_TRUE(array.ensureEnoughSpaceAllocated(0));
	LONGS_EQUAL(0, parameter_test::allocation_failures);
	parameter_test::allocations_before_failure = -1;
	CHECK_TRUE(array.insertAtIndex(0, 1) == Error::NONE);
	*static_cast<int32_t*>(array.getElementAddress(0)) = 42;
	parameter_test::allocations_before_failure = 0;
	CHECK_TRUE(array.ensureEnoughSpaceAllocated(0));
	LONGS_EQUAL(0, parameter_test::allocation_failures);
	LONGS_EQUAL(1, array.getNumElements());
	LONGS_EQUAL(42, *static_cast<int32_t*>(array.getElementAddress(0)));
}
TEST(parameter_lifecycle, array_reservation_counts_existing_elements_and_can_retry) {
	ResizeableArray array(sizeof(int32_t));
	CHECK_TRUE(array.insertAtIndex(0, 1) == Error::NONE);
	*static_cast<int32_t*>(array.getElementAddress(0)) = 42;
	parameter_test::allocations_before_failure = 0;
	CHECK_FALSE(array.ensureEnoughSpaceAllocated(INT32_MAX / sizeof(int32_t) - 15));
	LONGS_EQUAL(0, parameter_test::allocation_failures);
	LONGS_EQUAL(1, array.getNumElements());
	LONGS_EQUAL(42, *static_cast<int32_t*>(array.getElementAddress(0)));
	parameter_test::allocations_before_failure = -1;
	CHECK_TRUE(array.ensureEnoughSpaceAllocated(32));
	LONGS_EQUAL(1, array.getNumElements());
	LONGS_EQUAL(42, *static_cast<int32_t*>(array.getElementAddress(0)));
}
TEST(parameter_lifecycle, array_reservation_boundary_handles_allocation_failure) {
	ResizeableArray array(sizeof(int32_t));
	parameter_test::allocations_before_failure = 0;
	CHECK_FALSE(array.ensureEnoughSpaceAllocated(INT32_MAX / sizeof(int32_t) - 15));
	LONGS_EQUAL(1, parameter_test::allocation_failures);
	LONGS_EQUAL(0, array.getNumElements());
	parameter_test::allocations_before_failure = -1;
	CHECK_TRUE(array.ensureEnoughSpaceAllocated(2));
	LONGS_EQUAL(0, array.getNumElements());
}

TEST(parameter_lifecycle, array_clone_rejects_invalid_sources_without_changing_destination) {
	ResizeableArray destination(sizeof(int32_t));
	ResizeableArray incompatible(sizeof(int64_t));
	CHECK_TRUE(destination.insertAtIndex(0, 1) == Error::NONE);
	*static_cast<int32_t*>(destination.getElementAddress(0)) = 42;
	parameter_test::allocations_before_failure = 0;
	CHECK_FALSE(destination.cloneFrom(nullptr));
	CHECK_FALSE(destination.cloneFrom(&incompatible));
	CHECK_TRUE(destination.cloneFrom(&destination));
	LONGS_EQUAL(0, parameter_test::allocation_failures);
	LONGS_EQUAL(1, destination.getNumElements());
	LONGS_EQUAL(42, *static_cast<int32_t*>(destination.getElementAddress(0)));
}
TEST(parameter_lifecycle, array_clone_rejects_malformed_layout_and_capacity_before_allocation) {
	struct source_array : ResizeableArray {
		source_array() : ResizeableArray(sizeof(int32_t)) {}
		void set_layout(int32_t count, int32_t size, int32_t start) {
			numElements = count;
			memorySize = size;
			memoryStart = start;
		}
	} source;
	ResizeableArray destination(sizeof(int32_t));
	CHECK_TRUE(source.insertAtIndex(0, 1) == Error::NONE);
	CHECK_TRUE(destination.insertAtIndex(0, 1) == Error::NONE);
	*static_cast<int32_t*>(destination.getElementAddress(0)) = 42;
	parameter_test::allocations_before_failure = 0;
	for (auto layout : {std::array<int32_t, 3>{-1, 1, 0},
	                    {2, 1, 0},
	                    {1, 1, -1},
	                    {1, 1, 1},
	                    {INT32_MAX, INT32_MAX, 0},
	                    {1, INT32_MAX, 0}}) {
		source.set_layout(layout[0], layout[1], layout[2]);
		CHECK_FALSE(destination.cloneFrom(&source));
		LONGS_EQUAL(1, destination.getNumElements());
		LONGS_EQUAL(42, *static_cast<int32_t*>(destination.getElementAddress(0)));
	}
	LONGS_EQUAL(0, parameter_test::allocation_failures);
}

TEST(parameter_lifecycle, failed_array_clone_preserves_destination_and_successful_retry_releases_old_storage) {
	ResizeableArray source(sizeof(int32_t)), destination(sizeof(int32_t));
	CHECK_TRUE(source.insertAtIndex(0, 2) == Error::NONE);
	CHECK_TRUE(destination.insertAtIndex(0, 1) == Error::NONE);
	*static_cast<int32_t*>(source.getElementAddress(0)) = 11;
	*static_cast<int32_t*>(source.getElementAddress(1)) = 22;
	*static_cast<int32_t*>(destination.getElementAddress(0)) = 42;
	void* previous_address = destination.getElementAddress(0);
	const auto allocations = parameter_test::outstanding_allocations();
	parameter_test::allocations_before_failure = 0;
	CHECK_FALSE(destination.cloneFrom(&source));
	POINTERS_EQUAL(previous_address, destination.getElementAddress(0));
	LONGS_EQUAL(1, destination.getNumElements());
	LONGS_EQUAL(42, *static_cast<int32_t*>(destination.getElementAddress(0)));
	LONGS_EQUAL(allocations, parameter_test::outstanding_allocations());
	parameter_test::allocations_before_failure = -1;
	CHECK_TRUE(destination.cloneFrom(&source));
	LONGS_EQUAL(2, destination.getNumElements());
	LONGS_EQUAL(11, *static_cast<int32_t*>(destination.getElementAddress(0)));
	LONGS_EQUAL(22, *static_cast<int32_t*>(destination.getElementAddress(1)));
	LONGS_EQUAL(allocations, parameter_test::outstanding_allocations());
	*static_cast<int32_t*>(source.getElementAddress(0)) = 99;
	LONGS_EQUAL(11, *static_cast<int32_t*>(destination.getElementAddress(0)));
}
TEST(parameter_lifecycle, cloning_empty_array_releases_old_destination_storage) {
	ResizeableArray source(sizeof(int32_t)), destination(sizeof(int32_t));
	CHECK_TRUE(destination.insertAtIndex(0, 1) == Error::NONE);
	CHECK_TRUE(destination.cloneFrom(&source));
	LONGS_EQUAL(0, destination.getNumElements());
	LONGS_EQUAL(0, parameter_test::outstanding_allocations());
}

TEST(parameter_lifecycle, array_clone_detaches_shallow_source_alias_without_freeing_source) {
	ResizeableArray source(sizeof(int32_t));
	CHECK_TRUE(source.insertAtIndex(0, 1) == Error::NONE);
	*static_cast<int32_t*>(source.getElementAddress(0)) = 42;
	ResizeableArray destination = source;
	CHECK_TRUE(destination.cloneFrom(&source));
	LONGS_EQUAL(2, parameter_test::outstanding_allocations());
	LONGS_EQUAL(42, *static_cast<int32_t*>(source.getElementAddress(0)));
	LONGS_EQUAL(42, *static_cast<int32_t*>(destination.getElementAddress(0)));
}
TEST(parameter_lifecycle, array_clone_preserves_static_destination) {
	ResizeableArray source(sizeof(int32_t)), destination(sizeof(int32_t));
	std::array<int32_t, 4> storage{};
	destination.setStaticMemory(storage.data(), sizeof(storage));
	CHECK_TRUE(destination.insert_at_index_without_allocation(0, 1) == Error::NONE);
	*static_cast<int32_t*>(destination.getElementAddress(0)) = 42;
	CHECK_FALSE(destination.cloneFrom(&source));
	LONGS_EQUAL(1, destination.getNumElements());
	LONGS_EQUAL(42, *static_cast<int32_t*>(destination.getElementAddress(0)));
	CHECK_TRUE(destination.cloneFrom(&destination));
}

TEST(parameter_lifecycle, failed_borrowed_clone_destruction_preserves_original) {
	ResizeableArray source(sizeof(int32_t));
	CHECK_TRUE(source.insertAtIndex(0, 1) == Error::NONE);
	*static_cast<int32_t*>(source.getElementAddress(0)) = 42;
	void* original_address = source.getElementAddress(0);
	{
		ResizeableArray destination = source;
		parameter_test::allocations_before_failure = 0;
		CHECK_FALSE(destination.cloneFrom(&source));
		LONGS_EQUAL(0, destination.getNumElements());
	}
	LONGS_EQUAL(1, parameter_test::outstanding_allocations());
	POINTERS_EQUAL(original_address, source.getElementAddress(0));
	LONGS_EQUAL(1, source.getNumElements());
	LONGS_EQUAL(42, *static_cast<int32_t*>(source.getElementAddress(0)));
}
TEST(parameter_lifecycle, failed_borrowed_clone_can_retry_independently) {
	ResizeableArray source(sizeof(int32_t));
	CHECK_TRUE(source.insertAtIndex(0, 1) == Error::NONE);
	*static_cast<int32_t*>(source.getElementAddress(0)) = 42;
	ResizeableArray destination = source;
	parameter_test::allocations_before_failure = 0;
	CHECK_FALSE(destination.cloneFrom(&source));
	parameter_test::allocations_before_failure = -1;
	CHECK_TRUE(destination.cloneFrom(&source));
	*static_cast<int32_t*>(destination.getElementAddress(0)) = 99;
	LONGS_EQUAL(42, *static_cast<int32_t*>(source.getElementAddress(0)));
	LONGS_EQUAL(2, parameter_test::outstanding_allocations());
}
TEST(parameter_lifecycle, rejected_borrowed_clone_destruction_preserves_original) {
	ResizeableArray source(sizeof(int32_t));
	CHECK_TRUE(source.insertAtIndex(0, 1) == Error::NONE);
	*static_cast<int32_t*>(source.getElementAddress(0)) = 42;
	{
		ResizeableArray destination = source;
		destination.elementSize = sizeof(int64_t);
		CHECK_FALSE(destination.cloneFrom(&source));
		LONGS_EQUAL(0, destination.getNumElements());
	}
	LONGS_EQUAL(1, parameter_test::outstanding_allocations());
	LONGS_EQUAL(42, *static_cast<int32_t*>(source.getElementAddress(0)));
}

TEST(parameter_lifecycle, failed_shallow_clone_repair_destruction_preserves_original) {
	ResizeableArray source(sizeof(int32_t));
	CHECK_TRUE(source.insertAtIndex(0, 1) == Error::NONE);
	*static_cast<int32_t*>(source.getElementAddress(0)) = 42;
	{
		ResizeableArray destination = source;
		parameter_test::allocations_before_failure = 0;
		CHECK_TRUE(destination.beenCloned() == Error::INSUFFICIENT_RAM);
		LONGS_EQUAL(0, destination.getNumElements());
	}
	LONGS_EQUAL(1, parameter_test::outstanding_allocations());
	LONGS_EQUAL(42, *static_cast<int32_t*>(source.getElementAddress(0)));
}
TEST(parameter_lifecycle, rejected_shallow_clone_repair_detaches_original_storage) {
	struct source_array : ResizeableArray {
		source_array() : ResizeableArray(sizeof(int32_t)) {}
		void corrupt_count() { numElements = INT32_MAX; }
	} source;
	CHECK_TRUE(source.insertAtIndex(0, 1) == Error::NONE);
	*static_cast<int32_t*>(source.getElementAddress(0)) = 42;
	{
		source_array destination = source;
		destination.corrupt_count();
		parameter_test::allocations_before_failure = 0;
		CHECK_TRUE(destination.beenCloned() == Error::BUG);
		LONGS_EQUAL(0, destination.getNumElements());
		LONGS_EQUAL(0, parameter_test::allocation_failures);
	}
	LONGS_EQUAL(1, parameter_test::outstanding_allocations());
	LONGS_EQUAL(42, *static_cast<int32_t*>(source.getElementAddress(0)));
}
TEST(parameter_lifecycle, shallow_clone_repair_copies_wrapped_elements_independently) {
	ResizeableArray source(sizeof(int32_t));
	CHECK_TRUE(source.insertAtIndex(0, 2) == Error::NONE);
	*static_cast<int32_t*>(source.getElementAddress(0)) = 11;
	*static_cast<int32_t*>(source.getElementAddress(1)) = 22;
	CHECK_TRUE(source.insertAtIndex(0, 1) == Error::NONE);
	*static_cast<int32_t*>(source.getElementAddress(0)) = 7;
	ResizeableArray destination = source;
	CHECK_TRUE(destination.beenCloned() == Error::NONE);
	LONGS_EQUAL(3, destination.getNumElements());
	LONGS_EQUAL(7, *static_cast<int32_t*>(destination.getElementAddress(0)));
	LONGS_EQUAL(11, *static_cast<int32_t*>(destination.getElementAddress(1)));
	LONGS_EQUAL(22, *static_cast<int32_t*>(destination.getElementAddress(2)));
	*static_cast<int32_t*>(destination.getElementAddress(0)) = 99;
	LONGS_EQUAL(7, *static_cast<int32_t*>(source.getElementAddress(0)));
}

TEST(parameter_lifecycle, clone_rejects_source_storage_released_during_allocation) {
	ResizeableArray source(sizeof(int32_t)), destination(sizeof(int32_t));
	CHECK_TRUE(source.insertAtIndex(0, 1) == Error::NONE);
	CHECK_TRUE(destination.insertAtIndex(0, 1) == Error::NONE);
	*static_cast<int32_t*>(destination.getElementAddress(0)) = 42;
	void* previous_address = destination.getElementAddress(0);
	parameter_test::on_allocation = [&] { source.empty(); };
	CHECK_FALSE(destination.cloneFrom(&source));
	POINTERS_EQUAL(previous_address, destination.getElementAddress(0));
	LONGS_EQUAL(42, *static_cast<int32_t*>(destination.getElementAddress(0)));
	LONGS_EQUAL(0, source.getNumElements());
	LONGS_EQUAL(1, parameter_test::outstanding_allocations());
}
TEST(parameter_lifecycle, clone_rejects_source_layout_changed_during_allocation) {
	for (bool change_size : {false, true}) {
		ResizeableArray source(sizeof(int32_t)), destination(sizeof(int32_t));
		CHECK_TRUE(source.insertAtIndex(0, 2) == Error::NONE);
		*static_cast<int32_t*>(source.getElementAddress(0)) = 11;
		parameter_test::on_allocation = [&] {
			if (change_size)
				source.elementSize = sizeof(int64_t);
			else
				source.deleteAtIndex(1, 1, false);
		};
		CHECK_FALSE(destination.cloneFrom(&source));
		LONGS_EQUAL(0, destination.getNumElements());
		LONGS_EQUAL(1, parameter_test::outstanding_allocations());
		source.elementSize = sizeof(int32_t);
		CHECK_TRUE(destination.cloneFrom(&source));
		LONGS_EQUAL(11, *static_cast<int32_t*>(destination.getElementAddress(0)));
	}
}

TEST(parameter_lifecycle, clone_preserves_destination_emptied_during_allocation) {
	ResizeableArray source(sizeof(int32_t)), destination(sizeof(int32_t));
	CHECK_TRUE(source.insertAtIndex(0, 1) == Error::NONE);
	CHECK_TRUE(destination.insertAtIndex(0, 1) == Error::NONE);
	*static_cast<int32_t*>(source.getElementAddress(0)) = 11;
	parameter_test::on_allocation = [&] { destination.empty(); };
	CHECK_FALSE(destination.cloneFrom(&source));
	LONGS_EQUAL(0, destination.getNumElements());
	LONGS_EQUAL(1, parameter_test::outstanding_allocations());
	LONGS_EQUAL(11, *static_cast<int32_t*>(source.getElementAddress(0)));
	CHECK_TRUE(destination.cloneFrom(&source));
	LONGS_EQUAL(11, *static_cast<int32_t*>(destination.getElementAddress(0)));
}
TEST(parameter_lifecycle, clone_preserves_nested_destination_replacement) {
	ResizeableArray source(sizeof(int32_t)), destination(sizeof(int32_t)), nested_source(sizeof(int32_t));
	CHECK_TRUE(source.insertAtIndex(0, 1) == Error::NONE);
	CHECK_TRUE(destination.insertAtIndex(0, 1) == Error::NONE);
	CHECK_TRUE(nested_source.insertAtIndex(0, 1) == Error::NONE);
	*static_cast<int32_t*>(source.getElementAddress(0)) = 11;
	*static_cast<int32_t*>(nested_source.getElementAddress(0)) = 99;
	parameter_test::on_allocation = [&] { CHECK_TRUE(destination.cloneFrom(&nested_source)); };
	CHECK_FALSE(destination.cloneFrom(&source));
	LONGS_EQUAL(1, destination.getNumElements());
	LONGS_EQUAL(99, *static_cast<int32_t*>(destination.getElementAddress(0)));
	LONGS_EQUAL(11, *static_cast<int32_t*>(source.getElementAddress(0)));
	LONGS_EQUAL(3, parameter_test::outstanding_allocations());
}
TEST(parameter_lifecycle, clone_failure_preserves_callback_destination_change) {
	ResizeableArray source(sizeof(int32_t)), destination(sizeof(int32_t));
	CHECK_TRUE(source.insertAtIndex(0, 1) == Error::NONE);
	CHECK_TRUE(destination.insertAtIndex(0, 1) == Error::NONE);
	parameter_test::on_allocation = [&] { destination.empty(); };
	parameter_test::allocations_before_failure = 0;
	CHECK_FALSE(destination.cloneFrom(&source));
	LONGS_EQUAL(0, destination.getNumElements());
	LONGS_EQUAL(1, parameter_test::outstanding_allocations());
}

TEST(parameter_lifecycle, static_array_reserves_only_supplied_capacity_and_preserves_storage_on_destruction) {
	std::array<int32_t, 4> storage{42, 0, 0, 0};
	{
		ResizeableArray array(sizeof(int32_t));
		array.setStaticMemory(storage.data(), sizeof(storage));
		parameter_test::allocations_before_failure = 0;
		CHECK_TRUE(array.ensureEnoughSpaceAllocated(4));
		CHECK_FALSE(array.ensureEnoughSpaceAllocated(5));
		CHECK_TRUE(array.insert_at_index_without_allocation(0, 1) == Error::NONE);
		*static_cast<int32_t*>(array.getElementAddress(0)) = 99;
		void* original_address = array.getElementAddress(0);
		CHECK_TRUE(array.ensureEnoughSpaceAllocated(3));
		CHECK_FALSE(array.ensureEnoughSpaceAllocated(4));
		CHECK_FALSE(array.ensureEnoughSpaceAllocated(INT32_MAX));
		CHECK_TRUE(array.ensureEnoughSpaceAllocated(0));
		POINTERS_EQUAL(original_address, array.getElementAddress(0));
		LONGS_EQUAL(1, array.getNumElements());
		LONGS_EQUAL(99, *static_cast<int32_t*>(array.getElementAddress(0)));
		LONGS_EQUAL(0, parameter_test::allocation_failures);
		LONGS_EQUAL(0, parameter_test::outstanding_allocations());
	}
	LONGS_EQUAL(99, storage[0]);
}
TEST(parameter_lifecycle, repaired_static_array_copy_owns_only_its_new_storage) {
	std::array<int32_t, 2> storage{};
	ResizeableArray source(sizeof(int32_t));
	source.setStaticMemory(storage.data(), sizeof(storage));
	CHECK_TRUE(source.insert_at_index_without_allocation(0, 1) == Error::NONE);
	*static_cast<int32_t*>(source.getElementAddress(0)) = 42;
	{
		ResizeableArray destination = source;
		CHECK_TRUE(destination.beenCloned() == Error::NONE);
		LONGS_EQUAL(1, parameter_test::outstanding_allocations());
		*static_cast<int32_t*>(destination.getElementAddress(0)) = 99;
	}
	LONGS_EQUAL(0, parameter_test::outstanding_allocations());
	LONGS_EQUAL(42, storage[0]);
}

TEST(parameter_lifecycle, array_storage_swap_transfers_static_ownership_with_buffer) {
	std::array<int32_t, 4> storage{};
	{
		ResizeableArray external(sizeof(int32_t)), heap(sizeof(int32_t));
		external.setStaticMemory(storage.data(), sizeof(storage));
		CHECK_TRUE(external.insert_at_index_without_allocation(0, 1) == Error::NONE);
		*static_cast<int32_t*>(external.getElementAddress(0)) = 42;
		CHECK_TRUE(heap.insertAtIndex(0, 1) == Error::NONE);
		*static_cast<int32_t*>(heap.getElementAddress(0)) = 99;
		external.swapStateWith(&heap);
		LONGS_EQUAL(0, external.staticMemoryAllocationSize);
		LONGS_EQUAL(sizeof(storage), heap.staticMemoryAllocationSize);
		LONGS_EQUAL(99, *static_cast<int32_t*>(external.getElementAddress(0)));
		LONGS_EQUAL(42, *static_cast<int32_t*>(heap.getElementAddress(0)));
		CHECK_TRUE(heap.ensureEnoughSpaceAllocated(3));
		CHECK_FALSE(heap.ensureEnoughSpaceAllocated(4));
		heap.empty();
		LONGS_EQUAL(1, parameter_test::outstanding_allocations());
		external.empty();
		LONGS_EQUAL(0, parameter_test::outstanding_allocations());
	}
	LONGS_EQUAL(42, storage[0]);
}
TEST(parameter_lifecycle, swapped_external_arrays_keep_their_buffer_capacities) {
	std::array<int32_t, 2> small{};
	std::array<int32_t, 5> large{};
	ResizeableArray first(sizeof(int32_t)), second(sizeof(int32_t));
	first.setStaticMemory(small.data(), sizeof(small));
	second.setStaticMemory(large.data(), sizeof(large));
	first.swapStateWith(&second);
	LONGS_EQUAL(sizeof(large), first.staticMemoryAllocationSize);
	LONGS_EQUAL(sizeof(small), second.staticMemoryAllocationSize);
	CHECK_TRUE(first.ensureEnoughSpaceAllocated(5));
	CHECK_FALSE(first.ensureEnoughSpaceAllocated(6));
	CHECK_TRUE(second.ensureEnoughSpaceAllocated(2));
	CHECK_FALSE(second.ensureEnoughSpaceAllocated(3));
	first.swapStateWith(&first);
	LONGS_EQUAL(sizeof(large), first.staticMemoryAllocationSize);
	CHECK_TRUE(first.ensureEnoughSpaceAllocated(5));
}

TEST(parameter_lifecycle, regular_static_insertion_rejects_growth_without_heap_access) {
	std::array<int32_t, 6> storage{123, 0, 0, 0, 0, 456};
	ResizeableArray array(sizeof(int32_t));
	array.setStaticMemory(storage.data() + 1, 4 * sizeof(int32_t));
	parameter_test::allocations_before_failure = 0;
	CHECK_TRUE(array.insertAtIndex(0, 4) == Error::NONE);
	for (int32_t index = 0; index < 4; ++index)
		*static_cast<int32_t*>(array.getElementAddress(index)) = index + 1;
	CHECK_TRUE(array.insertAtIndex(2, 1) == Error::INSUFFICIENT_RAM);
	CHECK_TRUE(array.insertAtIndex(0, INT32_MAX) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(4, array.getNumElements());
	for (int32_t index = 0; index < 4; ++index)
		LONGS_EQUAL(index + 1, *static_cast<int32_t*>(array.getElementAddress(index)));
	LONGS_EQUAL(123, storage.front());
	LONGS_EQUAL(456, storage.back());
	LONGS_EQUAL(0, parameter_test::allocation_failures);
}
TEST(parameter_lifecycle, regular_static_insertion_reuses_wrapped_capacity) {
	std::array<int32_t, 6> storage{123, 0, 0, 0, 0, 456};
	ResizeableArray array(sizeof(int32_t));
	array.setStaticMemory(storage.data() + 1, 4 * sizeof(int32_t));
	CHECK_TRUE(array.insertAtIndex(0, 4) == Error::NONE);
	for (int32_t index = 0; index < 4; ++index)
		*static_cast<int32_t*>(array.getElementAddress(index)) = index + 1;
	array.deleteAtIndex(0, 2, false);
	CHECK_TRUE(array.insertAtIndex(1, 2) == Error::NONE);
	*static_cast<int32_t*>(array.getElementAddress(1)) = 7;
	*static_cast<int32_t*>(array.getElementAddress(2)) = 8;
	LONGS_EQUAL(4, array.getNumElements());
	LONGS_EQUAL(3, *static_cast<int32_t*>(array.getElementAddress(0)));
	LONGS_EQUAL(7, *static_cast<int32_t*>(array.getElementAddress(1)));
	LONGS_EQUAL(8, *static_cast<int32_t*>(array.getElementAddress(2)));
	LONGS_EQUAL(4, *static_cast<int32_t*>(array.getElementAddress(3)));
	LONGS_EQUAL(123, storage.front());
	LONGS_EQUAL(456, storage.back());
	LONGS_EQUAL(0, parameter_test::outstanding_allocations());
}

TEST(parameter_lifecycle, array_deletion_rejects_invalid_ranges_without_mutation) {
	for (bool external : {false, true}) {
		std::array<int32_t, 6> storage{123, 0, 0, 0, 0, 456};
		ResizeableArray array(sizeof(int32_t));
		if (external)
			array.setStaticMemory(storage.data() + 1, 4 * sizeof(int32_t));
		CHECK_TRUE(array.insertAtIndex(0, 4) == Error::NONE);
		for (int32_t index = 0; index < 4; ++index)
			*static_cast<int32_t*>(array.getElementAddress(index)) = index + 1;
		void* original_address = array.getElementAddress(0);
		const auto allocations = parameter_test::outstanding_allocations();
		for (auto range : {std::array<int32_t, 2>{-1, 1},
		                   {0, -1},
		                   {0, INT32_MIN},
		                   {0, 0},
		                   {0, 5},
		                   {2, 4},
		                   {4, 1},
		                   {INT32_MAX, 1},
		                   {1, INT32_MAX}}) {
			array.deleteAtIndex(range[0], range[1]);
			LONGS_EQUAL(4, array.getNumElements());
			POINTERS_EQUAL(original_address, array.getElementAddress(0));
			for (int32_t index = 0; index < 4; ++index)
				LONGS_EQUAL(index + 1, *static_cast<int32_t*>(array.getElementAddress(index)));
			LONGS_EQUAL(allocations, parameter_test::outstanding_allocations());
		}
		array.deleteAtIndex(1, 2, false);
		LONGS_EQUAL(2, array.getNumElements());
		LONGS_EQUAL(1, *static_cast<int32_t*>(array.getElementAddress(0)));
		LONGS_EQUAL(4, *static_cast<int32_t*>(array.getElementAddress(1)));
		array.deleteAtIndex(0, 2);
		LONGS_EQUAL(0, array.getNumElements());
		LONGS_EQUAL(0, parameter_test::outstanding_allocations());
		LONGS_EQUAL(123, storage.front());
		LONGS_EQUAL(456, storage.back());
	}
}

TEST(parameter_lifecycle, array_reordering_rejects_invalid_indices_and_self_moves) {
	for (bool external : {false, true}) {
		std::array<int32_t, 6> storage{123, 0, 0, 0, 0, 456};
		ResizeableArray array(sizeof(int32_t));
		array.swapElements(0, 1);
		array.repositionElement(0, 1);
		if (external)
			array.setStaticMemory(storage.data() + 1, 4 * sizeof(int32_t));
		CHECK_TRUE(array.insertAtIndex(0, 4) == Error::NONE);
		for (int32_t index = 0; index < 4; ++index)
			*static_cast<int32_t*>(array.getElementAddress(index)) = index + 1;
		for (auto indices :
		     {std::array<int32_t, 2>{-1, 0}, {0, -1}, {4, 0}, {0, 4}, {INT32_MIN, 0}, {0, INT32_MAX}, {0, 0}, {3, 3}}) {
			array.swapElements(indices[0], indices[1]);
			array.repositionElement(indices[0], indices[1]);
			LONGS_EQUAL(4, array.getNumElements());
			for (int32_t index = 0; index < 4; ++index)
				LONGS_EQUAL(index + 1, *static_cast<int32_t*>(array.getElementAddress(index)));
		}
		LONGS_EQUAL(123, storage.front());
		LONGS_EQUAL(456, storage.back());
	}
}
TEST(parameter_lifecycle, array_reordering_preserves_wrapped_external_buffer_guards) {
	std::array<int32_t, 6> storage{123, 0, 0, 0, 0, 456};
	ResizeableArray array(sizeof(int32_t));
	array.setStaticMemory(storage.data() + 1, 4 * sizeof(int32_t));
	CHECK_TRUE(array.insertAtIndex(0, 4) == Error::NONE);
	for (int32_t index = 0; index < 4; ++index)
		*static_cast<int32_t*>(array.getElementAddress(index)) = index + 1;
	array.deleteAtIndex(0, 1, false);
	CHECK_TRUE(array.insertAtIndex(3, 1) == Error::NONE);
	*static_cast<int32_t*>(array.getElementAddress(3)) = 5;
	array.repositionElement(0, 3);
	array.swapElements(0, 3);
	array.repositionElement(3, 0);
	const std::array<int32_t, 4> expected{3, 2, 4, 5};
	for (int32_t index = 0; index < 4; ++index)
		LONGS_EQUAL(expected[index], *static_cast<int32_t*>(array.getElementAddress(index)));
	LONGS_EQUAL(123, storage.front());
	LONGS_EQUAL(456, storage.back());
}
