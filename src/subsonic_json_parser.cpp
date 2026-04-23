#include "stdafx.h"

#include "subsonic_json_parser.h"

#include <string>

namespace subsonic {
namespace json_parser {

pfc::string8 to_string(const nlohmann::json &value) {
	if (value.is_string()) {
		return value.get_ref<const std::string &>().c_str();
	}
	if (value.is_number_integer()) {
		return std::to_string(value.get<long long>()).c_str();
	}
	if (value.is_number_unsigned()) {
		return std::to_string(value.get<unsigned long long>()).c_str();
	}
	if (value.is_number_float()) {
		return std::to_string(value.get<double>()).c_str();
	}
	if (value.is_boolean()) {
		return value.get<bool>() ? "true" : "false";
	}
	return {};
}

pfc::string8 to_metadata_string(const nlohmann::json &value) {
	const auto scalar = to_string(value);
	if (!scalar.is_empty() || value.is_string() || value.is_boolean() ||
		value.is_number()) {
		return scalar;
	}

	if (value.is_array() || value.is_object()) {
		return value.dump().c_str();
	}

	return {};
}

pfc::string8 get_string(const nlohmann::json &object, const char *member) {
	if (!object.is_object() || member == nullptr) {
		return {};
	}

	const auto found = object.find(member);
	if (found == object.end()) {
		return {};
	}

	return to_string(*found);
}

double get_number(const nlohmann::json &object, const char *member) {
	if (!object.is_object() || member == nullptr) {
		return 0.0;
	}

	const auto found = object.find(member);
	if (found == object.end()) {
		return 0.0;
	}

	if (found->is_number()) {
		return found->get<double>();
	}
	if (found->is_string()) {
		try {
			return std::stod(found->get_ref<const std::string &>());
		} catch (...) {
			return 0.0;
		}
	}

	return 0.0;
}

} // namespace json_parser
} // namespace subsonic
