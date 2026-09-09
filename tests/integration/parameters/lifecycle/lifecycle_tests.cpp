#include "CppUTest/TestHarness.h"
#include "memory/general_memory_allocator.h"
#include "model/consequence/consequence_param_change.h"
#include "model/mod_controllable/mod_controllable.h"
#include "modulation/automation/auto_param_pool.h"
#include "modulation/automation/copied_param_automation.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_node.h"
#include "modulation/params/param_set.h"
#include "platform.h"
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
        auto_param_pool::get().clear_unused();
        parameter_test::reset();
        LONGS_EQUAL(0, parameter_test::outstanding_allocations());
    }
    void teardown() override {
        LONGS_EQUAL(0, auto_param_pool::get().active_count());
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
		if (clear_cache)
			auto_param_pool::get().clear_unused();
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
