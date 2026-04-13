#pragma once

#include "types.h"

#include <SDK/abort_callback.h>
#include <SDK/threaded_process.h>

#include <vector>

namespace subsonic::library {

enum class sync_mode { library_only, playlists_only, all };

struct library_sync_result {
	std::vector<cached_track_metadata> entries;
	metadb_handle_list handles;
};

struct remote_playlist_sync_result {
	pfc::string8 remote_id;
	pfc::string8 name;
	metadb_handle_list handles;
};

struct sync_outcome {
	bool includes_library = false;
	bool includes_playlists = false;
	server_credentials server;
	library_sync_result library;
	std::vector<remote_playlist_sync_result> playlists;
	std::vector<cached_track_metadata> playlist_metadata_entries;
};

[[nodiscard]] metadb_handle_list
load_library_playlist_items(const server_credentials &credentials);
[[nodiscard]] pfc::string8 make_success_message(const sync_outcome &outcome);
[[nodiscard]] sync_outcome perform_sync(sync_mode mode, const char *server_id,
										threaded_process_status &status,
										abort_callback &abort);
void apply_sync_outcome(const sync_outcome &outcome);
void refresh_managed_playlists(const server_credentials &credentials);
void clear_managed_playlists();
void clear_managed_playlists(const char *server_id);

} // namespace subsonic::library
