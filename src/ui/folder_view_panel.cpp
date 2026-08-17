#include "stdafx.h"
#include "folder_view_panel.h"
#include "../folder_actions.h"
#include "../http/folder_api.h"
#include "../http/foobar_http_client.h"
#include "../utils/utils.h"
#include "../utils/track_path_util.h"
#include "../config.h"

namespace subsonic::ui {

static const GUID guid_folder_view_panel = { 
	0x519890a9, 0xb800, 0x4f36, 
	{ 0xa6, 0x5c, 0x7c, 0xc1, 0x1f, 0x8a, 0x1d, 0x6e } 
};

enum menu_commands : UINT_PTR {
	ID_MENU_PLAY_FOLDER = 10001,
	ID_MENU_REFRESH_FOLDER,
	ID_MENU_PLAY_TRACK,
	ID_MENU_ADD_TRACK
};

folder_view_panel::folder_view_panel(ui_element_config::ptr cfg, ui_element_instance_callback::ptr callback)
	: m_bMsgHandled(0), m_config(cfg), m_callback(callback),
	  m_is_alive(std::make_shared<std::atomic<bool>>(true)) {
}

folder_view_panel::~folder_view_panel() {
	if (m_is_alive) {
		*m_is_alive = false;
	}
}

void folder_view_panel::initialize_window(HWND parent) {
	WIN32_OP(Create(parent) != NULL);
}

HWND folder_view_panel::get_wnd() {
	return m_hWnd;
}

void folder_view_panel::set_configuration(ui_element_config::ptr cfg) {
	m_config = cfg;
}

ui_element_config::ptr folder_view_panel::get_configuration() {
	return m_config;
}

void folder_view_panel::notify(const GUID & what, t_size param1, const void * param2, t_size param2size) {
	if (what == ui_element_notify_colors_changed) {
		update_colors();
	}
}

GUID folder_view_panel::g_get_guid() {
	return guid_folder_view_panel;
}

GUID folder_view_panel::g_get_subclass() {
	return ui_element_subclass_media_library_viewers;
}

void folder_view_panel::g_get_name(pfc::string_base &out) {
	out = "OpenSubsonic Folder View";
}

ui_element_config::ptr folder_view_panel::g_get_default_configuration() {
	return ui_element_config::g_create_empty(g_get_guid());
}

const char *folder_view_panel::g_get_description() {
	return "Browse and play OpenSubsonic library by folders.";
}

void folder_view_panel::update_colors() {
	if (m_callback.is_valid()) {
		m_tree.SetBkColor(m_callback->query_std_color(ui_color_background));
		m_tree.SetTextColor(m_callback->query_std_color(ui_color_text));
	}
}

int folder_view_panel::OnCreate(LPCREATESTRUCT lpCreateStruct) {
	m_dark.AddDialogWithControls(*this);

	m_tree.Create(m_hWnd, rcDefault, NULL, 
				  WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | 
				  TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS, 
				  WS_EX_CLIENTEDGE);

	try {
		m_selection_holder = ui_selection_manager::get()->acquire();
	} catch (...) {
		FB2K_console_formatter() << "[foo_opensubsonic][folder_view] Warning: Failed to acquire selection manager.";
	}

	update_colors();
	load_root_folders();
	return 0;
}

void folder_view_panel::OnSize(UINT nType, CSize size) {
	if (m_tree.m_hWnd != NULL) {
		m_tree.SetWindowPos(NULL, 0, 0, size.cx, size.cy, SWP_NOZORDER | SWP_NOACTIVATE);
	}
	SetMsgHandled(FALSE);
}

void folder_view_panel::OnDestroy() {
	if (m_is_alive) {
		*m_is_alive = false;
	}
	m_selection_holder.release();
	m_node_store.clear();
	SetMsgHandled(FALSE);
}

HTREEITEM folder_view_panel::insert_tree_node(HTREEITEM hParent, const char *clean_name, const char *id, bool is_root, bool has_children, bool is_track, std::optional<cached_track_metadata> track_meta, size_t initial_track_count) {
	auto node = std::make_unique<tree_node_data>();
	node->id = id;
	node->name = clean_name;
	node->is_root_music_folder = is_root;
	node->children_loaded = false;
	node->is_track = is_track;
	node->track_meta = track_meta;

	TVINSERTSTRUCT tvis = {};
	tvis.hParent = hParent;
	tvis.hInsertAfter = TVI_LAST;
	tvis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
	
	pfc::string8 display_text = clean_name;
	if (!is_track && initial_track_count > 0) {
		display_text << " (" << initial_track_count << ")";
	}

	pfc::stringcvt::string_os_from_utf8 os_name(display_text);
	tvis.item.pszText = const_cast<TCHAR *>(os_name.get_ptr());
	tvis.item.lParam = reinterpret_cast<LPARAM>(node.get());
	tvis.item.cChildren = has_children ? 1 : 0;

	HTREEITEM hItem = m_tree.InsertItem(&tvis);
	m_node_store[hItem] = std::move(node);
	return hItem;
}

void folder_view_panel::load_root_folders() {
	m_tree.DeleteAllItems();
	m_node_store.clear();
	insert_tree_node(TVI_ROOT, "Loading folders...", "", true, false, false, std::nullopt);

	auto alive = m_is_alive;
	auto pThis = this; 

	fb2k::splitTask([alive, pThis] {
		try {
			if (!*alive) return;
			
			auto credentials = subsonic::config::load_server_credentials();
			if (!credentials.is_configured()) {
				fb2k::inMainThread([alive, pThis] {
					if (!*alive) return;
					pThis->m_tree.DeleteAllItems();
					pThis->insert_tree_node(TVI_ROOT, "Server not configured in Preferences", "", true, false, false, std::nullopt);
				});
				return;
			}

			subsonic::foobar_http_client standalone_client(credentials);
			abort_callback_dummy abort;
			auto folders = folder_api::fetch_music_folders(standalone_client, abort);

			fb2k::inMainThread([alive, pThis, folders = std::move(folders)] {
				if (!*alive) return;

				pThis->m_tree.DeleteAllItems();

				if (folders.empty()) {
					pThis->insert_tree_node(TVI_ROOT, "No music folders returned by server", "", true, false, false, std::nullopt);
					return;
				}

				for (const auto &folder : folders) {
					HTREEITEM hItem = pThis->insert_tree_node(TVI_ROOT, folder.name.c_str(), folder.id.c_str(), true, true, false, std::nullopt);
					pThis->m_tree.Expand(hItem, TVE_EXPAND);
				}
			});
		} catch (const std::exception &e) {
			fb2k::inMainThread([alive, pThis, err = pfc::string8(e.what())] {
				if (!*alive) return;
				pThis->m_tree.DeleteAllItems();
				pThis->insert_tree_node(TVI_ROOT, (PFC_string_formatter() << "Error: " << err).c_str(), "", true, false, false, std::nullopt);
			});
		}
	});
}

void folder_view_panel::expand_folder_node(HTREEITEM hItem, tree_node_data *data) {
	if (data == nullptr || data->children_loaded || data->id.is_empty()) {
		return;
	}
	
	if (m_fetching_nodes.count(data->id.c_str())) {
		return; 
	}
	m_fetching_nodes.insert(data->id.c_str());
	
	insert_tree_node(hItem, "Loading...", "", false, false, false, std::nullopt, 0);

	auto alive = m_is_alive;
	auto pThis = this;
	pfc::string8 node_id = data->id;
	bool is_root = data->is_root_music_folder;
	
	fb2k::splitTask([alive, pThis, hItem, node_id, is_root] {
		try {
			if (!*alive) return;

			auto credentials = subsonic::config::load_server_credentials();
			if (!credentials.is_configured()) return;

			subsonic::foobar_http_client standalone_client(credentials);
			abort_callback_dummy abort;
			auto dir_result = folder_api::fetch_directory(standalone_client, node_id.c_str(), is_root, abort);
			
			std::vector<folder::directory_entry> valid_dirs;
			for (auto& sub : dir_result.subdirectories) {
				if (sub.has_song_count && sub.song_count == 0) continue;
				valid_dirs.push_back(std::move(sub));
			}
			dir_result.subdirectories = std::move(valid_dirs);
			
			fb2k::inMainThread([alive, pThis, hItem, node_id, dir_result = std::move(dir_result)]() mutable {
				if (!*alive) return;
				pThis->m_fetching_nodes.erase(node_id.c_str());
				
				tree_node_data* active_data = nullptr;
				if (pThis->m_node_store.count(hItem)) {
					active_data = pThis->m_node_store[hItem].get();
				}
				if (!active_data || active_data->id != node_id) return;
				
				HTREEITEM hChild = pThis->m_tree.GetChildItem(hItem);
				while (hChild != NULL) {
					HTREEITEM hNext = pThis->m_tree.GetNextSiblingItem(hChild);
					pThis->m_tree.DeleteItem(hChild);
					hChild = hNext;
				}

				if (dir_result.child_tracks.empty() && dir_result.subdirectories.empty()) {
					active_data->children_loaded = true;
					return;
				}

				active_data->folder_tracks = dir_result.child_tracks;
				folder_actions::register_directory_tracks_metadata(dir_result.child_tracks);
				
				for (const auto &sub : dir_result.subdirectories) {
					pThis->insert_tree_node(hItem, sub.name.c_str(), sub.id.c_str(), false, true, false, std::nullopt, sub.song_count);
				}
				
				for (const auto &track : dir_result.child_tracks) {
					pfc::string8 display_name = track.title;
					if (display_name.is_empty()) display_name = track.track_id;
					
					if (!track.suffix.is_empty() && !subsonic::ends_with_ascii_nocase(display_name.c_str(), track.suffix.c_str())) {
						display_name << "." << track.suffix;
					}
					
					pThis->insert_tree_node(hItem, display_name.c_str(), track.track_id.c_str(), false, false, true, track, 0);
				}
				
				pfc::string8 folder_label = active_data->name;
				if (!dir_result.child_tracks.empty()) {
					folder_label << " (" << dir_result.child_tracks.size() << ")";
				}
				pfc::stringcvt::string_os_from_utf8 os_folder_label(folder_label);

				TVITEM item = {};
				item.mask = TVIF_TEXT;
				item.hItem = hItem;
				item.pszText = const_cast<TCHAR*>(os_folder_label.get_ptr());
				pThis->m_tree.SetItem(&item);

				active_data->children_loaded = true;
				pThis->m_tree.Expand(hItem, TVE_EXPAND);
			});
		} catch (const std::exception &e) {
			fb2k::inMainThread([alive, pThis, hItem, node_id, err = pfc::string8(e.what())] {
				if (!*alive) return;
				pThis->m_fetching_nodes.erase(node_id.c_str());
				
				HTREEITEM hChild = pThis->m_tree.GetChildItem(hItem);
				while (hChild != NULL) {
					HTREEITEM hNext = pThis->m_tree.GetNextSiblingItem(hChild);
					pThis->m_tree.DeleteItem(hChild);
					hChild = hNext;
				}
				pThis->insert_tree_node(hItem, (PFC_string_formatter() << "Error: " << err).c_str(), "", false, false, false, std::nullopt);
			});
		}
	});
}

void folder_view_panel::select_folder_node(HTREEITEM hItem, tree_node_data *data) {
	if (data == nullptr || data->id.is_empty()) {
		return;
	}

	m_current_folder_id = data->id;
	m_current_folder_name = data->name;

	if (!m_selection_holder.is_valid()) {
		return;
	}

	metadb_handle_list handles;

	if (data->is_track && data->track_meta.has_value()) {
		folder_actions::register_directory_tracks_metadata({ data->track_meta.value() });
		auto handle = static_api_ptr_t<metadb>()->handle_create(subsonic::track_path_util::make_subsonic_path(data->track_meta.value().track_id.c_str()), 0);
		if (handle.is_valid()) handles.add_item(handle);
	} else if (data->children_loaded && !data->folder_tracks.empty()) {
		for (const auto &track : data->folder_tracks) {
			auto handle = static_api_ptr_t<metadb>()->handle_create(subsonic::track_path_util::make_subsonic_path(track.track_id.c_str()), 0);
			if (handle.is_valid()) handles.add_item(handle);
		}
	}

	m_selection_holder->set_selection(handles);
}

LRESULT folder_view_panel::OnNotify(int idCtrl, LPNMHDR pnmh) {
	if (pnmh->hwndFrom == m_tree.m_hWnd) {
		if (pnmh->code == TVN_ITEMEXPANDINGW || pnmh->code == TVN_ITEMEXPANDINGA) {
			auto *pnmtv = reinterpret_cast<LPNMTREEVIEW>(pnmh);
			if (pnmtv->action == TVE_EXPAND) {
				auto *data = reinterpret_cast<tree_node_data *>(pnmtv->itemNew.lParam);
				if (data != nullptr) {
					expand_folder_node(pnmtv->itemNew.hItem, data);
				}
			}
		} else if (pnmh->code == TVN_SELCHANGEDW || pnmh->code == TVN_SELCHANGEDA) {
			auto *pnmtv = reinterpret_cast<LPNMTREEVIEW>(pnmh);
			auto *data = reinterpret_cast<tree_node_data *>(pnmtv->itemNew.lParam);
			if (data != nullptr) {
				select_folder_node(pnmtv->itemNew.hItem, data);
			}
		} else if (pnmh->code == NM_DBLCLK) {
			HTREEITEM hSel = m_tree.GetSelectedItem();
			if (hSel != NULL) {
				auto *data = reinterpret_cast<tree_node_data *>(m_tree.GetItemData(hSel));
				if (data != nullptr && data->is_track && data->track_meta.has_value()) {
					folder_actions::play_or_enqueue_track(data->track_meta.value(), false);
				}
			}
		} else if (pnmh->code == TVN_DELETEITEMW || pnmh->code == TVN_DELETEITEMA) {
			auto *pnmtv = reinterpret_cast<LPNMTREEVIEW>(pnmh);
			m_node_store.erase(pnmtv->itemOld.hItem);
		}
	}
	return 0;
}

void folder_view_panel::OnContextMenu(HWND hwnd, CPoint pt) {
	if (pt.x == -1 && pt.y == -1) {
		pt = { 0, 0 };
	}

	if (hwnd == m_tree.m_hWnd || ::IsChild(m_tree.m_hWnd, hwnd)) {
		HTREEITEM hSel = m_tree.GetSelectedItem();
		tree_node_data *data = nullptr;
		if (hSel != NULL) {
			data = reinterpret_cast<tree_node_data *>(m_tree.GetItemData(hSel));
		}

		CMenu menu;
		menu.CreatePopupMenu();

		if (data != nullptr && data->is_track) {
			menu.AppendMenu(MF_STRING, ID_MENU_PLAY_TRACK, _T("Play Track"));
			menu.AppendMenu(MF_STRING, ID_MENU_ADD_TRACK, _T("Add to Playlist"));
		} else {
			menu.AppendMenu(MF_STRING, ID_MENU_PLAY_FOLDER, _T("Play Folder"));
			menu.AppendMenu(MF_STRING, ID_MENU_REFRESH_FOLDER, _T("Refresh"));
		}

		UINT cmd = menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, m_hWnd);
		
		if (cmd == ID_MENU_PLAY_FOLDER) {
			if (data && !data->is_track && data->children_loaded) {
				folder_actions::play_folder_as_playlist(data->name.c_str(), data->folder_tracks);
			} else if (data && !data->is_track) {
				expand_folder_node(hSel, data);
				folder_actions::play_folder_as_playlist(data->name.c_str(), data->folder_tracks);
			}
		} else if (cmd == ID_MENU_REFRESH_FOLDER) {
			load_root_folders();
		} else if (cmd == ID_MENU_PLAY_TRACK) {
			if (data && data->is_track && data->track_meta.has_value()) {
				folder_actions::play_or_enqueue_track(data->track_meta.value(), false);
			}
		} else if (cmd == ID_MENU_ADD_TRACK) {
			if (data && data->is_track && data->track_meta.has_value()) {
				folder_actions::play_or_enqueue_track(data->track_meta.value(), true);
			}
		}
	}
}

static service_factory_single_t<folder_view_panel_impl> g_folder_view_panel_impl_factory;

} // namespace subsonic::ui