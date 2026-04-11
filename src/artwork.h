#pragma once

#include "types.h"

#include <SDK/abort_callback.h>
#include <SDK/album_art.h>

namespace subsonic::artwork {

[[nodiscard]] bool is_supported_path(const char *path) noexcept;
[[nodiscard]] bool try_load(const char *path, const GUID &art_id,
							album_art_data_ptr &out_data,
							album_art_path_list::ptr &out_paths,
							abort_callback &abort);

} // namespace subsonic::artwork