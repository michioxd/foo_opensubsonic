#include "stdafx.h"

#include "component_info.h"
#include "config.h"
#include "library.h"
#include "resource.h"
#include "utils/utils.h"

#include <SDK/preferences_page.h>
#include <helpers/DarkMode.h>
#include <helpers/atl-misc.h>
#include <libPPUI/HyperLinkCtrl.h>

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
		subsonic::config::save_server_credentials(read_dialog());
		on_changed();
	}

	void reset() override {
		set_dialog(default_credentials());
		on_changed();
	}

	BEGIN_MSG_MAP_EX(opensubsonic_preferences_dialog)
	MSG_WM_INITDIALOG(OnInitDialog)
	COMMAND_HANDLER_EX(IDC_OS_SERVER_URL, EN_CHANGE, OnEditChange)
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
		set_dialog(subsonic::config::load_server_credentials());
		set_footer_text();
		initialize_hyperlinks();
		return FALSE;
	}

	void OnEditChange(UINT, int, CWindow) { on_changed(); }

	void OnToggleChange(UINT, int, CWindow) { on_changed(); }

	void OnSyncNowClicked(UINT, int, CWindow) {
		if (has_changed()) {
			apply();
		}
		subsonic::library::sync_all_async();
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
		credentials.api_version = subsonic::k_default_api_version;
		credentials.client_name = subsonic::k_default_client_name;
		credentials.allow_insecure_tls = false;
		return credentials;
	}

	void set_dialog(const subsonic::server_credentials &credentials) {
		uSetDlgItemText(*this, IDC_OS_SERVER_URL, credentials.base_url);
		uSetDlgItemText(*this, IDC_OS_USERNAME, credentials.username);
		uSetDlgItemText(*this, IDC_OS_PASSWORD, credentials.password);
		uSetDlgItemText(*this, IDC_OS_CLIENT_NAME,
						credentials.client_name.is_empty()
							? subsonic::k_default_client_name
							: credentials.client_name.c_str());
		CheckDlgButton(IDC_OS_ALLOW_INSECURE_TLS, credentials.allow_insecure_tls
													  ? BST_CHECKED
													  : BST_UNCHECKED);
	}

	[[nodiscard]] subsonic::server_credentials read_dialog() const {
		subsonic::server_credentials credentials;
		uGetDlgItemText(*this, IDC_OS_SERVER_URL, credentials.base_url);
		uGetDlgItemText(*this, IDC_OS_USERNAME, credentials.username);
		uGetDlgItemText(*this, IDC_OS_PASSWORD, credentials.password);
		uGetDlgItemText(*this, IDC_OS_CLIENT_NAME, credentials.client_name);

		credentials.base_url =
			subsonic::normalize_base_url(credentials.base_url);
		credentials.api_version = subsonic::k_default_api_version;
		if (credentials.client_name.is_empty()) {
			credentials.client_name = subsonic::k_default_client_name;
		}
		credentials.allow_insecure_tls =
			IsDlgButtonChecked(IDC_OS_ALLOW_INSECURE_TLS) == BST_CHECKED;
		return credentials;
	}

	[[nodiscard]] bool has_changed() const {
		const auto current = subsonic::config::load_server_credentials();
		const auto edited = read_dialog();

		return !subsonic::strings_equal(current.base_url, edited.base_url) ||
			   !subsonic::strings_equal(current.username, edited.username) ||
			   !subsonic::strings_equal(current.password, edited.password) ||
			   !subsonic::strings_equal(current.client_name,
										edited.client_name) ||
			   current.allow_insecure_tls != edited.allow_insecure_tls;
	}

	void on_changed() { m_callback->on_state_changed(); }

	const preferences_page_callback::ptr m_callback;
	fb2k::CDarkModeHooks m_dark;
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