#pragma once

#include "types.h"

#include <SDK/abort_callback.h>

namespace subsonic::metadata {

void initialize(abort_callback &abort);
void shutdown();

// Try to get cached track metadata by track_id
// Returns false if track_id not found in cache
[[nodiscard]] bool try_get_track_metadata(const char *track_id,
										  cached_track_metadata &out);

// Try to get cached track metadata by virtual path (subsonic://track/{id})
// Returns false if path invalid or track_id not found in cache
[[nodiscard]] bool try_get_track_metadata_for_path(const char *path,
												   cached_track_metadata &out);

// Try to create foobar2000 file_info from cached metadata for a virtual path
// Returns false if path invalid or metadata not cached
[[nodiscard]] bool try_make_file_info_for_path(const char *path,
											   file_info_impl &out);

// Publish single metadata entry to cache
// Writes to both in-memory cache and persistent storage
// Thread-safe: can be called from any thread
void publish_track_metadata(const cached_track_metadata &entry);

// Clear all cached metadata (both in-memory and persistent storage)
void clear_all(threaded_process_status &status, abort_callback &abort);

// Merge new metadata entries into cache, deduplicating by track_id
// Existing entries are preserved and merged with new data
// Used during library sync to update metadata without clearing existing cache
void merge_track_metadata(const std::vector<cached_track_metadata> &entries,
						  threaded_process_status &status,
						  abort_callback &abort);

// Replace entire metadata cache with new entries
// Discards all existing cached metadata
// Used for full library resync
void replace_track_metadata(const std::vector<cached_track_metadata> &entries,
							threaded_process_status &status,
							abort_callback &abort);

// Remove single track metadata from cache
void remove_track_metadata(const char *track_id);

// Trigger metadata refresh for a specific track
// Notifies foobar2000 to reload metadata from cache
void refresh_track(const char *track_id);

// Create display name for track path (for VFS)
// Tries to use metadata if available, falls back to track_id
[[nodiscard]] pfc::string8 make_display_name_for_path(const char *path);

// Overlay file_info with metadata from path (for VFS)
// Merges cached metadata into provided file_info
void overlay_file_info_for_path(const char *path, file_info &info);

} // namespace subsonic::metadata