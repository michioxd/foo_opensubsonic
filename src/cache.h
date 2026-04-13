#pragma once

#include "types.h"

#include <SDK/abort_callback.h>
#include <SDK/threaded_process.h>

#include <vector>

namespace subsonic::cache {

void initialize(abort_callback &abort);
void reset(abort_callback &abort);

void upsert_track_metadata(const cached_track_metadata &entry);
void remove_track_metadata(const char *server_id, const char *track_id);
[[nodiscard]] bool try_get_track_metadata(const char *server_id,
										  const char *track_id,
										  cached_track_metadata &out);
[[nodiscard]] std::vector<cached_track_metadata> load_all_track_metadata();
void replace_track_metadata(const std::vector<cached_track_metadata> &entries,
							threaded_process_status &status,
							abort_callback &abort);

void upsert_artwork_entry(const artwork_cache_entry &entry);
void remove_artwork_entry(const char *server_id, const char *cover_art_id);
void remove_server_artwork_entries(const char *server_id,
								   threaded_process_status &status,
								   abort_callback &abort);
[[nodiscard]] bool try_get_artwork_entry(const char *server_id,
										 const char *cover_art_id,
										 artwork_cache_entry &out);
[[nodiscard]] std::vector<artwork_cache_entry> load_all_artwork_entries();

} // namespace subsonic::cache