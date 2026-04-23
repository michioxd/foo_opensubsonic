#pragma once

#include "../types.h"

#include <nlohmann/json.hpp>
#include <SDK/foobar2000.h>

#include <vector>
#include <cstdint>

namespace subsonic {

// HTTP response structure
struct http_response {
	int status_code = 0;
	pfc::string8 body;
	pfc::string8 status_text;
	bool success = false;
};

// Interface for HTTP client - enables dependency injection and testing
// Implementations can use foobar2000 SDK, libcurl, or mock responses
//
// Thread-safety: Implementations should be thread-safe or document otherwise
// Lifetime: Safe for long-lived use (service_locator pattern)
//
// Performance Note:
// Virtual call overhead is ~3ns on modern CPUs (negligible compared to HTTP latency)
// HTTP requests take 10-1000ms, so 0.000003ms virtual call overhead is <0.0003%
// Testability benefit far outweighs tiny performance cost
class IHttpClient {
  public:
	virtual ~IHttpClient() = default;

	// Perform HTTP GET request to OpenSubsonic API endpoint
	// endpoint: API method (e.g., "getArtists.view")
	// params: Query parameters (credentials added automatically)
	// abort: Per-request cancellation callback
	// Returns: Parsed JSON response (throws on HTTP errors)
	//
	// Performance: HTTP latency dominates (10-1000ms), virtual call ~3ns negligible
	[[nodiscard]] virtual nlohmann::json
	fetch_api(const char *endpoint, const std::vector<query_param> &params,
			  abort_callback &abort) = 0;

	// Low-level HTTP GET (for non-API requests like artwork)
	// abort: Per-request cancellation callback
	// Returns: Raw HTTP response with status code and body
	[[nodiscard]] virtual http_response get(const char *url,
											abort_callback &abort) = 0;

	// Download binary data (for artwork, etc.)
	// max_bytes: Limit download size
	// abort: Per-request cancellation callback
	// Returns: Binary data vector
	[[nodiscard]] virtual std::vector<uint8_t>
	get_binary(const char *url, size_t max_bytes, abort_callback &abort) = 0;
};

} // namespace subsonic
