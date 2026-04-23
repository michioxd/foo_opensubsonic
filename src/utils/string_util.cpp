// PCH disabled for utils folder

#include "string_util.h"

#include <cstring>

namespace subsonic {
namespace string_util {

bool strings_equal(const char *lhs, const char *rhs) noexcept {
	const char *left = lhs != nullptr ? lhs : "";
	const char *right = rhs != nullptr ? rhs : "";
	return std::strcmp(left, right) == 0;
}

bool starts_with_ascii_nocase(const char *text, const char *prefix) noexcept {
	if (text == nullptr || prefix == nullptr) {
		return false;
	}
	const auto prefix_len = strlen(prefix);
	return pfc::stricmp_ascii_ex(text, prefix_len, prefix, prefix_len) == 0;
}

bool ends_with_ascii_nocase(const char *text, const char *suffix) noexcept {
	if (text == nullptr || suffix == nullptr) {
		return false;
	}
	const auto text_len = strlen(text);
	const auto suffix_len = strlen(suffix);
	if (suffix_len > text_len) {
		return false;
	}
	return pfc::stricmp_ascii_ex(text + (text_len - suffix_len), suffix_len,
								 suffix, suffix_len) == 0;
}

} // namespace string_util
} // namespace subsonic
