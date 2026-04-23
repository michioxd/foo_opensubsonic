#pragma once

#include <cstdint>

namespace subsonic::time_utils {

// Returns current Unix timestamp in milliseconds
// Used for tracking cache access times and playback timestamps
[[nodiscard]] std::uint64_t current_unix_time_ms() noexcept;

} // namespace subsonic::time_utils
