#include "stdafx.h"

#include "config.h"
#include "http/http.h"
#include "utils/time_utils.h"
#include "utils/utils.h"

#include <SDK/app_close_blocker.h>
#include <SDK/play_callback.h>

#include <mutex>

namespace {

constexpr const char *k_now_playing_scope = "now-playing";
constexpr const char *k_scrobble_endpoint = "scrobble.view";
constexpr std::uint64_t k_now_playing_resume_dedupe_ms = 15 * 1000;

std::mutex g_playback_state_mutex;
pfc::string8 g_last_now_playing_track_id;
std::uint64_t g_last_now_playing_unix_ms = 0;

[[nodiscard]] bool should_send_now_playing(const char *track_id, bool force) {
	if (track_id == nullptr || *track_id == '\0') {
		return false;
	}

	std::scoped_lock lock(g_playback_state_mutex);
	const auto now_ms = subsonic::time_utils::current_unix_time_ms();
	const bool same_track = g_last_now_playing_track_id == track_id;
	if (!force && same_track &&
		(now_ms - g_last_now_playing_unix_ms) <
			k_now_playing_resume_dedupe_ms) {
		return false;
	}

	g_last_now_playing_track_id = track_id;
	g_last_now_playing_unix_ms = now_ms;
	return true;
}

void clear_now_playing_state() {
	std::scoped_lock lock(g_playback_state_mutex);
	g_last_now_playing_track_id.reset();
	g_last_now_playing_unix_ms = 0;
}

void send_playback_ping_async(const char *track_id, bool submission,
							  const char *scope) {
	if (track_id == nullptr || *track_id == '\0') {
		return;
	}

	const auto credentials = subsonic::config::load_server_credentials();
	if (!credentials.is_configured()) {
		return;
	}

	const pfc::string8 track_id_copy = track_id;
	const pfc::string8 scope_copy =
		scope != nullptr ? scope : k_now_playing_scope;

	fb2k::splitTask([credentials, track_id_copy, submission, scope_copy] {
		try {
			auto response = subsonic::http::open_api(
				credentials, k_scrobble_endpoint, fb2k::noAbort,
				{subsonic::query_param("id", track_id_copy.c_str()),
				 subsonic::query_param("submission",
									   submission ? "true" : "false")});

			if (!subsonic::http::status_is_success(response)) {
				subsonic::log_warning(
					scope_copy,
					(PFC_string_formatter()
					 << "failed to send playback ping for id=" << track_id_copy
					 << " submission=" << (submission ? "true" : "false")
					 << " status=" << response.status_text)
						.c_str());
				return;
			}

			subsonic::log_info(scope_copy, (PFC_string_formatter()
											<< "sent playback ping for id="
											<< track_id_copy << " submission="
											<< (submission ? "true" : "false"))
											   .c_str());
		} catch (const std::exception &e) {
			subsonic::log_exception(scope_copy, e);
		} catch (...) {
			subsonic::log_error(scope_copy, "failed to send playback ping");
		}
	});
}

void notify_now_playing_async(const char *path, bool force) {
	pfc::string8 track_id;
	if (!subsonic::extract_track_id_from_path(path, track_id) ||
		track_id.is_empty() || !should_send_now_playing(track_id, force)) {
		return;
	}

	send_playback_ping_async(track_id, false, k_now_playing_scope);
}

void submit_scrobble_async(const char *path) {
	pfc::string8 track_id;
	if (!subsonic::extract_track_id_from_path(path, track_id) ||
		track_id.is_empty()) {
		return;
	}

	send_playback_ping_async(track_id, true, "scrobble");
}

class opensubsonic_playback_callback : public play_callback_static {
  public:
	unsigned get_flags() override {
		return flag_on_playback_new_track | flag_on_playback_pause;
	}

	void on_playback_new_track(metadb_handle_ptr track) override {
		notify_for_track(track, true);
	}

	void on_playback_pause(bool state) override {
		if (state) {
			return;
		}

		metadb_handle_ptr now_playing;
		if (!playback_control::get()->get_now_playing(now_playing)) {
			return;
		}

		notify_for_track(now_playing, false);
	}

	void on_playback_starting(play_control::t_track_command, bool) override {}
	void on_playback_stop(play_control::t_stop_reason) override {
		clear_now_playing_state();
	}
	void on_playback_seek(double) override {}
	void on_playback_edited(metadb_handle_ptr) override {}
	void on_playback_dynamic_info(const file_info &) override {}
	void on_playback_dynamic_info_track(const file_info &) override {}
	void on_playback_time(double) override {}
	void on_volume_change(float) override {}

  private:
	static void notify_for_track(const metadb_handle_ptr &track, bool force) {
		if (!track.is_valid()) {
			return;
		}

		const char *path = track->get_path();
		if (path == nullptr || !subsonic::is_subsonic_path(path)) {
			return;
		}

		notify_now_playing_async(path, force);
	}
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