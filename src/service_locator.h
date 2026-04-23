#pragma once

#include "http/http_client_interface.h"
#include "metadata_repository_interface.h"

#include <atomic>

namespace subsonic {

// Service Locator for dependency injection in foobar2000 plugin context
//
// Why Service Locator instead of pure DI?
// - foobar2000 SDK instantiates VFS/input components via service factory
// - Cannot pass constructor parameters to these components
// - Service Locator is acceptable pattern for plugin architectures
//
// Usage:
//   // At plugin initialization:
//   service_locator::initialize(http_client, metadata_repo);
//
//   // In VFS/input components:
//   auto& http = service_locator::http_client();
//   auto& metadata = service_locator::metadata_repository();
//
// Thread-safety: Atomic operations ensure consistent snapshot across threads
class service_locator {
  public:
	// Initialize service locator with concrete implementations
	// Must be called during plugin initialization (foo_opensubsonic.cpp)
	// Thread-safe: Uses atomic store with release semantics
	static void initialize(IHttpClient *http_client,
						   IMetadataRepository *metadata_repo) noexcept;

	// Shutdown service locator
	// Called during plugin shutdown
	// Thread-safe: Uses atomic exchange with release semantics
	static void shutdown() noexcept;

	// Get HTTP client instance
	// Returns: Reference to registered HTTP client
	// Throws: std::logic_error if not initialized
	// Thread-safe: Uses atomic load with acquire semantics
	[[nodiscard]] static IHttpClient &http_client();

	// Get metadata repository instance
	// Returns: Reference to registered metadata repository
	// Throws: std::logic_error if not initialized
	// Thread-safe: Uses atomic load with acquire semantics
	[[nodiscard]] static IMetadataRepository &metadata_repository();

	// Check if services are initialized
	// Thread-safe: Uses atomic load with acquire semantics
	[[nodiscard]] static bool is_initialized() noexcept;

  private:
	service_locator() = delete;
	~service_locator() = delete;

	// Immutable service bundle for atomic snapshot consistency
	struct ServiceBundle {
		IHttpClient *http;
		IMetadataRepository *metadata;

		ServiceBundle(IHttpClient *h, IMetadataRepository *m) noexcept
			: http(h), metadata(m) {}
	};

	// Single atomic pointer ensures consistent snapshot
	// Eliminates TOCTOU race between http_client/metadata_repository
	static std::atomic<ServiceBundle *> s_services;
};

} // namespace subsonic
