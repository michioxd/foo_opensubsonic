#include "stdafx.h"

#include "vfs.h"

#include "config.h"
#include "http.h"
#include "metadata.h"
#include "utils.h"

#include <SDK/file.h>
#include <SDK/service_impl.h>

namespace {

constexpr const char *k_scope = "vfs";
constexpr const char *k_download_endpoint = "download.view";

bool content_type_looks_like_error_payload(const char *content_type) noexcept {
	return subsonic::starts_with_ascii_nocase(content_type, "text/xml") ||
		   subsonic::starts_with_ascii_nocase(content_type,
											  "application/xml") ||
		   subsonic::starts_with_ascii_nocase(content_type,
											  "application/json") ||
		   subsonic::starts_with_ascii_nocase(content_type, "text/json");
}

bool try_get_track_id(const char *path, pfc::string_base &out_track_id) {
	return subsonic::extract_track_id_from_path(path, out_track_id);
}

bool try_get_track_identity(const char *path,
							subsonic::track_identity &out_identity) {
	return subsonic::extract_track_identity_from_path(path, out_identity);
}

[[nodiscard]] bool
try_get_extra_field_value(const subsonic::cached_track_metadata &track,
						  const char *field_name, pfc::string_base &out_value) {
	out_value.reset();
	if (field_name == nullptr || *field_name == '\0') {
		return false;
	}

	for (const auto &field : track.extra_fields) {
		if (!field.is_valid()) {
			continue;
		}

		if (pfc::stricmp_ascii(field.key, field_name) == 0) {
			out_value = field.value;
			return true;
		}
	}

	return false;
}

[[nodiscard]] bool try_parse_filesize(const char *text,
									  t_filesize &out_value) noexcept {
	out_value = filesize_invalid;
	if (text == nullptr || *text == '\0') {
		return false;
	}

	char *end_ptr = nullptr;
	const auto parsed = std::strtoull(text, &end_ptr, 10);
	if (end_ptr == text) {
		return false;
	}

	out_value = static_cast<t_filesize>(parsed);
	return true;
}

[[nodiscard]] t_filesize
determine_total_size(const subsonic::http::response &response,
					 abort_callback &abort) {
	pfc::string8 content_length;
	if (subsonic::http::try_get_header(response, "content-length",
									   content_length)) {
		t_filesize parsed = filesize_invalid;
		if (try_parse_filesize(content_length, parsed)) {
			return parsed;
		}
	}

	try {
		const auto stream_size = response.stream->get_size(abort);
		if (stream_size != filesize_invalid) {
			return stream_size;
		}
	} catch (...) {
	}

	return filesize_invalid;
}

[[nodiscard]] subsonic::http::response
open_download_response(const subsonic::server_credentials &credentials,
					   const char *track_id, abort_callback &abort) {
	auto response =
		subsonic::http::open_api(credentials, k_download_endpoint, abort,
								 {subsonic::query_param("id", track_id)});

	if (!subsonic::http::status_is_success(response) ||
		!response.stream.is_valid() ||
		content_type_looks_like_error_payload(response.content_type)) {
		throw exception_io_data();
	}

	return response;
}

pfc::string8 guess_content_type_from_suffix(const char *suffix) {
	if (suffix == nullptr || *suffix == '\0') {
		return {};
	}

	if (subsonic::ends_with_ascii_nocase(suffix, "mp3")) {
		return "audio/mpeg";
	}
	if (subsonic::ends_with_ascii_nocase(suffix, "flac")) {
		return "audio/flac";
	}
	if (subsonic::ends_with_ascii_nocase(suffix, "ogg")) {
		return "audio/ogg";
	}
	if (subsonic::ends_with_ascii_nocase(suffix, "opus")) {
		return "audio/opus";
	}
	if (subsonic::ends_with_ascii_nocase(suffix, "m4a") ||
		subsonic::ends_with_ascii_nocase(suffix, "mp4")) {
		return "audio/mp4";
	}
	if (subsonic::ends_with_ascii_nocase(suffix, "aac")) {
		return "audio/aac";
	}
	if (subsonic::ends_with_ascii_nocase(suffix, "wav")) {
		return "audio/wav";
	}
	if (subsonic::ends_with_ascii_nocase(suffix, "wv")) {
		return "audio/wavpack";
	}
	if (subsonic::ends_with_ascii_nocase(suffix, "ape")) {
		return "audio/ape";
	}

	return {};
}

pfc::string8
make_fallback_content_type(const subsonic::cached_track_metadata &track) {
	if (!track.stream_mime_type.is_empty()) {
		return track.stream_mime_type;
	}
	return guess_content_type_from_suffix(track.suffix);
}

pfc::string8
make_display_name_from_metadata(const subsonic::cached_track_metadata &track) {
	pfc::string8 out;

	if (!track.artist.is_empty() && !track.title.is_empty()) {
		out << track.artist << " - " << track.title;
	} else if (!track.title.is_empty()) {
		out = track.title;
	} else if (!track.track_id.is_empty()) {
		out = track.track_id;
	}

	if (!track.suffix.is_empty()) {
		out << "." << track.suffix;
	}

	return out;
}

[[nodiscard]] t_filesize
try_get_known_total_size(const subsonic::cached_track_metadata &track) {
	pfc::string8 value;
	if (try_get_extra_field_value(track, "size", value)) {
		t_filesize parsed = filesize_invalid;
		if (try_parse_filesize(value, parsed)) {
			return parsed;
		}
	}

	if (try_get_extra_field_value(track, "contentLength", value)) {
		t_filesize parsed = filesize_invalid;
		if (try_parse_filesize(value, parsed)) {
			return parsed;
		}
	}

	return filesize_invalid;
}

pfc::string8 make_display_name_for_path(const char *path) {
	subsonic::cached_track_metadata track;
	if (subsonic::metadata::try_get_track_metadata_for_path(path, track)) {
		const auto value = make_display_name_from_metadata(track);
		if (!value.is_empty()) {
			return value;
		}
	}

	pfc::string8 track_id;
	if (try_get_track_id(path, track_id)) {
		return track_id;
	}

	return path != nullptr ? pfc::string8(path) : pfc::string8();
}

void populate_path_stats(foobar2000_io::t_filestats2 &stats) {
	stats.set_file(true);
	stats.set_readonly(true);
	stats.set_remote(true);
	stats.m_size = filesize_invalid;
	stats.m_timestamp = 3;
}

class subsonic_remote_file : public file_readonly_t<file_v2> {
  public:
	void initialize(subsonic::server_credentials credentials,
					const char *track_id, pfc::string8 fallback_content_type,
					t_filesize known_total_size, abort_callback &abort) {
		if (track_id == nullptr || *track_id == '\0' ||
			!credentials.is_configured()) {
			throw pfc::exception_invalid_params();
		}

		m_credentials = std::move(credentials);
		m_track_id = track_id;
		m_fallback_content_type = std::move(fallback_content_type);
		m_total_size = known_total_size;
		reopen_at(0, abort);
	}

	t_size read(void *buffer, t_size bytes, abort_callback &abort) override {
		const auto got = m_stream->read(buffer, bytes, abort);
		m_position += got;
		return got;
	}

	t_filesize get_size(abort_callback &abort) override {
		abort.check();
		return m_total_size;
	}

	t_filesize get_position(abort_callback &abort) override {
		abort.check();
		return m_position;
	}

	void seek(t_filesize position, abort_callback &abort) override {
		abort.check();
		if (m_total_size != filesize_invalid && position > m_total_size) {
			position = m_total_size;
		}

		if (position == m_position) {
			return;
		}

		reopen_at(position, abort);
	}

	bool can_seek() override { return true; }

	bool get_content_type(pfc::string_base &out) override {
		if (!m_content_type.is_empty()) {
			out = m_content_type;
			return true;
		}
		if (m_stream->get_content_type(out)) {
			return true;
		}
		if (!m_fallback_content_type.is_empty()) {
			out = m_fallback_content_type;
			return true;
		}
		out.reset();
		return false;
	}

	bool is_in_memory() override { return false; }

	void on_idle(abort_callback &abort) override { m_stream->on_idle(abort); }

	t_filetimestamp get_timestamp(abort_callback &abort) override { return 3; }

	void reopen(abort_callback &abort) override { reopen_at(0, abort); }

	bool is_remote() override { return true; }

	service_ptr get_metadata(abort_callback &abort) override {
		return m_stream->get_metadata_(abort);
	}

	t_filestats2 get_stats2(uint32_t s2flags, abort_callback &abort) override {
		auto stats = m_stream->get_stats2_(s2flags, abort);
		populate_path_stats(stats);
		stats.m_size = m_total_size;
		return stats;
	}

	size_t lowLevelIO(const GUID &guid, size_t arg1, void *arg2,
					  size_t arg2size, abort_callback &abort) override {
		return m_stream->lowLevelIO_(guid, arg1, arg2, arg2size, abort);
	}

  private:
	void reopen_at(t_filesize offset, abort_callback &abort) {
		auto response =
			open_download_response(m_credentials, m_track_id, abort);

		const auto response_total_size = determine_total_size(response, abort);
		if (response_total_size != filesize_invalid) {
			m_total_size = response_total_size;
		}

		if (m_total_size != filesize_invalid && offset > m_total_size) {
			offset = m_total_size;
		}

		if (offset > 0) {
			const auto skipped = response.stream->skip(offset, abort);
			if (skipped != offset) {
				subsonic::log_warning(
					k_scope,
					(PFC_string_formatter()
					 << "remote seek clamped id=" << m_track_id
					 << " requested=" << offset << " actual=" << skipped)
						.c_str());
				offset = skipped;
				if (m_total_size == filesize_invalid || offset > m_total_size) {
					m_total_size = offset;
				}
			}
		}

		m_content_type = response.content_type;
		if (m_content_type.is_empty()) {
			response.stream->get_content_type(m_content_type);
		}
		m_stream = response.stream;
		m_position = offset;

		subsonic::log_info(k_scope, (PFC_string_formatter()
									 << "opened remote stream id=" << m_track_id
									 << " offset=" << offset
									 << " status=" << response.status_text)
										.c_str());
	}

	subsonic::server_credentials m_credentials;
	pfc::string8 m_track_id;
	file::ptr m_stream;
	pfc::string8 m_fallback_content_type;
	pfc::string8 m_content_type;
	t_filesize m_total_size = filesize_invalid;
	t_filesize m_position = 0;
};

class subsonic_filesystem_impl : public filesystem_v3 {
  public:
	bool get_canonical_path(const char *path, pfc::string_base &out) override {
		subsonic::track_identity identity;
		if (!try_get_track_identity(path, identity)) {
			return false;
		}

		pfc::string8 server_id = identity.server_id;
		if (server_id.is_empty()) {
			server_id = subsonic::config::load_selected_server_id();
			if (server_id.is_empty()) {
				server_id =
					subsonic::config::load_server_credentials().server_id;
			}
		}

		out = subsonic::make_subsonic_path(server_id, identity.track_id);
		return true;
	}

	bool is_our_path(const char *path) override {
		pfc::string8 ignored;
		return try_get_track_id(path, ignored);
	}

	bool get_display_path(const char *path, pfc::string_base &out) override {
		if (!is_our_path(path)) {
			return false;
		}

		out = make_display_name_for_path(path);
		return true;
	}

	void open(service_ptr_t<file> &out, const char *path, t_open_mode mode,
			  abort_callback &abort) override {
		abort.check();

		const auto requested_mode = mode & open_mode_mask;
		if (requested_mode != open_mode_read) {
			throw exception_io_denied_readonly();
		}

		pfc::string8 canonical_path;
		if (!get_canonical_path(path, canonical_path)) {
			throw exception_io_no_handler_for_path();
		}

		pfc::string8 track_id;
		subsonic::track_identity identity;
		if (!try_get_track_identity(canonical_path, identity)) {
			throw exception_io_not_found();
		}

		subsonic::cached_track_metadata track;
		[[maybe_unused]] const bool has_cached_track_metadata =
			subsonic::metadata::try_get_track_metadata(
				identity.server_id, identity.track_id, track);

		subsonic::server_credentials credentials;
		const bool has_server_credentials =
			subsonic::config::try_get_server_credentials(identity.server_id,
														 credentials);
		if (!has_server_credentials || !credentials.is_configured()) {
			throw exception_io_data();
		}

		auto wrapped = fb2k::service_new<subsonic_remote_file>();
		wrapped->initialize(credentials, identity.track_id,
							make_fallback_content_type(track),
							try_get_known_total_size(track), abort);

		file::ptr file_out = wrapped.get_ptr();
		out = file_out;

		subsonic::log_info(k_scope,
						   (PFC_string_formatter()
							<< "opened remote track id=" << identity.track_id
							<< " server=" << identity.server_id
							<< " path=" << canonical_path));
	}

	void remove(const char *path, abort_callback &abort) override {
		(void)path;
		abort.check();
		throw exception_io_denied_readonly();
	}

	void move(const char *src, const char *dst,
			  abort_callback &abort) override {
		(void)src;
		(void)dst;
		abort.check();
		throw exception_io_denied_readonly();
	}

	bool is_remote(const char *src) override { return true; }

	void get_stats(const char *path, t_filestats &stats, bool &is_writeable,
				   abort_callback &abort) override {
		const auto stats2 = get_stats2(path,
									   stats2_legacy | stats2_readOnly |
										   stats2_fileOrFolder | stats2_remote,
									   abort);
		stats = stats2.to_legacy();
		is_writeable = false;
	}

	void create_directory(const char *path, abort_callback &abort) override {
		(void)path;
		abort.check();
		throw exception_io_denied_readonly();
	}

	void list_directory(const char *path, directory_callback &out,
						abort_callback &abort) override {
		(void)path;
		(void)out;
		abort.check();
		throw exception_io_not_directory();
	}

	bool supports_content_types() override { return true; }

	void move_overwrite(const char *src, const char *dst,
						abort_callback &abort) override {
		(void)src;
		(void)dst;
		abort.check();
		throw exception_io_denied_readonly();
	}

	void make_directory(const char *path, abort_callback &abort,
						bool *did_create) override {
		(void)path;
		abort.check();
		if (did_create != nullptr) {
			*did_create = false;
		}
		throw exception_io_denied_readonly();
	}

	bool directory_exists(const char *path, abort_callback &abort) override {
		(void)path;
		abort.check();
		return false;
	}

	bool file_exists(const char *path, abort_callback &abort) override {
		abort.check();
		return is_our_path(path);
	}

	char pathSeparator() override { return '/'; }

	void extract_filename_ext(const char *path,
							  pfc::string_base &out) override {
		out = make_display_name_for_path(path);
	}

	bool get_parent_path(const char *path, pfc::string_base &out) override {
		(void)path;
		out.reset();
		return false;
	}

	void list_directory_ex(const char *path, directory_callback &out,
						   unsigned list_mode, abort_callback &abort) override {
		(void)list_mode;
		list_directory(path, out, abort);
	}

	t_filestats2 get_stats2(const char *path, uint32_t s2flags,
							abort_callback &abort) override {
		abort.check();

		pfc::string8 canonical_path;
		if (!get_canonical_path(path, canonical_path)) {
			throw exception_io_no_handler_for_path();
		}

		pfc::string8 track_id;
		if (!try_get_track_id(canonical_path, track_id)) {
			throw exception_io_not_found();
		}

		(void)s2flags;

		t_filestats2 stats = filestats2_invalid;
		populate_path_stats(stats);
		subsonic::cached_track_metadata track;
		subsonic::track_identity identity;
		if (try_get_track_identity(canonical_path, identity) &&
			subsonic::metadata::try_get_track_metadata(
				identity.server_id, identity.track_id, track)) {
			stats.m_size = try_get_known_total_size(track);
		}
		return stats;
	}

	bool get_display_name_short(const char *path,
								pfc::string_base &out) override {
		if (!is_our_path(path)) {
			return false;
		}

		out = make_display_name_for_path(path);
		return true;
	}

	void list_directory_v3(const char *path, directory_callback_v3 &callback,
						   unsigned list_mode, abort_callback &abort) override {
		(void)path;
		(void)callback;
		(void)list_mode;
		abort.check();
		throw exception_io_not_directory();
	}
};

static service_factory_single_t<subsonic_filesystem_impl>
	g_subsonic_filesystem_impl_factory;

} // namespace