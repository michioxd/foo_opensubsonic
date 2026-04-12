#pragma once

namespace subsonic::library {

void sync_library_async();
void sync_playlists_async();
void sync_all_async();
void cache_artwork_async();
void clear_cache_async();

} // namespace subsonic::library