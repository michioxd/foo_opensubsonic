#pragma once

#include "../types.h"

#include <SDK/file.h>
#include <SDK/http_client.h>

#include <vector>

namespace subsonic::http {

struct header {
	pfc::string8 name;
	pfc::string8 value;

	header() = default;
	header(const char *p_name, const char *p_value)
		: name(p_name), value(p_value) {}
	header(pfc::string8 p_name, pfc::string8 p_value)
		: name(std::move(p_name)), value(std::move(p_value)) {}

	[[nodiscard]] bool is_valid() const noexcept { return !name.is_empty(); }
};

struct request_params {
	pfc::string8 method = "GET";
	std::vector<header> headers;
	bool allow_http_error_response = false;
};

struct response {
	file::ptr stream;
	service_ptr_t<http_reply> reply;
	pfc::string8 effective_url;
	pfc::string8 status_text;
	pfc::string8 content_type;

	[[nodiscard]] bool has_reply() const noexcept { return reply.is_valid(); }
	[[nodiscard]] bool has_stream() const noexcept { return stream.is_valid(); }
};

response open(const char *url, abort_callback &abort,
			  const request_params &params = {});
response open_api(const server_credentials &credentials, const char *endpoint,
				  abort_callback &abort,
				  const std::vector<query_param> &query_params = {},
				  const request_params &params = {});

void read_all(file::ptr stream, mem_block_container &out, abort_callback &abort,
			  size_t chunk_size = 64 * 1024,
			  size_t max_bytes = (std::numeric_limits<size_t>::max)());
void read_all(const response &http_response, mem_block_container &out,
			  abort_callback &abort, size_t chunk_size = 64 * 1024,
			  size_t max_bytes = (std::numeric_limits<size_t>::max)());

[[nodiscard]] pfc::string8 read_text(file::ptr stream, abort_callback &abort,
									 size_t max_bytes = 8 * 1024 * 1024);
[[nodiscard]] pfc::string8 read_text(const response &http_response,
									 abort_callback &abort,
									 size_t max_bytes = 8 * 1024 * 1024);

[[nodiscard]] bool try_get_header(const response &http_response,
								  const char *name,
								  pfc::string_base &out_value);
[[nodiscard]] bool status_is_success(const response &http_response) noexcept;

// Parse numeric HTTP status code from status text (e.g., "200 OK" -> 200)
// Returns: Status code (100-599) or 0 if parsing failed
[[nodiscard]] int parse_status_code(const pfc::string8 &status_text) noexcept;

} // namespace subsonic::http