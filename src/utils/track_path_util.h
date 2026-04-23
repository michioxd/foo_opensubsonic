#pragma once

#include <SDK/foobar2000.h>

namespace subsonic {
namespace track_path_util {

// Check if path is a Subsonic virtual path
// Returns true if path starts with "subsonic://" (case-insensitive)
// Used by VFS, metadata, and playback systems to identify virtual tracks
[[nodiscard]] bool is_subsonic_path(const char *path) noexcept;

// Create virtual path for a track: subsonic://{track_id}
// Example: make_subsonic_path("123") → "subsonic://123"
// Used when creating metadb handles for remote tracks
[[nodiscard]] pfc::string8 make_subsonic_path(const char *track_id);

// Extract track_id from subsonic://{track_id} path
// Strips k_scheme ("subsonic://") prefix to produce out_track_id
// Returns true if extraction succeeded, false if path is invalid
// Example: extract_track_id_from_path("subsonic://123", out) → out="123"
// See make_subsonic_path() for reverse operation
// Used for reverse lookup: path → track_id → metadata
[[nodiscard]] bool extract_track_id_from_path(const char *path,
											  pfc::string_base &out_track_id);

} // namespace track_path_util
} // namespace subsonic
