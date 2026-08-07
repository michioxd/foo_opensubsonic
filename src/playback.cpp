#include "stdafx.h"

#include "config.h"
#include "http/http.h"
#include "utils/time_utils.h"
#include "utils/utils.h"

#include <SDK/app_close_blocker.h>
#include <SDK/play_callback.h>

#include <mutex>

namespace {

constexpr const char *k_report_scope = "report-playback";
constexpr const char *k_scrobble_scope = "scrobble";
constexpr const char *k_report_endpoint = "reportPlayback.view";
constexpr const char *k_scrobble_endpoint = "scrobble.view";

std::mutex g_current_track_mutex;
pfc::string8 g_current_track_id;

void set_current_track_id(const char *track_id) {
	std::scoped_lock lock(g_current_track_mutex);
	if (track_id != nullptr) {
		g_current_track_id = track_id;
	} else {
		g_current_track_id.reset();
	}
}

pfc::string8 get_current_track_id() {
	std::scoped_lock lock(g_current_track_mutex);
	return g_current_track_id;
}

enum class playback_state { starting, playing, paused, stopped };

constexpr const char *state_to_str(playback_state state) noexcept {
	switch (state) {
	case playback_state::starting:
		return "starting";
	case playback_state::playing:
		return "playing";
	case playback_state::paused:
		return "paused";
	case playback_state::stopped:
		return "stopped";
	}
	return "stopped";
}

void report_playback_async(const char *track_id, playback_state state,
						   std::int64_t position_ms) {
	if (track_id == nullptr || *track_id == '\0') {
		return;
	}

	const auto credentials = subsonic::config::load_server_credentials();
	if (!credentials.is_configured()) {
		return;
	}

	const pfc::string8 track_id_copy = track_id;
	const pfc::string8 state_str = state_to_str(state);

	fb2k::splitTask([credentials, track_id_copy, state_str, position_ms] {
		try {
			auto response = subsonic::http::open_api(
				credentials, k_report_endpoint, fb2k::noAbort,
				{subsonic::query_param("mediaId", track_id_copy.c_str()),
				 subsonic::query_param("mediaType", "song"),
				 subsonic::query_param(
					 "positionMs",
					 (PFC_string_formatter() << (t_int64)position_ms).c_str()),
				 subsonic::query_param("state", state_str.c_str()),
				 subsonic::query_param("ignoreScrobble", "true")});

			if (!subsonic::http::status_is_success(response)) {
				subsonic::log_warning(
					k_report_scope,
					(PFC_string_formatter()
					 << "failed to report playback for id=" << track_id_copy
					 << " state=" << state_str
					 << " status=" << response.status_text)
						.c_str());
				return;
			}

			subsonic::log_info(
				k_report_scope,
				(PFC_string_formatter()
				 << "reported playback for id=" << track_id_copy
				 << " state=" << state_str << " positionMs="
				 << (t_int64)position_ms)
					.c_str());
		} catch (const std::exception &e) {
			subsonic::log_exception(k_report_scope, e);
		} catch (...) {
			subsonic::log_error(k_report_scope, "failed to report playback");
		}
	});
}

void submit_scrobble_async(const char *path) {
	pfc::string8 track_id;
	if (!subsonic::extract_track_id_from_path(path, track_id) ||
		track_id.is_empty()) {
		return;
	}

	const auto credentials = subsonic::config::load_server_credentials();
	if (!credentials.is_configured()) {
		return;
	}

	const pfc::string8 track_id_copy = track_id;

	fb2k::splitTask([credentials, track_id_copy] {
		try {
			auto response = subsonic::http::open_api(
				credentials, k_scrobble_endpoint, fb2k::noAbort,
				{subsonic::query_param("id", track_id_copy.c_str()),
				 subsonic::query_param("submission", "true")});

			if (!subsonic::http::status_is_success(response)) {
				subsonic::log_warning(
					k_scrobble_scope,
					(PFC_string_formatter()
					 << "failed to scrobble id=" << track_id_copy
					 << " status=" << response.status_text)
						.c_str());
				return;
			}

			subsonic::log_info(
				k_scrobble_scope,
				(PFC_string_formatter() << "scrobbled id=" << track_id_copy)
					.c_str());
		} catch (const std::exception &e) {
			subsonic::log_exception(k_scrobble_scope, e);
		} catch (...) {
			subsonic::log_error(k_scrobble_scope, "failed to scrobble");
		}
	});
}

[[nodiscard]] bool extract_track_id_from_handle(const metadb_handle_ptr &track,
												 pfc::string8 &out_id) {
	if (!track.is_valid()) {
		return false;
	}
	const char *path = track->get_path();
	if (path == nullptr || !subsonic::is_subsonic_path(path)) {
		return false;
	}
	return subsonic::extract_track_id_from_path(path, out_id) &&
		   !out_id.is_empty();
}

class opensubsonic_playback_callback : public play_callback_static {
  public:
	unsigned get_flags() override {
		return flag_on_playback_new_track | flag_on_playback_pause |
			   flag_on_playback_seek | flag_on_playback_stop;
	}

	void on_playback_new_track(metadb_handle_ptr track) override {
		pfc::string8 track_id;
		if (!extract_track_id_from_handle(track, track_id)) {
			return;
		}

		set_current_track_id(track_id.c_str());
		report_playback_async(track_id, playback_state::starting, 0);
		report_playback_async(track_id, playback_state::playing, 0);
	}

	void on_playback_pause(bool state) override {
		metadb_handle_ptr now_playing;
		if (!playback_control::get()->get_now_playing(now_playing)) {
			return;
		}

		pfc::string8 track_id;
		if (!extract_track_id_from_handle(now_playing, track_id)) {
			return;
		}

		const auto position_ms = static_cast<std::int64_t>(
			playback_control::get()->playback_get_position() * 1000.0);

		report_playback_async(
			track_id, state ? playback_state::paused : playback_state::playing,
			position_ms);
	}

	void on_playback_seek(double time_secs) override {
		metadb_handle_ptr now_playing;
		if (!playback_control::get()->get_now_playing(now_playing)) {
			return;
		}

		pfc::string8 track_id;
		if (!extract_track_id_from_handle(now_playing, track_id)) {
			return;
		}

		const auto position_ms =
			static_cast<std::int64_t>(time_secs * 1000.0);
		report_playback_async(track_id, playback_state::playing, position_ms);
	}

	void on_playback_starting(play_control::t_track_command, bool) override {}

	void on_playback_stop(play_control::t_stop_reason) override {
		const pfc::string8 track_id = get_current_track_id();
		if (track_id.is_empty()) {
			return;
		}

		const auto position_ms = static_cast<std::int64_t>(
			playback_control::get()->playback_get_position() * 1000.0);
		report_playback_async(track_id, playback_state::stopped, position_ms);
		set_current_track_id(nullptr);
	}

	void on_playback_edited(metadb_handle_ptr) override {}
	void on_playback_dynamic_info(const file_info &) override {}
	void on_playback_dynamic_info_track(const file_info &) override {}
	void on_playback_time(double) override {}
	void on_volume_change(float) override {}
};

class opensubsonic_playback_statistics_collector
	: public playback_statistics_collector {
  public:
	void on_item_played(metadb_handle_ptr item) override {
		if (!item.is_valid()) {
			return;
		}

		const char *path = item->get_path();
		if (path == nullptr || !subsonic::is_subsonic_path(path)) {
			return;
		}

		submit_scrobble_async(path);
	}
};

static play_callback_static_factory_t<opensubsonic_playback_callback>
	g_opensubsonic_playback_callback;

static playback_statistics_collector_factory_t<
	opensubsonic_playback_statistics_collector>
	g_opensubsonic_playback_statistics_collector;

} // namespace
