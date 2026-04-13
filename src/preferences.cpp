#include "stdafx.h"

#include "component_info.h"
#include "config.h"
#include "library.h"
#include "library_sync.h"
#include "resource.h"
#include "utils.h"

#include <SDK/preferences_page.h>
#include <helpers/DarkMode.h>
#include <helpers/atl-misc.h>
#include <libPPUI/HyperLinkCtrl.h>

#include <unordered_set>

namespace {

const GUID guid_preferences_page_opensubsonic = {
	0xbe20fd5b,
	0x0d18,
	0x464a,
	{0xb5, 0xfb, 0xc7, 0xf9, 0xd5, 0x0e, 0xb0, 0xa6}};

class opensubsonic_preferences_dialog
	: public CDialogImpl<opensubsonic_preferences_dialog>,
	  public preferences_page_instance {
  public:
	opensubsonic_preferences_dialog(preferences_page_callback::ptr callback)
		: m_callback(std::move(callback)) {}

	enum { IDD = IDD_OPEN_SUBSONIC_PREFERENCES };

	t_uint32 get_state() override {
		t_uint32 state = preferences_state::resettable |
						 preferences_state::dark_mode_supported;
		if (has_changed()) {
			state |= preferences_state::changed;
		}
		return state;
	}

	void apply() override {
		sync_current_editor_to_settings();
		const auto normalized = normalize_settings_for_ui(m_settings);
		subsonic::config::save_server_settings(normalized);
		m_settings = normalized;
		m_selected_index = find_selected_server_index();
		for (const auto &server : m_settings.servers) {
			subsonic::library::refresh_managed_playlists(server);
		}
		if (!m_servers_pending_cleanup.empty()) {
			subsonic::library::remove_servers_data_async(
				m_servers_pending_cleanup);
			m_servers_pending_cleanup.clear();
		}
		on_changed();
	}

	void reset() override {
		m_settings = make_default_settings();
		m_selected_index = 0;
		m_servers_pending_cleanup.clear();
		refresh_server_list();
		load_selected_server_into_dialog();
		on_changed();
	}

	BEGIN_MSG_MAP_EX(opensubsonic_preferences_dialog)
	MSG_WM_INITDIALOG(OnInitDialog)
	COMMAND_HANDLER_EX(IDC_OS_SERVER_LIST, LBN_SELCHANGE,
					   OnServerSelectionChanged)
	COMMAND_HANDLER_EX(IDC_OS_SERVER_ADD, BN_CLICKED, OnAddServerClicked)
	COMMAND_HANDLER_EX(IDC_OS_SERVER_DELETE, BN_CLICKED, OnDeleteServerClicked)
	COMMAND_HANDLER_EX(IDC_OS_SERVER_NAME, EN_CHANGE, OnEditChange)
	COMMAND_HANDLER_EX(IDC_OS_SERVER_URL, EN_CHANGE, OnEditChange)
	COMMAND_HANDLER_EX(IDC_OS_LOCAL_SERVER_URL, EN_CHANGE, OnEditChange)
	COMMAND_HANDLER_EX(IDC_OS_USERNAME, EN_CHANGE, OnEditChange)
	COMMAND_HANDLER_EX(IDC_OS_PASSWORD, EN_CHANGE, OnEditChange)
	COMMAND_HANDLER_EX(IDC_OS_CLIENT_NAME, EN_CHANGE, OnEditChange)
	COMMAND_HANDLER_EX(IDC_OS_ALLOW_INSECURE_TLS, BN_CLICKED, OnToggleChange)
	COMMAND_HANDLER_EX(IDC_OS_SYNC_NOW, BN_CLICKED, OnSyncNowClicked)
	COMMAND_HANDLER_EX(IDC_OS_CACHE_ARTWORK, BN_CLICKED, OnCacheArtworkClicked)
	COMMAND_HANDLER_EX(IDC_OS_CLEAR_ARTWORK_CACHE, BN_CLICKED,
					   OnClearArtworkCacheClicked)
	COMMAND_HANDLER_EX(IDC_OS_CLEAR_CACHE, BN_CLICKED, OnClearCacheClicked)
	END_MSG_MAP()

  private:
	BOOL OnInitDialog(CWindow, LPARAM) {
		m_dark.AddDialogWithControls(*this);
		m_settings =
			normalize_settings_for_ui(subsonic::config::load_server_settings());
		if (m_settings.servers.empty()) {
			m_settings = make_default_settings();
		}
		m_selected_index = find_selected_server_index();
		refresh_server_list();
		load_selected_server_into_dialog();
		set_footer_text();
		initialize_hyperlinks();
		return FALSE;
	}

	void OnEditChange(UINT, int, CWindow) {
		if (m_updating_controls) {
			return;
		}

		sync_current_editor_to_settings();
		refresh_server_list_label(m_selected_index);
		on_changed();
	}

	void OnToggleChange(UINT, int, CWindow) { on_changed(); }

	void OnServerSelectionChanged(UINT, int, CWindow) {
		if (m_updating_controls) {
			return;
		}

		sync_current_editor_to_settings();
		const auto selected = static_cast<int>(::SendDlgItemMessage(
			m_hWnd, IDC_OS_SERVER_LIST, LB_GETCURSEL, 0, 0));
		if (selected == LB_ERR || selected < 0 ||
			selected >= static_cast<int>(m_settings.servers.size())) {
			return;
		}

		m_selected_index = static_cast<size_t>(selected);
		m_settings.selected_server_id =
			m_settings.servers[m_selected_index].server_id;
		load_selected_server_into_dialog();
		on_changed();
	}

	void OnAddServerClicked(UINT, int, CWindow) {
		sync_current_editor_to_settings();

		auto server = default_credentials();
		server.server_name = PFC_string_formatter()
							 << "Server " << (m_settings.servers.size() + 1);
		server.server_id = subsonic::generate_server_id();

		m_settings.servers.push_back(std::move(server));
		m_selected_index = m_settings.servers.size() - 1;
		m_settings.selected_server_id =
			m_settings.servers[m_selected_index].server_id;
		refresh_server_list();
		load_selected_server_into_dialog();
		on_changed();
	}

	void OnDeleteServerClicked(UINT, int, CWindow) {
		sync_current_editor_to_settings();
		if (m_settings.servers.empty()) {
			return;
		}

		const auto server = m_settings.servers[m_selected_index];
		const auto answer = ::MessageBoxA(
			m_hWnd,
			(PFC_string_formatter()
			 << "Delete server '"
			 << (server.server_name.is_empty() ? server.server_id.c_str()
											   : server.server_name.c_str())
			 << "'?\n\nYes = delete server and remove all synced tracks, "
				"playlists, and cached artwork for this server.\n"
				"No = delete only the server profile.\n"
				"Cancel = keep this server.")
				.c_str(),
			"foo_opensubsonic",
			MB_ICONWARNING | MB_YESNOCANCEL | MB_DEFBUTTON3);
		if (answer == IDCANCEL) {
			return;
		}

		if (answer == IDYES) {
			m_servers_pending_cleanup.erase(
				std::remove_if(m_servers_pending_cleanup.begin(),
							   m_servers_pending_cleanup.end(),
							   [&](const subsonic::server_credentials &entry) {
								   return subsonic::strings_equal(
									   entry.server_id, server.server_id);
							   }),
				m_servers_pending_cleanup.end());
			m_servers_pending_cleanup.push_back(server);
		}

		m_settings.servers.erase(m_settings.servers.begin() +
								 static_cast<std::ptrdiff_t>(m_selected_index));
		if (m_settings.servers.empty()) {
			m_settings = make_default_settings();
			m_selected_index = 0;
		} else if (m_selected_index >= m_settings.servers.size()) {
			m_selected_index = m_settings.servers.size() - 1;
		}

		m_settings.selected_server_id =
			m_settings.servers[m_selected_index].server_id;
		refresh_server_list();
		load_selected_server_into_dialog();
		on_changed();
	}

	void OnSyncNowClicked(UINT, int, CWindow) {
		if (has_changed()) {
			apply();
		}
		const auto selected = subsonic::config::load_server_credentials();
		subsonic::library::sync_server_async(selected.server_id);
	}

	void OnCacheArtworkClicked(UINT, int, CWindow) {
		if (has_changed()) {
			apply();
		}
		subsonic::library::cache_artwork_async();
	}

	void OnClearArtworkCacheClicked(UINT, int, CWindow) {
		const auto answer = ::MessageBoxA(
			m_hWnd,
			"This will remove only cached artwork files. Metadata and "
			"playlists will remain. Continue?",
			"foo_opensubsonic", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
		if (answer != IDYES) {
			return;
		}

		subsonic::library::clear_artwork_cache_async();
	}

	void OnClearCacheClicked(UINT, int, CWindow) {
		const auto answer = ::MessageBoxA(
			m_hWnd, "This will clear cached metadata and artwork. Continue?",
			"foo_opensubsonic", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
		if (answer != IDYES) {
			return;
		}

		subsonic::library::clear_cache_async();
	}

	void initialize_hyperlinks() {
		PP::createHyperLink(GetDlgItem(IDC_OS_GITHUB_LINK),
							L"https://github.com/michioxd/foo_opensubsonic");
	}

	void set_footer_text() {
		pfc::string_formatter version_text;
		version_text << OPENSUBSONIC_COMPONENT_NAME << " v"
					 << OPENSUBSONIC_COMPONENT_VERSION;
		uSetDlgItemText(*this, IDC_OS_VERSION_TEXT, version_text);
		uSetDlgItemText(*this, IDC_OS_GITHUB_LINK,
						OPENSUBSONIC_COMPONENT_REPOSITORY_URL);
	}

	[[nodiscard]] subsonic::server_credentials default_credentials() const {
		subsonic::server_credentials credentials;
		credentials.server_name = "Default Server";
		credentials.api_version = subsonic::k_default_api_version;
		credentials.client_name = subsonic::k_default_client_name;
		credentials.allow_insecure_tls = false;
		credentials.server_id = subsonic::generate_server_id();
		return credentials;
	}

	[[nodiscard]] subsonic::server_settings make_default_settings() const {
		subsonic::server_settings settings;
		settings.servers.push_back(default_credentials());
		settings.selected_server_id = settings.servers.front().server_id;
		return settings;
	}

	void set_dialog(const subsonic::server_credentials &credentials) {
		m_updating_controls = true;
		uSetDlgItemText(*this, IDC_OS_SERVER_NAME, credentials.server_name);
		uSetDlgItemText(*this, IDC_OS_SERVER_URL, credentials.base_url);
		uSetDlgItemText(*this, IDC_OS_LOCAL_SERVER_URL, credentials.local_url);
		uSetDlgItemText(*this, IDC_OS_USERNAME, credentials.username);
		uSetDlgItemText(*this, IDC_OS_PASSWORD, credentials.password);
		uSetDlgItemText(*this, IDC_OS_CLIENT_NAME,
						credentials.client_name.is_empty()
							? subsonic::k_default_client_name
							: credentials.client_name.c_str());
		CheckDlgButton(IDC_OS_ALLOW_INSECURE_TLS, credentials.allow_insecure_tls
													  ? BST_CHECKED
													  : BST_UNCHECKED);
		m_updating_controls = false;
	}

	[[nodiscard]] subsonic::server_credentials read_dialog() const {
		subsonic::server_credentials credentials;
		uGetDlgItemText(*this, IDC_OS_SERVER_NAME, credentials.server_name);
		uGetDlgItemText(*this, IDC_OS_SERVER_URL, credentials.base_url);
		uGetDlgItemText(*this, IDC_OS_LOCAL_SERVER_URL, credentials.local_url);
		uGetDlgItemText(*this, IDC_OS_USERNAME, credentials.username);
		uGetDlgItemText(*this, IDC_OS_PASSWORD, credentials.password);
		uGetDlgItemText(*this, IDC_OS_CLIENT_NAME, credentials.client_name);

		if (credentials.server_name.is_empty()) {
			if (!credentials.base_url.is_empty()) {
				credentials.server_name = credentials.base_url;
			} else {
				credentials.server_name = "Server";
			}
		}
		credentials.base_url =
			subsonic::normalize_base_url(credentials.base_url);
		credentials.local_url =
			subsonic::normalize_base_url(credentials.local_url);
		credentials.api_version = subsonic::k_default_api_version;
		if (credentials.client_name.is_empty()) {
			credentials.client_name = subsonic::k_default_client_name;
		}
		credentials.allow_insecure_tls =
			IsDlgButtonChecked(IDC_OS_ALLOW_INSECURE_TLS) == BST_CHECKED;
		return credentials;
	}

	[[nodiscard]] subsonic::server_credentials
	normalize_server_for_ui(subsonic::server_credentials credentials) const {
		credentials.base_url =
			subsonic::normalize_base_url(credentials.base_url);
		credentials.local_url =
			subsonic::normalize_base_url(credentials.local_url);
		if (credentials.server_name.is_empty()) {
			if (!credentials.base_url.is_empty()) {
				credentials.server_name = credentials.base_url;
			} else {
				credentials.server_name = "Server";
			}
		}
		if (credentials.client_name.is_empty()) {
			credentials.client_name = subsonic::k_default_client_name;
		}
		if (credentials.api_version.is_empty()) {
			credentials.api_version = subsonic::k_default_api_version;
		}
		if (credentials.server_id.is_empty()) {
			credentials.server_id = subsonic::generate_server_id();
		}
		return credentials;
	}

	[[nodiscard]] subsonic::server_settings
	normalize_settings_for_ui(subsonic::server_settings settings) const {
		if (settings.servers.empty()) {
			return make_default_settings();
		}

		std::unordered_set<std::string> seen_ids;
		seen_ids.reserve(settings.servers.size());
		for (size_t i = 0; i < settings.servers.size(); ++i) {
			auto &server = settings.servers[i];
			server = normalize_server_for_ui(server);

			std::string candidate = server.server_id.c_str();
			if (candidate.empty()) {
				candidate = subsonic::generate_server_id().c_str();
			}

			if (!seen_ids.emplace(candidate).second) {
				for (size_t suffix = 2;; ++suffix) {
					const pfc::string8 next = PFC_string_formatter()
											  << candidate.c_str() << "-"
											  << suffix;
					if (seen_ids.emplace(next.c_str()).second) {
						candidate = next.c_str();
						break;
					}
				}
			}

			server.server_id = candidate.c_str();
		}

		if (settings.selected_server_id.is_empty()) {
			settings.selected_server_id = settings.servers.front().server_id;
		}

		for (const auto &server : settings.servers) {
			if (subsonic::strings_equal(server.server_id,
										settings.selected_server_id)) {
				return settings;
			}
		}

		settings.selected_server_id = settings.servers.front().server_id;
		return settings;
	}

	[[nodiscard]] pfc::string8
	make_server_list_label(const subsonic::server_credentials &server) const {
		const auto normalized = normalize_server_for_ui(server);
		return PFC_string_formatter()
			   << normalized.server_name << " ["
			   << (normalized.base_url.is_empty() ? "no-url"
												  : normalized.base_url.c_str())
			   << "]";
	}

	void refresh_server_list() {
		m_updating_controls = true;
		::SendDlgItemMessage(m_hWnd, IDC_OS_SERVER_LIST, LB_RESETCONTENT, 0, 0);
		for (const auto &server : m_settings.servers) {
			const auto label = make_server_list_label(server);
			uSendDlgItemMessageText(m_hWnd, IDC_OS_SERVER_LIST, LB_ADDSTRING, 0,
									label.c_str());
		}
		::SendDlgItemMessage(m_hWnd, IDC_OS_SERVER_LIST, LB_SETCURSEL,
							 static_cast<WPARAM>(m_selected_index), 0);
		::EnableWindow(GetDlgItem(IDC_OS_SERVER_DELETE),
					   m_settings.servers.size() > 1);
		m_updating_controls = false;
	}

	void refresh_server_list_label(size_t index) {
		if (index >= m_settings.servers.size()) {
			return;
		}

		const auto selected = ::SendDlgItemMessage(m_hWnd, IDC_OS_SERVER_LIST,
												   LB_GETCURSEL, 0, 0);
		::SendDlgItemMessage(m_hWnd, IDC_OS_SERVER_LIST, LB_DELETESTRING,
							 static_cast<WPARAM>(index), 0);
		const auto label = make_server_list_label(m_settings.servers[index]);
		uSendDlgItemMessageText(m_hWnd, IDC_OS_SERVER_LIST, LB_INSERTSTRING,
								static_cast<WPARAM>(index), label.c_str());
		::SendDlgItemMessage(
			m_hWnd, IDC_OS_SERVER_LIST, LB_SETCURSEL,
			selected == LB_ERR ? static_cast<WPARAM>(index) : selected, 0);
	}

	void load_selected_server_into_dialog() {
		if (m_settings.servers.empty()) {
			set_dialog(default_credentials());
			return;
		}

		if (m_selected_index >= m_settings.servers.size()) {
			m_selected_index = 0;
		}

		m_settings.selected_server_id =
			m_settings.servers[m_selected_index].server_id;
		set_dialog(m_settings.servers[m_selected_index]);
	}

	void sync_current_editor_to_settings() {
		if (m_updating_controls || m_settings.servers.empty() ||
			m_selected_index >= m_settings.servers.size()) {
			return;
		}

		auto updated = read_dialog();
		updated.server_id = m_settings.servers[m_selected_index].server_id;
		m_settings.servers[m_selected_index] = normalize_server_for_ui(updated);
		m_settings.selected_server_id =
			m_settings.servers[m_selected_index].server_id;
	}

	[[nodiscard]] size_t find_selected_server_index() const {
		for (size_t i = 0; i < m_settings.servers.size(); ++i) {
			if (subsonic::strings_equal(m_settings.servers[i].server_id,
										m_settings.selected_server_id)) {
				return i;
			}
		}
		return 0;
	}

	[[nodiscard]] bool has_changed() const {
		auto edited_settings = m_settings;
		if (!m_updating_controls && !edited_settings.servers.empty() &&
			m_selected_index < edited_settings.servers.size()) {
			auto updated = read_dialog();
			updated.server_id =
				edited_settings.servers[m_selected_index].server_id;
			edited_settings.servers[m_selected_index] = updated;
			edited_settings.selected_server_id =
				edited_settings.servers[m_selected_index].server_id;
		}

		const auto lhs = normalize_settings_for_ui(edited_settings);
		const auto rhs =
			normalize_settings_for_ui(subsonic::config::load_server_settings());

		if (!subsonic::strings_equal(lhs.selected_server_id,
									 rhs.selected_server_id) ||
			lhs.servers.size() != rhs.servers.size()) {
			return true;
		}

		for (size_t i = 0; i < lhs.servers.size(); ++i) {
			const auto &a = lhs.servers[i];
			const auto &b = rhs.servers[i];
			if (!subsonic::strings_equal(a.server_id, b.server_id) ||
				!subsonic::strings_equal(a.server_name, b.server_name) ||
				!subsonic::strings_equal(a.base_url, b.base_url) ||
				!subsonic::strings_equal(a.local_url, b.local_url) ||
				!subsonic::strings_equal(a.username, b.username) ||
				!subsonic::strings_equal(a.password, b.password) ||
				!subsonic::strings_equal(a.client_name, b.client_name) ||
				a.allow_insecure_tls != b.allow_insecure_tls) {
				return true;
			}
		}

		return false;
	}

	void on_changed() { m_callback->on_state_changed(); }

	const preferences_page_callback::ptr m_callback;
	fb2k::CDarkModeHooks m_dark;
	subsonic::server_settings m_settings;
	size_t m_selected_index = 0;
	bool m_updating_controls = false;
	std::vector<subsonic::server_credentials> m_servers_pending_cleanup;
};

class preferences_page_opensubsonic
	: public preferences_page_impl<opensubsonic_preferences_dialog> {
  public:
	const char *get_name() override { return "OpenSubsonic"; }

	GUID get_guid() override { return guid_preferences_page_opensubsonic; }

	GUID get_parent_guid() override { return guid_tools; }
};

static preferences_page_factory_t<preferences_page_opensubsonic>
	g_preferences_page_opensubsonic;

} // namespace