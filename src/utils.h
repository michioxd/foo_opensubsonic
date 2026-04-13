#pragma once

#include "types.h"

#include <SDK/filesystem.h>
#include <SDK/hasher_md5.h>

#include <vector>

namespace subsonic {

[[nodiscard]] bool strings_equal(const char *lhs, const char *rhs) noexcept;

[[nodiscard]] bool starts_with_ascii_nocase(const char *text,
											const char *prefix) noexcept;
[[nodiscard]] bool ends_with_ascii_nocase(const char *text,
										  const char *suffix) noexcept;

[[nodiscard]] bool is_subsonic_path(const char *path) noexcept;
[[nodiscard]] pfc::string8 normalize_base_url(const char *base_url);
[[nodiscard]] pfc::string8 generate_server_id();
[[nodiscard]] pfc::string8 make_server_storage_id(const char *server_name,
												  const char *base_url);
[[nodiscard]] pfc::string8 make_subsonic_path(const char *server_id,
											  const char *track_id);
[[nodiscard]] pfc::string8 make_subsonic_path(const char *track_id);
[[nodiscard]] bool
extract_track_identity_from_path(const char *path,
								 track_identity &out_identity);
[[nodiscard]] bool extract_track_id_from_path(const char *path,
											  pfc::string_base &out_track_id);

[[nodiscard]] pfc::string8 generate_salt();
[[nodiscard]] pfc::string8 md5_hex(const char *text);
[[nodiscard]] pfc::string8 make_auth_token(const char *password,
										   const char *salt);

void append_query_param(pfc::string_base &query, const char *key,
						const char *value);
[[nodiscard]] pfc::string8
build_auth_query(const server_credentials &credentials, const char *salt);
[[nodiscard]] std::vector<pfc::string8>
build_api_base_urls(const server_credentials &credentials);
[[nodiscard]] pfc::string8
build_api_url(const server_credentials &credentials, const char *endpoint,
			  const std::vector<query_param> &query_params = {});

void ensure_directory_exists(const char *path, abort_callback &abort);

void log_info(const char *scope, const char *message);
void log_warning(const char *scope, const char *message);
void log_error(const char *scope, const char *message);
void log_exception(const char *scope, const std::exception &e);

} // namespace subsonic