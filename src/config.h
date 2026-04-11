#pragma once

#include "types.h"

#include <SDK/abort_callback.h>

namespace subsonic::config {

[[nodiscard]] server_credentials load_server_credentials();
void save_server_credentials(const server_credentials &credentials);

[[nodiscard]] pfc::string8 component_profile_directory();
[[nodiscard]] pfc::string8 cache_root_directory();
[[nodiscard]] pfc::string8 artwork_cache_directory();
[[nodiscard]] pfc::string8 database_path();
[[nodiscard]] pfc::string8 metadata_json_cache_path();

void ensure_cache_layout(abort_callback &abort);

} // namespace subsonic::config