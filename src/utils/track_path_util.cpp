// PCH disabled for utils folder

#include "track_path_util.h"
#include "string_util.h"

#include "../types.h"

#include <cstring>

namespace subsonic {
namespace track_path_util {

bool is_subsonic_path(const char *path) noexcept {
	return string_util::starts_with_ascii_nocase(path, k_scheme);
}

pfc::string8 make_subsonic_path(const char *track_id) {
	pfc::string8 path = k_scheme;
	if (track_id != nullptr) {
		path += track_id;
	}
	return path;
}

bool extract_track_id_from_path(const char *path,
								pfc::string_base &out_track_id) {
	out_track_id.reset();
	if (!is_subsonic_path(path)) {
		return false;
	}

	const char *cursor = path + strlen(k_scheme);
	const char *end = cursor;
	while (*end != '\0' && *end != '?' && *end != '#') {
		++end;
	}
	if (cursor == end) {
		return false;
	}
	out_track_id.add_string(cursor, static_cast<t_size>(end - cursor));
	return out_track_id.length() > 0;
}

} // namespace track_path_util
} // namespace subsonic
