#include "stdafx.h"

#include "cache.h"

#include "config.h"
#include "utils.h"

#include <SDK/configStore.h>
#include <SDK/filesystem_helper.h>

namespace {

constexpr const char *k_track_domain = "foo_opensubsonic.cache.track";
constexpr const char *k_artwork_domain = "foo_opensubsonic.cache.artwork";

constexpr t_uint32 k_track_blob_version = 3;
constexpr t_uint32 k_artwork_blob_version = 3;

[[nodiscard]] pfc::string8 make_cache_key(const char *domain, const char *id) {
	if (domain == nullptr || id == nullptr || *id == '\0') {
		return {};
	}
	return PFC_string_formatter() << domain << "." << subsonic::md5_hex(id);
}

[[nodiscard]] pfc::string8 make_scoped_id(const char *server_id,
										  const char *id) {
	if (server_id == nullptr || *server_id == '\0' || id == nullptr ||
		*id == '\0') {
		return {};
	}

	return PFC_string_formatter() << server_id << "|" << id;
}

[[nodiscard]] pfc::string8 current_server_id() {
	return subsonic::config::load_selected_server_id();
}

[[nodiscard]] bool
is_same_artwork_identity(const subsonic::artwork_cache_entry &lhs,
						 const char *server_id, const char *cover_art_id) {
	return subsonic::strings_equal(lhs.server_id, server_id) &&
		   subsonic::strings_equal(lhs.cover_art_id, cover_art_id);
}

[[nodiscard]] bool is_shared_artwork_file(
	const subsonic::artwork_cache_entry &target,
	const std::vector<subsonic::artwork_cache_entry> &entries) {
	if (target.local_path.is_empty()) {
		return false;
	}

	for (const auto &entry : entries) {
		if (!entry.is_valid() ||
			is_same_artwork_identity(entry, target.server_id,
									 target.cover_art_id)) {
			continue;
		}

		if ((!target.content_hash.is_empty() &&
			 !entry.content_hash.is_empty() &&
			 subsonic::strings_equal(entry.content_hash,
									 target.content_hash)) ||
			subsonic::strings_equal(entry.local_path, target.local_path)) {
			return true;
		}
	}

	return false;
}

void write_track_extra_fields(
	stream_writer_formatter_simple<> &writer,
	const std::vector<subsonic::track_metadata_field> &fields) {
	writer << pfc::downcast_guarded<t_uint32>(fields.size());
	for (const auto &field : fields) {
		writer << field.key;
		writer << field.value;
	}
}

void read_track_extra_fields(
	stream_reader_formatter_simple_ref<> &reader,
	std::vector<subsonic::track_metadata_field> &fields) {
	t_uint32 count = 0;
	reader >> count;

	fields.clear();
	fields.reserve(count);

	for (t_uint32 i = 0; i < count; ++i) {
		subsonic::track_metadata_field field;
		reader >> field.key;
		reader >> field.value;
		if (field.is_valid()) {
			fields.push_back(std::move(field));
		}
	}
}

fb2k::memBlockRef
serialize_track(const subsonic::cached_track_metadata &entry) {
	stream_writer_formatter_simple<> writer;
	writer << k_track_blob_version;
	writer << entry.server_id;
	writer << entry.track_id;
	writer << entry.artist;
	writer << entry.title;
	writer << entry.album;
	writer << entry.cover_art_id;
	writer << entry.stream_mime_type;
	writer << entry.suffix;
	writer << entry.duration_seconds;
	writer << entry.track_number;
	writer << entry.disc_number;
	writer << entry.year;
	writer << entry.genre;
	writer << entry.bitrate;
	write_track_extra_fields(writer, entry.extra_fields);
	return fb2k::memBlock::blockWithVector(writer.m_buffer);
}

bool deserialize_track(fb2k::memBlockRef block,
					   subsonic::cached_track_metadata &out) {
	if (!block.is_valid() || block->size() == 0) {
		return false;
	}

	stream_reader_formatter_simple_ref<> reader(
		block->data(), pfc::downcast_guarded<t_size>(block->size()));
	t_uint32 version = 0;
	reader >> version;
	if (version < 1 || version > k_track_blob_version) {
		return false;
	}

	subsonic::cached_track_metadata value;
	if (version >= 3) {
		reader >> value.server_id;
	}
	reader >> value.track_id;
	reader >> value.artist;
	reader >> value.title;
	reader >> value.album;
	reader >> value.cover_art_id;
	reader >> value.stream_mime_type;
	reader >> value.suffix;
	reader >> value.duration_seconds;
	reader >> value.track_number;
	reader >> value.disc_number;
	reader >> value.year;
	reader >> value.genre;
	reader >> value.bitrate;

	if (version >= 2) {
		read_track_extra_fields(reader, value.extra_fields);
	} else {
		value.extra_fields.clear();
	}

	if (value.server_id.is_empty()) {
		value.server_id = current_server_id();
	}

	if (!value.is_valid()) {
		return false;
	}

	out = std::move(value);
	return true;
}

fb2k::memBlockRef
serialize_artwork(const subsonic::artwork_cache_entry &entry) {
	stream_writer_formatter_simple<> writer;
	writer << k_artwork_blob_version;
	writer << entry.server_id;
	writer << entry.cover_art_id;
	writer << entry.mime_type;
	writer << entry.local_path;
	writer << entry.content_hash;
	writer << pfc::downcast_guarded<t_uint64>(entry.last_access_unix_ms);
	return fb2k::memBlock::blockWithVector(writer.m_buffer);
}

bool deserialize_artwork(fb2k::memBlockRef block,
						 subsonic::artwork_cache_entry &out) {
	if (!block.is_valid() || block->size() == 0) {
		return false;
	}

	stream_reader_formatter_simple_ref<> reader(
		block->data(), pfc::downcast_guarded<t_size>(block->size()));
	t_uint32 version = 0;
	reader >> version;
	if (version < 1 || version > k_artwork_blob_version) {
		return false;
	}

	subsonic::artwork_cache_entry value;
	if (version >= 3) {
		reader >> value.server_id;
	}
	reader >> value.cover_art_id;
	reader >> value.mime_type;
	reader >> value.local_path;
	if (version >= 2) {
		t_uint64 last_access = 0;
		reader >> value.content_hash;
		reader >> last_access;
		value.last_access_unix_ms = last_access;
	} else {
		value.content_hash.reset();
		value.last_access_unix_ms = 0;
	}
	if (value.server_id.is_empty()) {
		value.server_id = current_server_id();
	}
	if (!value.is_valid()) {
		return false;
	}

	out = std::move(value);
	return true;
}

template <typename TEntry>
std::vector<TEntry> load_all_entries(const char *domain,
									 bool (*deserialize)(fb2k::memBlockRef,
														 TEntry &)) {
	std::vector<TEntry> entries;
	static_api_ptr_t<fb2k::configStore> store;
	const auto values = store->listDomainValues(domain, true);
	entries.reserve(values->count());

	for (const auto &value : values->typed<fb2k::string>()) {
		try {
			TEntry entry;
			if (deserialize(store->getConfigBlob(value->c_str()), entry)) {
				entries.push_back(std::move(entry));
			}
		} catch (const std::exception &e) {
			subsonic::log_exception("cache", e);
		} catch (...) {
			subsonic::log_error(
				"cache", "failed to deserialize cached entry from config "
						 "store");
		}
	}

	return entries;
}

} // namespace

namespace subsonic::cache {

void initialize(abort_callback &abort) { config::ensure_cache_layout(abort); }
void reset(abort_callback &abort) {
	const auto artwork_entries = load_all_artwork_entries();
	for (const auto &entry : artwork_entries) {
		if (!entry.local_path.is_empty() &&
			filesystem::g_exists(entry.local_path, abort)) {
			filesystem::g_remove(entry.local_path, abort);
		}
	}

	const auto database_path = config::database_path();
	if (!database_path.is_empty() &&
		filesystem::g_exists(database_path, abort)) {
		filesystem::g_remove(database_path, abort);
	}

	const auto metadata_cache_path = config::metadata_json_cache_path();
	if (!metadata_cache_path.is_empty() &&
		filesystem::g_exists(metadata_cache_path, abort)) {
		filesystem::g_remove(metadata_cache_path, abort);
	}

	static_api_ptr_t<fb2k::configStore> store;
	std::vector<pfc::string8> keys_to_delete;
	{
		auto values = store->listDomainValues(k_track_domain, true);
		for (const auto &val : values->typed<fb2k::string>())
			keys_to_delete.push_back(val->c_str());

		values = store->listDomainValues(k_artwork_domain, true);
		for (const auto &val : values->typed<fb2k::string>())
			keys_to_delete.push_back(val->c_str());
	}

	const auto transaction = store->acquireTransactionScope();
	for (const auto &key : keys_to_delete) {
		abort.check();
		store->deleteConfigBlob(key);
	}

	FB2K_console_formatter()
		<< "[foo_opensubsonic][cache] local cache reset complete";
}

void upsert_track_metadata(const cached_track_metadata &entry) {
	if (!entry.is_valid()) {
		throw pfc::exception_invalid_params();
	}

	const pfc::string8 scoped_id =
		make_scoped_id(entry.server_id, entry.track_id);
	static_api_ptr_t<fb2k::configStore> store;
	store->setConfigBlob(make_cache_key(k_track_domain, scoped_id),
						 serialize_track(entry));
}

void remove_track_metadata(const char *server_id, const char *track_id) {
	const pfc::string8 scoped_id = make_scoped_id(server_id, track_id);
	const pfc::string8 key = make_cache_key(k_track_domain, scoped_id);
	if (key.is_empty()) {
		return;
	}

	static_api_ptr_t<fb2k::configStore> store;
	store->deleteConfigBlob(key);
}

bool try_get_track_metadata(const char *server_id, const char *track_id,
							cached_track_metadata &out) {
	out = {};

	const pfc::string8 key =
		make_cache_key(k_track_domain, make_scoped_id(server_id, track_id));
	if (key.is_empty()) {
		return false;
	}

	try {
		static_api_ptr_t<fb2k::configStore> store;
		return deserialize_track(store->getConfigBlob(key), out);
	} catch (const std::exception &e) {
		log_exception("cache", e);
	} catch (...) {
		log_error("cache", "failed to read cached track metadata");
	}
	return false;
}

std::vector<cached_track_metadata> load_all_track_metadata() {
	return load_all_entries<cached_track_metadata>(k_track_domain,
												   &deserialize_track);
}

void replace_track_metadata(const std::vector<cached_track_metadata> &entries,
							threaded_process_status &status,
							abort_callback &abort) {
	static_api_ptr_t<fb2k::configStore> store;

	status.set_item("Scanning old database...");
	std::vector<pfc::string8> old_keys;
	{
		const auto values = store->listDomainValues(k_track_domain, true);
		for (const auto &value : values->typed<fb2k::string>()) {
			old_keys.push_back(value->c_str());
		}
	}

	const auto transaction = store->acquireTransactionScope();

	for (size_t i = 0; i < old_keys.size(); ++i) {
		if (i % 500 == 0) {
			abort.check();
			status.set_progress(i, old_keys.size());
			status.set_item(PFC_string_formatter()
							<< "Clearing old local database: " << i << " / "
							<< old_keys.size());
		}
		store->deleteConfigBlob(old_keys[i]);
	}

	for (size_t i = 0; i < entries.size(); ++i) {
		if (i % 500 == 0) {
			abort.check();
			status.set_progress(i, entries.size());
			status.set_item(PFC_string_formatter()
							<< "Saving tracks to local cache: " << i << " / "
							<< entries.size());
		}

		const auto &entry = entries[i];
		if (!entry.is_valid())
			continue;

		const pfc::string8 key = make_cache_key(
			k_track_domain, make_scoped_id(entry.server_id, entry.track_id));
		store->setConfigBlob(key, serialize_track(entry));
	}
}

void upsert_artwork_entry(const artwork_cache_entry &entry) {
	if (!entry.is_valid()) {
		throw pfc::exception_invalid_params();
	}

	const pfc::string8 key = make_cache_key(
		k_artwork_domain, make_scoped_id(entry.server_id, entry.cover_art_id));
	static_api_ptr_t<fb2k::configStore> store;
	store->setConfigBlob(key, serialize_artwork(entry));
}

void remove_artwork_entry(const char *server_id, const char *cover_art_id) {
	const pfc::string8 key = make_cache_key(
		k_artwork_domain, make_scoped_id(server_id, cover_art_id));
	if (key.is_empty()) {
		return;
	}

	artwork_cache_entry existing;
	abort_callback_dummy abort;
	if (try_get_artwork_entry(server_id, cover_art_id, existing) &&
		!existing.local_path.is_empty() &&
		filesystem::g_exists(existing.local_path, abort) &&
		!is_shared_artwork_file(existing, load_all_artwork_entries())) {
		filesystem::g_remove(existing.local_path, abort);
	}

	static_api_ptr_t<fb2k::configStore> store;
	store->deleteConfigBlob(key);
}

void remove_server_artwork_entries(const char *server_id,
								   threaded_process_status &status,
								   abort_callback &abort) {
	if (server_id == nullptr || *server_id == '\0') {
		return;
	}

	const auto entries = load_all_artwork_entries();
	std::vector<artwork_cache_entry> matches;
	matches.reserve(entries.size());
	for (const auto &entry : entries) {
		if (entry.is_valid() &&
			subsonic::strings_equal(entry.server_id, server_id)) {
			matches.push_back(entry);
		}
	}

	static_api_ptr_t<fb2k::configStore> store;
	const auto transaction = store->acquireTransactionScope();

	for (size_t i = 0; i < matches.size(); ++i) {
		if (i % 100 == 0) {
			abort.check();
			status.set_progress(i, matches.size());
			status.set_item(PFC_string_formatter()
							<< "Removing cached artwork: " << i << " / "
							<< matches.size());
		}

		const auto &entry = matches[i];
		if (!entry.local_path.is_empty() &&
			filesystem::g_exists(entry.local_path, abort) &&
			!is_shared_artwork_file(entry, entries)) {
			filesystem::g_remove(entry.local_path, abort);
		}

		store->deleteConfigBlob(make_cache_key(
			k_artwork_domain,
			make_scoped_id(entry.server_id, entry.cover_art_id)));
	}

	status.set_progress(matches.size(), matches.size());
}

bool try_get_artwork_entry(const char *server_id, const char *cover_art_id,
						   artwork_cache_entry &out) {
	out = {};

	const pfc::string8 key = make_cache_key(
		k_artwork_domain, make_scoped_id(server_id, cover_art_id));
	if (key.is_empty()) {
		return false;
	}

	try {
		static_api_ptr_t<fb2k::configStore> store;
		return deserialize_artwork(store->getConfigBlob(key), out);
	} catch (const std::exception &e) {
		log_exception("cache", e);
	} catch (...) {
		log_error("cache", "failed to read cached artwork entry");
	}
	return false;
}

std::vector<artwork_cache_entry> load_all_artwork_entries() {
	return load_all_entries<artwork_cache_entry>(k_artwork_domain,
												 &deserialize_artwork);
}

} // namespace subsonic::cache