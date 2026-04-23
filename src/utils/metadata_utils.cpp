// PCH disabled for utils folder

#include "metadata_utils.h"

#include "utils.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdlib>
#include <string>

namespace subsonic::metadata_utils {

namespace {

using json = nlohmann::json;

[[nodiscard]] bool try_parse_float_string(const char *text, float &out_value) {
	out_value = 0.0f;
	if (text == nullptr) {
		return false;
	}

	char *end = nullptr;
	const float value = std::strtof(text, &end);
	if (end == text) {
		return false;
	}

	while (*end != '\0' &&
		   std::isspace(static_cast<unsigned char>(*end)) != 0) {
		++end;
	}

	if (*end != '\0') {
		return false;
	}

	out_value = value;
	return true;
}

[[nodiscard]] bool
try_get_float_extra_field(const cached_track_metadata &entry,
						  std::initializer_list<const char *> field_names,
						  float &out_value) {
	pfc::string8 value;
	return try_get_first_extra_field(entry, field_names, value) &&
		   !value.is_empty() &&
		   try_parse_float_string(value.c_str(), out_value);
}

[[nodiscard]] bool try_get_json_float_member(const pfc::string8 &json_text,
											 const char *member_name,
											 float &out_value) {
	if (json_text.is_empty() || member_name == nullptr ||
		*member_name == '\0') {
		return false;
	}

	const auto parsed = json::parse(json_text.c_str(), nullptr, false);
	if (parsed.is_discarded() || !parsed.is_object()) {
		return false;
	}

	const auto found = parsed.find(member_name);
	if (found == parsed.end()) {
		return false;
	}

	if (found->is_number()) {
		out_value = found->get<float>();
		return true;
	}
	if (found->is_string()) {
		return try_parse_float_string(
			found->get_ref<const std::string &>().c_str(), out_value);
	}

	return false;
}

} // namespace

pfc::string8 make_extra_info_key(const char *raw_key) {
	std::string key = "foo_opensubsonic_";
	if (raw_key != nullptr) {
		const char *cursor = raw_key;
		while (*cursor != '\0') {
			const unsigned char ch = static_cast<unsigned char>(*cursor);
			if (std::isalnum(ch) != 0) {
				key.push_back(static_cast<char>(std::tolower(ch)));
			} else {
				key.push_back('_');
			}
			++cursor;
		}
	}
	return key.c_str();
}

bool try_get_extra_field(const cached_track_metadata &entry,
						 const char *field_name, pfc::string8 &out_value) {
	out_value.reset();
	if (field_name == nullptr || *field_name == '\0') {
		return false;
	}

	for (const auto &field : entry.extra_fields) {
		if (!field.is_valid()) {
			continue;
		}

		if (pfc::stricmp_ascii(field.key, field_name) == 0) {
			out_value = field.value;
			return true;
		}
	}

	return false;
}

bool try_get_first_extra_field(const cached_track_metadata &entry,
							   std::initializer_list<const char *> field_names,
							   pfc::string8 &out_value) {
	out_value.reset();

	for (const auto *field_name : field_names) {
		if (try_get_extra_field(entry, field_name, out_value) &&
			!out_value.is_empty()) {
			return true;
		}
	}

	return false;
}

bool try_get_replaygain_value(
	const cached_track_metadata &entry,
	std::initializer_list<const char *> direct_field_names,
	const char *nested_field_name, float &out_value) {
	if (try_get_float_extra_field(entry, direct_field_names, out_value)) {
		return true;
	}

	pfc::string8 replaygain_json;
	if (!try_get_first_extra_field(entry, {"replayGain", "replaygain"},
								   replaygain_json) ||
		replaygain_json.is_empty()) {
		return false;
	}

	return try_get_json_float_member(replaygain_json, nested_field_name,
									 out_value);
}

bool try_join_named_array_extra_field(
	const cached_track_metadata &entry,
	std::initializer_list<const char *> field_names, pfc::string8 &out_value) {
	out_value.reset();

	pfc::string8 raw_value;
	if (!try_get_first_extra_field(entry, field_names, raw_value) ||
		raw_value.is_empty()) {
		return false;
	}

	const auto parsed = json::parse(raw_value.c_str(), nullptr, false);
	if (parsed.is_discarded() || !parsed.is_array()) {
		return false;
	}

	std::string combined;
	for (const auto &item : parsed) {
		std::string name;
		if (item.is_object()) {
			const auto found = item.find("name");
			if (found != item.end() && found->is_string()) {
				name = found->get_ref<const std::string &>();
			}
		} else if (item.is_string()) {
			name = item.get_ref<const std::string &>();
		}

		if (name.empty()) {
			continue;
		}

		if (!combined.empty()) {
			combined += ", ";
		}
		combined += name;
	}

	if (combined.empty()) {
		return false;
	}

	out_value = combined.c_str();
	return true;
}

void try_set_meta_from_extra(file_info_impl &info,
							 const cached_track_metadata &entry,
							 const char *meta_name, const char *field_name) {
	pfc::string8 value;
	if (!info.meta_exists(meta_name) &&
		try_get_extra_field(entry, field_name, value) && !value.is_empty()) {
		info.meta_set(meta_name, value);
	}
}

void try_set_meta_from_extra(file_info_impl &info,
							 const cached_track_metadata &entry,
							 const char *meta_name,
							 std::initializer_list<const char *> field_names) {
	pfc::string8 value;
	if (!info.meta_exists(meta_name) &&
		try_get_first_extra_field(entry, field_names, value) &&
		!value.is_empty()) {
		info.meta_set(meta_name, value);
	}
}

void try_set_info_from_extra(file_info_impl &info,
							 const cached_track_metadata &entry,
							 const char *info_name,
							 std::initializer_list<const char *> field_names) {
	pfc::string8 value;
	if (try_get_first_extra_field(entry, field_names, value) &&
		!value.is_empty()) {
		info.info_set(info_name, value);
	}
}

pfc::string8 try_extract_year_from_created(const cached_track_metadata &entry) {
	pfc::string8 created;
	if (!try_get_first_extra_field(entry, {"created", "createdAt", "date"},
								   created) ||
		created.length() < 4) {
		return {};
	}

	const char *text = created.c_str();
	if (std::isdigit(static_cast<unsigned char>(text[0])) == 0 ||
		std::isdigit(static_cast<unsigned char>(text[1])) == 0 ||
		std::isdigit(static_cast<unsigned char>(text[2])) == 0 ||
		std::isdigit(static_cast<unsigned char>(text[3])) == 0) {
		return {};
	}

	pfc::string8 year;
	year.add_string(text, 4);
	return year;
}

pfc::string8 guess_codec(const cached_track_metadata &entry) {
	std::string codec;

	if (!entry.suffix.is_empty()) {
		codec = entry.suffix.c_str();
	} else if (!entry.stream_mime_type.is_empty()) {
		codec = entry.stream_mime_type.c_str();
		const auto semicolon = codec.find(';');
		if (semicolon != std::string::npos) {
			codec.erase(semicolon);
		}
		const auto slash = codec.rfind('/');
		if (slash != std::string::npos && slash + 1 < codec.size()) {
			codec.erase(0, slash + 1);
		}
	}

	for (char &c : codec) {
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	}

	return codec.c_str();
}

bool try_parse_filesize(const char *text, t_filesize &out_value) noexcept {
	out_value = filesize_invalid;
	if (text == nullptr || *text == '\0') {
		return false;
	}

	char *end_ptr = nullptr;
	const auto parsed = std::strtoull(text, &end_ptr, 10);
	if (end_ptr == text) {
		return false;
	}

	out_value = static_cast<t_filesize>(parsed);
	return true;
}

t_filesize try_get_known_total_size(const cached_track_metadata &track) {
	pfc::string8 value;
	if (try_get_extra_field(track, "size", value)) {
		t_filesize parsed = filesize_invalid;
		if (try_parse_filesize(value, parsed)) {
			return parsed;
		}
	}

	if (try_get_extra_field(track, "contentLength", value)) {
		t_filesize parsed = filesize_invalid;
		if (try_parse_filesize(value, parsed)) {
			return parsed;
		}
	}

	return filesize_invalid;
}

pfc::string8 guess_content_type_from_suffix(const char *suffix) {
	if (suffix == nullptr || *suffix == '\0') {
		return {};
	}

	if (subsonic::ends_with_ascii_nocase(suffix, "mp3")) {
		return "audio/mpeg";
	}
	if (subsonic::ends_with_ascii_nocase(suffix, "flac")) {
		return "audio/flac";
	}
	if (subsonic::ends_with_ascii_nocase(suffix, "ogg")) {
		return "audio/ogg";
	}
	if (subsonic::ends_with_ascii_nocase(suffix, "opus")) {
		return "audio/opus";
	}
	if (subsonic::ends_with_ascii_nocase(suffix, "m4a") ||
		subsonic::ends_with_ascii_nocase(suffix, "mp4")) {
		return "audio/mp4";
	}
	if (subsonic::ends_with_ascii_nocase(suffix, "aac")) {
		return "audio/aac";
	}
	if (subsonic::ends_with_ascii_nocase(suffix, "wav")) {
		return "audio/wav";
	}
	if (subsonic::ends_with_ascii_nocase(suffix, "wv")) {
		return "audio/wavpack";
	}
	if (subsonic::ends_with_ascii_nocase(suffix, "ape")) {
		return "audio/ape";
	}

	return {};
}

pfc::string8 make_fallback_content_type(const cached_track_metadata &track) {
	if (!track.stream_mime_type.is_empty()) {
		return track.stream_mime_type;
	}
	return guess_content_type_from_suffix(track.suffix);
}

pfc::string8
make_display_name_from_metadata(const cached_track_metadata &track) {
	pfc::string8 out;

	if (!track.artist.is_empty() && !track.title.is_empty()) {
		out << track.artist << " - " << track.title;
	} else if (!track.title.is_empty()) {
		out = track.title;
	} else if (!track.track_id.is_empty()) {
		out = track.track_id;
	}

	if (!track.suffix.is_empty()) {
		out << "." << track.suffix;
	}

	return out;
}

// Functions that depend on metadata system moved to metadata.cpp
// - make_display_name_for_path
// - overlay_file_info_for_path

void populate_remote_path_stats(foobar2000_io::t_filestats2 &stats) {
	stats.set_file(true);
	stats.set_readonly(true);
	stats.set_remote(true);
	stats.m_size = filesize_invalid;
	stats.m_timestamp = 3;
}

} // namespace subsonic::metadata_utils
