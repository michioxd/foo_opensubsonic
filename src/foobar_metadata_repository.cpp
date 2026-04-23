#include "stdafx.h"

#include "foobar_metadata_repository.h"
#include "metadata.h"

// foobar_metadata_repository wraps existing metadata.h functions with IMetadataRepository interface
//
// WRAPPING VERIFICATION:
// This implementation is a 1:1 wrapper of existing metadata.h functions
// No new logic - just delegates to global metadata cache
// See metadata.h/cpp for original implementation

namespace subsonic {

[[nodiscard]] std::optional<cached_track_metadata>
foobar_metadata_repository::try_get(const char *track_id) {
	cached_track_metadata result;
	if (metadata::try_get_track_metadata(track_id, result)) {
		return result;
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<cached_track_metadata>
foobar_metadata_repository::try_get_by_path(const char *path) {
	cached_track_metadata result;
	if (metadata::try_get_track_metadata_for_path(path, result)) {
		return result;
	}
	return std::nullopt;
}

void foobar_metadata_repository::publish(const cached_track_metadata &entry) {
	metadata::publish_track_metadata(entry);
}

void foobar_metadata_repository::clear_all(threaded_process_status &status,
										   abort_callback &abort) {
	metadata::clear_all(status, abort);
}

void foobar_metadata_repository::merge(
	const std::vector<cached_track_metadata> &entries,
	threaded_process_status &status, abort_callback &abort) {
	metadata::merge_track_metadata(entries, status, abort);
}

void foobar_metadata_repository::replace(
	const std::vector<cached_track_metadata> &entries,
	threaded_process_status &status, abort_callback &abort) {
	metadata::replace_track_metadata(entries, status, abort);
}

void foobar_metadata_repository::remove(const char *track_id) {
	metadata::remove_track_metadata(track_id);
}

void foobar_metadata_repository::refresh_track(const char *track_id) {
	metadata::refresh_track(track_id);
}

} // namespace subsonic
