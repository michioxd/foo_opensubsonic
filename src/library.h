#pragma once

#include "types.h"

namespace subsonic::library {

void sync_library_async();
void sync_library_async(const char *server_id);
void sync_playlists_async();
void sync_playlists_async(const char *server_id);
void sync_all_async();
void sync_all_async(const char *server_id);
void sync_server_async(const char *server_id);
void cache_artwork_async();
void clear_artwork_cache_async();
void clear_cache_async();
void remove_servers_data_async(const std::vector<server_credentials> &servers);

} // namespace subsonic::library