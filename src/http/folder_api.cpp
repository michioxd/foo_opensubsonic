#include "../stdafx.h"

#include "folder_api.h"
#include "../utils/subsonic_json_parser.h"
#include "../utils/utils.h"
#include "../library_sync_engine.h"

namespace subsonic::folder_api {

static void extract_counts(const nlohmann::json &node, folder::directory_entry &entry) {
	auto songCountIt = node.find("songCount");
	auto trackCountIt = node.find("trackCount");
	if (songCountIt != node.end() && songCountIt->is_number()) {
		entry.song_count = static_cast<size_t>(songCountIt->get<double>());
		entry.has_song_count = true;
	} else if (trackCountIt != node.end() && trackCountIt->is_number()) {
		entry.song_count = static_cast<size_t>(trackCountIt->get<double>());
		entry.has_song_count = true;
	} else {
		entry.song_count = 0;
		entry.has_song_count = false;
	}
}

[[nodiscard]] std::vector<folder::music_folder>
fetch_music_folders(IHttpClient &http_client, abort_callback &abort) {
	std::vector<folder::music_folder> result;
	
	try {
		const auto root = http_client.fetch_api("getMusicFolders.view", {}, abort);
		const auto folders_it = root.find("musicFolders");
		
		if (folders_it != root.end()) {
			if (folders_it->is_object()) {
				json_parser::for_each_member_item(*folders_it, "musicFolder", [&](const nlohmann::json &node) {
					folder::music_folder mf;
					mf.id = json_parser::get_string(node, "id");
					mf.name = json_parser::get_string(node, "name");
					if (mf.name.is_empty()) {
						mf.name = json_parser::get_string(node, "title");
					}
					if (!mf.id.is_empty()) {
						result.push_back(std::move(mf));
					}
				});
			} else if (folders_it->is_array()) {
				for (const auto &node : *folders_it) {
					folder::music_folder mf;
					mf.id = json_parser::get_string(node, "id");
					mf.name = json_parser::get_string(node, "name");
					if (!mf.id.is_empty()) {
						result.push_back(std::move(mf));
					}
				}
			}
		}
	} catch (const std::exception &e) {
		log_error("folder_api", (PFC_string_formatter() << "getMusicFolders.view failed: " << e.what()).c_str());
	}

	if (result.empty()) {
		try {
			const auto root = http_client.fetch_api("getIndexes.view", {}, abort);
			const auto indexes_it = root.find("indexes");
			if (indexes_it != root.end()) {
				folder::music_folder mf;
				mf.id = "indexes_root";
				mf.name = "Music Library (Indexes)";
				result.push_back(std::move(mf));
			}
		} catch (const std::exception &e) {
			log_error("folder_api", (PFC_string_formatter() << "getIndexes.view failed: " << e.what()).c_str());
		}
	}

	return result;
}

[[nodiscard]] folder::folder_directory_result
fetch_directory(IHttpClient &http_client, const char *dir_id, bool is_root_folder, abort_callback &abort) {
	folder::folder_directory_result result;
	
	if (dir_id == nullptr || *dir_id == '\0') {
		return result;
	}

	if (is_root_folder || subsonic::strings_equal(dir_id, "indexes_root")) {
		try {
			std::vector<query_param> params;
			if (!subsonic::strings_equal(dir_id, "indexes_root")) {
				params.emplace_back("musicFolderId", dir_id);
			}

			const auto root = http_client.fetch_api("getIndexes.view", params, abort);
			const auto indexes_it = root.find("indexes");
			if (indexes_it != root.end() && indexes_it->is_object()) {
				result.id = dir_id;
				result.name = "Music Library";

				json_parser::for_each_member_item(*indexes_it, "folder", [&](const nlohmann::json &node) {
					folder::directory_entry entry;
					entry.id = json_parser::get_string(node, "id");
					entry.name = json_parser::get_string(node, "name");
					entry.is_directory = true;
					
					extract_counts(node, entry);
					if (entry.has_song_count && entry.song_count == 0) return;

					if (!entry.id.is_empty()) {
						result.subdirectories.push_back(std::move(entry));
					}
				});

				json_parser::for_each_member_item(*indexes_it, "index", [&](const nlohmann::json &index_node) {
					json_parser::for_each_member_item(index_node, "artist", [&](const nlohmann::json &artist_node) {
						folder::directory_entry entry;
						entry.id = json_parser::get_string(artist_node, "id");
						entry.name = json_parser::get_string(artist_node, "name");
						entry.is_directory = true;

						extract_counts(artist_node, entry);
						if (entry.has_song_count && entry.song_count == 0) return;

						if (!entry.id.is_empty()) {
							result.subdirectories.push_back(std::move(entry));
						}
					});
				});
			}
		} catch (const std::exception &e) {
			log_error("folder_api", (PFC_string_formatter() << "getIndexes parse failed: " << e.what()).c_str());
		}
		return result;
	}

	std::vector<query_param> params;
	params.emplace_back("id", dir_id);

	try {
		const auto root = http_client.fetch_api("getMusicDirectory.view", params, abort);
		const auto dir_it = root.find("directory");

		if (dir_it != root.end() && dir_it->is_object()) {
			result.id = json_parser::get_string(*dir_it, "id");
			result.name = json_parser::get_string(*dir_it, "name");

			json_parser::for_each_member_item(*dir_it, "child", [&](const nlohmann::json &node) {
				bool is_dir = false;
				const auto is_dir_it = node.find("isDir");
				if (is_dir_it != node.end() && is_dir_it->is_boolean()) {
					is_dir = is_dir_it->get<bool>();
				}

				if (is_dir) {
					folder::directory_entry entry;
					entry.id = json_parser::get_string(node, "id");
					entry.parent_id = json_parser::get_string(node, "parent");
					entry.is_directory = true;
					entry.name = json_parser::get_string(node, "title");
					if (entry.name.is_empty()) {
						entry.name = json_parser::get_string(node, "name");
					}

					extract_counts(node, entry);
					if (entry.has_song_count && entry.song_count == 0) return;

					if (!entry.id.is_empty()) {
						result.subdirectories.push_back(std::move(entry));
					}
				} else {
					auto track_meta = sync::parse_track_metadata(node);
					if (track_meta.is_valid()) {
						result.child_tracks.push_back(std::move(track_meta));
					}
				}
			});
		}
	} catch (const std::exception &e) {
		log_error("folder_api", (PFC_string_formatter() << "getMusicDirectory.view failed for id=" << dir_id << ": " << e.what()).c_str());
	}

	return result;
}

} // namespace subsonic::folder_api