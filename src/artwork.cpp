#include "stdafx.h"

#include "artwork.h"
#include "cache.h"
#include "config.h"
#include "http.h"
#include "metadata.h"
#include "utils.h"

#include <SDK/album_art_helpers.h>

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

namespace {

static const GUID guid_subsonic_album_art_extractor = {
	0xf6c66017,
	0xff17,
	0x4dcd,
	{0xa0, 0xd4, 0x66, 0x6c, 0x94, 0x39, 0x5d, 0xa1}};

constexpr size_t k_max_artwork_bytes = 32 * 1024 * 1024;
constexpr std::uint64_t k_cache_touch_interval_ms = 5ull * 60ull * 1000ull;

enum class ensure_artwork_cached_result {
	unavailable,
	already_cached,
	downloaded,
};

[[nodiscard]] std::string make_track_key(const char *server_id,
										 const char *track_id) {
	return std::string(PFC_string_formatter()
					   << (server_id != nullptr ? server_id : "") << "|"
					   << (track_id != nullptr ? track_id : ""));
}

[[nodiscard]] std::string make_cover_art_key(const char *server_id,
											 const char *cover_art_id) {
	return std::string(PFC_string_formatter()
					   << (server_id != nullptr ? server_id : "") << "|"
					   << (cover_art_id != nullptr ? cover_art_id : ""));
}

[[nodiscard]] bool is_supported_art_id(const GUID &art_id) noexcept {
	return art_id == album_art_ids::cover_front;
}

[[nodiscard]] const char *extension_from_mime_type(const char *mime_type) {
	if (mime_type == nullptr || *mime_type == '\0') {
		return "img";
	}

	std::string mime = mime_type;
	const auto semicolon = mime.find(';');
	if (semicolon != std::string::npos) {
		mime.erase(semicolon);
	}

	for (char &ch : mime) {
		ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
	}

	if (mime == "image/jpeg" || mime == "image/jpg")
		return "jpg";
	if (mime == "image/png")
		return "png";
	if (mime == "image/webp")
		return "webp";
	if (mime == "image/gif")
		return "gif";
	if (mime == "image/bmp")
		return "bmp";
	if (mime == "image/tiff")
		return "tiff";
	return "img";
}

[[nodiscard]] std::string normalize_mime_type(const char *mime_type) {
	if (mime_type == nullptr || *mime_type == '\0') {
		return {};
	}

	std::string mime = mime_type;
	const auto semicolon = mime.find(';');
	if (semicolon != std::string::npos) {
		mime.erase(semicolon);
	}

	for (char &ch : mime) {
		ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
	}

	return mime;
}

[[nodiscard]] bool is_image_mime_type(const char *mime_type) {
	const auto mime = normalize_mime_type(mime_type);
	return !mime.empty() && mime.rfind("image/", 0) == 0;
}

[[nodiscard]] bool payload_looks_like_json(const void *data,
										   t_size size) noexcept {
	if (data == nullptr || size == 0) {
		return false;
	}

	const auto *bytes = static_cast<const unsigned char *>(data);
	t_size i = 0;

	if (size >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
		i = 3;
	}

	for (; i < size; ++i) {
		const unsigned char ch = bytes[i];
		if (!std::isspace(ch)) {
			return ch == '{' || ch == '[';
		}
	}

	return false;
}

[[nodiscard]] bool should_accept_artwork_payload(const pfc::string8 &mime_type,
												 const void *data,
												 t_size size) {
	if (is_image_mime_type(mime_type.c_str())) {
		return true;
	}

	if (!mime_type.is_empty()) {
		return false;
	}

	return !payload_looks_like_json(data, size);
}

[[nodiscard]] pfc::string8
make_artwork_cache_path_from_hash(const char *content_hash,
								  const char *mime_type) {
	pfc::string8 path = subsonic::config::artwork_cache_directory();
	std::string file_name = (content_hash != nullptr && *content_hash != '\0')
								? std::string(content_hash)
								: std::string("artwork");
	file_name.push_back('.');
	file_name += extension_from_mime_type(mime_type);
	path.add_filename(file_name.c_str());
	return path;
}

[[nodiscard]] std::uint64_t current_unix_time_ms() noexcept {
	using namespace std::chrono;
	return static_cast<std::uint64_t>(
		duration_cast<milliseconds>(system_clock::now().time_since_epoch())
			.count());
}

[[nodiscard]] pfc::string8 hash_artwork_bytes(const void *data, t_size size) {
	const auto hash =
		static_api_ptr_t<hasher_md5>()->process_single(data, size);
	return pfc::format_hexdump_lowercase(hash.m_data, sizeof(hash.m_data), "");
}

album_art_path_list::ptr make_artwork_path_list(const char *path) {
	return new service_impl_t<album_art_path_list_impl>(path);
}

bool try_read_artwork_file(const char *path, album_art_data_ptr &out_data,
						   abort_callback &abort) {
	out_data.release();

	if (path == nullptr || *path == '\0' ||
		!filesystem::g_exists(path, abort)) {
		return false;
	}

	file::ptr stream;
	filesystem::g_open_read(stream, path, abort);

	mem_block_container_impl bytes;
	subsonic::http::read_all(stream, bytes, abort, 256 * 1024,
							 k_max_artwork_bytes);
	if (bytes.get_size() == 0) {
		return false;
	}

	out_data = album_art_data_impl::g_create(bytes.get_ptr(), bytes.get_size());
	return out_data.is_valid();
}

[[nodiscard]] bool
try_use_cached_artwork(subsonic::artwork_cache_entry &cached_entry,
					   pfc::string8 &out_path, pfc::string8 &out_mime_type,
					   abort_callback &abort) {
	out_path.reset();
	out_mime_type.reset();

	if (cached_entry.local_path.is_empty() ||
		!filesystem::g_exists(cached_entry.local_path, abort)) {
		return false;
	}

	const auto now = current_unix_time_ms();
	if (cached_entry.last_access_unix_ms == 0 ||
		now < cached_entry.last_access_unix_ms ||
		now - cached_entry.last_access_unix_ms >= k_cache_touch_interval_ms) {
		cached_entry.last_access_unix_ms = now;
		subsonic::cache::upsert_artwork_entry(cached_entry);
	}

	out_path = cached_entry.local_path;
	out_mime_type = cached_entry.mime_type;
	return true;
}

[[nodiscard]] bool try_reuse_existing_artwork_mapping(
	const subsonic::cached_track_metadata &track_meta, pfc::string8 &out_path,
	pfc::string8 &out_mime_type, abort_callback &abort) {
	if (track_meta.cover_art_id.is_empty()) {
		return false;
	}

	auto entries = subsonic::cache::load_all_artwork_entries();
	for (auto &entry : entries) {
		abort.check();
		if (!entry.is_valid() ||
			!subsonic::strings_equal(entry.cover_art_id,
									 track_meta.cover_art_id)) {
			continue;
		}

		if (!try_use_cached_artwork(entry, out_path, out_mime_type, abort)) {
			continue;
		}

		subsonic::artwork_cache_entry alias = entry;
		alias.server_id = track_meta.server_id;
		alias.cover_art_id = track_meta.cover_art_id;
		subsonic::cache::upsert_artwork_entry(alias);
		return true;
	}

	return false;
}

ensure_artwork_cached_result
ensure_artwork_cached(const subsonic::cached_track_metadata &track_meta,
					  pfc::string8 &out_path, pfc::string8 &out_mime_type,
					  abort_callback &abort) {
	out_path.reset();
	out_mime_type.reset();

	if (track_meta.cover_art_id.is_empty()) {
		return ensure_artwork_cached_result::unavailable;
	}

	subsonic::artwork_cache_entry cached_entry;
	if (subsonic::cache::try_get_artwork_entry(
			track_meta.server_id, track_meta.cover_art_id, cached_entry)) {
		if (try_use_cached_artwork(cached_entry, out_path, out_mime_type,
								   abort)) {
			return ensure_artwork_cached_result::already_cached;
		}

		subsonic::cache::remove_artwork_entry(track_meta.server_id,
											  track_meta.cover_art_id);
	}

	if (try_reuse_existing_artwork_mapping(track_meta, out_path, out_mime_type,
										   abort)) {
		return ensure_artwork_cached_result::already_cached;
	}

	subsonic::server_credentials credentials;
	if (!subsonic::config::try_get_server_credentials(track_meta.server_id,
													  credentials) ||
		!credentials.is_configured()) {
		return ensure_artwork_cached_result::unavailable;
	}

	subsonic::config::ensure_cache_layout(abort);

	auto response =
		subsonic::http::open_api(credentials, "getCoverArt.view", abort,
								 {{"id", track_meta.cover_art_id.c_str()}});

	mem_block_container_impl bytes;
	subsonic::http::read_all(response, bytes, abort, 256 * 1024,
							 k_max_artwork_bytes);
	if (bytes.get_size() == 0) {
		return ensure_artwork_cached_result::unavailable;
	}

	pfc::string8 mime_type = response.content_type;
	if (mime_type.is_empty()) {
		(void)subsonic::http::try_get_header(response, "content-type",
											 mime_type);
	}

	if (!should_accept_artwork_payload(
			mime_type, bytes.get_ptr(),
			pfc::downcast_guarded<t_size>(bytes.get_size()))) {
		subsonic::log_error(
			"artwork",
			(PFC_string_formatter()
			 << "ignoring non-image artwork response for cover art id "
			 << track_meta.cover_art_id));
		return ensure_artwork_cached_result::unavailable;
	}

	const auto content_hash = hash_artwork_bytes(
		bytes.get_ptr(), pfc::downcast_guarded<t_size>(bytes.get_size()));
	auto local_path = make_artwork_cache_path_from_hash(content_hash.c_str(),
														mime_type.c_str());
	if (!filesystem::g_exists(local_path, abort)) {
		file::ptr output;
		filesystem::g_open_write_new(output, local_path, abort);
		output->write(bytes.get_ptr(),
					  pfc::downcast_guarded<t_size>(bytes.get_size()), abort);
	}

	subsonic::artwork_cache_entry entry;
	entry.server_id = track_meta.server_id;
	entry.cover_art_id = track_meta.cover_art_id;
	entry.mime_type = mime_type;
	entry.local_path = local_path;
	entry.content_hash = content_hash;
	entry.last_access_unix_ms = current_unix_time_ms();
	subsonic::cache::upsert_artwork_entry(entry);

	out_path = local_path;
	out_mime_type = mime_type;
	return ensure_artwork_cached_result::downloaded;
}

[[nodiscard]] std::unordered_map<std::string, subsonic::artwork_cache_entry>
load_valid_cached_artwork_entries(abort_callback &abort) {
	auto entries = subsonic::cache::load_all_artwork_entries();
	std::unordered_map<std::string, subsonic::artwork_cache_entry> result;
	result.reserve(entries.size());

	for (const auto &entry : entries) {
		abort.check();
		if (!entry.is_valid()) {
			continue;
		}

		if (!filesystem::g_exists(entry.local_path, abort)) {
			continue;
		}

		result.emplace(make_cover_art_key(entry.server_id, entry.cover_art_id),
					   entry);
	}

	return result;
}

[[nodiscard]] bool
download_artwork_to_cache(const subsonic::cached_track_metadata &track_meta,
						  const subsonic::server_credentials &credentials,
						  pfc::string8 &out_path, pfc::string8 &out_mime_type,
						  abort_callback &abort) {
	out_path.reset();
	out_mime_type.reset();

	if (track_meta.cover_art_id.is_empty()) {
		return false;
	}

	auto response =
		subsonic::http::open_api(credentials, "getCoverArt.view", abort,
								 {{"id", track_meta.cover_art_id.c_str()}});

	mem_block_container_impl bytes;
	subsonic::http::read_all(response, bytes, abort, 256 * 1024,
							 k_max_artwork_bytes);
	if (bytes.get_size() == 0) {
		return false;
	}

	pfc::string8 mime_type = response.content_type;
	if (mime_type.is_empty()) {
		(void)subsonic::http::try_get_header(response, "content-type",
											 mime_type);
	}

	if (!should_accept_artwork_payload(
			mime_type, bytes.get_ptr(),
			pfc::downcast_guarded<t_size>(bytes.get_size()))) {
		subsonic::log_error(
			"artwork",
			(PFC_string_formatter()
			 << "ignoring non-image artwork response for cover art id "
			 << track_meta.cover_art_id));
		return false;
	}

	const auto content_hash = hash_artwork_bytes(
		bytes.get_ptr(), pfc::downcast_guarded<t_size>(bytes.get_size()));
	auto local_path = make_artwork_cache_path_from_hash(content_hash.c_str(),
														mime_type.c_str());
	if (!filesystem::g_exists(local_path, abort)) {
		file::ptr output;
		filesystem::g_open_write_new(output, local_path, abort);
		output->write(bytes.get_ptr(),
					  pfc::downcast_guarded<t_size>(bytes.get_size()), abort);
	}

	subsonic::artwork_cache_entry entry;
	entry.server_id = track_meta.server_id;
	entry.cover_art_id = track_meta.cover_art_id;
	entry.mime_type = mime_type;
	entry.local_path = local_path;
	entry.content_hash = content_hash;
	entry.last_access_unix_ms = current_unix_time_ms();
	subsonic::cache::upsert_artwork_entry(entry);

	out_path = local_path;
	out_mime_type = mime_type;
	return true;
}

class subsonic_album_art_extractor_instance
	: public album_art_extractor_instance_v2 {
  public:
	explicit subsonic_album_art_extractor_instance(const char *path)
		: m_path(path) {}

	album_art_data_ptr query(const GUID &art_id,
							 abort_callback &abort) override {
		album_art_data_ptr data;
		album_art_path_list::ptr paths;
		if (!subsonic::artwork::try_load(m_path.c_str(), art_id, data, paths,
										 abort)) {
			throw exception_album_art_not_found();
		}

		return data;
	}

	album_art_path_list::ptr query_paths(const GUID &art_id,
										 abort_callback &abort) override {
		album_art_data_ptr data;
		album_art_path_list::ptr paths;
		if (!subsonic::artwork::try_load(m_path.c_str(), art_id, data, paths,
										 abort) ||
			!paths.is_valid()) {
			throw exception_album_art_not_found();
		}

		return paths;
	}

  private:
	pfc::string8 m_path;
};

class empty_album_art_extractor_instance
	: public album_art_extractor_instance_v2 {
  public:
	album_art_data_ptr query(const GUID &, abort_callback &abort) override {
		abort.check();
		throw exception_album_art_not_found();
	}

	album_art_path_list::ptr query_paths(const GUID &,
										 abort_callback &abort) override {
		abort.check();
		throw exception_album_art_not_found();
	}
};

class subsonic_album_art_fallback_impl : public album_art_fallback {
  public:
	album_art_extractor_instance_v2::ptr
	open(metadb_handle_list_cref items, pfc::list_base_const_t<GUID> const &ids,
		 abort_callback &abort) override {
		(void)ids;
		abort.check();

		if (items.get_count() != 1) {
			return new service_impl_t<empty_album_art_extractor_instance>();
		}

		const auto item = items.get_item(0);
		const char *path = item.is_valid() ? item->get_path() : nullptr;
		if (path == nullptr || !subsonic::artwork::is_supported_path(path)) {
			return new service_impl_t<empty_album_art_extractor_instance>();
		}

		return new service_impl_t<subsonic_album_art_extractor_instance>(path);
	}
};

class subsonic_album_art_extractor_impl : public album_art_extractor_v2 {
  public:
	bool is_our_path(const char *path, const char *extension) override {
		subsonic::track_identity identity;
		(void)extension;
		return subsonic::extract_track_identity_from_path(path, identity);
	}

	album_art_extractor_instance_ptr open(file_ptr file_hint, const char *path,
										  abort_callback &abort) override {
		(void)file_hint;
		abort.check();
		return new service_impl_t<subsonic_album_art_extractor_instance>(path);
	}

	GUID get_guid() override { return guid_subsonic_album_art_extractor; }
};

static service_factory_single_t<subsonic_album_art_extractor_impl>
	g_subsonic_album_art_extractor_impl;

static service_factory_single_t<subsonic_album_art_fallback_impl>
	g_subsonic_album_art_fallback_impl;

} // namespace

namespace subsonic::artwork {
bool is_supported_path(const char *path) noexcept {
	track_identity identity;
	return extract_track_identity_from_path(path, identity);
}
bool try_load(const char *path, const GUID &art_id,
			  album_art_data_ptr &out_data, album_art_path_list::ptr &out_paths,
			  abort_callback &abort) {
	out_data.release();
	out_paths.release();

	if (!is_supported_path(path) || !is_supported_art_id(art_id)) {
		return false;
	}

	cached_track_metadata track_meta;
	if (!metadata::try_get_track_metadata_for_path(path, track_meta) ||
		track_meta.cover_art_id.is_empty()) {
		return false;
	}

	try {
		pfc::string8 local_path;
		pfc::string8 mime_type;
		if (ensure_artwork_cached(track_meta, local_path, mime_type, abort) ==
				ensure_artwork_cached_result::unavailable ||
			local_path.is_empty()) {
			return false;
		}

		out_paths = make_artwork_path_list(local_path);
		return try_read_artwork_file(local_path, out_data, abort);
	} catch (const std::exception &e) {
		log_exception("artwork", e);
	} catch (...) {
		log_error("artwork", "failed to load artwork");
	}

	return false;
}

prefetch_artwork_result
prefetch_for_library_items(metadb_handle_list_cref items,
						   threaded_process_status &status,
						   abort_callback &abort) {
	prefetch_artwork_result result;
	result.track_count = items.get_count();

	auto metadata_entries = subsonic::cache::load_all_track_metadata();
	std::unordered_map<std::string, const cached_track_metadata *>
		metadata_by_track_id;
	metadata_by_track_id.reserve(metadata_entries.size());
	for (const auto &entry : metadata_entries) {
		if (!entry.is_valid()) {
			continue;
		}

		metadata_by_track_id.emplace(
			make_track_key(entry.server_id, entry.track_id), &entry);
	}

	auto cached_artwork_entries = load_valid_cached_artwork_entries(abort);

	std::vector<cached_track_metadata> pending_downloads;
	pending_downloads.reserve(items.get_count());
	std::unordered_set<std::string> seen_cover_art_keys;
	seen_cover_art_keys.reserve(items.get_count());

	for (t_size i = 0; i < items.get_count(); ++i) {
		if (i % 500 == 0) {
			abort.check();
			status.set_progress(i, items.get_count());
			status.set_item(PFC_string_formatter()
							<< "Scanning library tracks for artwork: " << i
							<< " / " << items.get_count());
		}

		const auto item = items.get_item(i);
		if (!item.is_valid()) {
			continue;
		}

		const char *path = item->get_path();
		if (path == nullptr || !is_supported_path(path)) {
			continue;
		}

		track_identity identity;
		if (!extract_track_identity_from_path(path, identity) ||
			!identity.is_valid()) {
			continue;
		}

		const auto metadata_it = metadata_by_track_id.find(
			make_track_key(identity.server_id, identity.track_id));
		if (metadata_it == metadata_by_track_id.end() ||
			metadata_it->second == nullptr) {
			continue;
		}

		const auto &track_meta = *metadata_it->second;
		if (track_meta.cover_art_id.is_empty()) {
			continue;
		}

		const auto cover_art_key =
			make_cover_art_key(track_meta.server_id, track_meta.cover_art_id);
		if (!seen_cover_art_keys.emplace(cover_art_key).second) {
			continue;
		}

		if (cached_artwork_entries.find(cover_art_key) !=
			cached_artwork_entries.end()) {
			++result.already_cached_count;
			continue;
		}

		pending_downloads.push_back(track_meta);
	}

	result.unique_artwork_count = seen_cover_art_keys.size();

	if (pending_downloads.empty()) {
		status.set_progress(result.unique_artwork_count,
							result.unique_artwork_count);
		status.set_item(PFC_string_formatter()
						<< "Artwork cache complete: " << result.downloaded_count
						<< " downloaded, " << result.already_cached_count
						<< " already cached.");
		return result;
	}

	subsonic::config::ensure_cache_layout(abort);
	std::unordered_map<std::string, subsonic::server_credentials>
		credentials_by_server_id;

	for (size_t i = 0; i < pending_downloads.size(); ++i) {
		abort.check();
		status.set_progress(i, pending_downloads.size());
		status.set_item(PFC_string_formatter()
						<< "Caching artwork: " << i << " / "
						<< pending_downloads.size());

		pfc::string8 local_path;
		pfc::string8 mime_type;
		auto &credentials = credentials_by_server_id[std::string(
			pending_downloads[i].server_id.c_str())];
		if (!credentials.is_configured()) {
			if (!subsonic::config::try_get_server_credentials(
					pending_downloads[i].server_id, credentials) ||
				!credentials.is_configured()) {
				continue;
			}
		}

		if (download_artwork_to_cache(pending_downloads[i], credentials,
									  local_path, mime_type, abort)) {
			++result.downloaded_count;
		}
	}

	status.set_progress(result.unique_artwork_count,
						result.unique_artwork_count);
	status.set_item(PFC_string_formatter()
					<< "Artwork cache complete: " << result.downloaded_count
					<< " downloaded, " << result.already_cached_count
					<< " already cached.");

	return result;
}
} // namespace subsonic::artwork