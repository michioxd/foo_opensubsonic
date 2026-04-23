#pragma once

#include "sync_types.h"
#include "types.h"

#include <nlohmann/json.hpp>
#include <SDK/foobar2000.h>

#include <functional>
#include <vector>
#include <unordered_map>

namespace subsonic {
namespace sync {

// ============================================================================
// PART 1: Pure Parsing Functions (no side effects)
// ============================================================================

// Parse track metadata from OpenSubsonic JSON song object
// Returns invalid entry if required fields (id) are missing
[[nodiscard]] cached_track_metadata parse_track_metadata(const nlohmann::json &song);

// Parse album summary from JSON album object
// Used by getAlbumList2 endpoint
[[nodiscard]] album_sync_summary parse_album_summary(const nlohmann::json &album_node);

// Merge unique metadata entry into target vector
// Deduplicates by track_id - if exists, overwrites with new entry
// Updates index map for O(1) lookup
void merge_unique_metadata(std::vector<cached_track_metadata> &target,
						   std::unordered_map<std::string, size_t> &index,
						   const cached_track_metadata &entry);

// Group albums by artist, creating sync plans
// Returns vector of artist plans with aggregated track counts
[[nodiscard]] std::vector<artist_sync_plan>
group_albums_by_artist(const std::vector<album_sync_summary> &albums);

// ============================================================================
// PART 2: Orchestration with Dependency Injection
// ============================================================================

// Callback for HTTP requests - returns JSON response
// Throws on HTTP errors
using http_fetch_callback = std::function<nlohmann::json(
	const char *endpoint,
	const std::vector<query_param> &params)>;

// Callback for progress updates
using progress_callback = std::function<void(const char *message)>;

// Callback for progress with numeric values
using progress_numeric_callback = std::function<void(size_t current, size_t total)>;

// Callback for abort checking
using abort_check_callback = std::function<void()>;

// Sync context - bundles all callbacks for sync operations
struct sync_context {
	http_fetch_callback http_fetch;
	progress_callback set_progress_text;
	progress_numeric_callback set_progress_numeric;
	abort_check_callback check_abort;
};

// Build artist sync plan from server
// Fetches album list with pagination, groups by artist
[[nodiscard]] std::vector<artist_sync_plan>
build_artist_sync_plan(sync_context &ctx);

// Fetch full library sync result
// Orchestrates: build plan → fetch albums → parse tracks → deduplicate
[[nodiscard]] library_sync_result
fetch_library_sync_result(sync_context &ctx);

// Fetch remote playlists from server
// Returns playlist sync results with deduplicated metadata
[[nodiscard]] std::vector<remote_playlist_sync_result>
fetch_remote_playlists(sync_context &ctx,
					   std::vector<cached_track_metadata> &playlist_metadata_entries);

} // namespace sync
} // namespace subsonic
