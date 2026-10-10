#pragma once

namespace deluge::lifetime {

class lifetime_watch;

// Intrusive cancellation for serialized, reentrant firmware callbacks. This does
// not retain the object or provide synchronization between concurrent threads.
class lifetime_source final {
public:
	lifetime_source() = default;
	~lifetime_source() { retire(); }
	lifetime_source(const lifetime_source&) = delete;
	lifetime_source& operator=(const lifetime_source&) = delete;
	lifetime_source(lifetime_source&&) = delete;
	lifetime_source& operator=(lifetime_source&&) = delete;
	void retire();

private:
	friend class lifetime_watch;
	lifetime_watch* first_ = nullptr;
	bool retiring_ = false;
};

// The watch itself must remain at a stable address. After retirement it no longer
// references the source, so querying or destroying it never reads freed memory.
class lifetime_watch final {
public:
	explicit lifetime_watch(lifetime_source& source) {
		if (source.retiring_)
			return;
		source_ = &source;
		next_ = source.first_;
		previous_link_ = &source.first_;
		if (next_)
			next_->previous_link_ = &next_;
		source.first_ = this;
	}
	~lifetime_watch() {
		if (!source_)
			return;
		*previous_link_ = next_;
		if (next_)
			next_->previous_link_ = previous_link_;
	}
	lifetime_watch(const lifetime_watch&) = delete;
	lifetime_watch& operator=(const lifetime_watch&) = delete;
	lifetime_watch(lifetime_watch&&) = delete;
	lifetime_watch& operator=(lifetime_watch&&) = delete;
	[[nodiscard]] bool alive() const { return source_ != nullptr; }

private:
	friend class lifetime_source;
	lifetime_source* source_ = nullptr;
	lifetime_watch* next_ = nullptr;
	lifetime_watch** previous_link_ = nullptr;
};

inline void lifetime_source::retire() {
	retiring_ = true;
	auto* watch = first_;
	first_ = nullptr;
	while (watch) {
		auto* next = watch->next_;
		watch->source_ = nullptr;
		watch->next_ = nullptr;
		watch->previous_link_ = nullptr;
		watch = next;
	}
}

} // namespace deluge::lifetime
