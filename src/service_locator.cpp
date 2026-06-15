#include "stdafx.h"

#include "service_locator.h"

#include <memory>
#include <stdexcept>

namespace subsonic {

// Static member initialization
// shared_ptr provides automatic lifetime management and thread-safe ref
// counting
std::atomic<std::shared_ptr<service_locator::ServiceBundle>>
	service_locator::s_services{nullptr};

void service_locator::initialize(
	std::shared_ptr<IHttpClient> http_client,
	std::shared_ptr<IMetadataRepository> metadata_repo) noexcept {
	// Create new immutable bundle with shared ownership of both services
	auto bundle = std::make_shared<ServiceBundle>(std::move(http_client),
												  std::move(metadata_repo));

	// Atomically store new shared_ptr with release semantics
	// Old bundle (if any) is automatically destroyed when last reference drops
	// Thread-safe: Any thread holding a copy keeps the old bundle alive
	s_services.store(bundle, std::memory_order_release);
}

void service_locator::shutdown() noexcept {
	// Atomically reset shared_ptr with release semantics
	// Outstanding copies in accessor threads keep bundle alive until released
	// No manual delete needed - automatic when ref count reaches zero
	s_services.store(nullptr, std::memory_order_release);
}

[[nodiscard]] IHttpClient &service_locator::http_client() {
	// Keep a per-thread shared_ptr snapshot so the returned reference remains
	// valid after this function returns. Callers must still avoid storing the
	// reference long-term; use http_client_ptr() for explicit ownership.
	thread_local std::shared_ptr<IHttpClient> service_snapshot;
	service_snapshot = http_client_ptr();
	return *service_snapshot;
}

[[nodiscard]] std::shared_ptr<IHttpClient> service_locator::http_client_ptr() {
	// Atomic load creates a shared_ptr copy with acquire semantics
	// This copy keeps the bundle and service shared_ptrs alive while we
	// take a service shared_ptr snapshot.
	auto bundle = s_services.load(std::memory_order_acquire);

	if (!bundle || !bundle->http) {
		throw std::logic_error(
			"service_locator::http_client() called before initialize()");
	}

	return bundle->http;
}

[[nodiscard]] IMetadataRepository &service_locator::metadata_repository() {
	// Keep a per-thread shared_ptr snapshot so the returned reference remains
	// valid after this function returns. Callers must still avoid storing the
	// reference long-term; use metadata_repository_ptr() for explicit
	// ownership.
	thread_local std::shared_ptr<IMetadataRepository> service_snapshot;
	service_snapshot = metadata_repository_ptr();
	return *service_snapshot;
}

[[nodiscard]] std::shared_ptr<IMetadataRepository>
service_locator::metadata_repository_ptr() {
	// Atomic load creates a shared_ptr copy with acquire semantics
	// Keeps bundle and service shared_ptrs alive while we take a service
	// snapshot.
	auto bundle = s_services.load(std::memory_order_acquire);

	if (!bundle || !bundle->metadata) {
		throw std::logic_error("service_locator::metadata_repository() called "
							   "before initialize()");
	}

	return bundle->metadata;
}

[[nodiscard]] bool service_locator::is_initialized() noexcept {
	// Atomic load with acquire semantics
	// shared_ptr copy goes out of scope immediately (RAII cleanup)
	auto bundle = s_services.load(std::memory_order_acquire);

	// Single atomic load gives consistent snapshot of both pointers
	return bundle && bundle->http && bundle->metadata;
}

} // namespace subsonic
