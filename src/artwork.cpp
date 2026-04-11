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

namespace {

static const GUID guid_subsonic_album_art_extractor = {
	0xf6c66017,
	0xff17,
	0x4dcd,
	{0xa0, 0xd4, 0x66, 0x6c, 0x94, 0x39, 0x5d, 0xa1}};

constexpr size_t k_max_artwork_bytes = 32 * 1024 * 1024;
constexpr size_t k_max_cached_artwork_entries = 64;

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

void prune_artwork_cache(abort_callback &abort) {
	auto entries = subsonic::cache::load_all_artwork_entries();
	if (entries.size() <= k_max_cached_artwork_entries) {
		return;
	}

	std::sort(entries.begin(), entries.end(),
			  [](const subsonic::artwork_cache_entry &lhs,
				 const subsonic::artwork_cache_entry &rhs) {
				  return lhs.last_access_unix_ms > rhs.last_access_unix_ms;
			  });

	std::vector<pfc::string8> paths_in_use;
	paths_in_use.reserve(k_max_cached_artwork_entries);
	for (size_t i = 0; i < k_max_cached_artwork_entries && i < entries.size();
		 ++i) {
		if (!entries[i].local_path.is_empty()) {
			paths_in_use.push_back(entries[i].local_path);
		}
	}

	for (size_t i = k_max_cached_artwork_entries; i < entries.size(); ++i) {
		abort.check();

		const auto &entry = entries[i];
		if (!entry.local_path.is_empty()) {
			const bool path_is_still_used =
				std::find_if(paths_in_use.begin(), paths_in_use.end(),
							 [&](const pfc::string8 &path) {
								 return path == entry.local_path;
							 }) != paths_in_use.end();
			if (!path_is_still_used &&
				filesystem::g_exists(entry.local_path, abort)) {
				filesystem::g_remove(entry.local_path, abort);
			}
		}

		subsonic::cache::remove_artwork_entry(entry.cover_art_id);
	}
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

bool ensure_artwork_cached(const subsonic::cached_track_metadata &track_meta,
						   pfc::string8 &out_path, pfc::string8 &out_mime_type,
						   abort_callback &abort) {
	out_path.reset();
	out_mime_type.reset();

	if (track_meta.cover_art_id.is_empty()) {
		return false;
	}

	subsonic::artwork_cache_entry cached_entry;
	if (subsonic::cache::try_get_artwork_entry(track_meta.cover_art_id,
											   cached_entry)) {
		if (!cached_entry.local_path.is_empty() &&
			filesystem::g_exists(cached_entry.local_path, abort)) {
			cached_entry.last_access_unix_ms = current_unix_time_ms();
			subsonic::cache::upsert_artwork_entry(cached_entry);
			out_path = cached_entry.local_path;
			out_mime_type = cached_entry.mime_type;
			return true;
		}

		subsonic::cache::remove_artwork_entry(track_meta.cover_art_id);
	}

	const auto credentials = subsonic::config::load_server_credentials();
	if (!credentials.is_configured()) {
		return false;
	}

	subsonic::config::ensure_cache_layout(abort);

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
	entry.cover_art_id = track_meta.cover_art_id;
	entry.mime_type = mime_type;
	entry.local_path = local_path;
	entry.content_hash = content_hash;
	entry.last_access_unix_ms = current_unix_time_ms();
	subsonic::cache::upsert_artwork_entry(entry);

	prune_artwork_cache(abort);

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
		pfc::string8 dummy_id;
		(void)extension;
		return subsonic::extract_track_id_from_path(path, dummy_id);
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
	pfc::string8 dummy;
	return extract_track_id_from_path(path, dummy);
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
		if (!ensure_artwork_cached(track_meta, local_path, mime_type, abort) ||
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
} // namespace subsonic::artwork