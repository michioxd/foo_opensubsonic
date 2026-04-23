// PCH disabled for utils folder

#include "url_builder.h"
#include "crypto_util.h"
#include "string_util.h"

namespace {

constexpr const char *k_rest_prefix = "rest/";

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

} // namespace

namespace subsonic {
namespace url_builder {

pfc::string8 normalize_base_url(const char *base_url) {
	pfc::string8 normalized = base_url != nullptr ? base_url : "";
	trim_trailing_slashes(normalized);
	return normalized;
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
	append_query_param(query, "t",
					   crypto_util::make_auth_token(credentials.password, salt));
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

pfc::string8 build_api_url(const server_credentials &credentials,
						   const char *endpoint,
						   const std::vector<query_param> &query_params) {
	pfc::string8 base = normalize_base_url(credentials.base_url);
	if (base.is_empty()) {
		return {};
	}

	const char *normalized_endpoint = skip_leading_slashes(endpoint);
	pfc::string8 url = base;
	url += "/";
	if (!string_util::starts_with_ascii_nocase(normalized_endpoint,
											   k_rest_prefix)) {
		url += k_rest_prefix;
	}
	url += normalized_endpoint;

	const pfc::string8 salt = crypto_util::generate_salt();
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

} // namespace url_builder
} // namespace subsonic
