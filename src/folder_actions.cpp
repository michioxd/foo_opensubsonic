#include "stdafx.h"
#include "folder_actions.h"
#include "service_locator.h"
#include "utils/track_path_util.h"

#include <SDK/metadb.h>
#include <SDK/playlist.h>

namespace subsonic::folder_actions {

void register_directory_tracks_metadata(const std::vector<cached_track_metadata> &tracks) {
	auto &repo = service_locator::metadata_repository();
	for (const auto &track : tracks) {
		if (track.is_valid()) {
			repo.publish(track);
		}
	}
}

void play_folder_as_playlist(const char *folder_name, const std::vector<cached_track_metadata> &tracks) {
	pfc::string8 playlist_name = "[Folder] ";
	if (folder_name != nullptr && *folder_name != '\0') {
		playlist_name += folder_name;
	} else {
		playlist_name += "Unknown";
	}
	
	metadb_handle_list handles;
	auto metadb_api = metadb::get();
	for (const auto &track : tracks) {
		if (!track.is_valid()) {
			continue;
		}
		
		pfc::string8 path = track_path_util::make_subsonic_path(track.track_id);
		auto handle = metadb_api->handle_create(path, 0);
		if (handle.is_valid()) {
			handles.add_item(handle);
		}
	}

	auto pm = playlist_manager::get();
	
	t_size pl_index = pm->find_playlist(playlist_name);
	if (pl_index == SIZE_MAX) {
		pl_index = pm->create_playlist(playlist_name, SIZE_MAX, SIZE_MAX);
	}

	if (pl_index != SIZE_MAX) {
		pm->playlist_clear(pl_index);
		
		if (handles.get_count() > 0) {
			pm->playlist_add_items(pl_index, handles, pfc::bit_array_false());
		}
		
		pm->set_active_playlist(pl_index);
		pm->set_playing_playlist(pl_index);
		
		if (handles.get_count() > 0) {
			pm->playlist_execute_default_action(pl_index, 0);
		}
	}
}

void play_or_enqueue_track(const cached_track_metadata &track, bool enqueue_only) {
	if (!track.is_valid()) {
		return;
	}

	auto metadb_api = metadb::get();
	pfc::string8 path = track_path_util::make_subsonic_path(track.track_id);
	auto handle = metadb_api->handle_create(path, 0);
	if (!handle.is_valid()) {
		return;
	}

	metadb_handle_list handles;
	handles.add_item(handle);

	auto pm = playlist_manager::get();
	t_size pl_index = pm->get_active_playlist();
	
	if (pl_index == SIZE_MAX) {
		pl_index = pm->create_playlist("OpenSubsonic", SIZE_MAX, SIZE_MAX);
		pm->set_active_playlist(pl_index);
	}

	t_size item_index = pm->playlist_get_item_count(pl_index);
	pm->playlist_add_items(pl_index, handles, pfc::bit_array_false());

	if (!enqueue_only) {
		pm->set_playing_playlist(pl_index);
		pm->playlist_execute_default_action(pl_index, item_index);
	}
}

} // namespace subsonic::folder_actions