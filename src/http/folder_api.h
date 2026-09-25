#pragma once

#include "../folder_types.h"
#include "http_client_interface.h"

#include <vector>

namespace subsonic::folder_api {

// Fetch top-level music folders from the server
// Wraps the 'getMusicFolders.view' OpenSubsonic endpoint
[[nodiscard]] std::vector<folder::music_folder>
fetch_music_folders(IHttpClient &http_client, abort_callback &abort);

// Fetch the contents of a specific directory by ID
// If is_root_folder is true, uses getIndexes.view instead of getMusicDirectory.view
[[nodiscard]] folder::folder_directory_result
fetch_directory(IHttpClient &http_client, const char *dir_id, bool is_root_folder, abort_callback &abort);

} // namespace subsonic::folder_api