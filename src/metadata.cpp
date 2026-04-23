#include "stdafx.h"

#include "cache.h"
#include "metadata.h"
#include "utils/metadata_utils.h"

#include "utils/utils.h"

#include <SDK/core_api.h>
#include <SDK/initquit.h>
#include <SDK/metadb.h>
#include <SDK/threaded_process.h>
#include <SDK/threadsLite.h>
#include <SDK/titleformat.h>

#include <unordered_set>

namespace {

enum metadata_field_index : t_uint32 {
	field_track_id = 0,
	field_artist,
	field_title,
	field_album,
	field_cover_art_id,
	field_stream_mime_type,
	field_suffix,
	field_duration,
	field_count,
};

constexpr std::array<const char *, field_count> k_field_names = {
	"foo_opensubsonic_track_id",	 "foo_opensubsonic_artist",
	"foo_opensubsonic_title",		 "foo_opensubsonic_album",
	"foo_opensubsonic_cover_art_id", "foo_opensubsonic_stream_mime_type",
	"foo_opensubsonic_suffix",		 "foo_opensubsonic_duration"};

using track_metadata_map =
	std::unordered_map<std::string, subsonic::cached_track_metadata>;

std::shared_mutex g_track_metadata_mutex;
track_metadata_map g_track_metadata;

[[nodiscard]] std::string make_track_key(const char *track_id) {
	return track_id != nullptr ? std::string(track_id) : std::string();
}

void populate_file_info(const subsonic::cached_track_metadata &entry,
						file_info_impl &info) {
	info = file_info_impl();

	if (entry.duration_seconds > 0)
		info.set_length(entry.duration_seconds);
	if (!entry.artist.is_empty())
		info.meta_set("artist", entry.artist);
	if (!entry.title.is_empty())
		info.meta_set("title", entry.title);
	if (!entry.album.is_empty())
		info.meta_set("album", entry.album);
	if (!entry.track_number.is_empty())
		info.meta_set("tracknumber", entry.track_number);
	if (!entry.disc_number.is_empty())
		info.meta_set("discnumber", entry.disc_number);
	if (!entry.year.is_empty())
		info.meta_set("date", entry.year);
	if (!entry.genre.is_empty())
		info.meta_set("genre", entry.genre);

	if (!info.meta_exists("date")) {
		const auto inferred_year =
			subsonic::metadata_utils::try_extract_year_from_created(entry);
		if (!inferred_year.is_empty()) {
			info.meta_set("date", inferred_year);
		}
	}

	if (!info.meta_exists("album artist")) {
		pfc::string8 album_artist;
		if ((subsonic::metadata_utils::try_get_first_extra_field(
				 entry, {"displayAlbumArtist", "albumArtist", "albumartist"},
				 album_artist) ||
			 subsonic::metadata_utils::try_join_named_array_extra_field(
				 entry, {"albumArtists", "albumartists"}, album_artist)) &&
			!album_artist.is_empty()) {
			info.meta_set("album artist", album_artist);
		}
	}
	subsonic::metadata_utils::try_set_meta_from_extra(
		info, entry, "composer", {"composer", "displayComposer"});
	subsonic::metadata_utils::try_set_meta_from_extra(info, entry, "lyricist",
													  {"lyricist"});
	subsonic::metadata_utils::try_set_meta_from_extra(info, entry, "conductor",
													  {"conductor"});
	subsonic::metadata_utils::try_set_meta_from_extra(
		info, entry, "performer", {"performer", "performers"});
	subsonic::metadata_utils::try_set_meta_from_extra(info, entry, "comment",
													  {"comment"});
	subsonic::metadata_utils::try_set_meta_from_extra(info, entry, "lyrics",
													  {"lyrics"});
	subsonic::metadata_utils::try_set_meta_from_extra(info, entry, "publisher",
													  {"publisher", "label"});
	subsonic::metadata_utils::try_set_meta_from_extra(info, entry, "label",
													  {"label"});
	subsonic::metadata_utils::try_set_meta_from_extra(info, entry, "copyright",
													  {"copyright"});
	subsonic::metadata_utils::try_set_meta_from_extra(info, entry, "isrc",
													  {"isrc"});
	subsonic::metadata_utils::try_set_meta_from_extra(
		info, entry, "catalognumber", {"catalogNumber", "catalog_number"});
	subsonic::metadata_utils::try_set_meta_from_extra(info, entry, "barcode",
													  {"barcode"});
	subsonic::metadata_utils::try_set_meta_from_extra(info, entry, "asin",
													  {"asin"});
	subsonic::metadata_utils::try_set_meta_from_extra(info, entry, "language",
													  {"language"});
	subsonic::metadata_utils::try_set_meta_from_extra(info, entry, "bpm",
													  {"bpm"});
	subsonic::metadata_utils::try_set_meta_from_extra(info, entry, "grouping",
													  {"grouping", "work"});
	subsonic::metadata_utils::try_set_meta_from_extra(info, entry, "subtitle",
													  {"subtitle"});
	subsonic::metadata_utils::try_set_meta_from_extra(
		info, entry, "discsubtitle", {"discSubtitle", "discsubtitle"});
	subsonic::metadata_utils::try_set_meta_from_extra(info, entry, "remixer",
													  {"remixer", "mixArtist"});
	subsonic::metadata_utils::try_set_meta_from_extra(info, entry, "producer",
													  {"producer"});
	subsonic::metadata_utils::try_set_meta_from_extra(info, entry, "engineer",
													  {"engineer"});
	subsonic::metadata_utils::try_set_meta_from_extra(info, entry, "arranger",
													  {"arranger"});
	subsonic::metadata_utils::try_set_meta_from_extra(
		info, entry, "compilation", {"compilation", "isCompilation"});
	subsonic::metadata_utils::try_set_meta_from_extra(
		info, entry, "totaltracks", {"songCount", "trackCount", "totalTracks"});
	subsonic::metadata_utils::try_set_meta_from_extra(
		info, entry, "totaldiscs", {"discCount", "totalDiscs"});
	subsonic::metadata_utils::try_set_meta_from_extra(
		info, entry, "musicbrainz_trackid",
		{"musicBrainzId", "musicBrainzTrackId"});
	subsonic::metadata_utils::try_set_meta_from_extra(
		info, entry, "musicbrainz_albumid",
		{"musicBrainzAlbumId", "albumMusicBrainzId"});
	subsonic::metadata_utils::try_set_meta_from_extra(
		info, entry, "musicbrainz_artistid",
		{"musicBrainzArtistId", "artistMusicBrainzId"});
	subsonic::metadata_utils::try_set_meta_from_extra(
		info, entry, "musicbrainz_albumartistid", {"musicBrainzAlbumArtistId"});
	subsonic::metadata_utils::try_set_meta_from_extra(
		info, entry, "original artist", {"originalArtist"});
	subsonic::metadata_utils::try_set_meta_from_extra(
		info, entry, "original album", {"originalAlbum"});
	subsonic::metadata_utils::try_set_meta_from_extra(
		info, entry, "originaldate", {"originalDate", "originalYear"});

	const auto codec = subsonic::metadata_utils::guess_codec(entry);
	if (!codec.is_empty()) {
		info.info_set("codec", codec);
	}

	if (!entry.bitrate.is_empty())
		info.info_set("bitrate", entry.bitrate);
	if (!entry.stream_mime_type.is_empty())
		info.info_set("mime_type", entry.stream_mime_type);
	if (!entry.cover_art_id.is_empty())
		info.info_set("subsonic_cover_art_id", entry.cover_art_id);
	if (!entry.track_id.is_empty())
		info.info_set("subsonic_track_id", entry.track_id);

	subsonic::metadata_utils::try_set_info_from_extra(
		info, entry, "encoding", {"suffix", "transcodedSuffix"});
	subsonic::metadata_utils::try_set_info_from_extra(
		info, entry, "samplerate", {"samplingRate", "sampleRate"});
	subsonic::metadata_utils::try_set_info_from_extra(
		info, entry, "channels", {"channelCount", "channels"});
	subsonic::metadata_utils::try_set_info_from_extra(
		info, entry, "bitspersample", {"bitsPerSample", "bitDepth"});
	subsonic::metadata_utils::try_set_info_from_extra(
		info, entry, "filesize", {"size", "contentLength"});
	subsonic::metadata_utils::try_set_info_from_extra(
		info, entry, "subsonic_album_id", {"albumId"});
	subsonic::metadata_utils::try_set_info_from_extra(
		info, entry, "subsonic_artist_id", {"artistId"});
	subsonic::metadata_utils::try_set_info_from_extra(
		info, entry, "subsonic_parent_id", {"parent"});
	subsonic::metadata_utils::try_set_info_from_extra(
		info, entry, "subsonic_media_type", {"type"});
	subsonic::metadata_utils::try_set_info_from_extra(
		info, entry, "subsonic_created", {"created"});
	subsonic::metadata_utils::try_set_info_from_extra(
		info, entry, "subsonic_path", {"path"});
	subsonic::metadata_utils::try_set_info_from_extra(
		info, entry, "subsonic_play_count", {"playCount"});
	subsonic::metadata_utils::try_set_info_from_extra(
		info, entry, "subsonic_user_rating", {"userRating"});
	subsonic::metadata_utils::try_set_info_from_extra(
		info, entry, "subsonic_average_rating", {"averageRating"});
	subsonic::metadata_utils::try_set_info_from_extra(
		info, entry, "subsonic_starred", {"starred"});
	subsonic::metadata_utils::try_set_info_from_extra(
		info, entry, "subsonic_is_video", {"isVideo"});
	subsonic::metadata_utils::try_set_info_from_extra(
		info, entry, "subsonic_explicit_status", {"explicitStatus"});

	float replaygain_value = 0.0f;
	if (subsonic::metadata_utils::try_get_replaygain_value(
			entry,
			{"replaygain_track_gain", "replayGainTrackGain",
			 "replaygainTrackGain", "trackGain"},
			"trackGain", replaygain_value)) {
		info.info_set_replaygain_track_gain(replaygain_value);
	}
	if (subsonic::metadata_utils::try_get_replaygain_value(
			entry,
			{"replaygain_album_gain", "replayGainAlbumGain",
			 "replaygainAlbumGain", "albumGain"},
			"albumGain", replaygain_value)) {
		info.info_set_replaygain_album_gain(replaygain_value);
	}
	if (subsonic::metadata_utils::try_get_replaygain_value(
			entry,
			{"replaygain_track_peak", "replayGainTrackPeak",
			 "replaygainTrackPeak", "trackPeak"},
			"trackPeak", replaygain_value)) {
		info.info_set_replaygain_track_peak(replaygain_value);
	}
	if (subsonic::metadata_utils::try_get_replaygain_value(
			entry,
			{"replaygain_album_peak", "replayGainAlbumPeak",
			 "replaygainAlbumPeak", "albumPeak"},
			"albumPeak", replaygain_value)) {
		info.info_set_replaygain_album_peak(replaygain_value);
	}

	const auto replaygain = info.get_replaygain();
	replaygain.for_each([&info](const char *name, const char *value) {
		if (name != nullptr && value != nullptr && *value != '\0') {
			info.meta_set(name, value);
		}
	});

	for (const auto &field : entry.extra_fields) {
		if (!field.is_valid() || field.value.is_empty()) {
			continue;
		}

		const auto key =
			subsonic::metadata_utils::make_extra_info_key(field.key);
		info.info_set(key, field.value);
	}
}

[[nodiscard]] metadb_handle_ptr make_handle_for_track(const char *track_id) {
	if (track_id == nullptr || *track_id == '\0') {
		return {};
	}

	const pfc::string8 path = subsonic::make_subsonic_path(track_id);
	return static_api_ptr_t<metadb>()->handle_create(path, 0);
}

void dispatch_refresh_for_handles(metadb_handle_list_cref handles) {
	if (handles.get_count() == 0) {
		return;
	}

	metadb_handle_list handles_copy = handles;
	fb2k::inMainThread([handles_copy] {
		static_api_ptr_t<metadb_io>()->dispatch_refresh(handles_copy);
	});
}

[[nodiscard]] service_ptr_t<metadb_hint_list_v3>
try_get_hint_list_v3(const metadb_hint_list::ptr &hint_list) {
	service_ptr_t<metadb_hint_list_v3> hint_list_v3;
	if (hint_list.is_valid()) {
		hint_list->service_query_t(hint_list_v3);
	}
	return hint_list_v3;
}

void add_hint_to_list(metadb_hint_list &hint_list,
					  metadb_hint_list_v3 *hint_list_v3,
					  const metadb_handle_ptr &handle,
					  const file_info_impl &info, const t_filestats &stats) {
	if (hint_list_v3 != nullptr) {
		hint_list_v3->add_hint_forced(handle, info, stats, true);
	} else {
		hint_list.add_hint(handle, info, stats, true);
	}
}

void finalize_hint_list(metadb_hint_list::ptr hint_list,
						metadb_handle_list handles) {
	handles.sort_by_pointer_remove_duplicates();
	fb2k::inMainThreadSynchronous2([hint_list, handles] {
		if (hint_list.is_valid()) {
			hint_list->on_done();
		}

		if (handles.get_count() > 0) {
			static_api_ptr_t<metadb_io>()->dispatch_refresh(handles);
		}
	});
}

void remove_info_for_handles(metadb_handle_list handles,
							 threaded_process_status &status,
							 abort_callback &abort) {
	handles.sort_by_pointer_remove_duplicates();
	if (handles.get_count() == 0) {
		return;
	}

	service_ptr_t<threaded_process_callback> remove_task;
	fb2k::inMainThreadSynchronous2([&] {
		service_ptr_t<metadb_io_v4> api_v4;
		static_api_ptr_t<metadb_io>()->service_query_t(api_v4);
		if (api_v4.is_valid()) {
			remove_task = api_v4->spawn_remove_info(
				handles,
				metadb_io_v2::op_flag_silent | metadb_io_v2::op_flag_no_errors,
				nullptr);
		}

		if (remove_task.is_valid()) {
			remove_task->on_init(threaded_process_context::g_default());
		}
	});

	if (remove_task.is_valid()) {
		try {
			status.set_item("Removing OpenSubsonic metadata from database...");
			remove_task->run(status, abort);
			fb2k::inMainThreadSynchronous2([&] {
				remove_task->on_done(threaded_process_context::g_default(),
									 false);
			});
		} catch (...) {
			fb2k::inMainThreadSynchronous2([&] {
				remove_task->on_done(threaded_process_context::g_default(),
									 std::current_exception() != nullptr);
			});
			throw;
		}
		return;
	}

	fb2k::inMainThreadSynchronous2([handles] {
		auto api_v2 = metadb_io_v2::get();
		api_v2->remove_info_async(handles, core_api::get_main_window(),
								  metadb_io_v2::op_flag_silent |
									  metadb_io_v2::op_flag_no_errors,
								  nullptr);
	});
}

void dispatch_refresh_for_track_ids(
	const std::vector<subsonic::cached_track_metadata> &entries) {
	metadb_handle_list handles;

	for (const auto &entry : entries) {
		if (!entry.is_valid()) {
			continue;
		}

		const auto handle = make_handle_for_track(entry.track_id);
		if (handle.is_valid()) {
			handles.add_item(handle);
		}
	}

	handles.sort_by_pointer_remove_duplicates();
	dispatch_refresh_for_handles(handles);
}

void hint_metadata_async(const subsonic::cached_track_metadata &entry) {
	if (!entry.is_valid()) {
		return;
	}

	const auto handle = make_handle_for_track(entry.track_id);
	if (!handle.is_valid()) {
		return;
	}

	file_info_impl info;
	populate_file_info(entry, info);

	t_filestats fake_stats;
	fake_stats.m_size = filesize_invalid;
	fake_stats.m_timestamp = 3;

	static_api_ptr_t<metadb_io>()->hint_async(handle, info, fake_stats, true);
}

// Snapshot = In-memory cache of track metadata (g_track_metadata map)
// Synchronized from server during library sync
// Provides fast lookups without hitting persistent storage

// Replace entire in-memory cache with new entries
// REQUIRES: Caller must NOT hold g_track_metadata_mutex (function acquires it)
void replace_snapshot_locked(
	const std::vector<subsonic::cached_track_metadata> &entries) {
	std::unordered_map<std::string, subsonic::cached_track_metadata> next;
	next.reserve(entries.size());

	for (const auto &entry : entries) {
		if (!entry.is_valid()) {
			continue;
		}
		next.insert_or_assign(make_track_key(entry.track_id), entry);
	}

	std::unique_lock lock(g_track_metadata_mutex);
	g_track_metadata = std::move(next);
}

// Insert or update single entry in in-memory cache
// Thread-safe: acquires g_track_metadata_mutex
void upsert_snapshot(const subsonic::cached_track_metadata &entry) {
	if (!entry.is_valid()) {
		return;
	}

	std::unique_lock lock(g_track_metadata_mutex);
	g_track_metadata.insert_or_assign(make_track_key(entry.track_id), entry);
}

// Remove single entry from in-memory cache
// Thread-safe: acquires g_track_metadata_mutex
void remove_snapshot(const char *track_id) {
	const auto key = make_track_key(track_id);
	if (key.empty()) {
		return;
	}

	std::unique_lock lock(g_track_metadata_mutex);
	g_track_metadata.erase(key);
}

// Get metadata from in-memory cache
// Thread-safe: acquires g_track_metadata_mutex (shared lock for read)
// Returns false if track_id not found
[[nodiscard]] bool try_get_snapshot(const char *track_id,
									subsonic::cached_track_metadata &out) {
	out = {};

	const auto key = make_track_key(track_id);
	if (key.empty()) {
		return false;
	}

	std::shared_lock lock(g_track_metadata_mutex);
	const auto found = g_track_metadata.find(key);
	if (found == g_track_metadata.end()) {
		return false;
	}

	out = found->second;
	return true;
}

class metadata_display_field_provider_impl
	: public metadb_display_field_provider_v2 {
  public:
	t_uint32 get_field_count() override { return field_count; }

	void get_field_name(t_uint32 index, pfc::string_base &out) override {
		PFC_ASSERT(index < get_field_count());
		out = k_field_names[index];
	}

	bool process_field(t_uint32 index, metadb_handle *handle,
					   titleformat_text_out *out) override {
		return process_field_v2(index, handle, handle->query_v2_(), out);
	}

	bool process_field_v2(t_uint32 index, metadb_handle *handle,
						  metadb_v2::rec_t const &,
						  titleformat_text_out *out) override {
		if (handle == nullptr || out == nullptr || index >= get_field_count()) {
			return false;
		}

		subsonic::cached_track_metadata entry;
		if (!subsonic::metadata::try_get_track_metadata_for_path(
				handle->get_path(), entry)) {
			return false;
		}

		switch (index) {
		case field_track_id:
			if (entry.track_id.is_empty()) {
				return false;
			}
			out->write(titleformat_inputtypes::meta, entry.track_id);
			return true;
		case field_artist:
			if (entry.artist.is_empty()) {
				return false;
			}
			out->write(titleformat_inputtypes::meta, entry.artist);
			return true;
		case field_title:
			if (entry.title.is_empty()) {
				return false;
			}
			out->write(titleformat_inputtypes::meta, entry.title);
			return true;
		case field_album:
			if (entry.album.is_empty()) {
				return false;
			}
			out->write(titleformat_inputtypes::meta, entry.album);
			return true;
		case field_cover_art_id:
			if (entry.cover_art_id.is_empty()) {
				return false;
			}
			out->write(titleformat_inputtypes::meta, entry.cover_art_id);
			return true;
		case field_stream_mime_type:
			if (entry.stream_mime_type.is_empty()) {
				return false;
			}
			out->write(titleformat_inputtypes::meta, entry.stream_mime_type);
			return true;
		case field_suffix:
			if (entry.suffix.is_empty()) {
				return false;
			}
			out->write(titleformat_inputtypes::meta, entry.suffix);
			return true;
		case field_duration:
			if (entry.duration_seconds <= 0) {
				return false;
			}
			out->write(titleformat_inputtypes::meta,
					   pfc::format_time_ex(entry.duration_seconds, 6));
			return true;
		default:
			return false;
		}
	}
};

static service_factory_single_t<metadata_display_field_provider_impl>
	g_metadata_display_field_provider_impl;

class metadata_init_stage_callback_impl : public init_stage_callback {
  public:
	void on_init_stage(t_uint32 stage) override {
		if (stage != init_stages::after_config_read) {
			return;
		}

		abort_callback_dummy abort;
		try {
			subsonic::metadata::initialize(abort);
		} catch (const std::exception &e) {
			subsonic::log_exception("metadata", e);
		} catch (...) {
			subsonic::log_error("metadata",
								"failed to initialize metadata provider cache");
		}
	}
};

static service_factory_single_t<metadata_init_stage_callback_impl>
	g_metadata_init_stage_callback_impl;

class metadata_initquit_impl : public initquit {
  public:
	void on_quit() override { subsonic::metadata::shutdown(); }
};

static service_factory_single_t<metadata_initquit_impl>
	g_metadata_initquit_impl;

} // namespace

namespace subsonic::metadata {

void initialize(abort_callback &abort) {
	cache::initialize(abort);
	const auto entries = cache::load_all_track_metadata();
	replace_snapshot_locked(entries);
	dispatch_refresh_for_track_ids(entries);
	log_info("metadata", "metadata provider initialized from local cache");
}

void shutdown() {
	std::unique_lock lock(g_track_metadata_mutex);
	g_track_metadata.clear();
}

bool try_get_track_metadata(const char *track_id, cached_track_metadata &out) {
	return try_get_snapshot(track_id, out);
}

bool try_get_track_metadata_for_path(const char *path,
									 cached_track_metadata &out) {
	pfc::string8 track_id;
	if (!extract_track_id_from_path(path, track_id)) {
		out = {};
		return false;
	}

	return try_get_snapshot(track_id, out);
}

bool try_make_file_info_for_path(const char *path, file_info_impl &out) {
	cached_track_metadata entry;
	if (!try_get_track_metadata_for_path(path, entry)) {
		out = file_info_impl();
		return false;
	}

	populate_file_info(entry, out);
	return true;
}

void publish_track_metadata(const cached_track_metadata &entry) {
	if (!entry.is_valid()) {
		throw pfc::exception_invalid_params();
	}

	cache::upsert_track_metadata(entry);
	upsert_snapshot(entry);
	hint_metadata_async(entry);
	refresh_track(entry.track_id);
}

void clear_all(threaded_process_status &status, abort_callback &abort) {
	std::vector<cached_track_metadata> entries;
	{
		std::shared_lock lock(g_track_metadata_mutex);
		entries.reserve(g_track_metadata.size());
		for (const auto &item : g_track_metadata) {
			entries.push_back(item.second);
		}
	}

	cache::replace_track_metadata({}, status, abort);

	replace_snapshot_locked({});
	metadb_handle_list handles;

	for (size_t i = 0; i < entries.size(); ++i) {
		if (i % 500 == 0) {
			abort.check();
			status.set_progress(i, entries.size());
			status.set_item(PFC_string_formatter()
							<< "Clearing OpenSubsonic metadata: " << i << " / "
							<< entries.size());
		}

		const auto handle = make_handle_for_track(entries[i].track_id);
		if (!handle.is_valid()) {
			continue;
		}

		handles.add_item(handle);
	}

	status.set_progress(entries.size(), entries.size());
	remove_info_for_handles(handles, status, abort);
}

void merge_track_metadata(const std::vector<cached_track_metadata> &entries,
						  threaded_process_status &status,
						  abort_callback &abort) {
	std::vector<cached_track_metadata> unique_entries;
	unique_entries.reserve(entries.size());
	std::unordered_map<std::string, size_t> seen;

	for (const auto &entry : entries) {
		if (!entry.is_valid())
			continue;
		const auto key = make_track_key(entry.track_id);
		if (key.empty())
			continue;

		const auto [it, inserted] = seen.emplace(key, unique_entries.size());
		if (inserted) {
			unique_entries.push_back(entry);
		} else {
			unique_entries[it->second] = entry;
		}
	}

	auto api_v2 = metadb_io_v2::get();
	auto hint_list = api_v2->create_hint_list();
	auto hint_list_v3 = try_get_hint_list_v3(hint_list);
	metadb_handle_list handles;

	for (size_t i = 0; i < unique_entries.size(); ++i) {
		if (i % 500 == 0) {
			abort.check();
			status.set_progress(i, unique_entries.size());
			status.set_item(PFC_string_formatter()
							<< "Updating playlist cache: " << i << " / "
							<< unique_entries.size());
		}
		const auto &entry = unique_entries[i];
		cache::upsert_track_metadata(entry);
		upsert_snapshot(entry);

		const auto handle = make_handle_for_track(entry.track_id);
		if (handle.is_valid()) {
			handles.add_item(handle);

			file_info_impl info;
			populate_file_info(entry, info);
			t_filestats fake_stats;
			fake_stats.m_size = filesize_invalid;
			fake_stats.m_timestamp = 3;
			add_hint_to_list(*hint_list, hint_list_v3.get_ptr(), handle, info,
							 fake_stats);
		}
	}

	status.set_item("Injecting metadata into database...");
	finalize_hint_list(hint_list, handles);
}

void replace_track_metadata(const std::vector<cached_track_metadata> &entries,
							threaded_process_status &status,
							abort_callback &abort) {
	std::vector<cached_track_metadata> previous_entries;
	{
		std::shared_lock lock(g_track_metadata_mutex);
		previous_entries.reserve(g_track_metadata.size());
		for (const auto &item : g_track_metadata) {
			previous_entries.push_back(item.second);
		}
	}

	cache::replace_track_metadata(entries, status, abort);

	status.set_item("Updating memory snapshot...");
	replace_snapshot_locked(entries);

	auto api_v2 = metadb_io_v2::get();
	auto hint_list = api_v2->create_hint_list();
	auto hint_list_v3 = try_get_hint_list_v3(hint_list);
	metadb_handle_list handles;
	metadb_handle_list stale_handles;
	std::unordered_set<std::string> next_track_ids;
	next_track_ids.reserve(entries.size());

	for (size_t i = 0; i < entries.size(); ++i) {
		if (i % 500 == 0) {
			abort.check();
			status.set_progress(i, entries.size());
			status.set_item(PFC_string_formatter()
							<< "Refreshing internal paths: " << i << " / "
							<< entries.size());
		}
		const auto &entry = entries[i];
		if (!entry.is_valid())
			continue;

		next_track_ids.emplace(make_track_key(entry.track_id));

		const auto handle = make_handle_for_track(entry.track_id);
		if (handle.is_valid()) {
			handles.add_item(handle);

			file_info_impl info;
			populate_file_info(entry, info);
			t_filestats fake_stats;
			fake_stats.m_size = filesize_invalid;
			fake_stats.m_timestamp = 3;
			add_hint_to_list(*hint_list, hint_list_v3.get_ptr(), handle, info,
							 fake_stats);
		}
	}

	for (const auto &entry : previous_entries) {
		if (!entry.is_valid()) {
			continue;
		}

		const auto key = make_track_key(entry.track_id);
		if (key.empty() || next_track_ids.find(key) != next_track_ids.end()) {
			continue;
		}

		const auto handle = make_handle_for_track(entry.track_id);
		if (handle.is_valid()) {
			stale_handles.add_item(handle);
		}
	}

	status.set_item("Injecting metadata into database...");
	finalize_hint_list(hint_list, handles);
	remove_info_for_handles(stale_handles, status, abort);
}

void remove_track_metadata(const char *track_id) {
	cache::remove_track_metadata(track_id);
	remove_snapshot(track_id);
	refresh_track(track_id);
}

void refresh_track(const char *track_id) {
	const auto handle = make_handle_for_track(track_id);
	if (!handle.is_valid()) {
		return;
	}

	fb2k::inMainThread(
		[handle] { static_api_ptr_t<metadb_io>()->dispatch_refresh(handle); });
}

pfc::string8 make_display_name_for_path(const char *path) {
	cached_track_metadata track;
	if (try_get_track_metadata_for_path(path, track)) {
		const auto value =
			subsonic::metadata_utils::make_display_name_from_metadata(track);
		if (!value.is_empty()) {
			return value;
		}
	}

	pfc::string8 track_id;
	if (subsonic::extract_track_id_from_path(path, track_id)) {
		return track_id;
	}

	return path != nullptr ? pfc::string8(path) : pfc::string8();
}

void overlay_file_info_for_path(const char *path, file_info &info) {
	file_info_impl overlay;
	if (!try_make_file_info_for_path(path, overlay)) {
		return;
	}

	info.overwrite_meta(overlay);
	info.overwrite_info(overlay);
	if (overlay.get_length() > 0) {
		info.set_length(overlay.get_length());
	}
	info.set_replaygain(replaygain_info::g_merge(overlay.get_replaygain(),
												 info.get_replaygain()));
}

} // namespace subsonic::metadata