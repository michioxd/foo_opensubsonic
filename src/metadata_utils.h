#pragma once

#include "types.h"

#include <SDK/file.h>
#include <SDK/file_info_impl.h>

#include <initializer_list>

namespace subsonic::metadata_utils {

[[nodiscard]] pfc::string8 make_extra_info_key(const char *raw_key);

[[nodiscard]] bool try_get_extra_field(const cached_track_metadata &entry,
									   const char *field_name,
									   pfc::string8 &out_value);
[[nodiscard]] bool
try_get_first_extra_field(const cached_track_metadata &entry,
						  std::initializer_list<const char *> field_names,
						  pfc::string8 &out_value);

[[nodiscard]] bool
try_get_replaygain_value(const cached_track_metadata &entry,
						 std::initializer_list<const char *> direct_field_names,
						 const char *nested_field_name, float &out_value);
[[nodiscard]] bool try_join_named_array_extra_field(
	const cached_track_metadata &entry,
	std::initializer_list<const char *> field_names, pfc::string8 &out_value);

void try_set_meta_from_extra(file_info_impl &info,
							 const cached_track_metadata &entry,
							 const char *meta_name, const char *field_name);
void try_set_meta_from_extra(file_info_impl &info,
							 const cached_track_metadata &entry,
							 const char *meta_name,
							 std::initializer_list<const char *> field_names);
void try_set_info_from_extra(file_info_impl &info,
							 const cached_track_metadata &entry,
							 const char *info_name,
							 std::initializer_list<const char *> field_names);

[[nodiscard]] pfc::string8
try_extract_year_from_created(const cached_track_metadata &entry);
[[nodiscard]] pfc::string8 guess_codec(const cached_track_metadata &entry);

[[nodiscard]] bool try_parse_filesize(const char *text,
									  t_filesize &out_value) noexcept;
[[nodiscard]] t_filesize
try_get_known_total_size(const cached_track_metadata &track);
[[nodiscard]] pfc::string8 guess_content_type_from_suffix(const char *suffix);
[[nodiscard]] pfc::string8
make_fallback_content_type(const cached_track_metadata &track);
[[nodiscard]] pfc::string8
make_display_name_from_metadata(const cached_track_metadata &track);
[[nodiscard]] pfc::string8 make_display_name_for_path(const char *path);

void populate_remote_path_stats(foobar2000_io::t_filestats2 &stats);
void overlay_file_info_for_path(const char *path, file_info &info);

} // namespace subsonic::metadata_utils
