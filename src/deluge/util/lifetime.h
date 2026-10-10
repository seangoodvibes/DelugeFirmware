#pragma once

namespace deluge::lifetime {

class lifetime_watch;

// Non-owning synchronous validation across a callback/allocation boundary. The
// lvalue predicate must outlive the operation, must not yield, and must establish
// owner lifetime before reading its context. Temporaries are intentionally rejected.
class callback_validation final {
public:
	template <typename Predicate>
	explicit callback_validation(Predicate& predicate)
	    : context_(&predicate),
	      check_([](const void* context) { return (*static_cast<const Predicate*>(context))(); }) {}
	[[nodiscard]] bool valid() const { return check_(context_); }

private:
	const void* context_;
	bool (*check_)(const void*);
};

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
	bool retire();

private:
	friend class lifetime_watch;
	lifetime_watch* first_ = nullptr;
	bool retiring_ = false;
};

// The watch itself must remain at a stable address. After retirement it no longer
// references the source, so querying or destroying it never reads freed memory.
class lifetime_watch final {
public:
	lifetime_watch() = default;
	explicit lifetime_watch(lifetime_source& source) { reset(source); }
	~lifetime_watch() { reset(); }
	// Reuse a stable-address watch for deferred work without allocating or moving it.
	void reset() {
		if (source_) {
			*previous_link_ = next_;
			if (next_)
				next_->previous_link_ = previous_link_;
		}
		source_ = nullptr;
		next_ = nullptr;
		previous_link_ = nullptr;
	}
	void reset(lifetime_source& source) {
		reset();
		if (source.retiring_)
			return;
		source_ = &source;
		next_ = source.first_;
		previous_link_ = &source.first_;
		if (next_)
			next_->previous_link_ = &next_;
		source.first_ = this;
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

inline bool lifetime_source::retire() {
	if (retiring_)
		return false;
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
	return true;
}

} // namespace deluge::lifetime
