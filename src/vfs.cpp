#include "stdafx.h"

#include "vfs.h"

#include "config.h"
#include "http.h"
#include "metadata.h"
#include "utils/metadata_utils.h"
#include "utils/utils.h"

#include <SDK/file.h>
#include <SDK/file_info_impl.h>
#include <SDK/input.h>
#include <SDK/input_impl.h>
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

[[nodiscard]] t_filesize
determine_total_size(const subsonic::http::response &response,
					 abort_callback &abort) {
	pfc::string8 content_length;
	if (subsonic::http::try_get_header(response, "content-length",
									   content_length)) {
		t_filesize parsed = filesize_invalid;
		if (subsonic::metadata_utils::try_parse_filesize(content_length,
														 parsed)) {
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
		subsonic::metadata_utils::populate_remote_path_stats(stats);
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

class subsonic_input : public input_stubs {
  public:
	void open(service_ptr_t<file> p_filehint, const char *p_path,
			  t_input_open_reason p_reason, abort_callback &p_abort) {
		m_path = p_path;
		m_file = p_filehint;

		if (p_reason == input_open_info_write) {
			throw exception_tagging_unsupported();
		}

		input_open_file_helper(m_file, p_path, p_reason, p_abort);

		switch (p_reason) {
		case input_open_info_read:
			input_entry::g_open_for_info_read(m_reader, m_file, p_path, p_abort,
											  true);
			break;
		case input_open_decode:
			input_entry::g_open_for_decoding(m_decoder, m_file, p_path, p_abort,
											 true);
			m_reader = m_decoder;
			break;
		case input_open_info_write:
		default:
			throw exception_tagging_unsupported();
		}
	}

	void get_info(file_info &p_info, abort_callback &p_abort) {
		if (m_reader.is_empty()) {
			throw exception_io_data();
		}

		m_reader->get_info(0, p_info, p_abort);
		subsonic::metadata::overlay_file_info_for_path(m_path, p_info);
	}

	t_filestats2 get_stats2(unsigned f, abort_callback &a) {
		if (m_file.is_valid()) {
			return m_file->get_stats2_(f, a);
		}

		t_filestats2 stats = filestats2_invalid;
		subsonic::metadata_utils::populate_remote_path_stats(stats);
		return stats;
	}

	void decode_initialize(unsigned p_flags, abort_callback &p_abort) {
		if (m_decoder.is_empty()) {
			throw exception_io_data();
		}
		m_decoder->initialize(0, p_flags, p_abort);
	}

	bool decode_run(audio_chunk &p_chunk, abort_callback &p_abort) {
		if (m_decoder.is_empty()) {
			throw exception_io_data();
		}
		return m_decoder->run(p_chunk, p_abort);
	}

	void decode_seek(double p_seconds, abort_callback &p_abort) {
		if (m_decoder.is_empty()) {
			throw exception_io_data();
		}
		m_decoder->seek(p_seconds, p_abort);
	}

	bool decode_can_seek() {
		return m_decoder.is_valid() && m_decoder->can_seek();
	}

	bool decode_get_dynamic_info(file_info &p_out, double &p_timestamp_delta) {
		if (m_decoder.is_empty()) {
			return false;
		}
		return m_decoder->get_dynamic_info(p_out, p_timestamp_delta);
	}

	bool decode_get_dynamic_info_track(file_info &p_out,
									   double &p_timestamp_delta) {
		if (m_decoder.is_empty()) {
			return false;
		}

		if (!m_decoder->get_dynamic_info_track(p_out, p_timestamp_delta)) {
			return false;
		}

		subsonic::metadata::overlay_file_info_for_path(m_path, p_out);
		return true;
	}

	void decode_on_idle(abort_callback &p_abort) {
		if (m_decoder.is_valid()) {
			m_decoder->on_idle(p_abort);
		} else if (m_file.is_valid()) {
			m_file->on_idle(p_abort);
		}
	}

	void retag(const file_info &p_info, abort_callback &p_abort) {
		(void)p_info;
		(void)p_abort;
		throw exception_tagging_unsupported();
	}

	void remove_tags(abort_callback &) {
		throw exception_tagging_unsupported();
	}

	static bool g_is_our_content_type(const char *p_content_type) {
		(void)p_content_type;
		return false;
	}

	static bool g_is_our_path(const char *p_path, const char *p_extension) {
		(void)p_extension;
		pfc::string8 track_id;
		return try_get_track_id(p_path, track_id);
	}

	static const char *g_get_name() { return "OpenSubsonic virtual input"; }

	static GUID g_get_guid() {
		static const GUID guid = {
			0x63862f8d,
			0x9822,
			0x47ee,
			{0x8b, 0x2f, 0x8c, 0xa0, 0x69, 0x85, 0x75, 0x62}};
		return guid;
	}

	static GUID g_get_preferences_guid() { return pfc::guid_null; }

	static bool g_is_low_merit() { return false; }

	static bool g_fallback_is_our_payload(const void *bytes, size_t bytesAvail,
										  t_filesize bytesWhole) {
		(void)bytes;
		(void)bytesAvail;
		(void)bytesWhole;
		return false;
	}

	service_ptr_t<file> m_file;
	service_ptr_t<input_info_reader> m_reader;
	service_ptr_t<input_decoder> m_decoder;
	pfc::string8 m_path;
};

class subsonic_filesystem_impl : public filesystem_v3 {
  public:
	bool get_canonical_path(const char *path, pfc::string_base &out) override {
		pfc::string8 track_id;
		if (!try_get_track_id(path, track_id)) {
			return false;
		}

		out = subsonic::make_subsonic_path(track_id);
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

		out = subsonic::metadata::make_display_name_for_path(path);
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
		if (!try_get_track_id(canonical_path, track_id)) {
			throw exception_io_not_found();
		}

		subsonic::cached_track_metadata track;
		[[maybe_unused]] const bool has_cached_track_metadata =
			subsonic::metadata::try_get_track_metadata(track_id, track);

		const auto credentials = subsonic::config::load_server_credentials();
		if (!credentials.is_configured()) {
			throw exception_io_data();
		}

		auto wrapped = fb2k::service_new<subsonic_remote_file>();
		wrapped->initialize(
			credentials, track_id,
			subsonic::metadata_utils::make_fallback_content_type(track),
			subsonic::metadata_utils::try_get_known_total_size(track), abort);

		file::ptr file_out = wrapped.get_ptr();
		out = file_out;

		subsonic::log_info(k_scope, (PFC_string_formatter()
									 << "opened remote track id=" << track_id
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
		out = subsonic::metadata::make_display_name_for_path(path);
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
		subsonic::metadata_utils::populate_remote_path_stats(stats);
		subsonic::cached_track_metadata track;
		if (subsonic::metadata::try_get_track_metadata(track_id, track)) {
			stats.m_size =
				subsonic::metadata_utils::try_get_known_total_size(track);
		}
		return stats;
	}

	bool get_display_name_short(const char *path,
								pfc::string_base &out) override {
		if (!is_our_path(path)) {
			return false;
		}

		out = subsonic::metadata::make_display_name_for_path(path);
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

static input_singletrack_factory_t<subsonic_input, input_entry::flag_redirect>
	g_subsonic_input_factory;

} // namespace