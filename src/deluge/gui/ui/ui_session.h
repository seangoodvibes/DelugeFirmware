#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace deluge::gui::ui_session {

// UI ownership is independent of USB host/peripheral roles. Audio, song data,
// storage and settings are not duplicated by this context.
enum class Id : uint8_t { Local, Remote };

namespace detail {
inline Id active = Id::Local;
}

[[nodiscard]] inline Id current() {
	return detail::active;
}

// A yielded operation must resume with its original context. A nested hardware
// service can enter Local and return to the suspended Remote operation without
// copying UI objects, their owning pointers, or the project.
class Scope final {
public:
	explicit Scope(Id owner) : previous_(current()) { detail::active = owner; }
	~Scope() { detail::active = previous_; }
	Scope(const Scope&) = delete;
	Scope& operator=(const Scope&) = delete;
	Scope(Scope&&) = delete;
	Scope& operator=(Scope&&) = delete;

private:
	Id previous_;
};

// Each bank is constructed independently. In particular, an owning container in
// one UI must never be initialized by a byte copy of another UI's container.
template <typename T>
class State final {
public:
	T& for_owner(Id owner) { return values_[static_cast<size_t>(owner)]; }
	const T& for_owner(Id owner) const { return values_[static_cast<size_t>(owner)]; }
	T& active() { return for_owner(current()); }
	const T& active() const { return for_owner(current()); }

private:
	std::array<T, 2> values_{};
};

// Preserve the original boot-time Local object. Construct its Remote counterpart
// only when needed, under Remote ownership so constructor dependencies resolve
// to the same session. Storage is reserved statically; no UI is shallow-copied.
template <typename T>
class RemoteInstance final {
public:
	const T* get_if_initialized(const T& local) const {
		if (current() == Id::Local) {
			return &local;
		}
		return remote_ ? &*remote_ : nullptr;
	}

	template <typename... Args>
	T& get(T& local, Args&&... args) {
		if (current() == Id::Local) {
			return local;
		}
		if (!remote_) {
			Scope owner(Id::Remote);
			remote_.emplace(std::forward<Args>(args)...);
		}
		return *remote_;
	}
	RemoteInstance() = default;
	RemoteInstance(const RemoteInstance&) = delete;
	RemoteInstance& operator=(const RemoteInstance&) = delete;

private:
	std::optional<T> remote_;
};

} // namespace deluge::gui::ui_session
