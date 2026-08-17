#pragma once

#include "../types.h"

#include <SDK/ui_element.h>
#include <SDK/ui.h>
#include <helpers/atl-misc.h>
#include <helpers/DarkMode.h>

#include <atlbase.h>
#include <atlapp.h>
#include <atlcrack.h>
#include <atlctrls.h>

#include <memory>
#include <vector>
#include <atomic>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <string>

namespace subsonic::ui {

struct tree_node_data {
	pfc::string8 id;
	pfc::string8 name;
	bool is_root_music_folder = false;
	bool children_loaded = false;
	bool is_track = false;
	std::optional<cached_track_metadata> track_meta;
	std::vector<cached_track_metadata> folder_tracks;
};

class folder_view_panel : public ui_element_instance, public CWindowImpl<folder_view_panel> {
public:
	DECLARE_WND_CLASS_EX(_T("foo_opensubsonic_folder_view"), CS_VREDRAW | CS_HREDRAW, (-1));

	folder_view_panel(ui_element_config::ptr cfg, ui_element_instance_callback::ptr callback);
	~folder_view_panel() override;

	void initialize_window(HWND parent);

	HWND get_wnd() override;
	void set_configuration(ui_element_config::ptr cfg) override;
	ui_element_config::ptr get_configuration() override;
	void notify(const GUID & what, t_size param1, const void * param2, t_size param2size) override;

	static GUID g_get_guid();
	static GUID g_get_subclass();
	static void g_get_name(pfc::string_base &out);
	static ui_element_config::ptr g_get_default_configuration();
	static const char *g_get_description();

	BEGIN_MSG_MAP_EX(folder_view_panel)
		MSG_WM_CREATE(OnCreate)
		MSG_WM_SIZE(OnSize)
		MSG_WM_DESTROY(OnDestroy)
		MSG_WM_NOTIFY(OnNotify)
		MSG_WM_CONTEXTMENU(OnContextMenu)
	END_MSG_MAP()

private:
	int OnCreate(LPCREATESTRUCT lpCreateStruct);
	void OnSize(UINT nType, CSize size);
	void OnDestroy();
	LRESULT OnNotify(int idCtrl, LPNMHDR pnmh);
	void OnContextMenu(HWND hwnd, CPoint pt);

	void update_colors();
	void load_root_folders();
	void expand_folder_node(HTREEITEM hItem, tree_node_data *data);
	void select_folder_node(HTREEITEM hItem, tree_node_data *data);

	HTREEITEM insert_tree_node(HTREEITEM hParent, const char *clean_name, const char *id, bool is_root, bool has_children, bool is_track, std::optional<cached_track_metadata> track_meta, size_t initial_track_count = 0);

	ui_element_config::ptr m_config;
	ui_element_instance_callback::ptr m_callback;

	CTreeViewCtrl m_tree;
	fb2k::CDarkModeHooks m_dark;

	ui_selection_holder::ptr m_selection_holder;

	std::shared_ptr<std::atomic<bool>> m_is_alive;
	
	std::unordered_map<HTREEITEM, std::unique_ptr<tree_node_data>> m_node_store;
	std::unordered_set<std::string> m_fetching_nodes;

	pfc::string8 m_current_folder_name;
	pfc::string8 m_current_folder_id;
};

class folder_view_panel_impl : public ui_element_impl<folder_view_panel> {};

} // namespace subsonic::ui