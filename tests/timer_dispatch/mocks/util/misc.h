#pragma once
#include <type_traits>
namespace util {
template <typename T>
constexpr auto to_underlying(T value) {
	return static_cast<std::underlying_type_t<T>>(value);
}
} // namespace util
