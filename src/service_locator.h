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
// - initialize() receives shared ownership of the service objects
// - ServiceBundle stores shared_ptrs to the services, not borrowed raw pointers
// - Repeated initialize() is safe - old bundle and services remain valid until
//   all shared_ptr snapshots are dropped
// - shutdown() atomically resets the active bundle first; service objects are
//   destroyed only after all remaining shared_ptr copies are released
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
	static void
	initialize(std::shared_ptr<IHttpClient> http_client,
			   std::shared_ptr<IMetadataRepository> metadata_repo) noexcept;

	// Shutdown service locator
	// Called during plugin shutdown
	//
	// Safety: Atomically resets the active shared_ptr bundle before component
	// owned shared_ptrs are reset. Threads holding service shared_ptr snapshots
	// keep the old services alive until those snapshots are released.
	//
	// Thread-safe: Uses atomic store with release semantics
	static void shutdown() noexcept;

	// Get HTTP client instance
	// Returns: Reference to registered HTTP client
	// Throws: std::logic_error if not initialized
	// Thread-safe: Uses atomic load with acquire semantics
	// Lifetime: Safe for short scoped use only. Do not store the returned
	// reference long-term; use http_client_ptr() when retaining the service.
	[[nodiscard]] static IHttpClient &http_client();

	// Get HTTP client shared_ptr snapshot
	// Returns: Shared ownership of registered HTTP client
	// Throws: std::logic_error if not initialized
	// Thread-safe: Uses atomic load with acquire semantics
	[[nodiscard]] static std::shared_ptr<IHttpClient> http_client_ptr();

	// Get metadata repository instance
	// Returns: Reference to registered metadata repository
	// Throws: std::logic_error if not initialized
	// Thread-safe: Uses atomic load with acquire semantics
	// Lifetime: Safe for short scoped use only. Do not store the returned
	// reference long-term; use metadata_repository_ptr() when retaining the
	// service.
	[[nodiscard]] static IMetadataRepository &metadata_repository();

	// Get metadata repository shared_ptr snapshot
	// Returns: Shared ownership of registered metadata repository
	// Throws: std::logic_error if not initialized
	// Thread-safe: Uses atomic load with acquire semantics
	[[nodiscard]] static std::shared_ptr<IMetadataRepository>
	metadata_repository_ptr();

	// Check if services are initialized
	// Thread-safe: Uses atomic load with acquire semantics
	[[nodiscard]] static bool is_initialized() noexcept;

  private:
	service_locator() = delete;
	~service_locator() = delete;

	// Immutable service bundle for atomic snapshot consistency
	// Stored in shared_ptr for safe lifetime management of both the bundle and
	// service objects:
	// - Atomic loads copy the bundle shared_ptr (ref count++)
	// - Bundle owns service shared_ptrs, so services remain alive with
	// snapshots
	// - shutdown() resets the active bundle, but outstanding copies remain
	// valid
	struct ServiceBundle {
		std::shared_ptr<IHttpClient> http;
		std::shared_ptr<IMetadataRepository> metadata;

		ServiceBundle(std::shared_ptr<IHttpClient> h,
					  std::shared_ptr<IMetadataRepository> m) noexcept
			: http(std::move(h)), metadata(std::move(m)) {}
	};

	// Atomic shared_ptr ensures snapshot consistency and safe bundle lifetime
	// - Atomic load creates new shared_ptr copy (thread-safe ref count)
	// - Outstanding copies keep bundle and service shared_ptrs alive during
	// shutdown
	// - Eliminates TOCTOU race between http_client/metadata_repository
	static std::atomic<std::shared_ptr<ServiceBundle>> s_services;
};

} // namespace subsonic
