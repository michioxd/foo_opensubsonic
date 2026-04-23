#include "stdafx.h"

#include "service_locator.h"

#include <stdexcept>
#include <memory>

namespace subsonic {

// Static member initialization
// shared_ptr provides automatic lifetime management and thread-safe ref counting
std::atomic<std::shared_ptr<service_locator::ServiceBundle>>
	service_locator::s_services{nullptr};

void service_locator::initialize(IHttpClient *http_client,
								  IMetadataRepository *metadata_repo) noexcept {
	// Create new immutable bundle with both services wrapped in shared_ptr
	auto bundle =
		std::make_shared<ServiceBundle>(http_client, metadata_repo);

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
	// Atomic load creates a shared_ptr copy with acquire semantics
	// This copy keeps the bundle alive for the duration of this function
	// Even if shutdown() is called concurrently, our copy remains valid
	auto bundle = s_services.load(std::memory_order_acquire);

	if (!bundle || bundle->http == nullptr) {
		throw std::logic_error(
			"service_locator::http_client() called before initialize()");
	}

	// Return reference backed by shared_ptr - safe because bundle stays alive
	// until end of this scope (caller must not store the reference long-term)
	return *bundle->http;
}

[[nodiscard]] IMetadataRepository &service_locator::metadata_repository() {
	// Atomic load creates a shared_ptr copy with acquire semantics
	// Keeps bundle alive even if shutdown() is called concurrently
	auto bundle = s_services.load(std::memory_order_acquire);

	if (!bundle || bundle->metadata == nullptr) {
		throw std::logic_error("service_locator::metadata_repository() called "
							   "before initialize()");
	}

	// Return reference backed by shared_ptr copy
	return *bundle->metadata;
}

[[nodiscard]] bool service_locator::is_initialized() noexcept {
	// Atomic load with acquire semantics
	// shared_ptr copy goes out of scope immediately (RAII cleanup)
	auto bundle = s_services.load(std::memory_order_acquire);

	// Single atomic load gives consistent snapshot of both pointers
	return bundle && bundle->http != nullptr && bundle->metadata != nullptr;
}

} // namespace subsonic
