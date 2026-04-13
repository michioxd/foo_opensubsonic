#pragma once

#include "types.h"

#include <SDK/abort_callback.h>

namespace subsonic::config {

[[nodiscard]] server_settings load_server_settings();
void save_server_settings(const server_settings &settings);

[[nodiscard]] server_credentials load_server_credentials();
[[nodiscard]] bool try_get_server_credentials(const char *server_id,
											  server_credentials &out);
[[nodiscard]] pfc::string8 load_selected_server_id();
void save_server_credentials(const server_credentials &credentials);

[[nodiscard]] pfc::string8 component_profile_directory();
[[nodiscard]] pfc::string8 cache_root_directory();
[[nodiscard]] pfc::string8 artwork_cache_directory();
[[nodiscard]] pfc::string8 database_path();
[[nodiscard]] pfc::string8 metadata_json_cache_path();

void ensure_cache_layout(abort_callback &abort);

} // namespace subsonic::config