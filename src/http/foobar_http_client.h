#pragma once

#include "http_client_interface.h"
#include "../types.h"

#include <SDK/abort_callback.h>

namespace subsonic {

// Foobar2000 HTTP client implementation
// Wraps existing http.cpp functions with IHttpClient interface
//
// Lifetime: Safe for long-lived use (e.g., service_locator)
// - Stores credentials by value (no dangling reference)
// - abort_callback passed per-request (proper cancellation scope)
//
// Performance: Virtual call overhead is negligible compared to HTTP latency
// - Virtual call: ~3ns
// - HTTP request: 10-1000ms
// - Overhead: <0.001%
class foobar_http_client final : public IHttpClient {
  public:
	// Constructor stores credentials by value
	// credentials: Server URL, username, password (copied)
	explicit foobar_http_client(server_credentials credentials) noexcept;

	~foobar_http_client() override = default;

	// Fetch API endpoint - implements IHttpClient
	// Hot path: called frequently during sync
	// abort: Per-request cancellation (proper scoping)
	[[nodiscard]] nlohmann::json fetch_api(
		const char *endpoint, const std::vector<query_param> &params,
		abort_callback &abort) override;

	// Raw HTTP GET - implements IHttpClient
	// abort: Per-request cancellation
	[[nodiscard]] http_response get(const char *url,
									abort_callback &abort) override;

	// Binary download - implements IHttpClient
	// Used for artwork fetching (less frequent than API calls)
	// abort: Per-request cancellation
	[[nodiscard]] std::vector<uint8_t>
	get_binary(const char *url, size_t max_bytes, abort_callback &abort) override;

  private:
	server_credentials m_credentials; // Stored by value for safe long-lived use
};

} // namespace subsonic
