#pragma once

#include "http/http_client_interface.h"
#include "metadata_repository_interface.h"

#include <atomic>
#include <memory>

namespace subsonic {

// Service Locator for dependency injection in foobar2000 plugin context
//
// Why Service Locator instead of pure DI?
// - foobar2000 SDK instantiates VFS/input components via service factory
// - Cannot pass constructor parameters to these components
// - Service Locator is acceptable pattern for plugin architectures
//
// Ownership Contract:
// - initialize() creates shared_ptr<ServiceBundle> and atomically replaces old one
// - Repeated initialize() is safe - old bundle remains valid until all refs dropped
// - shutdown() atomically resets shared_ptr - accessors holding refs remain valid
// - Accessor methods return references backed by thread-local shared_ptr copies
//
// Thread-Safety Guarantees:
// - All operations are lock-free and wait-free
// - Outstanding accessors remain valid during shutdown (shared ownership)
// - No use-after-free or dangling references possible
//
// Usage:
//   // At plugin initialization:
//   service_locator::initialize(http_client, metadata_repo);
//
//   // In VFS/input components:
//   auto& http = service_locator::http_client();
//   auto& metadata = service_locator::metadata_repository();
//
//   // At plugin shutdown (safe even if accessors are active):
//   service_locator::shutdown();
//
class service_locator {
  public:
	// Initialize service locator with concrete implementations
	// Must be called during plugin initialization (foo_opensubsonic.cpp)
	//
	// Re-initialization: Safe to call multiple times. Old bundle remains
	// valid for threads currently using it (shared ownership). New calls
	// will see the new bundle.
	//
	// Thread-safe: Uses atomic store with release semantics
	static void initialize(IHttpClient *http_client,
						   IMetadataRepository *metadata_repo) noexcept;

	// Shutdown service locator
	// Called during plugin shutdown
	//
	// Safety: Atomically resets shared_ptr. Threads currently holding
	// references (via http_client/metadata_repository) will keep their
	// bundle alive until they release it. No use-after-free possible.
	//
	// Thread-safe: Uses atomic store with release semantics
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
	// Stored in shared_ptr for safe lifetime management:
	// - Accessors copy shared_ptr (ref count++)
	// - shutdown() resets atomic, but copies remain valid
	// - No use-after-free or manual delete needed
	struct ServiceBundle {
		IHttpClient *http;
		IMetadataRepository *metadata;

		ServiceBundle(IHttpClient *h, IMetadataRepository *m) noexcept
			: http(h), metadata(m) {}
	};

	// Atomic shared_ptr ensures both snapshot consistency AND safe lifetime
	// - Atomic load creates new shared_ptr copy (thread-safe ref count)
	// - Outstanding copies keep bundle alive during shutdown
	// - Eliminates TOCTOU race between http_client/metadata_repository
	static std::atomic<std::shared_ptr<ServiceBundle>> s_services;
};

} // namespace subsonic
