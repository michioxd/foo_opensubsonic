// PCH disabled for utils folder

#include "utils.h"
#include "crypto_util.h"
#include "string_util.h"
#include "track_path_util.h"
#include "url_builder.h"

namespace {

void append_log_line(const char *level, const char *scope,
					 const char *message) {
	FB2K_console_formatter() << "[foo_opensubsonic][" << level << "]["
							 << (scope != nullptr ? scope : "general") << "] "
							 << (message != nullptr ? message : "");
}

} // namespace

namespace subsonic {

// String utilities moved to string_util.h/cpp

bool strings_equal(const char *lhs, const char *rhs) noexcept {
	return string_util::strings_equal(lhs, rhs);
}

bool starts_with_ascii_nocase(const char *text, const char *prefix) noexcept {
	return string_util::starts_with_ascii_nocase(text, prefix);
}

bool ends_with_ascii_nocase(const char *text, const char *suffix) noexcept {
	return string_util::ends_with_ascii_nocase(text, suffix);
}

// Track path utilities moved to track_path_util.h/cpp

bool is_subsonic_path(const char *path) noexcept {
	return track_path_util::is_subsonic_path(path);
}

pfc::string8 normalize_base_url(const char *base_url) {
	return url_builder::normalize_base_url(base_url);
}

pfc::string8 make_subsonic_path(const char *track_id) {
	return track_path_util::make_subsonic_path(track_id);
}

bool extract_track_id_from_path(const char *path,
								pfc::string_base &out_track_id) {
	return track_path_util::extract_track_id_from_path(path, out_track_id);
}

// Crypto utilities moved to crypto_util.h/cpp

pfc::string8 generate_salt() {
	return crypto_util::generate_salt();
}

pfc::string8 md5_hex(const char *text) {
	return crypto_util::md5_hex(text);
}

pfc::string8 make_auth_token(const char *password, const char *salt) {
	return crypto_util::make_auth_token(password, salt);
}

// URL building utilities moved to url_builder.h/cpp

void append_query_param(pfc::string_base &query, const char *key,
						const char *value) {
	url_builder::append_query_param(query, key, value);
}

pfc::string8 build_auth_query(const server_credentials &credentials,
							  const char *salt) {
	return url_builder::build_auth_query(credentials, salt);
}

pfc::string8 build_api_url(const server_credentials &credentials,
						   const char *endpoint,
						   const std::vector<query_param> &query_params) {
	return url_builder::build_api_url(credentials, endpoint, query_params);
}

void ensure_directory_exists(const char *path, abort_callback &abort) {
	if (path == nullptr || *path == '\0') {
		throw pfc::exception_invalid_params();
	}

	if (filesystem::g_exists(path, abort)) {
		return;
	}

	try {
		filesystem::g_create_directory(path, abort);
	} catch (const exception_io_already_exists &) {
		// Another thread/process may have created it in the meantime.
	}
}

void log_info(const char *scope, const char *message) {
	append_log_line("info", scope, message);
}

void log_warning(const char *scope, const char *message) {
	append_log_line("warning", scope, message);
}

void log_error(const char *scope, const char *message) {
	append_log_line("error", scope, message);
}

void log_exception(const char *scope, const std::exception &e) {
	FB2K_console_formatter()
		<< "[foo_opensubsonic][error]["
		<< (scope != nullptr ? scope : "general") << "] exception: " << e;
}

} // namespace subsonic