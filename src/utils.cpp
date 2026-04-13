#include "stdafx.h"

#include "utils.h"

namespace {

constexpr const char *k_rest_prefix = "rest/";

void append_log_line(const char *level, const char *scope,
					 const char *message) {
	FB2K_console_formatter() << "[foo_opensubsonic][" << level << "]["
							 << (scope != nullptr ? scope : "general") << "] "
							 << (message != nullptr ? message : "");
}

void trim_trailing_slashes(pfc::string8 &value) {
	while (value.length() > 0 && value[value.length() - 1] == '/') {
		value.truncate(value.length() - 1);
	}
}

const char *skip_leading_slashes(const char *value) noexcept {
	if (value == nullptr) {
		return "";
	}
	while (*value == '/') {
		++value;
	}
	return value;
}

void append_server_key_part(std::string &out, const char *value) {
	if (value == nullptr) {
		return;
	}

	for (const char *cursor = value; *cursor != '\0'; ++cursor) {
		const auto ch = static_cast<unsigned char>(*cursor);
		if (std::isalnum(ch) != 0) {
			out.push_back(static_cast<char>(std::tolower(ch)));
		}
	}
}

} // namespace

namespace subsonic {

bool strings_equal(const char *lhs, const char *rhs) noexcept {
	const char *left = lhs != nullptr ? lhs : "";
	const char *right = rhs != nullptr ? rhs : "";
	return std::strcmp(left, right) == 0;
}

bool starts_with_ascii_nocase(const char *text, const char *prefix) noexcept {
	if (text == nullptr || prefix == nullptr) {
		return false;
	}
	const auto prefix_len = strlen(prefix);
	return pfc::stricmp_ascii_ex(text, prefix_len, prefix, prefix_len) == 0;
}

bool ends_with_ascii_nocase(const char *text, const char *suffix) noexcept {
	if (text == nullptr || suffix == nullptr) {
		return false;
	}
	const auto text_len = strlen(text);
	const auto suffix_len = strlen(suffix);
	if (suffix_len > text_len) {
		return false;
	}
	return pfc::stricmp_ascii_ex(text + (text_len - suffix_len), suffix_len,
								 suffix, suffix_len) == 0;
}

bool is_subsonic_path(const char *path) noexcept {
	return starts_with_ascii_nocase(path, k_scheme);
}

pfc::string8 normalize_base_url(const char *base_url) {
	pfc::string8 normalized = base_url != nullptr ? base_url : "";
	trim_trailing_slashes(normalized);
	return normalized;
}

pfc::string8 generate_server_id() {
	pfc::string8 guid = pfc::print_guid(pfc::createGUID());
	guid.replace_string("{", "");
	guid.replace_string("}", "");
	guid.replace_string("-", "");

	pfc::string8 out = "srv-";
	out += guid;
	return out;
}

pfc::string8 make_server_storage_id(const char *server_name,
									const char *base_url) {
	std::string seed;
	append_server_key_part(seed, server_name);
	seed.push_back('|');
	append_server_key_part(seed, normalize_base_url(base_url).c_str());
	if (seed.empty()) {
		seed = "server";
	}

	const auto hash = md5_hex(seed.c_str());
	pfc::string8 out = "srv-";
	out.add_string(hash.c_str(), 12);
	return out;
}

pfc::string8 make_subsonic_path(const char *server_id, const char *track_id) {
	pfc::string8 path = k_scheme;
	if (server_id != nullptr && *server_id != '\0') {
		path += server_id;
		path += "/";
	}
	if (track_id != nullptr) {
		path += track_id;
	}
	return path;
}

pfc::string8 make_subsonic_path(const char *track_id) {
	return make_subsonic_path(nullptr, track_id);
}

bool extract_track_identity_from_path(const char *path,
									  track_identity &out_identity) {
	out_identity = {};
	if (!is_subsonic_path(path)) {
		return false;
	}

	const char *cursor = path + strlen(k_scheme);
	const char *end = cursor;
	while (*end != '\0' && *end != '?' && *end != '#') {
		++end;
	}
	if (cursor == end) {
		return false;
	}

	const char *slash = cursor;
	while (slash < end && *slash != '/') {
		++slash;
	}

	if (slash < end && *slash == '/') {
		out_identity.server_id.add_string(cursor,
										  static_cast<t_size>(slash - cursor));
		out_identity.track_id.add_string(slash + 1,
										 static_cast<t_size>(end - slash - 1));
	} else {
		out_identity.track_id.add_string(cursor,
										 static_cast<t_size>(end - cursor));
	}

	out_identity.path = path;
	return !out_identity.track_id.is_empty();
}

bool extract_track_id_from_path(const char *path,
								pfc::string_base &out_track_id) {
	out_track_id.reset();
	track_identity identity;
	if (!extract_track_identity_from_path(path, identity)) {
		return false;
	}
	out_track_id = identity.track_id;
	return out_track_id.length() > 0;
}

pfc::string8 generate_salt() {
	pfc::string8 salt = pfc::print_guid(pfc::createGUID());
	salt.replace_string("{", "");
	salt.replace_string("}", "");
	salt.replace_string("-", "");
	return salt;
}

pfc::string8 md5_hex(const char *text) {
	const auto hash = static_api_ptr_t<hasher_md5>()->process_single_string(
		text != nullptr ? text : "");
	return pfc::format_hexdump_lowercase(hash.m_data, sizeof(hash.m_data), "");
}

pfc::string8 make_auth_token(const char *password, const char *salt) {
	pfc::string8 input = password != nullptr ? password : "";
	input += salt != nullptr ? salt : "";
	return md5_hex(input);
}

void append_query_param(pfc::string_base &query, const char *key,
						const char *value) {
	PFC_ASSERT(key != nullptr);
	if (query.length() > 0) {
		query += "&";
	}
	pfc::urlEncodeAppend(query, key);
	query += "=";
	pfc::urlEncodeAppend(query, value != nullptr ? value : "");
}

pfc::string8 build_auth_query(const server_credentials &credentials,
							  const char *salt) {
	pfc::string8 query;
	append_query_param(query, "u", credentials.username);
	append_query_param(query, "t", make_auth_token(credentials.password, salt));
	append_query_param(query, "s", salt != nullptr ? salt : "");
	append_query_param(query, "v",
					   credentials.api_version.is_empty()
						   ? k_default_api_version
						   : credentials.api_version.c_str());
	append_query_param(query, "c",
					   credentials.client_name.is_empty()
						   ? k_default_client_name
						   : credentials.client_name.c_str());
	append_query_param(query, "f", "json");
	return query;
}

std::vector<pfc::string8>
build_api_base_urls(const server_credentials &credentials) {
	std::vector<pfc::string8> bases;
	bases.reserve(2);

	const auto local_url = normalize_base_url(credentials.local_url);
	if (!local_url.is_empty()) {
		bases.push_back(local_url);
	}

	const auto base_url = normalize_base_url(credentials.base_url);
	if (!base_url.is_empty()) {
		bool exists = false;
		for (const auto &item : bases) {
			if (strings_equal(item, base_url)) {
				exists = true;
				break;
			}
		}
		if (!exists) {
			bases.push_back(base_url);
		}
	}

	return bases;
}

pfc::string8 build_api_url(const server_credentials &credentials,
						   const char *endpoint,
						   const std::vector<query_param> &query_params) {
	const auto bases = build_api_base_urls(credentials);
	if (bases.empty()) {
		return {};
	}

	pfc::string8 base = bases.front();
	if (base.is_empty()) {
		return {};
	}

	const char *normalized_endpoint = skip_leading_slashes(endpoint);
	pfc::string8 url = base;
	url += "/";
	if (!starts_with_ascii_nocase(normalized_endpoint, k_rest_prefix)) {
		url += k_rest_prefix;
	}
	url += normalized_endpoint;

	const pfc::string8 salt = generate_salt();
	pfc::string8 query = build_auth_query(credentials, salt);
	for (const auto &param : query_params) {
		if (!param.is_valid()) {
			continue;
		}
		append_query_param(query, param.key, param.value);
	}

	if (query.length() > 0) {
		url += "?";
		url += query;
	}
	return url;
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