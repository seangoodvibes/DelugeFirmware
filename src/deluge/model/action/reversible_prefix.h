#pragma once

// Applies a validated, reachable prefix without allocating or yielding. A failed
// apply must leave its node unchanged; rollback reports success and must not
// yield. Successful nodes are retired in reverse order for deferred destruction.
// On failure list links are restored. Model recovery is reported separately.
enum class PrefixResult { APPLIED, ROLLED_BACK, ROLLBACK_FAILED };
template <typename Node, typename Apply, typename Rollback>
PrefixResult apply_reversible_prefix(Node*& first, Node* end, Node*& retired, Apply apply, Rollback rollback) {
	retired = nullptr;
	for (auto* node = first; node != end;) {
		auto* next = node->next;
		if (!apply(*node)) {
			bool rollback_failed = false;
			while (retired) {
				auto* previous = retired->next;
				if (!rollback_failed && !rollback(*retired))
					rollback_failed = true;
				retired->next = node;
				node = retired;
				retired = previous;
			}
			return rollback_failed ? PrefixResult::ROLLBACK_FAILED : PrefixResult::ROLLED_BACK;
		}
		node->next = retired;
		retired = node;
		node = next;
	}
	first = end;
	return PrefixResult::APPLIED;
}
