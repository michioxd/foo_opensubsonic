#pragma once

#include "sync_types.h"
#include "types.h"

#include <nlohmann/json.hpp>
#include <SDK/foobar2000.h>

#include <vector>
#include <unordered_map>

namespace subsonic {
namespace sync {

// Library Sync Engine - Pure parsing and deduplication logic
// Extracted from library.cpp for testability
// These functions have NO side effects (no HTTP, no UI, no cache writes)

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

} // namespace sync
} // namespace subsonic
