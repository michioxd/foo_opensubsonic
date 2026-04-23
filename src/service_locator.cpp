#include "stdafx.h"

#include "service_locator.h"

#include <stdexcept>

namespace subsonic {

// Static member initialization
IHttpClient *service_locator::s_http_client = nullptr;
IMetadataRepository *service_locator::s_metadata_repo = nullptr;

void service_locator::initialize(IHttpClient *http_client,
								  IMetadataRepository *metadata_repo) noexcept {
	s_http_client = http_client;
	s_metadata_repo = metadata_repo;
}

void service_locator::shutdown() noexcept {
	s_http_client = nullptr;
	s_metadata_repo = nullptr;
}

[[nodiscard]] IHttpClient &service_locator::http_client() {
	if (s_http_client == nullptr) {
		throw std::logic_error(
			"service_locator::http_client() called before initialize()");
	}
	return *s_http_client;
}

[[nodiscard]] IMetadataRepository &service_locator::metadata_repository() {
	if (s_metadata_repo == nullptr) {
		throw std::logic_error("service_locator::metadata_repository() called "
							   "before initialize()");
	}
	return *s_metadata_repo;
}

[[nodiscard]] bool service_locator::is_initialized() noexcept {
	return s_http_client != nullptr && s_metadata_repo != nullptr;
}

} // namespace subsonic
