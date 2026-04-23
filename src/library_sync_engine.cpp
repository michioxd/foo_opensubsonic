#include "stdafx.h"

#include "library_sync_engine.h"
#include "utils/subsonic_json_parser.h"

#include <string>

namespace subsonic {
namespace sync {

[[nodiscard]] cached_track_metadata
parse_track_metadata(const nlohmann::json &song) {
	cached_track_metadata entry;
	entry.track_id = json_parser::get_string(song, "id");
	entry.artist = json_parser::get_string(song, "artist");
	entry.title = json_parser::get_string(song, "title");
	entry.album = json_parser::get_string(song, "album");
	entry.cover_art_id = json_parser::get_string(song, "coverArt");
	entry.stream_mime_type = json_parser::get_string(song, "contentType");
	entry.suffix = json_parser::get_string(song, "suffix");
	entry.duration_seconds = json_parser::get_number(song, "duration");
	entry.track_number = json_parser::get_string(song, "track");
	entry.disc_number = json_parser::get_string(song, "discNumber");
	entry.year = json_parser::get_string(song, "year");
	entry.genre = json_parser::get_string(song, "genre");
	entry.bitrate = json_parser::get_string(song, "bitRate");

	// Parse extra fields for extensibility
	if (song.is_object()) {
		entry.extra_fields.reserve(song.size());
		for (auto it = song.begin(); it != song.end(); ++it) {
			const auto value = json_parser::to_metadata_string(it.value());
			if (value.is_empty()) {
				continue;
			}

			track_metadata_field field;
			field.key = it.key().c_str();
			field.value = value;
			entry.extra_fields.push_back(std::move(field));
		}
	}

	return entry;
}

[[nodiscard]] album_sync_summary
parse_album_summary(const nlohmann::json &album_node) {
	album_sync_summary summary;
	summary.album_id = json_parser::get_string(album_node, "id");
	summary.artist_id = json_parser::get_string(album_node, "artistId");
	summary.artist_name = json_parser::get_string(album_node, "artist");

	// Parse track count
	const auto song_count = json_parser::get_number(album_node, "songCount");
	summary.track_count = song_count > 0.0 ? static_cast<size_t>(song_count) : 0;

	return summary;
}

void merge_unique_metadata(std::vector<cached_track_metadata> &target,
						   std::unordered_map<std::string, size_t> &index,
						   const cached_track_metadata &entry) {
	if (!entry.is_valid()) {
		return;
	}

	const std::string key = entry.track_id.c_str();
	const auto found = index.find(key);
	if (found == index.end()) {
		// New entry - add to target and index
		index.emplace(key, target.size());
		target.push_back(entry);
	} else {
		// Existing entry - overwrite with new data
		target[found->second] = entry;
	}
}

[[nodiscard]] std::vector<artist_sync_plan>
group_albums_by_artist(const std::vector<album_sync_summary> &albums) {
	std::vector<artist_sync_plan> plans;
	std::unordered_map<std::string, size_t> artist_index;

	for (const auto &album : albums) {
		// Generate unique artist key
		// Prefer artist_id, fall back to artist_name, then unknown
		std::string key = album.artist_id.is_empty()
							  ? std::string(album.artist_name.c_str())
							  : std::string(album.artist_id.c_str());
		if (key.empty()) {
			key = "<unknown-artist>";
		}

		const auto found = artist_index.find(key);
		if (found == artist_index.end()) {
			// New artist - create plan
			artist_sync_plan plan;
			plan.artist_id = album.artist_id;
			plan.artist_name = album.artist_name;
			plan.albums.push_back(album);
			plan.total_track_count += album.track_count;

			artist_index.emplace(std::move(key), plans.size());
			plans.push_back(std::move(plan));
		} else {
			// Existing artist - merge album
			auto &plan = plans[found->second];
			// Fill in missing artist info if available
			if (plan.artist_name.is_empty()) {
				plan.artist_name = album.artist_name;
			}
			if (plan.artist_id.is_empty()) {
				plan.artist_id = album.artist_id;
			}
			plan.total_track_count += album.track_count;
			plan.albums.push_back(album);
		}
	}

	return plans;
}

// ============================================================================
// PART 2: Orchestration Functions
// ============================================================================

namespace {
constexpr size_t k_album_list_page_size = 500;

[[nodiscard]] pfc::string8 format_size(size_t value) {
	return PFC_string_formatter() << value;
}

[[nodiscard]] pfc::string8 format_offset_message(size_t offset) {
	return PFC_string_formatter() << "Fetching album index... (offset " << offset << ")";
}

} // anonymous namespace

[[nodiscard]] std::vector<artist_sync_plan>
build_artist_sync_plan(sync_context &ctx) {
	std::vector<album_sync_summary> all_albums;

	// Fetch all albums with pagination
	for (size_t offset = 0;; offset += k_album_list_page_size) {
		ctx.check_abort();
		ctx.set_progress_text(format_offset_message(offset));

		// Build query params
		std::vector<query_param> params;
		params.push_back(query_param(pfc::string8("type"), pfc::string8("alphabeticalByArtist")));
		params.push_back(query_param(pfc::string8("size"), format_size(k_album_list_page_size)));
		params.push_back(query_param(pfc::string8("offset"), format_size(offset)));

		// Fetch via callback
		const auto album_list_root = ctx.http_fetch("getAlbumList2.view", params);

		const auto album_list_it = album_list_root.find("albumList2");
		if (album_list_it == album_list_root.end() ||
			!album_list_it->is_object()) {
			break;
		}

		std::vector<album_sync_summary> page_albums;
		json_parser::for_each_member_item(
			*album_list_it, "album", [&](const nlohmann::json &album_node) {
				const auto summary = parse_album_summary(album_node);
				if (!summary.album_id.is_empty()) {
					page_albums.push_back(summary);
				}
			});

		if (page_albums.empty()) {
			break;
		}

		all_albums.insert(all_albums.end(), page_albums.begin(), page_albums.end());

		if (page_albums.size() < k_album_list_page_size) {
			break;
		}
	}

	// Group albums by artist (pure function)
	return group_albums_by_artist(all_albums);
}

} // namespace sync
} // namespace subsonic
