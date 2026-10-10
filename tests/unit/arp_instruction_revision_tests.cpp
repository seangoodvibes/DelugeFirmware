#include "CppUTest/TestHarness.h"
#include "modulation/arpeggiator.h"
namespace {
class revision_probe final : public ArpeggiatorBase {
public:
	void noteOn(ArpeggiatorSettings*, int32_t, int32_t, ArpReturnInstruction*, int32_t, const int16_t*) override {}
	void noteOff(ArpeggiatorSettings*, int32_t, ArpReturnInstruction*) override {}
	void reset() override { invalidate_instructions(); }
	ArpType getArpType() override { return ArpType::DRUM; }
	bool hasAnyInputNotesActive() override { return false; }
	void switchNoteOn(ArpeggiatorSettings*, ArpReturnInstruction*, bool) override {}
};
} // namespace
#include "arp_pending_revision.inc"

TEST_GROUP(arp_instruction_revision){};
TEST(arp_instruction_revision, production_pending_generation_invalidates_prior_instruction) {
	revision_probe arp;
	const auto initial = arp.instruction_revision();
	ArpReturnInstruction instruction;
	CHECK_FALSE(arp.handlePendingNotes(nullptr, &instruction));
	CHECK(arp.instruction_revision() != initial);
	const auto generated = arp.instruction_revision();
	arp.reset();
	CHECK(arp.instruction_revision() != generated);
}
TEST(arp_instruction_revision, read_only_pending_query_preserves_revision) {
	revision_probe arp;
	const auto initial = arp.instruction_revision();
	CHECK_FALSE(arp.hasPendingNotes(nullptr));
	CHECK(arp.instruction_revision() == initial);
}
