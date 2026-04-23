#pragma once

#include "types.h"

#include <SDK/threaded_process.h>
#include <SDK/abort_callback.h>

#include <vector>
#include <optional>

namespace subsonic {

// Interface for metadata storage - enables dependency injection and testing
// Abstracts the underlying storage mechanism (configStore, SQLite, memory, etc.)
//
// Thread-safety: Implementations must be thread-safe for concurrent reads
// All operations are synchronous (for async, wrap in threaded_process)
class IMetadataRepository {
  public:
	virtual ~IMetadataRepository() = default;

	// Try to get cached track metadata by track_id
	// Returns std::nullopt if track_id not found
	[[nodiscard]] virtual std::optional<cached_track_metadata>
	try_get(const char *track_id) = 0;

	// Try to get cached track metadata by virtual path (subsonic://track/{id})
	// Returns std::nullopt if path invalid or track_id not found
	[[nodiscard]] virtual std::optional<cached_track_metadata>
	try_get_by_path(const char *path) = 0;

	// Publish single metadata entry to cache
	// Thread-safe: can be called from any thread
	virtual void publish(const cached_track_metadata &entry) = 0;

	// Clear all cached metadata
	// Requires: UI callbacks for progress reporting
	virtual void clear_all(threaded_process_status &status,
						   abort_callback &abort) = 0;

	// Merge new metadata entries into cache, deduplicating by track_id
	// Existing entries are preserved and merged with new data
	virtual void merge(const std::vector<cached_track_metadata> &entries,
					   threaded_process_status &status,
					   abort_callback &abort) = 0;

	// Replace entire metadata cache with new entries
	// Discards all existing cached metadata
	virtual void replace(const std::vector<cached_track_metadata> &entries,
						 threaded_process_status &status,
						 abort_callback &abort) = 0;

	// Remove single track metadata from cache
	virtual void remove(const char *track_id) = 0;

	// Trigger metadata refresh for a specific track
	// Notifies foobar2000 to reload metadata from cache
	virtual void refresh_track(const char *track_id) = 0;
};

} // namespace subsonic
