#pragma once

// Intrusive quarantine for objects removed from an active list while retained
// references may still exist. The owner decides when destruction is safe.
template <typename T>
class RetainedList {
public:
	RetainedList() = default;
	RetainedList(const RetainedList&) = delete;
	RetainedList& operator=(const RetainedList&) = delete;
	T*& head() { return first_; }
	T* head() const { return first_; }
	bool contains(const T* object) const {
		for (auto* entry = first_; entry; entry = entry->next) {
			if (entry == object)
				return true;
		}
		return false;
	}
	void retain(T* object) {
		if (!object || contains(object))
			return;
		object->next = first_;
		first_ = object;
	}
	bool release(T* object) {
		if (!object)
			return false;
		for (auto** entry = &first_; *entry; entry = &(*entry)->next) {
			if (*entry == object) {
				*entry = object->next;
				object->next = nullptr;
				return true;
			}
		}
		return false;
	}

private:
	T* first_ = nullptr;
};
