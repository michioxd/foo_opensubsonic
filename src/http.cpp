#include "stdafx.h"

#include "http.h"

#include "utils.h"

namespace {

constexpr const char *k_scope = "http";
constexpr size_t k_default_chunk_size = 64 * 1024;

void apply_headers(http_request::ptr request,
				   const std::vector<subsonic::http::header> &headers) {
	for (const auto &header : headers) {
		if (!header.is_valid()) {
			continue;
		}
		request->add_header(header.name, header.value);
	}
}

void populate_response_metadata(subsonic::http::response &out) {
	if (!out.stream.is_valid()) {
		return;
	}

	if (out.stream->cast(out.reply)) {
		out.reply->get_status(out.status_text);
		out.reply->get_http_header("content-type", out.content_type);
	} else {
		out.stream->get_content_type(out.content_type);
	}
}

int parse_status_code(const pfc::string8 &status_text) noexcept {
	if (status_text.length() < 3) {
		return 0;
	}

	const char *cursor = status_text.c_str();
	while (cursor[0] != '\0') {
		if (std::isdigit(static_cast<unsigned char>(cursor[0])) &&
			std::isdigit(static_cast<unsigned char>(cursor[1])) &&
			std::isdigit(static_cast<unsigned char>(cursor[2]))) {
			return ((cursor[0] - '0') * 100) + ((cursor[1] - '0') * 10) +
				   (cursor[2] - '0');
		}
		++cursor;
	}

	return 0;
}

size_t clamp_chunk_size(size_t chunk_size) noexcept {
	return chunk_size == 0 ? k_default_chunk_size : chunk_size;
}

} // namespace

namespace subsonic::http {

response open(const char *url, abort_callback &abort,
			  const request_params &params) {
	if (url == nullptr || *url == '\0') {
		throw pfc::exception_invalid_params();
	}

	abort.check();

	pfc::string8 method = params.method;
	if (method.is_empty()) {
		method = "GET";
	}
	log_info(k_scope,
			 (PFC_string_formatter() << "opening " << method << " " << url));

	auto request = http_client::get()->create_request(method);
	request->add_header("Accept", "application/json, */*");
	request->add_header("User-Agent", k_default_client_name);
	apply_headers(request, params.headers);

	response out;
	out.effective_url = url;
	out.stream = params.allow_http_error_response ? request->run_ex(url, abort)
												  : request->run(url, abort);
	populate_response_metadata(out);

	FB2K_console_formatter()
		<< "[foo_opensubsonic][http] response status="
		<< (out.status_text.is_empty() ? "<unavailable>"
									   : out.status_text.c_str())
		<< " url=" << out.effective_url;
	return out;
}

response open_api(const server_credentials &credentials, const char *endpoint,
				  abort_callback &abort,
				  const std::vector<query_param> &query_params,
				  const request_params &params) {
	if (!credentials.is_configured()) {
		throw exception_io_data();
	}

	std::exception_ptr last_error;
	const auto base_urls = build_api_base_urls(credentials);
	for (const auto &base_url : base_urls) {
		server_credentials candidate = credentials;
		candidate.base_url = base_url;
		candidate.local_url.reset();

		const pfc::string8 url =
			build_api_url(candidate, endpoint, query_params);
		if (url.is_empty()) {
			continue;
		}

		try {
			return open(url.c_str(), abort, params);
		} catch (...) {
			last_error = std::current_exception();
		}
	}

	if (last_error != nullptr) {
		std::rethrow_exception(last_error);
	}

	throw exception_io_data();
}

void read_all(file::ptr stream, mem_block_container &out, abort_callback &abort,
			  size_t chunk_size, size_t max_bytes) {
	if (!stream.is_valid()) {
		throw pfc::exception_invalid_params();
	}

	abort.check();
	const size_t actual_chunk_size = clamp_chunk_size(chunk_size);
	pfc::array_t<t_uint8> buffer;
	buffer.set_size(actual_chunk_size);

	out.set_size(0);

	size_t total = 0;
	for (;;) {
		abort.check();
		const size_t got =
			stream->read(buffer.get_ptr(), actual_chunk_size, abort);
		if (got == 0) {
			break;
		}

		if (total > max_bytes || got > (max_bytes - total)) {
			throw pfc::exception_overflow();
		}

		out.set_size(total + got);
		memcpy(static_cast<t_uint8 *>(out.get_ptr()) + total, buffer.get_ptr(),
			   got);
		total += got;
	}
}

void read_all(const response &http_response, mem_block_container &out,
			  abort_callback &abort, size_t chunk_size, size_t max_bytes) {
	read_all(http_response.stream, out, abort, chunk_size, max_bytes);
}

pfc::string8 read_text(file::ptr stream, abort_callback &abort,
					   size_t max_bytes) {
	mem_block_container_impl data;
	read_all(stream, data, abort, k_default_chunk_size, max_bytes);

	pfc::string8 text;
	if (data.get_size() > 0) {
		text.add_string(reinterpret_cast<const char *>(data.get_ptr()),
						data.get_size());
	}
	return text;
}

pfc::string8 read_text(const response &http_response, abort_callback &abort,
					   size_t max_bytes) {
	return read_text(http_response.stream, abort, max_bytes);
}

bool try_get_header(const response &http_response, const char *name,
					pfc::string_base &out_value) {
	out_value.reset();
	if (!http_response.reply.is_valid() || name == nullptr || *name == '\0') {
		return false;
	}
	return http_response.reply->get_http_header(name, out_value);
}

bool status_is_success(const response &http_response) noexcept {
	const int code = parse_status_code(http_response.status_text);
	return code >= 200 && code < 300;
}

} // namespace subsonic::http