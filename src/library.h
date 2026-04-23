#pragma once

namespace subsonic::library {

// Synchronize library metadata from OpenSubsonic server
// Fetches artists, albums, and tracks via paginated API calls
// Updates in-memory cache and persistent storage
// Non-blocking: runs in background thread with progress UI
void sync_library_async();

// Synchronize playlists from OpenSubsonic server
// Fetches playlist metadata and creates foobar2000 playlists
// Non-blocking: runs in background thread with progress UI
void sync_playlists_async();

// Synchronize both library and playlists
// Equivalent to calling sync_library_async() then sync_playlists_async()
void sync_all_async();

// Pre-fetch and cache artwork for all albums in library
// Downloads cover art images and stores in local cache
// Non-blocking: runs in background thread with progress UI
void cache_artwork_async();

// Clear cached artwork images from disk
// Does not affect metadata cache
void clear_artwork_cache_async();

// Clear all cached data (metadata + artwork)
// Removes both in-memory cache and persistent storage
// User will need to sync again to restore library
void clear_cache_async();

} // namespace subsonic::library