#pragma once

#include "types.h"

#include <SDK/abort_callback.h>
#include <SDK/album_art.h>
#include <SDK/threaded_process.h>

namespace subsonic::artwork {

struct prefetch_artwork_result {
	size_t track_count = 0;
	size_t unique_artwork_count = 0;
	size_t already_cached_count = 0;
	size_t downloaded_count = 0;
};

[[nodiscard]] bool is_supported_path(const char *path) noexcept;
[[nodiscard]] bool try_load(const char *path, const GUID &art_id,
							album_art_data_ptr &out_data,
							album_art_path_list::ptr &out_paths,
							abort_callback &abort);
[[nodiscard]] prefetch_artwork_result
prefetch_for_library_items(metadb_handle_list_cref items,
						   threaded_process_status &status,
						   abort_callback &abort);

} // namespace subsonic::artwork