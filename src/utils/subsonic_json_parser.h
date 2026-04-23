#pragma once

#include <SDK/foobar2000.h>

#include <nlohmann/json.hpp>
#include <functional>

namespace subsonic {
namespace json_parser {

// Convert JSON value to string representation
// Handles string, number, boolean types
// Returns empty string for null/undefined
[[nodiscard]] pfc::string8 to_string(const nlohmann::json &value);

// Convert JSON value to metadata-safe string
// Falls back to JSON dump for arrays/objects
// Used when storing arbitrary JSON in metadata fields
[[nodiscard]] pfc::string8 to_metadata_string(const nlohmann::json &value);

// Get string from JSON object member
// Returns empty string if member not found or not string-like
[[nodiscard]] pfc::string8 get_string(const nlohmann::json &object,
									  const char *member);

// Get number from JSON object member
// Returns 0.0 if member not found or not a number
[[nodiscard]] double get_number(const nlohmann::json &object,
								const char *member);

// Iterate over JSON value(s) with callback
// If value is array, calls callback for each element
// If value is single value, calls callback once
// If value is null, does nothing
template <typename Callback>
void for_each_value(const nlohmann::json &value, Callback &&callback) {
	if (value.is_null()) {
		return;
	}
	if (value.is_array()) {
		for (const auto &item : value) {
			callback(item);
		}
		return;
	}
	callback(value);
}

// Iterate over JSON object member's value(s) with callback
// Convenience wrapper for accessing object members and iterating their values
template <typename Callback>
void for_each_member_item(const nlohmann::json &parent, const char *member,
						  Callback &&callback) {
	if (member == nullptr || !parent.is_object()) {
		return;
	}

	const auto found = parent.find(member);
	if (found == parent.end()) {
		return;
	}

	for_each_value(*found, std::forward<Callback>(callback));
}

} // namespace json_parser
} // namespace subsonic
