#include "stdafx.h"

#include "library_sync.h"

#include "cache.h"
#include "config.h"
#include "http.h"
#include "metadata.h"
#include "utils.h"

#include <SDK/playlist.h>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <unordered_map>
#include <unordered_set>

namespace {

using json = nlohmann::json;

constexpr const char *k_library_scope = "library";
constexpr const char *k_remote_playlist_scope = "remote-playlist";
constexpr const char *k_library_playlist_name = "OpenSubsonic Library";
constexpr const char *k_remote_playlist_prefix = "OpenSubsonic: ";
constexpr const char *k_server_library_playlist_prefix =
	"OpenSubsonic Library - ";
constexpr const char *k_server_remote_playlist_prefix = "OpenSubsonic [";
constexpr size_t k_album_list_page_size = 500;

const GUID guid_playlist_property_scope = {
	0x4ec80c31,
	0x6341,
	0x4f55,
	{0x84, 0x86, 0x4e, 0x36, 0x61, 0x39, 0xdd, 0x7e}};
const GUID guid_playlist_property_remote_id = {
	0x1f4a9690,
	0x3506,
	0x44dd,
	{0x96, 0x9f, 0xc5, 0xb5, 0x58, 0x9b, 0xdd, 0x47}};

struct album_sync_summary {
	pfc::string8 album_id;
	pfc::string8 artist_id;
	pfc::string8 artist_name;
	size_t track_count = 0;
};

struct artist_sync_plan {
	pfc::string8 artist_id;
	pfc::string8 artist_name;
	std::vector<album_sync_summary> albums;
	size_t total_track_count = 0;
};

[[nodiscard]] pfc::string8
make_server_label(const subsonic::server_credentials &credentials) {
	if (!credentials.server_name.is_empty()) {
		return credentials.server_name;
	}
	if (!credentials.server_id.is_empty()) {
		return credentials.server_id;
	}
	return "Default Server";
}

[[nodiscard]] pfc::string8
make_library_playlist_name(const subsonic::server_credentials &credentials) {
	return PFC_string_formatter() << k_server_library_playlist_prefix
								  << make_server_label(credentials);
}

[[nodiscard]] pfc::string8
make_remote_playlist_name(const subsonic::server_credentials &credentials,
						  const char *name) {
	pfc::string8 out = PFC_string_formatter()
					   << k_server_remote_playlist_prefix
					   << make_server_label(credentials) << "]: ";
	out += (name != nullptr && *name != '\0') ? name : "Unnamed Playlist";
	return out;
}

[[nodiscard]] pfc::string8 make_scoped_value(const char *scope,
											 const char *server_id) {
	pfc::string8 out = scope != nullptr ? scope : "";
	if (server_id != nullptr && *server_id != '\0') {
		out += ":";
		out += server_id;
	}
	return out;
}

[[nodiscard]] bool scope_matches_base(const char *scope,
									  const char *base_scope) {
	if (!subsonic::starts_with_ascii_nocase(scope, base_scope)) {
		return false;
	}

	const auto base_length =
		std::strlen(base_scope != nullptr ? base_scope : "");
	const char next = scope != nullptr ? scope[base_length] : '\0';
	return next == '\0' || next == ':';
}

[[nodiscard]] pfc::string8 make_track_key(const char *server_id,
										  const char *track_id) {
	return PFC_string_formatter()
		   << (server_id != nullptr ? server_id : "") << "|"
		   << (track_id != nullptr ? track_id : "");
}

[[nodiscard]] metadb_handle_ptr make_handle_for_track_id(const char *server_id,
														 const char *track_id) {
	if (track_id == nullptr || *track_id == '\0') {
		return {};
	}

	const pfc::string8 path = subsonic::make_subsonic_path(server_id, track_id);
	return static_api_ptr_t<metadb>()->handle_create(path, 0);
}

[[nodiscard]] subsonic::server_credentials
resolve_server_credentials(const char *server_id) {
	if (server_id != nullptr && *server_id != '\0') {
		subsonic::server_credentials credentials;
		if (subsonic::config::try_get_server_credentials(server_id,
														 credentials)) {
			return credentials;
		}

		throw std::runtime_error(PFC_string_formatter()
								 << "OpenSubsonic server not found: "
								 << server_id);
	}

	return subsonic::config::load_server_credentials();
}

template <typename Callback>
void for_each_json_value(const json &value, Callback &&callback) {
	if (value.is_null()) {
		return;
	}
	if (value.is_array()) {
		for (const auto &item : value) {
			callback(item);
		}
		return;
	}
	callback(value);
}

template <typename Callback>
void for_each_member_item(const json &parent, const char *member,
						  Callback &&callback) {
	if (member == nullptr || !parent.is_object()) {
		return;
	}

	const auto found = parent.find(member);
	if (found == parent.end()) {
		return;
	}

	for_each_json_value(*found, std::forward<Callback>(callback));
}

[[nodiscard]] pfc::string8 json_to_string(const json &value) {
	if (value.is_string()) {
		return value.get_ref<const std::string &>().c_str();
	}
	if (value.is_number_integer()) {
		return std::to_string(value.get<long long>()).c_str();
	}
	if (value.is_number_unsigned()) {
		return std::to_string(value.get<unsigned long long>()).c_str();
	}
	if (value.is_number_float()) {
		return std::to_string(value.get<double>()).c_str();
	}
	if (value.is_boolean()) {
		return value.get<bool>() ? "true" : "false";
	}
	return {};
}

[[nodiscard]] pfc::string8 json_to_metadata_string(const json &value) {
	const auto scalar = json_to_string(value);
	if (!scalar.is_empty() || value.is_string() || value.is_boolean() ||
		value.is_number()) {
		return scalar;
	}

	if (value.is_array() || value.is_object()) {
		return value.dump().c_str();
	}

	return {};
}

[[nodiscard]] pfc::string8 json_get_string(const json &object,
										   const char *member) {
	if (!object.is_object() || member == nullptr) {
		return {};
	}

	const auto found = object.find(member);
	if (found == object.end()) {
		return {};
	}

	return json_to_string(*found);
}

[[nodiscard]] double json_get_number(const json &object, const char *member) {
	if (!object.is_object() || member == nullptr) {
		return 0.0;
	}

	const auto found = object.find(member);
	if (found == object.end()) {
		return 0.0;
	}

	if (found->is_number()) {
		return found->get<double>();
	}
	if (found->is_string()) {
		return std::atof(found->get_ref<const std::string &>().c_str());
	}
	return 0.0;
}

[[nodiscard]] size_t json_get_size_t(const json &object, const char *member) {
	const auto value = json_get_number(object, member);
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
	const auto status = json_get_string(root, "status");
	if (!subsonic::strings_equal(status, "ok")) {
		pfc::string8 message = "OpenSubsonic request failed";
		const auto error_it = root.find("error");
		if (error_it != root.end() && error_it->is_object()) {
			const auto detail = json_get_string(*error_it, "message");
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
parse_track_metadata(const subsonic::server_credentials &credentials,
					 const json &song) {
	subsonic::cached_track_metadata entry;
	entry.server_id = credentials.server_id;
	entry.track_id = json_get_string(song, "id");
	entry.artist = json_get_string(song, "artist");
	entry.title = json_get_string(song, "title");
	entry.album = json_get_string(song, "album");
	entry.cover_art_id = json_get_string(song, "coverArt");
	entry.stream_mime_type = json_get_string(song, "contentType");
	entry.suffix = json_get_string(song, "suffix");
	entry.duration_seconds = json_get_number(song, "duration");
	entry.track_number = json_get_string(song, "track");
	entry.disc_number = json_get_string(song, "discNumber");
	entry.year = json_get_string(song, "year");
	entry.genre = json_get_string(song, "genre");
	entry.bitrate = json_get_string(song, "bitRate");

	if (song.is_object()) {
		entry.extra_fields.reserve(song.size());
		for (auto it = song.begin(); it != song.end(); ++it) {
			const auto value = json_to_metadata_string(it.value());
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

	const std::string key =
		make_track_key(entry.server_id, entry.track_id).c_str();
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
	const auto handle =
		make_handle_for_track_id(entry.server_id, entry.track_id);
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
		page_albums.reserve(k_album_list_page_size);
		for_each_member_item(
			*album_list_it, "album", [&](const json &album_node) {
				album_sync_summary summary;
				summary.album_id = json_get_string(album_node, "id");
				summary.artist_id = json_get_string(album_node, "artistId");
				summary.artist_name = json_get_string(album_node, "artist");
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
				plan.total_track_count += album.track_count;
				plan.albums.push_back(std::move(album));

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
				plan.albums.push_back(std::move(album));
			}
		}

		if (page_albums.size() < k_album_list_page_size) {
			break;
		}
	}

	return plans;
}

[[nodiscard]] subsonic::library::library_sync_result
fetch_library_sync_result(const subsonic::server_credentials &credentials,
						  threaded_process_status &status,
						  abort_callback &abort) {
	subsonic::library::library_sync_result result;
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
		seen_track_ids.reserve(total_track_count);
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

			for_each_member_item(*album_it, "song", [&](const json &song_node) {
				abort.check();

				const auto entry = parse_track_metadata(credentials, song_node);
				++artist_track_index;
				++processed_track_count;

				set_library_fetch_status(status, i + 1, artist_plans.size(),
										 album_index + 1, artist.albums.size(),
										 artist_track_index,
										 artist.total_track_count);

				if (!entry.is_valid()) {
					return;
				}

				const auto [_, inserted] = seen_track_ids.emplace(std::string(
					make_track_key(entry.server_id, entry.track_id).c_str()));
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

[[nodiscard]] std::vector<subsonic::library::remote_playlist_sync_result>
fetch_remote_playlists(
	const subsonic::server_credentials &credentials,
	std::vector<subsonic::cached_track_metadata> &playlist_metadata_entries,
	threaded_process_status &status, abort_callback &abort) {
	std::vector<subsonic::library::remote_playlist_sync_result> playlists;
	std::unordered_map<std::string, size_t> metadata_index;

	const json playlists_root =
		fetch_endpoint(credentials, "getPlaylists.view", {}, abort);
	const auto playlists_it = playlists_root.find("playlists");
	if (playlists_it == playlists_root.end() || !playlists_it->is_object()) {
		return playlists;
	}

	status.set_item("Fetching playlist summaries...");

	std::vector<std::pair<pfc::string8, pfc::string8>> summaries;
	for_each_member_item(
		*playlists_it, "playlist", [&](const json &playlist_node) {
			const auto playlist_id = json_get_string(playlist_node, "id");
			if (playlist_id.is_empty()) {
				return;
			}

			summaries.emplace_back(playlist_id,
								   json_get_string(playlist_node, "name"));
		});
	playlists.reserve(summaries.size());

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

		subsonic::library::remote_playlist_sync_result local_playlist;
		local_playlist.remote_id = playlist_id;
		local_playlist.name = json_get_string(*playlist_it, "name");
		if (local_playlist.name.is_empty()) {
			local_playlist.name = playlist_name;
		}

		for_each_member_item(
			*playlist_it, "entry", [&](const json &entry_node) {
				const auto entry =
					parse_track_metadata(credentials, entry_node);
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

[[nodiscard]] t_size
find_library_playlist_index(playlist_manager &api, playlist_manager_v2 *api_v2,
							const subsonic::server_credentials &credentials) {
	const auto scoped_value =
		make_scoped_value(k_library_scope, credentials.server_id);
	const auto scoped_index = find_playlist_by_scope(api, api_v2, scoped_value);
	if (scoped_index != SIZE_MAX) {
		return scoped_index;
	}

	const auto named_index =
		api.find_playlist(make_library_playlist_name(credentials));
	if (named_index != SIZE_MAX) {
		return named_index;
	}

	return api.find_playlist(k_library_playlist_name);
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

void rename_playlist_if_needed(playlist_manager &api, t_size index,
							   const char *desired_name) {
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
}

[[nodiscard]] pfc::string8 extract_remote_playlist_name(const char *value) {
	const char *text = value != nullptr ? value : "";
	if (subsonic::starts_with_ascii_nocase(text, k_remote_playlist_prefix)) {
		return text + std::strlen(k_remote_playlist_prefix);
	}

	if (subsonic::starts_with_ascii_nocase(text,
										   k_server_remote_playlist_prefix)) {
		const char *marker = std::strstr(text, "]: ");
		if (marker != nullptr) {
			return marker + 3;
		}
	}

	return *text != '\0' ? pfc::string8(text)
						 : pfc::string8("Unnamed Playlist");
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

	rename_playlist_if_needed(api, index, desired_name);

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
	const subsonic::server_credentials &credentials,
	const std::vector<subsonic::library::remote_playlist_sync_result>
		&playlists) {
	if (api_v2 == nullptr) {
		return;
	}

	const auto playlist_scope =
		make_scoped_value(k_remote_playlist_scope, credentials.server_id);

	std::unordered_set<std::string> active_ids;
	active_ids.reserve(playlists.size());
	for (const auto &playlist : playlists) {
		active_ids.emplace(playlist.remote_id.c_str());
	}

	for (t_size index = api.get_playlist_count(); index > 0; --index) {
		const auto playlist_index = index - 1;
		const auto scope = read_playlist_property_string(
			*api_v2, playlist_index, guid_playlist_property_scope);
		if (!subsonic::strings_equal(scope, playlist_scope)) {
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
			remove = scope_matches_base(scope, k_library_scope) ||
					 scope_matches_base(scope, k_remote_playlist_scope);
		}

		if (!remove) {
			pfc::string8 name;
			if (api.playlist_get_name(playlist_index, name)) {
				remove =
					subsonic::strings_equal(name, k_library_playlist_name) ||
					subsonic::starts_with_ascii_nocase(
						name, k_server_library_playlist_prefix) ||
					subsonic::starts_with_ascii_nocase(
						name, k_remote_playlist_prefix) ||
					subsonic::starts_with_ascii_nocase(
						name, k_server_remote_playlist_prefix);
			}
		}

		if (remove) {
			api.remove_playlist(playlist_index);
		}
	}
}

void remove_managed_playlists_for_server(playlist_manager &api,
										 playlist_manager_v2 *api_v2,
										 const char *server_id) {
	if (api_v2 == nullptr || server_id == nullptr || *server_id == '\0') {
		return;
	}

	const auto library_scope = make_scoped_value(k_library_scope, server_id);
	const auto remote_scope =
		make_scoped_value(k_remote_playlist_scope, server_id);

	for (t_size index = api.get_playlist_count(); index > 0; --index) {
		const auto playlist_index = index - 1;
		const auto scope = read_playlist_property_string(
			*api_v2, playlist_index, guid_playlist_property_scope);
		if (subsonic::strings_equal(scope, library_scope) ||
			subsonic::strings_equal(scope, remote_scope)) {
			api.remove_playlist(playlist_index);
		}
	}
}

void refresh_managed_playlists_for_server(
	playlist_manager &api, playlist_manager_v2 *api_v2,
	const subsonic::server_credentials &credentials) {
	if (api_v2 == nullptr || credentials.server_id.is_empty()) {
		return;
	}

	const auto library_scope =
		make_scoped_value(k_library_scope, credentials.server_id);
	const auto library_index =
		find_playlist_by_scope(api, api_v2, library_scope);
	if (library_index != SIZE_MAX) {
		rename_playlist_if_needed(api, library_index,
								  make_library_playlist_name(credentials));
	}

	const auto remote_scope =
		make_scoped_value(k_remote_playlist_scope, credentials.server_id);
	for (t_size index = 0; index < api.get_playlist_count(); ++index) {
		const auto scope = read_playlist_property_string(
			*api_v2, index, guid_playlist_property_scope);
		if (!subsonic::strings_equal(scope, remote_scope)) {
			continue;
		}

		pfc::string8 current_name;
		if (!api.playlist_get_name(index, current_name)) {
			continue;
		}

		const auto remote_name = extract_remote_playlist_name(current_name);
		rename_playlist_if_needed(
			api, index, make_remote_playlist_name(credentials, remote_name));
	}
}

} // namespace

namespace subsonic::library {

metadb_handle_list
load_library_playlist_items(const server_credentials &credentials) {
	auto playlist_api = playlist_manager::get();
	auto playlist_api_v2 = try_get_playlist_manager_v2(playlist_api);
	const auto playlist_index = find_library_playlist_index(
		*playlist_api, playlist_api_v2.get_ptr(), credentials);
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

pfc::string8 make_success_message(const sync_outcome &outcome) {
	pfc::string8 message;
	if (outcome.includes_library) {
		message = PFC_string_formatter()
				  << "Library sync complete for "
				  << make_server_label(outcome.server) << ": "
				  << outcome.library.entries.size() << " tracks.";
	}

	if (outcome.includes_playlists) {
		pfc::string8 playlist_message =
			PFC_string_formatter()
			<< "Playlist sync complete for "
			<< make_server_label(outcome.server) << ": "
			<< outcome.playlists.size() << " playlists, "
			<< outcome.playlist_metadata_entries.size() << " unique tracks.";
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

sync_outcome perform_sync(sync_mode mode, const char *server_id,
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

	const auto credentials = resolve_server_credentials(server_id);
	if (!credentials.is_configured()) {
		throw std::runtime_error(
			"OpenSubsonic server settings are not configured yet.");
	}
	outcome.server = credentials;

	if (mode == sync_mode::library_only || mode == sync_mode::all) {
		outcome.includes_library = true;
		status.set_title(PFC_string_formatter()
						 << "Syncing OpenSubsonic Library - "
						 << make_server_label(credentials));
		outcome.library = fetch_library_sync_result(credentials, status, abort);

		status.set_item("Caching library metadata...");
		subsonic::metadata::replace_track_metadata(outcome.library.entries,
												   status, abort);
	}

	if (mode == sync_mode::playlists_only || mode == sync_mode::all) {
		outcome.includes_playlists = true;
		status.set_title(PFC_string_formatter()
						 << "Syncing OpenSubsonic Playlists - "
						 << make_server_label(credentials));
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
	const auto library_scope =
		make_scoped_value(k_library_scope, outcome.server.server_id);
	const auto playlist_scope =
		make_scoped_value(k_remote_playlist_scope, outcome.server.server_id);

	if (outcome.includes_library) {
		const auto playlist_index = ensure_playlist(
			*playlist_api, playlist_api_v2_raw,
			make_library_playlist_name(outcome.server), library_scope);
		replace_playlist_items(*playlist_api, playlist_index,
							   outcome.library.handles);
	}

	if (outcome.includes_playlists) {
		remove_stale_remote_playlists(*playlist_api, playlist_api_v2_raw,
									  outcome.server, outcome.playlists);

		for (const auto &playlist : outcome.playlists) {
			const auto display_name =
				make_remote_playlist_name(outcome.server, playlist.name);
			const auto playlist_index = ensure_playlist(
				*playlist_api, playlist_api_v2_raw, display_name,
				playlist_scope, playlist.remote_id);
			replace_playlist_items(*playlist_api, playlist_index,
								   playlist.handles);
		}
	}
}

void refresh_managed_playlists(const server_credentials &credentials) {
	auto playlist_api = playlist_manager::get();
	auto playlist_api_v2 = try_get_playlist_manager_v2(playlist_api);
	refresh_managed_playlists_for_server(
		*playlist_api, playlist_api_v2.get_ptr(), credentials);
}

void clear_managed_playlists() {
	auto playlist_api = playlist_manager::get();
	auto playlist_api_v2 = try_get_playlist_manager_v2(playlist_api);
	remove_all_managed_playlists(*playlist_api, playlist_api_v2.get_ptr());
}

void clear_managed_playlists(const char *server_id) {
	auto playlist_api = playlist_manager::get();
	auto playlist_api_v2 = try_get_playlist_manager_v2(playlist_api);
	remove_managed_playlists_for_server(*playlist_api,
										playlist_api_v2.get_ptr(), server_id);
}

} // namespace subsonic::library
