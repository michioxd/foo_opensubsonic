#pragma once

#include "http_client_interface.h"
#include "../types.h"

#include <SDK/abort_callback.h>

namespace subsonic {

// Foobar2000 HTTP client implementation
// Wraps existing http.cpp functions with IHttpClient interface
//
// Performance: Virtual call overhead is negligible compared to HTTP latency
// - Virtual call: ~3ns
// - HTTP request: 10-1000ms
// - Overhead: <0.001%
class foobar_http_client final : public IHttpClient {
  public:
	// Constructor requires credentials and abort callback
	// credentials: Server URL, username, password
	// abort: Foobar2000 abort callback for cancellation
	foobar_http_client(const server_credentials &credentials,
					   abort_callback &abort) noexcept;

	~foobar_http_client() override = default;

	// Fetch API endpoint - implements IHttpClient
	// Hot path: called frequently during sync
	[[nodiscard]] nlohmann::json fetch_api(
		const char *endpoint,
		const std::vector<query_param> &params = {}) override;

	// Raw HTTP GET - implements IHttpClient
	[[nodiscard]] http_response get(const char *url) override;

	// Binary download - implements IHttpClient
	// Used for artwork fetching (less frequent than API calls)
	[[nodiscard]] std::vector<uint8_t> get_binary(
		const char *url,
		size_t max_bytes = 10 * 1024 * 1024) override;

  private:
	const server_credentials &m_credentials;
	abort_callback &m_abort;
};

} // namespace subsonic
