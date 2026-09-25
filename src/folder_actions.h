#pragma once

#include "types.h"
#include <vector>

namespace subsonic::folder_actions {

// Register fetched directory tracks in the metadata repository
// Publishes them to the local cache and hints them to metadb_io for instant UI updates
void register_directory_tracks_metadata(const std::vector<cached_track_metadata> &tracks);

// Creates or replaces a playlist with the folder contents and starts playback
// Replaces the "[Folder] <folder_name>" playlist if it already exists
void play_folder_as_playlist(const char *folder_name, const std::vector<cached_track_metadata> &tracks);

// Adds a single track to the currently active playlist
// If enqueue_only is false, it immediately starts playback of the added track
void play_or_enqueue_track(const cached_track_metadata &track, bool enqueue_only);

} // namespace subsonic::folder_actions