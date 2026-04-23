#include "stdafx.h"

#include "artwork.h"
#include "library.h"

#include "cache.h"
#include "config.h"
#include "http.h"
#include "metadata.h"
#include "sync_types.h"
#include "utils/subsonic_json_parser.h"
#include "utils/utils.h"

#include <SDK/core_api.h>
#include <SDK/menu.h>
#include <SDK/metadb.h>
#include <SDK/playlist.h>
#include <SDK/popup_message.h>
#include <SDK/threaded_process.h>
#include <SDK/threadsLite.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace {

using json = nlohmann::json;

constexpr const char *k_library_scope = "library";
constexpr const char *k_remote_playlist_scope = "remote-playlist";
constexpr const char *k_library_playlist_name = "OpenSubsonic Library";
constexpr const char *k_remote_playlist_prefix = "OpenSubsonic: ";

const GUID guid_mainmenu_group_opensubsonic = {
	0x9a92cfbf,
	0x266d,
	0x4282,
	{0x94, 0x4d, 0xa7, 0x95, 0xa9, 0x96, 0x51, 0x92}};
const GUID guid_mainmenu_sync_library = {
	0xb767dbb7,
	0x09bb,
	0x4e83,
	{0xac, 0x39, 0x5c, 0xa2, 0x95, 0x8d, 0x73, 0x9d}};
const GUID guid_mainmenu_sync_playlists = {
	0x4af7a340,
	0xf4fc,
	0x45be,
	{0x9a, 0x1b, 0x25, 0x59, 0x75, 0x95, 0x8c, 0x7d}};
const GUID guid_mainmenu_sync_all = {
	0x4b649d1d,
	0xb76a,
	0x4df9,
	{0x95, 0x34, 0x82, 0xec, 0x17, 0xaa, 0xb5, 0x7f}};
const GUID guid_playlist_property_scope = {
	0xe7ea2212,
	0x31c8,
	0x4d44,
	{0x93, 0xad, 0x1d, 0x64, 0x1b, 0x6a, 0xae, 0x4d}};
const GUID guid_playlist_property_remote_id = {
	0xa1327aab,
	0xaacd,
	0x4330,
	{0xbf, 0x3a, 0xb7, 0xbb, 0x03, 0x59, 0x9f, 0x1d}};

// Mutex to guard sync operations (library sync, playlist sync, cache operations)
// Prevents concurrent sync jobs which could corrupt cache state
std::mutex g_sync_mutex;
bool g_sync_in_progress = false; // Protected by g_sync_mutex

// RAII guard for sync operations
// Ensures g_sync_in_progress is always reset even if operation throws
class sync_guard {
	std::unique_lock<std::mutex> m_lock;
	bool m_acquired = false;

public:
	sync_guard() = default;

	// Try to acquire sync lock
	// Returns false if another sync is already in progress
	bool try_acquire() {
		m_lock = std::unique_lock<std::mutex>(g_sync_mutex, std::try_to_lock);
		if (!m_lock.owns_lock()) {
			return false;
		}
		if (g_sync_in_progress) {
			m_lock.unlock();
			return false;
		}
		g_sync_in_progress = true;
		m_acquired = true;
		return true;
	}

	~sync_guard() {
		if (m_acquired && m_lock.owns_lock()) {
			g_sync_in_progress = false;
		}
	}

	// Non-copyable but movable
	sync_guard(const sync_guard &) = delete;
	sync_guard &operator=(const sync_guard &) = delete;

	sync_guard(sync_guard &&other) noexcept
		: m_lock(std::move(other.m_lock)), m_acquired(other.m_acquired) {
		other.m_acquired = false; // Transfer ownership
	}

	sync_guard &operator=(sync_guard &&other) noexcept {
		if (this != &other) {
			// Release current lock if held
			if (m_acquired && m_lock.owns_lock()) {
				g_sync_in_progress = false;
			}
			// Transfer from other
			m_lock = std::move(other.m_lock);
			m_acquired = other.m_acquired;
			other.m_acquired = false;
		}
		return *this;
	}
};

// Sync data structures moved to sync_types.h
using subsonic::sync::library_sync_result;
using subsonic::sync::album_sync_summary;
using subsonic::sync::artist_sync_plan;
using subsonic::sync::remote_playlist_sync_result;
using subsonic::sync::sync_outcome;
using subsonic::sync::sync_mode;

constexpr size_t k_album_list_page_size = 500;

[[nodiscard]] pfc::string8 make_remote_playlist_name(const char *name) {
	pfc::string8 out = k_remote_playlist_prefix;
	out += (name != nullptr && *name != '\0') ? name : "Unnamed Playlist";
	return out;
}

[[nodiscard]] metadb_handle_ptr make_handle_for_track_id(const char *track_id) {
	if (track_id == nullptr || *track_id == '\0') {
		return {};
	}

	const pfc::string8 path = subsonic::make_subsonic_path(track_id);
	return static_api_ptr_t<metadb>()->handle_create(path, 0);
}

// JSON parsing utilities moved to subsonic_json_parser.h/cpp

[[nodiscard]] size_t json_get_size_t(const json &object, const char *member) {
	const auto value = subsonic::json_parser::get_number(object, member);
	return value > 0.0 ? static_cast<size_t>(value) : 0;
}

[[nodiscard]] json parse_subsonic_response(const pfc::string8 &payload) {
	json document = json::parse(payload.c_str());

	const auto root_it = document.find("subsonic-response");
	if (root_it == document.end() || !root_it->is_object()) {
		throw std::runtime_error(
			"OpenSubsonic response is missing 'subsonic-response'.");
	}

	const json &root = *root_it;
	const auto status = subsonic::json_parser::get_string(root, "status");
	if (!subsonic::strings_equal(status, "ok")) {
		pfc::string8 message = "OpenSubsonic request failed";
		const auto error_it = root.find("error");
		if (error_it != root.end() && error_it->is_object()) {
			const auto detail = subsonic::json_parser::get_string(*error_it, "message");
			if (!detail.is_empty()) {
				message = detail;
			}
		}
		throw std::runtime_error(message.c_str());
	}

	return root;
}

[[nodiscard]] json fetch_endpoint(
	const subsonic::server_credentials &credentials, const char *endpoint,
	const std::vector<subsonic::query_param> &params, abort_callback &abort) {
	const auto response =
		subsonic::http::open_api(credentials, endpoint, abort, params);
	if (!subsonic::http::status_is_success(response)) {
		throw std::runtime_error(PFC_string_formatter()
								 << "HTTP request failed for "
								 << (endpoint != nullptr ? endpoint : "(null)")
								 << ": " << response.status_text);
	}

	return parse_subsonic_response(subsonic::http::read_text(response, abort));
}

[[nodiscard]] std::vector<subsonic::query_param>
make_query_params(std::initializer_list<subsonic::query_param> params) {
	return std::vector<subsonic::query_param>(params);
}

[[nodiscard]] subsonic::cached_track_metadata
parse_track_metadata(const json &song) {
	subsonic::cached_track_metadata entry;
	entry.track_id = subsonic::json_parser::get_string(song, "id");
	entry.artist = subsonic::json_parser::get_string(song, "artist");
	entry.title = subsonic::json_parser::get_string(song, "title");
	entry.album = subsonic::json_parser::get_string(song, "album");
	entry.cover_art_id = subsonic::json_parser::get_string(song, "coverArt");
	entry.stream_mime_type = subsonic::json_parser::get_string(song, "contentType");
	entry.suffix = subsonic::json_parser::get_string(song, "suffix");
	entry.duration_seconds = subsonic::json_parser::get_number(song, "duration");
	entry.track_number = subsonic::json_parser::get_string(song, "track");
	entry.disc_number = subsonic::json_parser::get_string(song, "discNumber");
	entry.year = subsonic::json_parser::get_string(song, "year");
	entry.genre = subsonic::json_parser::get_string(song, "genre");
	entry.bitrate = subsonic::json_parser::get_string(song, "bitRate");

	if (song.is_object()) {
		entry.extra_fields.reserve(song.size());
		for (auto it = song.begin(); it != song.end(); ++it) {
			const auto value = subsonic::json_parser::to_metadata_string(it.value());
			if (value.is_empty()) {
				continue;
			}

			subsonic::track_metadata_field field;
			field.key = it.key().c_str();
			field.value = value;
			entry.extra_fields.push_back(std::move(field));
		}
	}

	return entry;
}

void merge_unique_metadata(std::vector<subsonic::cached_track_metadata> &target,
						   std::unordered_map<std::string, size_t> &index,
						   const subsonic::cached_track_metadata &entry) {
	if (!entry.is_valid()) {
		return;
	}

	const std::string key = entry.track_id.c_str();
	const auto found = index.find(key);
	if (found == index.end()) {
		index.emplace(key, target.size());
		target.push_back(entry);
	} else {
		target[found->second] = entry;
	}
}

void append_handle(metadb_handle_list &handles,
				   const subsonic::cached_track_metadata &entry) {
	const auto handle = make_handle_for_track_id(entry.track_id);
	if (handle.is_valid()) {
		handles.add_item(handle);
	}
}

void set_library_fetch_status(threaded_process_status &status,
							  size_t artist_index, size_t artist_total,
							  size_t album_index, size_t album_total,
							  size_t track_index, size_t track_total) {
	status.set_item(PFC_string_formatter()
					<< "Fetching artist " << artist_index << " of "
					<< artist_total << " (" << album_index << " of "
					<< album_total << " albums, " << track_index << " of "
					<< track_total << " tracks)");
}

[[nodiscard]] std::vector<artist_sync_plan>
build_artist_sync_plan(const subsonic::server_credentials &credentials,
					   threaded_process_status &status, abort_callback &abort) {
	std::vector<artist_sync_plan> plans;
	std::unordered_map<std::string, size_t> artist_index;

	for (size_t offset = 0;; offset += k_album_list_page_size) {
		abort.check();

		status.set_item(PFC_string_formatter()
						<< "Fetching album index... (offset " << offset << ")");

		const json album_list_root = fetch_endpoint(
			credentials, "getAlbumList2.view",
			make_query_params(
				{subsonic::query_param("type", "alphabeticalByArtist"),
				 subsonic::query_param("size", PFC_string_formatter()
												   << k_album_list_page_size),
				 subsonic::query_param("offset", PFC_string_formatter()
													 << offset)}),
			abort);

		const auto album_list_it = album_list_root.find("albumList2");
		if (album_list_it == album_list_root.end() ||
			!album_list_it->is_object()) {
			break;
		}

		std::vector<album_sync_summary> page_albums;
		subsonic::json_parser::for_each_member_item(
			*album_list_it, "album", [&](const json &album_node) {
				album_sync_summary summary;
				summary.album_id = subsonic::json_parser::get_string(album_node, "id");
				summary.artist_id = subsonic::json_parser::get_string(album_node, "artistId");
				summary.artist_name = subsonic::json_parser::get_string(album_node, "artist");
				summary.track_count = json_get_size_t(album_node, "songCount");
				if (!summary.album_id.is_empty()) {
					page_albums.push_back(std::move(summary));
				}
			});

		if (page_albums.empty()) {
			break;
		}

		for (auto &album : page_albums) {
			std::string key = album.artist_id.is_empty()
								  ? std::string(album.artist_name.c_str())
								  : std::string(album.artist_id.c_str());
			if (key.empty()) {
				key = "<unknown-artist>";
			}

			const auto found = artist_index.find(key);
			if (found == artist_index.end()) {
				artist_sync_plan plan;
				plan.artist_id = album.artist_id;
				plan.artist_name = album.artist_name;
				plan.albums.push_back(album);
				plan.total_track_count += album.track_count;

				artist_index.emplace(std::move(key), plans.size());
				plans.push_back(std::move(plan));
			} else {
				auto &plan = plans[found->second];
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

		if (page_albums.size() < k_album_list_page_size) {
			break;
		}
	}

	return plans;
}

[[nodiscard]] library_sync_result
fetch_library_sync_result(const subsonic::server_credentials &credentials,
						  threaded_process_status &status,
						  abort_callback &abort) {
	library_sync_result result;
	std::unordered_set<std::string> seen_track_ids;

	status.set_item("Fetching album index...");
	auto artist_plans = build_artist_sync_plan(credentials, status, abort);
	if (artist_plans.empty()) {
		return result;
	}

	size_t total_album_count = 0;
	size_t total_track_count = 0;
	for (const auto &artist : artist_plans) {
		total_album_count += artist.albums.size();
		total_track_count += artist.total_track_count;
	}

	if (total_track_count > 0) {
		result.entries.reserve(total_track_count);
	}

	subsonic::log_info("library",
					   (PFC_string_formatter()
						<< "syncing " << artist_plans.size() << " artists, "
						<< total_album_count << " albums, " << total_track_count
						<< " tracks from OpenSubsonic")
						   .c_str());

	size_t processed_track_count = 0;
	for (size_t i = 0; i < artist_plans.size(); ++i) {
		abort.check();
		const auto &artist = artist_plans[i];
		size_t artist_track_index = 0;

		for (size_t album_index = 0; album_index < artist.albums.size();
			 ++album_index) {
			abort.check();

			const auto &album = artist.albums[album_index];

			if (total_track_count > 0) {
				status.set_progress(processed_track_count, total_track_count);
			} else {
				status.set_progress(album_index, artist.albums.size());
			}

			set_library_fetch_status(status, i + 1, artist_plans.size(),
									 album_index + 1, artist.albums.size(),
									 artist_track_index,
									 artist.total_track_count);

			const json album_root =
				fetch_endpoint(credentials, "getAlbum.view",
							   make_query_params({subsonic::query_param(
								   pfc::string8("id"), album.album_id)}),
							   abort);
			const auto album_it = album_root.find("album");
			if (album_it == album_root.end() || !album_it->is_object()) {
				continue;
			}

			subsonic::json_parser::for_each_member_item(*album_it, "song", [&](const json &song_node) {
				abort.check();

				const auto entry = parse_track_metadata(song_node);
				++artist_track_index;
				++processed_track_count;

				set_library_fetch_status(status, i + 1, artist_plans.size(),
										 album_index + 1, artist.albums.size(),
										 artist_track_index,
										 artist.total_track_count);

				if (!entry.is_valid()) {
					return;
				}

				const auto [_, inserted] =
					seen_track_ids.emplace(std::string(entry.track_id.c_str()));
				if (!inserted) {
					return;
				}

				result.entries.push_back(entry);
				append_handle(result.handles, entry);
			});
		}
	}

	if (total_track_count > 0) {
		status.set_progress(total_track_count, total_track_count);
	}

	return result;
}

[[nodiscard]] std::vector<remote_playlist_sync_result> fetch_remote_playlists(
	const subsonic::server_credentials &credentials,
	std::vector<subsonic::cached_track_metadata> &playlist_metadata_entries,
	threaded_process_status &status, abort_callback &abort) {
	std::vector<remote_playlist_sync_result> playlists;
	std::unordered_map<std::string, size_t> metadata_index;

	const json playlists_root =
		fetch_endpoint(credentials, "getPlaylists.view", {}, abort);
	const auto playlists_it = playlists_root.find("playlists");
	if (playlists_it == playlists_root.end() || !playlists_it->is_object()) {
		return playlists;
	}

	status.set_item("Fetching playlist summaries...");

	std::vector<std::pair<pfc::string8, pfc::string8>> summaries;
	subsonic::json_parser::for_each_member_item(
		*playlists_it, "playlist", [&](const json &playlist_node) {
			const auto playlist_id = subsonic::json_parser::get_string(playlist_node, "id");
			if (playlist_id.is_empty()) {
				return;
			}

			summaries.emplace_back(playlist_id,
								   subsonic::json_parser::get_string(playlist_node, "name"));
		});

	subsonic::log_info("playlist",
					   (PFC_string_formatter() << "syncing " << summaries.size()
											   << " OpenSubsonic playlists")
						   .c_str());
	for (size_t i = 0; i < summaries.size(); ++i) {
		abort.check();
		const auto &[playlist_id, playlist_name] = summaries[i];

		status.set_progress(i, summaries.size());
		status.set_item(PFC_string_formatter()
						<< "Fetching playlist: " << playlist_name);

		const json playlist_root = fetch_endpoint(
			credentials, "getPlaylist.view",
			make_query_params(
				{subsonic::query_param(pfc::string8("id"), playlist_id)}),
			abort);
		const auto playlist_it = playlist_root.find("playlist");
		if (playlist_it == playlist_root.end() || !playlist_it->is_object()) {
			continue;
		}

		remote_playlist_sync_result local_playlist;
		local_playlist.remote_id = playlist_id;
		local_playlist.name = subsonic::json_parser::get_string(*playlist_it, "name");
		if (local_playlist.name.is_empty()) {
			local_playlist.name = playlist_name;
		}

		subsonic::json_parser::for_each_member_item(
			*playlist_it, "entry", [&](const json &entry_node) {
				const auto entry = parse_track_metadata(entry_node);
				if (!entry.is_valid()) {
					return;
				}

				append_handle(local_playlist.handles, entry);
				merge_unique_metadata(playlist_metadata_entries, metadata_index,
									  entry);
			});

		playlists.push_back(std::move(local_playlist));
	}

	return playlists;
}

[[nodiscard]] pfc::array_t<t_uint8> make_property_blob(const char *value) {
	pfc::array_t<t_uint8> data;
	const char *text = value != nullptr ? value : "";
	const auto length = std::strlen(text);
	data.set_size(length);
	if (length > 0) {
		std::memcpy(data.get_ptr(), text, length);
	}
	return data;
}

[[nodiscard]] pfc::string8
read_playlist_property_string(playlist_manager_v2 &api, t_size playlist_index,
							  const GUID &property) {
	pfc::string8 value;
	pfc::array_t<t_uint8> data;
	if (!api.playlist_get_property(playlist_index, property, data) ||
		data.get_size() == 0) {
		return value;
	}

	value.add_string(reinterpret_cast<const char *>(data.get_ptr()),
					 data.get_size());
	return value;
}

[[nodiscard]] service_ptr_t<playlist_manager_v2>
try_get_playlist_manager_v2(const service_ptr_t<playlist_manager> &api) {
	service_ptr_t<playlist_manager_v2> api_v2;
	if (api.is_valid()) {
		api->service_query_t(api_v2);
	}
	return api_v2;
}

[[nodiscard]] t_size find_playlist_by_scope(playlist_manager &api,
											playlist_manager_v2 *api_v2,
											const char *scope,
											const char *remote_id = nullptr) {
	if (api_v2 == nullptr || scope == nullptr || *scope == '\0') {
		return SIZE_MAX;
	}

	const auto playlist_count = api.get_playlist_count();
	for (t_size index = 0; index < playlist_count; ++index) {
		const auto stored_scope = read_playlist_property_string(
			*api_v2, index, guid_playlist_property_scope);
		if (!subsonic::strings_equal(stored_scope, scope)) {
			continue;
		}

		if (remote_id == nullptr || *remote_id == '\0') {
			return index;
		}

		const auto stored_id = read_playlist_property_string(
			*api_v2, index, guid_playlist_property_remote_id);
		if (subsonic::strings_equal(stored_id, remote_id)) {
			return index;
		}
	}

	return SIZE_MAX;
}

[[nodiscard]] t_size find_library_playlist_index(playlist_manager &api,
												 playlist_manager_v2 *api_v2) {
	const auto scoped_index =
		find_playlist_by_scope(api, api_v2, k_library_scope);
	if (scoped_index != SIZE_MAX) {
		return scoped_index;
	}

	return api.find_playlist(k_library_playlist_name);
}

[[nodiscard]] metadb_handle_list load_library_playlist_items() {
	auto playlist_api = playlist_manager::get();
	auto playlist_api_v2 = try_get_playlist_manager_v2(playlist_api);
	const auto playlist_index =
		find_library_playlist_index(*playlist_api, playlist_api_v2.get_ptr());
	if (playlist_index == SIZE_MAX) {
		throw std::runtime_error("OpenSubsonic Library playlist not found. "
								 "Please sync the library first.");
	}

	metadb_handle_list items;
	playlist_api->playlist_get_all_items(playlist_index, items);
	if (items.get_count() == 0) {
		throw std::runtime_error("OpenSubsonic Library playlist is empty. "
								 "Please sync the library first.");
	}

	return items;
}

[[nodiscard]] pfc::string8
make_unique_playlist_name(playlist_manager &api, const char *desired_name,
						  t_size self_index = SIZE_MAX) {
	const pfc::string8 base = (desired_name != nullptr && *desired_name != '\0')
								  ? pfc::string8(desired_name)
								  : pfc::string8("OpenSubsonic Playlist");

	auto is_name_available = [&](const char *candidate) {
		const t_size existing = api.find_playlist(candidate);
		return existing == SIZE_MAX || existing == self_index;
	};

	if (is_name_available(base)) {
		return base;
	}

	for (t_uint32 suffix = 2; suffix < 10000; ++suffix) {
		const pfc::string8 candidate = PFC_string_formatter()
									   << base << " (" << suffix << ")";
		if (is_name_available(candidate)) {
			return candidate;
		}
	}

	return PFC_string_formatter()
		   << base << " (" << pfc::print_guid(pfc::createGUID()) << ")";
}

[[nodiscard]] t_size ensure_playlist(playlist_manager &api,
									 playlist_manager_v2 *api_v2,
									 const char *desired_name,
									 const char *scope,
									 const char *remote_id = nullptr) {
	t_size index = find_playlist_by_scope(api, api_v2, scope, remote_id);
	if (index == SIZE_MAX) {
		const pfc::string8 create_name =
			make_unique_playlist_name(api, desired_name);
		index = api.create_playlist(create_name, SIZE_MAX, SIZE_MAX);
	}

	if (index == SIZE_MAX) {
		throw std::runtime_error(PFC_string_formatter()
								 << "Unable to create playlist '"
								 << desired_name << "'.");
	}

	const pfc::string8 target_name =
		make_unique_playlist_name(api, desired_name, index);

	pfc::string8 current_name;
	if (api.playlist_get_name(index, current_name) &&
		!subsonic::strings_equal(current_name, target_name)) {
		if (!api.playlist_rename(index, target_name, SIZE_MAX)) {
			subsonic::log_warning(
				"library", (PFC_string_formatter()
							<< "failed to rename playlist index=" << index
							<< " to '" << target_name << "'")
							   .c_str());
		}
	}

	if (api_v2 != nullptr) {
		try {
			api_v2->playlist_set_property(index, guid_playlist_property_scope,
										  make_property_blob(scope));
			if (remote_id != nullptr && *remote_id != '\0') {
				api_v2->playlist_set_property(index,
											  guid_playlist_property_remote_id,
											  make_property_blob(remote_id));
			}
		} catch (const std::exception &e) {
			subsonic::log_exception("library", e);
		} catch (...) {
			subsonic::log_warning(
				"library", "unknown error while setting playlist properties");
		}
	}

	return index;
}

void replace_playlist_items(playlist_manager &api, t_size playlist_index,
							metadb_handle_list_cref items) {
	api.playlist_clear(playlist_index);
	if (items.get_count() > 0) {
		api.playlist_add_items(playlist_index, items, pfc::bit_array_false());
	}
}

void remove_stale_remote_playlists(
	playlist_manager &api, playlist_manager_v2 *api_v2,
	const std::vector<remote_playlist_sync_result> &playlists) {
	if (api_v2 == nullptr) {
		return;
	}

	std::unordered_set<std::string> active_ids;
	active_ids.reserve(playlists.size());
	for (const auto &playlist : playlists) {
		active_ids.emplace(playlist.remote_id.c_str());
	}

	for (t_size index = api.get_playlist_count(); index > 0; --index) {
		const auto playlist_index = index - 1;
		const auto scope = read_playlist_property_string(
			*api_v2, playlist_index, guid_playlist_property_scope);
		if (!subsonic::strings_equal(scope, k_remote_playlist_scope)) {
			continue;
		}

		const auto remote_id = read_playlist_property_string(
			*api_v2, playlist_index, guid_playlist_property_remote_id);
		if (active_ids.find(remote_id.c_str()) == active_ids.end()) {
			api.remove_playlist(playlist_index);
		}
	}
}

void remove_all_managed_playlists(playlist_manager &api,
								  playlist_manager_v2 *api_v2) {
	for (t_size index = api.get_playlist_count(); index > 0; --index) {
		const auto playlist_index = index - 1;

		bool remove = false;
		if (api_v2 != nullptr) {
			const auto scope = read_playlist_property_string(
				*api_v2, playlist_index, guid_playlist_property_scope);
			remove = subsonic::strings_equal(scope, k_library_scope) ||
					 subsonic::strings_equal(scope, k_remote_playlist_scope);
		}

		if (!remove) {
			pfc::string8 name;
			if (api.playlist_get_name(playlist_index, name)) {
				remove =
					subsonic::strings_equal(name, k_library_playlist_name) ||
					subsonic::starts_with_ascii_nocase(
						name, k_remote_playlist_prefix);
			}
		}

		if (remove) {
			api.remove_playlist(playlist_index);
		}
	}
}

[[nodiscard]] pfc::string8 make_success_message(const sync_outcome &outcome) {
	pfc::string8 message;
	if (outcome.includes_library) {
		message = PFC_string_formatter()
				  << "Library sync complete: " << outcome.library.entries.size()
				  << " tracks.";
	}

	if (outcome.includes_playlists) {
		pfc::string8 playlist_message =
			PFC_string_formatter()
			<< "Playlist sync complete: " << outcome.playlists.size()
			<< " playlists, " << outcome.playlist_metadata_entries.size()
			<< " unique tracks.";
		if (message.is_empty()) {
			message = playlist_message;
		} else {
			message += "\n";
			message += playlist_message;
		}
	}

	if (message.is_empty()) {
		message = "OpenSubsonic sync complete.";
	}
	return message;
}

[[nodiscard]] sync_outcome perform_sync(sync_mode mode,
										threaded_process_status &status,
										abort_callback &abort) {
	sync_outcome outcome;

	try {
		subsonic::config::ensure_cache_layout(abort);
	} catch (const std::exception &e) {
		throw std::runtime_error(PFC_string_formatter()
								 << "sync stage ensure_cache_layout failed: "
								 << e);
	}

	const auto credentials = subsonic::config::load_server_credentials();
	if (!credentials.is_configured()) {
		throw std::runtime_error(
			"OpenSubsonic server settings are not configured yet.");
	}

	if (mode == sync_mode::library_only || mode == sync_mode::all) {
		outcome.includes_library = true;
		status.set_title("Syncing OpenSubsonic Library");
		outcome.library = fetch_library_sync_result(credentials, status, abort);

		status.set_item("Caching library metadata...");
		subsonic::metadata::replace_track_metadata(outcome.library.entries,
												   status, abort);
	}

	if (mode == sync_mode::playlists_only || mode == sync_mode::all) {
		outcome.includes_playlists = true;
		status.set_title("Syncing OpenSubsonic Playlists");
		outcome.playlists = fetch_remote_playlists(
			credentials, outcome.playlist_metadata_entries, status, abort);

		status.set_item("Caching playlist metadata...");
		if (!outcome.playlist_metadata_entries.empty()) {
			subsonic::metadata::merge_track_metadata(
				outcome.playlist_metadata_entries, status, abort);
		}
	}
	return outcome;
}

void apply_sync_outcome(const sync_outcome &outcome) {
	auto playlist_api = playlist_manager::get();
	auto playlist_api_v2 = try_get_playlist_manager_v2(playlist_api);
	playlist_manager_v2 *playlist_api_v2_raw = playlist_api_v2.get_ptr();

	if (outcome.includes_library) {
		const auto playlist_index =
			ensure_playlist(*playlist_api, playlist_api_v2_raw,
							k_library_playlist_name, k_library_scope);
		replace_playlist_items(*playlist_api, playlist_index,
							   outcome.library.handles);
	}

	if (outcome.includes_playlists) {
		remove_stale_remote_playlists(*playlist_api, playlist_api_v2_raw,
									  outcome.playlists);

		for (const auto &playlist : outcome.playlists) {
			const auto display_name = make_remote_playlist_name(playlist.name);
			const auto playlist_index = ensure_playlist(
				*playlist_api, playlist_api_v2_raw, display_name,
				k_remote_playlist_scope, playlist.remote_id);
			replace_playlist_items(*playlist_api, playlist_index,
								   playlist.handles);
		}
	}
}

class sync_process_callback : public threaded_process_callback {
  public:
	sync_process_callback(sync_mode mode, sync_guard &&guard)
		: m_mode(mode), m_guard(std::move(guard)) {}

	void on_init(HWND p_wnd) override {}

	void run(threaded_process_status &p_status,
			 abort_callback &p_abort) override {
		try {
			m_outcome = perform_sync(m_mode, p_status, p_abort);
		} catch (const exception_aborted &) {
			m_aborted = true;
		} catch (const std::exception &e) {
			m_error_msg = e.what();
		}
	}

	void on_done(HWND p_wnd, bool p_was_aborted) override {
		// sync_guard destructor automatically resets g_sync_in_progress
		if (p_was_aborted || m_aborted)
			return;

		if (!m_error_msg.is_empty()) {
			popup_message::g_show(PFC_string_formatter()
									  << "OpenSubsonic sync failed:\n"
									  << m_error_msg,
								  "foo_opensubsonic");
			return;
		}

		try {
			apply_sync_outcome(m_outcome);
			popup_message::g_show(make_success_message(m_outcome),
								  "foo_opensubsonic");
		} catch (const std::exception &e) {
			popup_message::g_show(PFC_string_formatter()
									  << "Apply sync failed:\n"
									  << e.what(),
								  "foo_opensubsonic");
		}
	}

  private:
	sync_mode m_mode;
	sync_guard m_guard; // RAII: automatically releases on destruction
	sync_outcome m_outcome;
	bool m_aborted = false;
	pfc::string8 m_error_msg;
};

class clear_cache_process_callback : public threaded_process_callback {
  public:
	explicit clear_cache_process_callback(sync_guard &&guard)
		: m_guard(std::move(guard)) {}

	void on_init(HWND p_wnd) override {}

	void run(threaded_process_status &p_status,
			 abort_callback &p_abort) override {
		p_status.set_title("Clearing OpenSubsonic Cache");
		try {
			p_status.set_item("Removing artwork files...");
			subsonic::cache::reset(p_abort);

			p_status.set_item("Clearing metadata database...");

			subsonic::metadata::clear_all(p_status, p_abort);

		} catch (const exception_aborted &) {
			m_aborted = true;
		} catch (const std::exception &e) {
			m_error_msg = e.what();
		}
	}

	void on_done(HWND p_wnd, bool p_was_aborted) override {
		// sync_guard destructor automatically resets g_sync_in_progress
		if (p_was_aborted || m_aborted)
			return;

		if (!m_error_msg.is_empty()) {
			popup_message::g_show(PFC_string_formatter()
									  << "Clear cache failed:\n"
									  << m_error_msg,
								  "foo_opensubsonic");
			return;
		}

		try {
			auto playlist_api = playlist_manager::get();
			auto playlist_api_v2 = try_get_playlist_manager_v2(playlist_api);
			remove_all_managed_playlists(*playlist_api,
										 playlist_api_v2.get_ptr());
			popup_message::g_show("OpenSubsonic cache cleared successfully.",
								  "foo_opensubsonic");
		} catch (const std::exception &e) {
			popup_message::g_show(PFC_string_formatter()
									  << "Failed to remove playlists:\n"
									  << e.what(),
								  "foo_opensubsonic");
		}
	}

  private:
	sync_guard m_guard; // RAII: automatically releases on destruction
	bool m_aborted = false;
	pfc::string8 m_error_msg;
};

class clear_artwork_cache_process_callback : public threaded_process_callback {
  public:
	explicit clear_artwork_cache_process_callback(sync_guard &&guard)
		: m_guard(std::move(guard)) {}

	void on_init(HWND p_wnd) override {}

	void run(threaded_process_status &p_status,
			 abort_callback &p_abort) override {
		p_status.set_title("Clearing OpenSubsonic Artwork Cache");
		try {
			auto entries = subsonic::cache::load_all_artwork_entries();
			for (size_t i = 0; i < entries.size(); ++i) {
				if (i % 100 == 0) {
					p_abort.check();
					p_status.set_progress(i, entries.size());
					p_status.set_item(PFC_string_formatter()
									  << "Removing cached artwork: " << i
									  << " / " << entries.size());
				}

				if (entries[i].cover_art_id.is_empty()) {
					continue;
				}

				subsonic::cache::remove_artwork_entry(entries[i].cover_art_id);
			}

			p_status.set_progress(entries.size(), entries.size());
			p_status.set_item("Artwork cache cleared.");
		} catch (const exception_aborted &) {
			m_aborted = true;
		} catch (const std::exception &e) {
			m_error_msg = e.what();
		}
	}

	void on_done(HWND p_wnd, bool p_was_aborted) override {
		// sync_guard destructor automatically resets g_sync_in_progress
		if (p_was_aborted || m_aborted)
			return;

		if (!m_error_msg.is_empty()) {
			popup_message::g_show(PFC_string_formatter()
									  << "Clear artwork cache failed:\n"
									  << m_error_msg,
								  "foo_opensubsonic");
			return;
		}

		popup_message::g_show(
			"OpenSubsonic artwork cache cleared successfully.",
			"foo_opensubsonic");
	}

  private:
	sync_guard m_guard; // RAII: automatically releases on destruction
	bool m_aborted = false;
	pfc::string8 m_error_msg;
};

class cache_artwork_process_callback : public threaded_process_callback {
  public:
	cache_artwork_process_callback(metadb_handle_list items, sync_guard &&guard)
		: m_items(std::move(items)), m_guard(std::move(guard)) {}

	void on_init(HWND p_wnd) override {}

	void run(threaded_process_status &p_status,
			 abort_callback &p_abort) override {
		p_status.set_title("Caching OpenSubsonic Artwork");
		try {
			subsonic::config::ensure_cache_layout(p_abort);
			m_result = subsonic::artwork::prefetch_for_library_items(
				m_items, p_status, p_abort);
		} catch (const exception_aborted &) {
			m_aborted = true;
		} catch (const std::exception &e) {
			m_error_msg = e.what();
		}
	}

	void on_done(HWND p_wnd, bool p_was_aborted) override {
		// sync_guard destructor automatically resets g_sync_in_progress
		if (p_was_aborted || m_aborted)
			return;

		if (!m_error_msg.is_empty()) {
			popup_message::g_show(PFC_string_formatter()
									  << "Cache artwork failed:\n"
									  << m_error_msg,
								  "foo_opensubsonic");
			return;
		}

		popup_message::g_show(
			PFC_string_formatter()
				<< "Artwork cache complete:\n"
				<< "- Tracks scanned: " << m_result.track_count << "\n"
				<< "- Unique artworks: " << m_result.unique_artwork_count
				<< "\n"
				<< "- Downloaded: " << m_result.downloaded_count << "\n"
				<< "- Already cached: " << m_result.already_cached_count,
			"foo_opensubsonic");
	}

  private:
	metadb_handle_list m_items;
	sync_guard m_guard; // RAII: automatically releases on destruction
	subsonic::artwork::prefetch_artwork_result m_result;
	bool m_aborted = false;
	pfc::string8 m_error_msg;
};

void launch_sync(sync_mode mode) {
	sync_guard guard;
	if (!guard.try_acquire()) {
		popup_message::g_show("An OpenSubsonic sync is already running.",
							  "foo_opensubsonic");
		return;
	}
	threaded_process::g_run_modeless(
		new service_impl_t<sync_process_callback>(mode, std::move(guard)),
		threaded_process::flag_show_progress |
			threaded_process::flag_show_abort |
			threaded_process::flag_show_item,
		core_api::get_main_window(), "OpenSubsonic Status");
}

void launch_clear_cache() {
	sync_guard guard;
	if (!guard.try_acquire()) {
		popup_message::g_show("An OpenSubsonic job is already running.",
							  "foo_opensubsonic");
		return;
	}
	threaded_process::g_run_modeless(
		new service_impl_t<clear_cache_process_callback>(std::move(guard)),
		threaded_process::flag_show_progress |
			threaded_process::flag_show_abort |
			threaded_process::flag_show_item,
		core_api::get_main_window(), "OpenSubsonic Status");
}

void launch_cache_artwork() {
	sync_guard guard;
	if (!guard.try_acquire()) {
		popup_message::g_show("An OpenSubsonic job is already running.",
							  "foo_opensubsonic");
		return;
	}

	try {
		auto items = load_library_playlist_items();
		threaded_process::g_run_modeless(
			new service_impl_t<cache_artwork_process_callback>(
				std::move(items), std::move(guard)),
			threaded_process::flag_show_progress |
				threaded_process::flag_show_abort |
				threaded_process::flag_show_item,
			core_api::get_main_window(), "OpenSubsonic Status");
	} catch (const std::exception &e) {
		// guard destructor automatically releases lock
		popup_message::g_show(PFC_string_formatter()
								  << "Cache artwork failed:\n"
								  << e.what(),
							  "foo_opensubsonic");
	}
}

void launch_clear_artwork_cache() {
	sync_guard guard;
	if (!guard.try_acquire()) {
		popup_message::g_show("An OpenSubsonic job is already running.",
							  "foo_opensubsonic");
		return;
	}

	threaded_process::g_run_modeless(
		new service_impl_t<clear_artwork_cache_process_callback>(std::move(guard)),
		threaded_process::flag_show_progress |
			threaded_process::flag_show_abort |
			threaded_process::flag_show_item,
		core_api::get_main_window(), "OpenSubsonic Status");
}

class mainmenu_commands_opensubsonic : public mainmenu_commands {
  public:
	enum : t_uint32 {
		cmd_sync_library = 0,
		cmd_sync_playlists,
		cmd_sync_all,
		cmd_total,
	};

	t_uint32 get_command_count() override { return cmd_total; }

	GUID get_command(t_uint32 index) override {
		switch (index) {
		case cmd_sync_library:
			return guid_mainmenu_sync_library;
		case cmd_sync_playlists:
			return guid_mainmenu_sync_playlists;
		case cmd_sync_all:
			return guid_mainmenu_sync_all;
		default:
			uBugCheck();
		}
	}

	void get_name(t_uint32 index, pfc::string_base &out) override {
		switch (index) {
		case cmd_sync_library:
			out = "Sync OpenSubsonic Library";
			break;
		case cmd_sync_playlists:
			out = "Sync OpenSubsonic Playlists";
			break;
		case cmd_sync_all:
			out = "Sync OpenSubsonic Library + Playlists";
			break;
		default:
			uBugCheck();
		}
	}

	bool get_description(t_uint32 index, pfc::string_base &out) override {
		switch (index) {
		case cmd_sync_library:
			out = "Fetches the remote OpenSubsonic catalog into the local "
				  "cache and "
				  "a foobar2000 playlist.";
			return true;
		case cmd_sync_playlists:
			out = "Fetches remote OpenSubsonic playlists and mirrors them into "
				  "foobar2000 playlists.";
			return true;
		case cmd_sync_all:
			out = "Runs both the OpenSubsonic library sync and playlist sync.";
			return true;
		default:
			return false;
		}
	}

	GUID get_parent() override { return guid_mainmenu_group_opensubsonic; }

	void execute(t_uint32 index, service_ptr_t<service_base>) override {
		switch (index) {
		case cmd_sync_library:
			subsonic::library::sync_library_async();
			break;
		case cmd_sync_playlists:
			subsonic::library::sync_playlists_async();
			break;
		case cmd_sync_all:
			subsonic::library::sync_all_async();
			break;
		default:
			uBugCheck();
		}
	}
};

static mainmenu_group_popup_factory g_mainmenu_group_opensubsonic(
	guid_mainmenu_group_opensubsonic, mainmenu_groups::library,
	mainmenu_commands::sort_priority_base, "OpenSubsonic");
static mainmenu_commands_factory_t<mainmenu_commands_opensubsonic>
	g_mainmenu_commands_opensubsonic;

} // namespace

namespace subsonic::library {

void sync_library_async() { launch_sync(sync_mode::library_only); }

void sync_playlists_async() { launch_sync(sync_mode::playlists_only); }

void sync_all_async() { launch_sync(sync_mode::all); }

void cache_artwork_async() { launch_cache_artwork(); }

void clear_artwork_cache_async() { launch_clear_artwork_cache(); }

void clear_cache_async() { launch_clear_cache(); }

} // namespace subsonic::library