#pragma once

#include "types.h"

#include <vector>
#include <optional>

namespace subsonic::folder {

struct music_folder {
	pfc::string8 id;
	pfc::string8 name;
};

struct directory_entry {
	pfc::string8 id;
	pfc::string8 parent_id;
	pfc::string8 name;
	bool is_directory = false;
	size_t song_count = 0;
	bool has_song_count = false;
	
	std::optional<cached_track_metadata> track_meta;
};

struct folder_directory_result {
	pfc::string8 id;
	pfc::string8 name;
	std::vector<directory_entry> subdirectories;
	std::vector<cached_track_metadata> child_tracks;
};

} // namespace subsonic::folder