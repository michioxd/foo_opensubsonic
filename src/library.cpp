#include "stdafx.h"

#include "artwork.h"
#include "library.h"
#include "library_sync.h"

#include "cache.h"
#include "config.h"
#include "metadata.h"

#include <SDK/core_api.h>
#include <SDK/menu.h>
#include <SDK/popup_message.h>
#include <SDK/threaded_process.h>

#include <atomic>

namespace {

const GUID guid_mainmenu_group_opensubsonic = {
	0x9a92cfbf,
	0x266d,
	0x4282,
	{0x94, 0x4d, 0xa7, 0x95, 0xa9, 0x96, 0x51, 0x92}};
const GUID guid_mainmenu_sync_library = {
	0xb767dbb7,
	0x09bb,
	0x4e83,
	{0xac, 0x39, 0x5c, 0xa2, 0x95, 0x8d, 0x73, 0x9d}};
const GUID guid_mainmenu_sync_playlists = {
	0x4af7a340,
	0xf4fc,
	0x45be,
	{0x9a, 0x1b, 0x25, 0x59, 0x75, 0x95, 0x8c, 0x7d}};
const GUID guid_mainmenu_sync_all = {
	0x4b649d1d,
	0xb76a,
	0x4df9,
	{0x95, 0x34, 0x82, 0xec, 0x17, 0xaa, 0xb5, 0x7f}};

std::atomic_bool g_sync_in_progress = false;

class sync_process_callback : public threaded_process_callback {
  public:
	sync_process_callback(subsonic::library::sync_mode mode,
						  pfc::string8 server_id)
		: m_mode(mode), m_server_id(std::move(server_id)) {}

	void on_init(HWND p_wnd) override {}

	void run(threaded_process_status &p_status,
			 abort_callback &p_abort) override {
		try {
			m_outcome = subsonic::library::perform_sync(m_mode, m_server_id,
														p_status, p_abort);
		} catch (const exception_aborted &) {
			m_aborted = true;
		} catch (const std::exception &e) {
			m_error_msg = e.what();
		}
	}

	void on_done(HWND p_wnd, bool p_was_aborted) override {
		g_sync_in_progress = false;
		if (p_was_aborted || m_aborted)
			return;

		if (!m_error_msg.is_empty()) {
			popup_message::g_show(PFC_string_formatter()
									  << "OpenSubsonic sync failed:\n"
									  << m_error_msg,
								  "foo_opensubsonic");
			return;
		}

		try {
			subsonic::library::apply_sync_outcome(m_outcome);
			popup_message::g_show(
				subsonic::library::make_success_message(m_outcome),
				"foo_opensubsonic");
		} catch (const std::exception &e) {
			popup_message::g_show(PFC_string_formatter()
									  << "Apply sync failed:\n"
									  << e.what(),
								  "foo_opensubsonic");
		}
	}

  private:
	subsonic::library::sync_mode m_mode;
	pfc::string8 m_server_id;
	subsonic::library::sync_outcome m_outcome;
	bool m_aborted = false;
	pfc::string8 m_error_msg;
};

class clear_cache_process_callback : public threaded_process_callback {
  public:
	void on_init(HWND p_wnd) override {}

	void run(threaded_process_status &p_status,
			 abort_callback &p_abort) override {
		p_status.set_title("Clearing OpenSubsonic Cache");
		try {
			p_status.set_item("Removing artwork files...");
			subsonic::cache::reset(p_abort);

			p_status.set_item("Clearing metadata database...");

			subsonic::metadata::clear_all(p_status, p_abort);

		} catch (const exception_aborted &) {
			m_aborted = true;
		} catch (const std::exception &e) {
			m_error_msg = e.what();
		}
	}

	void on_done(HWND p_wnd, bool p_was_aborted) override {
		g_sync_in_progress = false;
		if (p_was_aborted || m_aborted)
			return;

		if (!m_error_msg.is_empty()) {
			popup_message::g_show(PFC_string_formatter()
									  << "Clear cache failed:\n"
									  << m_error_msg,
								  "foo_opensubsonic");
			return;
		}

		try {
			subsonic::library::clear_managed_playlists();
			popup_message::g_show("OpenSubsonic cache cleared successfully.",
								  "foo_opensubsonic");
		} catch (const std::exception &e) {
			popup_message::g_show(PFC_string_formatter()
									  << "Failed to remove playlists:\n"
									  << e.what(),
								  "foo_opensubsonic");
		}
	}

  private:
	bool m_aborted = false;
	pfc::string8 m_error_msg;
};

class clear_artwork_cache_process_callback : public threaded_process_callback {
  public:
	void on_init(HWND p_wnd) override {}

	void run(threaded_process_status &p_status,
			 abort_callback &p_abort) override {
		p_status.set_title("Clearing OpenSubsonic Artwork Cache");
		try {
			auto entries = subsonic::cache::load_all_artwork_entries();
			for (size_t i = 0; i < entries.size(); ++i) {
				if (i % 100 == 0) {
					p_abort.check();
					p_status.set_progress(i, entries.size());
					p_status.set_item(PFC_string_formatter()
									  << "Removing cached artwork: " << i
									  << " / " << entries.size());
				}

				if (entries[i].cover_art_id.is_empty()) {
					continue;
				}

				subsonic::cache::remove_artwork_entry(entries[i].server_id,
													  entries[i].cover_art_id);
			}

			p_status.set_progress(entries.size(), entries.size());
			p_status.set_item("Artwork cache cleared.");
		} catch (const exception_aborted &) {
			m_aborted = true;
		} catch (const std::exception &e) {
			m_error_msg = e.what();
		}
	}

	void on_done(HWND p_wnd, bool p_was_aborted) override {
		g_sync_in_progress = false;
		if (p_was_aborted || m_aborted)
			return;

		if (!m_error_msg.is_empty()) {
			popup_message::g_show(PFC_string_formatter()
									  << "Clear artwork cache failed:\n"
									  << m_error_msg,
								  "foo_opensubsonic");
			return;
		}

		popup_message::g_show(
			"OpenSubsonic artwork cache cleared successfully.",
			"foo_opensubsonic");
	}

  private:
	bool m_aborted = false;
	pfc::string8 m_error_msg;
};

class remove_servers_data_process_callback : public threaded_process_callback {
  public:
	explicit remove_servers_data_process_callback(
		std::vector<subsonic::server_credentials> servers)
		: m_servers(std::move(servers)) {}

	void on_init(HWND p_wnd) override {}

	void run(threaded_process_status &p_status,
			 abort_callback &p_abort) override {
		p_status.set_title("Removing OpenSubsonic Server Data");
		try {
			for (size_t i = 0; i < m_servers.size(); ++i) {
				p_abort.check();
				const auto &server = m_servers[i];
				const auto label = server.server_name.is_empty()
									   ? server.server_id
									   : server.server_name;

				p_status.set_progress(i, m_servers.size());
				p_status.set_item(PFC_string_formatter()
								  << "Removing synced tracks for " << label);
				subsonic::metadata::remove_server_metadata(server.server_id,
														   p_status, p_abort);

				p_status.set_item(PFC_string_formatter()
								  << "Removing cached artwork for " << label);
				subsonic::cache::remove_server_artwork_entries(
					server.server_id, p_status, p_abort);
			}
		} catch (const exception_aborted &) {
			m_aborted = true;
		} catch (const std::exception &e) {
			m_error_msg = e.what();
		}
	}

	void on_done(HWND p_wnd, bool p_was_aborted) override {
		g_sync_in_progress = false;
		if (p_was_aborted || m_aborted)
			return;

		if (!m_error_msg.is_empty()) {
			popup_message::g_show(PFC_string_formatter()
									  << "Remove server data failed:\n"
									  << m_error_msg,
								  "foo_opensubsonic");
			return;
		}

		try {
			for (const auto &server : m_servers) {
				subsonic::library::clear_managed_playlists(server.server_id);
			}

			popup_message::g_show(PFC_string_formatter()
									  << "Removed synced data for "
									  << m_servers.size()
									  << " OpenSubsonic server(s).",
								  "foo_opensubsonic");
		} catch (const std::exception &e) {
			popup_message::g_show(PFC_string_formatter()
									  << "Failed to remove playlists:\n"
									  << e.what(),
								  "foo_opensubsonic");
		}
	}

  private:
	std::vector<subsonic::server_credentials> m_servers;
	bool m_aborted = false;
	pfc::string8 m_error_msg;
};

class cache_artwork_process_callback : public threaded_process_callback {
  public:
	explicit cache_artwork_process_callback(metadb_handle_list items)
		: m_items(std::move(items)) {}

	void on_init(HWND p_wnd) override {}

	void run(threaded_process_status &p_status,
			 abort_callback &p_abort) override {
		p_status.set_title("Caching OpenSubsonic Artwork");
		try {
			subsonic::config::ensure_cache_layout(p_abort);
			m_result = subsonic::artwork::prefetch_for_library_items(
				m_items, p_status, p_abort);
		} catch (const exception_aborted &) {
			m_aborted = true;
		} catch (const std::exception &e) {
			m_error_msg = e.what();
		}
	}

	void on_done(HWND p_wnd, bool p_was_aborted) override {
		g_sync_in_progress = false;
		if (p_was_aborted || m_aborted)
			return;

		if (!m_error_msg.is_empty()) {
			popup_message::g_show(PFC_string_formatter()
									  << "Cache artwork failed:\n"
									  << m_error_msg,
								  "foo_opensubsonic");
			return;
		}

		popup_message::g_show(
			PFC_string_formatter()
				<< "Artwork cache complete:\n"
				<< "- Tracks scanned: " << m_result.track_count << "\n"
				<< "- Unique artworks: " << m_result.unique_artwork_count
				<< "\n"
				<< "- Downloaded: " << m_result.downloaded_count << "\n"
				<< "- Already cached: " << m_result.already_cached_count,
			"foo_opensubsonic");
	}

  private:
	metadb_handle_list m_items;
	subsonic::artwork::prefetch_artwork_result m_result;
	bool m_aborted = false;
	pfc::string8 m_error_msg;
};

void launch_sync(subsonic::library::sync_mode mode,
				 const char *server_id = nullptr) {
	if (g_sync_in_progress.exchange(true)) {
		popup_message::g_show("An OpenSubsonic sync is already running.",
							  "foo_opensubsonic");
		return;
	}
	threaded_process::g_run_modeless(
		new service_impl_t<sync_process_callback>(
			mode,
			server_id != nullptr ? pfc::string8(server_id) : pfc::string8()),
		threaded_process::flag_show_progress |
			threaded_process::flag_show_abort |
			threaded_process::flag_show_item,
		core_api::get_main_window(), "OpenSubsonic Status");
}

void launch_clear_cache() {
	if (g_sync_in_progress.exchange(true)) {
		popup_message::g_show("An OpenSubsonic job is already running.",
							  "foo_opensubsonic");
		return;
	}
	threaded_process::g_run_modeless(
		new service_impl_t<clear_cache_process_callback>(),
		threaded_process::flag_show_progress |
			threaded_process::flag_show_abort |
			threaded_process::flag_show_item,
		core_api::get_main_window(), "OpenSubsonic Status");
}

void launch_cache_artwork() {
	if (g_sync_in_progress.exchange(true)) {
		popup_message::g_show("An OpenSubsonic job is already running.",
							  "foo_opensubsonic");
		return;
	}

	try {
		auto items = subsonic::library::load_library_playlist_items(
			subsonic::config::load_server_credentials());
		threaded_process::g_run_modeless(
			new service_impl_t<cache_artwork_process_callback>(
				std::move(items)),
			threaded_process::flag_show_progress |
				threaded_process::flag_show_abort |
				threaded_process::flag_show_item,
			core_api::get_main_window(), "OpenSubsonic Status");
	} catch (const std::exception &e) {
		g_sync_in_progress = false;
		popup_message::g_show(PFC_string_formatter()
								  << "Cache artwork failed:\n"
								  << e.what(),
							  "foo_opensubsonic");
	}
}

void launch_clear_artwork_cache() {
	if (g_sync_in_progress.exchange(true)) {
		popup_message::g_show("An OpenSubsonic job is already running.",
							  "foo_opensubsonic");
		return;
	}

	threaded_process::g_run_modeless(
		new service_impl_t<clear_artwork_cache_process_callback>(),
		threaded_process::flag_show_progress |
			threaded_process::flag_show_abort |
			threaded_process::flag_show_item,
		core_api::get_main_window(), "OpenSubsonic Status");
}

void launch_remove_servers_data(
	const std::vector<subsonic::server_credentials> &servers) {
	if (servers.empty()) {
		return;
	}

	if (g_sync_in_progress.exchange(true)) {
		popup_message::g_show("An OpenSubsonic job is already running.",
							  "foo_opensubsonic");
		return;
	}

	threaded_process::g_run_modeless(
		new service_impl_t<remove_servers_data_process_callback>(servers),
		threaded_process::flag_show_progress |
			threaded_process::flag_show_abort |
			threaded_process::flag_show_item,
		core_api::get_main_window(), "OpenSubsonic Status");
}

class mainmenu_commands_opensubsonic : public mainmenu_commands {
  public:
	enum : t_uint32 {
		cmd_sync_library = 0,
		cmd_sync_playlists,
		cmd_sync_all,
		cmd_total,
	};

	t_uint32 get_command_count() override { return cmd_total; }

	GUID get_command(t_uint32 index) override {
		switch (index) {
		case cmd_sync_library:
			return guid_mainmenu_sync_library;
		case cmd_sync_playlists:
			return guid_mainmenu_sync_playlists;
		case cmd_sync_all:
			return guid_mainmenu_sync_all;
		default:
			uBugCheck();
		}
	}

	void get_name(t_uint32 index, pfc::string_base &out) override {
		switch (index) {
		case cmd_sync_library:
			out = "Sync OpenSubsonic Library";
			break;
		case cmd_sync_playlists:
			out = "Sync OpenSubsonic Playlists";
			break;
		case cmd_sync_all:
			out = "Sync OpenSubsonic Library + Playlists";
			break;
		default:
			uBugCheck();
		}
	}

	bool get_description(t_uint32 index, pfc::string_base &out) override {
		switch (index) {
		case cmd_sync_library:
			out = "Fetches the remote OpenSubsonic catalog into the local "
				  "cache and "
				  "a foobar2000 playlist.";
			return true;
		case cmd_sync_playlists:
			out = "Fetches remote OpenSubsonic playlists and mirrors them into "
				  "foobar2000 playlists.";
			return true;
		case cmd_sync_all:
			out = "Runs both the OpenSubsonic library sync and playlist sync.";
			return true;
		default:
			return false;
		}
	}

	GUID get_parent() override { return guid_mainmenu_group_opensubsonic; }

	void execute(t_uint32 index, service_ptr_t<service_base>) override {
		switch (index) {
		case cmd_sync_library:
			subsonic::library::sync_library_async();
			break;
		case cmd_sync_playlists:
			subsonic::library::sync_playlists_async();
			break;
		case cmd_sync_all:
			subsonic::library::sync_all_async();
			break;
		default:
			uBugCheck();
		}
	}
};

static mainmenu_group_popup_factory g_mainmenu_group_opensubsonic(
	guid_mainmenu_group_opensubsonic, mainmenu_groups::library,
	mainmenu_commands::sort_priority_base, "OpenSubsonic");
static mainmenu_commands_factory_t<mainmenu_commands_opensubsonic>
	g_mainmenu_commands_opensubsonic;

} // namespace

namespace subsonic::library {

void sync_library_async() { launch_sync(sync_mode::library_only); }

void sync_library_async(const char *server_id) {
	launch_sync(sync_mode::library_only, server_id);
}

void sync_playlists_async() { launch_sync(sync_mode::playlists_only); }

void sync_playlists_async(const char *server_id) {
	launch_sync(sync_mode::playlists_only, server_id);
}

void sync_all_async() { launch_sync(sync_mode::all); }

void sync_all_async(const char *server_id) {
	launch_sync(sync_mode::all, server_id);
}

void sync_server_async(const char *server_id) {
	launch_sync(sync_mode::all, server_id);
}

void cache_artwork_async() { launch_cache_artwork(); }

void clear_artwork_cache_async() { launch_clear_artwork_cache(); }

void clear_cache_async() { launch_clear_cache(); }

void remove_servers_data_async(const std::vector<server_credentials> &servers) {
	launch_remove_servers_data(servers);
}

} // namespace subsonic::library