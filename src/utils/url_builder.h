#pragma once

#include <SDK/foobar2000.h>

#include "../types.h"

#include <vector>

namespace subsonic {
namespace url_builder {

// Normalize base URL by removing trailing slashes
// Used for consistent URL formatting in configuration
[[nodiscard]] pfc::string8 normalize_base_url(const char *base_url);

// Append single query parameter to URL string
// Handles URL encoding and proper separator (&)
// Example: append_query_param(url, "id", "123") → url += "&id=123"
void append_query_param(pfc::string_base &query, const char *key,
						const char *value);

// Build authentication query string for OpenSubsonic API
// Generates: u=username&t=token&s=salt&v=1.16.1&c=client&f=json
// Token = md5(password + salt)
// Salt is provided by caller (should be randomly generated)
[[nodiscard]] pfc::string8
build_auth_query(const server_credentials &credentials, const char *salt);

// Build full OpenSubsonic API URL
// Combines base URL, endpoint, auth query, and additional params
// Example: https://server/rest/getArtists.view?u=user&t=...&id=123
// Handles:
// - Trailing slashes normalization
// - "rest/" prefix injection if missing
// - Query parameter encoding
[[nodiscard]] pfc::string8
build_api_url(const server_credentials &credentials, const char *endpoint,
			  const std::vector<query_param> &query_params = {});

} // namespace url_builder
} // namespace subsonic
