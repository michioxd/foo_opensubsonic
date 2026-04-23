#include "stdafx.h"

#include "service_locator.h"

#include <stdexcept>

namespace subsonic {

// Static member initialization
std::atomic<service_locator::ServiceBundle *> service_locator::s_services{
	nullptr};

void service_locator::initialize(IHttpClient *http_client,
								  IMetadataRepository *metadata_repo) noexcept {
	// Create new immutable bundle with both services
	auto *bundle = new (std::nothrow) ServiceBundle(http_client, metadata_repo);

	// Store with release semantics - ensures all writes to bundle are visible
	// to other threads that load with acquire semantics
	s_services.store(bundle, std::memory_order_release);
}

void service_locator::shutdown() noexcept {
	// Atomically exchange with nullptr and get old value
	// Release semantics ensures all operations on bundle are complete
	auto *old_bundle = s_services.exchange(nullptr, std::memory_order_release);

	// Delete the old bundle (safe because no other thread can see it now)
	delete old_bundle;
}

[[nodiscard]] IHttpClient &service_locator::http_client() {
	// Load with acquire semantics - synchronizes with store/release in
	// initialize() Ensures we see all writes to the bundle
	auto *bundle = s_services.load(std::memory_order_acquire);

	if (bundle == nullptr || bundle->http == nullptr) {
		throw std::logic_error(
			"service_locator::http_client() called before initialize()");
	}

	return *bundle->http;
}

[[nodiscard]] IMetadataRepository &service_locator::metadata_repository() {
	// Load with acquire semantics - synchronizes with store/release in
	// initialize()
	auto *bundle = s_services.load(std::memory_order_acquire);

	if (bundle == nullptr || bundle->metadata == nullptr) {
		throw std::logic_error("service_locator::metadata_repository() called "
							   "before initialize()");
	}

	return *bundle->metadata;
}

[[nodiscard]] bool service_locator::is_initialized() noexcept {
	// Load with acquire semantics to see consistent state
	auto *bundle = s_services.load(std::memory_order_acquire);

	// Single atomic load gives consistent snapshot of both pointers
	return bundle != nullptr && bundle->http != nullptr &&
		   bundle->metadata != nullptr;
}

} // namespace subsonic
