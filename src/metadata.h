#pragma once

#include "types.h"

#include <SDK/abort_callback.h>

namespace subsonic::metadata {

void initialize(abort_callback &abort);
void shutdown();

[[nodiscard]] bool try_get_track_metadata(const char *track_id,
                                          cached_track_metadata &out);
[[nodiscard]] bool try_get_track_metadata_for_path(const char *path,
                                                   cached_track_metadata &out);
[[nodiscard]] bool try_make_file_info_for_path(const char *path,
                                               file_info_impl &out);

void publish_track_metadata(const cached_track_metadata &entry);
void clear_all(threaded_process_status &status, abort_callback &abort);
void merge_track_metadata(const std::vector<cached_track_metadata> &entries,
                          threaded_process_status &status,
                          abort_callback &abort);
void replace_track_metadata(const std::vector<cached_track_metadata> &entries,
                            threaded_process_status &status,
                            abort_callback &abort);
void remove_track_metadata(const char *track_id);
void refresh_track(const char *track_id);

} // namespace subsonic::metadata