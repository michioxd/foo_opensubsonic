#pragma once

#include "metadata_repository_interface.h"

namespace subsonic {

// Foobar2000 metadata repository implementation
// Wraps existing metadata.h functions with IMetadataRepository interface
// Storage: Uses foobar2000 configStore for persistence + in-memory cache
class foobar_metadata_repository final : public IMetadataRepository {
  public:
	foobar_metadata_repository() = default;
	~foobar_metadata_repository() override = default;

	// Try to get by track_id - implements IMetadataRepository
	[[nodiscard]] std::optional<cached_track_metadata>
	try_get(const char *track_id) override;

	// Try to get by path - implements IMetadataRepository
	[[nodiscard]] std::optional<cached_track_metadata>
	try_get_by_path(const char *path) override;

	// Publish single entry - implements IMetadataRepository
	void publish(const cached_track_metadata &entry) override;

	// Clear all - implements IMetadataRepository
	void clear_all(threaded_process_status &status,
				   abort_callback &abort) override;

	// Merge entries - implements IMetadataRepository
	void merge(const std::vector<cached_track_metadata> &entries,
			   threaded_process_status &status,
			   abort_callback &abort) override;

	// Replace all - implements IMetadataRepository
	void replace(const std::vector<cached_track_metadata> &entries,
				 threaded_process_status &status,
				 abort_callback &abort) override;

	// Remove single entry - implements IMetadataRepository
	void remove(const char *track_id) override;

	// Refresh track - implements IMetadataRepository
	void refresh_track(const char *track_id) override;
};

} // namespace subsonic
