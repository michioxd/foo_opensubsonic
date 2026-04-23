#pragma once

#include "types.h"

#include <SDK/metadb.h>

#include <vector>

namespace subsonic {
namespace sync {

// Result of library sync operation
// Contains all synced track metadata and corresponding metadb handles
struct library_sync_result {
	std::vector<cached_track_metadata> entries;
	metadb_handle_list handles;
};

// Summary of an album during sync
// Used to track progress and determine which albums to fetch
struct album_sync_summary {
	pfc::string8 album_id;
	pfc::string8 artist_id;
	pfc::string8 artist_name;
	size_t track_count = 0;
};

// Plan for syncing a single artist
// Contains all albums belonging to this artist
struct artist_sync_plan {
	pfc::string8 artist_id;
	pfc::string8 artist_name;
	std::vector<album_sync_summary> albums;
	size_t total_track_count = 0;
};

// Result of syncing a remote playlist
// Contains playlist name and handles for all tracks
struct remote_playlist_sync_result {
	pfc::string8 remote_id;
	pfc::string8 name;
	metadb_handle_list handles;
};

// Overall sync outcome
// Combines library and playlist sync results
struct sync_outcome {
	bool includes_library = false;
	bool includes_playlists = false;
	library_sync_result library;
	std::vector<remote_playlist_sync_result> playlists;
	std::vector<cached_track_metadata> playlist_metadata_entries;
};

// Sync mode selection
enum class sync_mode {
	library_only,    // Sync only music library
	playlists_only,  // Sync only playlists
	all              // Sync both library and playlists
};

} // namespace sync
} // namespace subsonic
