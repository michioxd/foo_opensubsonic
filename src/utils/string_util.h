#pragma once

namespace subsonic {
namespace string_util {

// Compare two C strings for equality, treating null as empty string
// Null-safe: null and "" are considered equal
[[nodiscard]] bool strings_equal(const char *lhs, const char *rhs) noexcept;

// Case-insensitive ASCII string prefix check
// Returns true if text starts with prefix (case-insensitive)
// Null-safe: returns false if either argument is null
[[nodiscard]] bool starts_with_ascii_nocase(const char *text,
											const char *prefix) noexcept;

// Case-insensitive ASCII string suffix check
// Returns true if text ends with suffix (case-insensitive)
// Null-safe: returns false if either argument is null
[[nodiscard]] bool ends_with_ascii_nocase(const char *text,
										  const char *suffix) noexcept;

} // namespace string_util
} // namespace subsonic
