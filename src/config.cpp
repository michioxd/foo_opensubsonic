#include "stdafx.h"

#include "config.h"

#include "utils.h"

#include <nlohmann/json.hpp>

#include <unordered_set>

namespace {

using json = nlohmann::json;

static const GUID guid_cfg_server_url = {
	0x4ffdf1db,
	0x06be,
	0x4404,
	{0x87, 0x5f, 0x68, 0x44, 0x43, 0x1d, 0x2d, 0xb0}};
static const GUID guid_cfg_username = {
	0xd9e6f3b3,
	0x155d,
	0x4ef6,
	{0x87, 0xc4, 0x64, 0x73, 0xdc, 0x2c, 0x7b, 0x0d}};
static const GUID guid_cfg_password = {
	0x8d66de6f,
	0x37a4,
	0x4db0,
	{0x9c, 0x39, 0xbb, 0xe3, 0x18, 0xf3, 0xa2, 0x9c}};
static const GUID guid_cfg_api_version = {
	0x4cb0ca1d,
	0x2dc6,
	0x4c80,
	{0x8f, 0x82, 0x70, 0xe1, 0x3c, 0xd8, 0xca, 0xd4}};
static const GUID guid_cfg_client_name = {
	0x4d8d0c17,
	0xab55,
	0x4b77,
	{0x9f, 0x5f, 0xfb, 0xee, 0xf7, 0xd1, 0x45, 0xb5}};
static const GUID guid_cfg_allow_insecure_tls = {
	0x0bc1ea4c,
	0x4d96,
	0x467f,
	{0xa9, 0xa7, 0x7a, 0x22, 0xf7, 0x60, 0x52, 0x17}};
static const GUID guid_cfg_servers_json = {
	0xe6eaa504,
	0x98ad,
	0x4c98,
	{0xa3, 0x41, 0xc5, 0x65, 0x14, 0xfd, 0x0f, 0x7d}};
static const GUID guid_cfg_selected_server_id = {
	0x23861d2b,
	0xca9b,
	0x4d3d,
	{0x94, 0x6a, 0xfb, 0x0a, 0x27, 0x96, 0x50, 0xe5}};

cfg_string g_cfg_server_url(guid_cfg_server_url, "");
cfg_string g_cfg_username(guid_cfg_username, "");
cfg_string g_cfg_password(guid_cfg_password, "");
cfg_string g_cfg_api_version(guid_cfg_api_version,
							 subsonic::k_default_api_version);
cfg_string g_cfg_client_name(guid_cfg_client_name,
							 subsonic::k_default_client_name);
cfg_bool g_cfg_allow_insecure_tls(guid_cfg_allow_insecure_tls, false);
cfg_string g_cfg_servers_json(guid_cfg_servers_json, "");
cfg_string g_cfg_selected_server_id(guid_cfg_selected_server_id, "");

subsonic::server_credentials make_default_server() {
	subsonic::server_credentials credentials;
	credentials.server_name = "Default Server";
	credentials.server_id = subsonic::generate_server_id();
	credentials.api_version = subsonic::k_default_api_version;
	credentials.client_name = subsonic::k_default_client_name;
	credentials.allow_insecure_tls = false;
	return credentials;
}

void normalize_server(subsonic::server_credentials &credentials) {
	credentials.base_url = subsonic::normalize_base_url(credentials.base_url);
	credentials.local_url = subsonic::normalize_base_url(credentials.local_url);
	if (credentials.server_name.is_empty()) {
		if (!credentials.base_url.is_empty()) {
			credentials.server_name = credentials.base_url;
		} else {
			credentials.server_name = "Server";
		}
	}
	if (credentials.api_version.is_empty()) {
		credentials.api_version = subsonic::k_default_api_version;
	}
	if (credentials.client_name.is_empty()) {
		credentials.client_name = subsonic::k_default_client_name;
	}
	if (credentials.server_id.is_empty()) {
		credentials.server_id = subsonic::make_server_storage_id(
			credentials.server_name, credentials.base_url);
	}
}

void ensure_unique_server_ids(
	std::vector<subsonic::server_credentials> &servers) {
	std::unordered_set<std::string> seen_ids;
	seen_ids.reserve(servers.size());

	for (size_t i = 0; i < servers.size(); ++i) {
		auto &server = servers[i];
		normalize_server(server);

		std::string candidate = server.server_id.c_str();
		if (candidate.empty()) {
			candidate = subsonic::generate_server_id().c_str();
		}

		if (seen_ids.emplace(candidate).second) {
			server.server_id = candidate.c_str();
			continue;
		}

		for (size_t suffix = 2;; ++suffix) {
			const pfc::string8 next = PFC_string_formatter()
									  << candidate.c_str() << "-" << suffix;
			if (seen_ids.emplace(next.c_str()).second) {
				server.server_id = next;
				break;
			}
		}
	}
}

subsonic::server_settings
normalize_settings(subsonic::server_settings settings) {
	ensure_unique_server_ids(settings.servers);

	if (settings.selected_server_id.is_empty() && !settings.servers.empty()) {
		settings.selected_server_id = settings.servers.front().server_id;
	}

	if (!settings.selected_server_id.is_empty()) {
		for (const auto &server : settings.servers) {
			if (subsonic::strings_equal(server.server_id,
										settings.selected_server_id)) {
				return settings;
			}
		}
	}

	if (!settings.servers.empty()) {
		settings.selected_server_id = settings.servers.front().server_id;
	}

	return settings;
}

subsonic::server_settings load_legacy_settings() {
	subsonic::server_settings settings;
	if (g_cfg_server_url.get_length() == 0 &&
		g_cfg_username.get_length() == 0 && g_cfg_password.get_length() == 0) {
		return settings;
	}

	auto server = make_default_server();
	server.base_url = subsonic::normalize_base_url(g_cfg_server_url.get());
	server.username = g_cfg_username.get();
	server.password = g_cfg_password.get();
	server.api_version = g_cfg_api_version.get();
	server.client_name = g_cfg_client_name.get();
	server.allow_insecure_tls = g_cfg_allow_insecure_tls.get();
	if (!server.base_url.is_empty()) {
		server.server_name = server.base_url;
	}
	normalize_server(server);
	settings.servers.push_back(std::move(server));
	settings.selected_server_id = settings.servers.front().server_id;
	return settings;
}

json to_json(const subsonic::server_credentials &credentials) {
	return json{{"id", credentials.server_id.c_str()},
				{"name", credentials.server_name.c_str()},
				{"url", credentials.base_url.c_str()},
				{"localUrl", credentials.local_url.c_str()},
				{"username", credentials.username.c_str()},
				{"password", credentials.password.c_str()},
				{"apiVersion", credentials.api_version.c_str()},
				{"clientName", credentials.client_name.c_str()},
				{"allowInsecureTls", credentials.allow_insecure_tls}};
}

subsonic::server_credentials from_json(const json &value) {
	subsonic::server_credentials credentials;
	if (!value.is_object()) {
		return credentials;
	}

	auto read_string = [&](const char *key) -> pfc::string8 {
		const auto found = value.find(key);
		if (found == value.end() || !found->is_string()) {
			return {};
		}
		return found->get_ref<const std::string &>().c_str();
	};

	credentials.server_id = read_string("id");
	credentials.server_name = read_string("name");
	credentials.base_url = read_string("url");
	credentials.local_url = read_string("localUrl");
	credentials.username = read_string("username");
	credentials.password = read_string("password");
	credentials.api_version = read_string("apiVersion");
	credentials.client_name = read_string("clientName");
	const auto insecure = value.find("allowInsecureTls");
	credentials.allow_insecure_tls =
		insecure != value.end() && insecure->is_boolean()
			? insecure->get<bool>()
			: false;
	normalize_server(credentials);
	return credentials;
}

} // namespace

namespace subsonic::config {

server_settings load_server_settings() {
	try {
		const pfc::string8 raw = g_cfg_servers_json.get();
		if (!raw.is_empty()) {
			const auto parsed = json::parse(raw.c_str());
			server_settings settings;
			if (parsed.is_object()) {
				const auto selected = parsed.find("selectedServerId");
				if (selected != parsed.end() && selected->is_string()) {
					settings.selected_server_id =
						selected->get_ref<const std::string &>().c_str();
				}

				const auto servers = parsed.find("servers");
				if (servers != parsed.end() && servers->is_array()) {
					settings.servers.reserve(servers->size());
					for (const auto &item : *servers) {
						auto server = from_json(item);
						if (!server.server_id.is_empty()) {
							settings.servers.push_back(std::move(server));
						}
					}
				}
			}

			if (settings.selected_server_id.is_empty()) {
				settings.selected_server_id = g_cfg_selected_server_id.get();
			}
			return normalize_settings(std::move(settings));
		}
	} catch (const std::exception &e) {
		log_exception("config", e);
	}

	return normalize_settings(load_legacy_settings());
}

void save_server_settings(const server_settings &settings) {
	auto normalized = normalize_settings(settings);

	json payload;
	payload["selectedServerId"] = normalized.selected_server_id.c_str();
	payload["servers"] = json::array();
	for (const auto &server : normalized.servers) {
		payload["servers"].push_back(to_json(server));
	}

	g_cfg_servers_json = payload.dump().c_str();
	g_cfg_selected_server_id = normalized.selected_server_id;

	FB2K_console_formatter()
		<< "[foo_opensubsonic][config] updated " << normalized.servers.size()
		<< " server profile(s)";
}

server_credentials load_server_credentials() {
	const auto settings = load_server_settings();
	for (const auto &server : settings.servers) {
		if (subsonic::strings_equal(server.server_id,
									settings.selected_server_id)) {
			return server;
		}
	}
	return settings.servers.empty() ? make_default_server()
									: settings.servers.front();
}

bool try_get_server_credentials(const char *server_id,
								server_credentials &out) {
	out = {};
	const auto settings = load_server_settings();
	for (const auto &server : settings.servers) {
		if (subsonic::strings_equal(server.server_id, server_id)) {
			out = server;
			return true;
		}
	}
	return false;
}

pfc::string8 load_selected_server_id() {
	return load_server_settings().selected_server_id;
}

void save_server_credentials(const server_credentials &credentials) {
	auto settings = load_server_settings();
	server_credentials normalized = credentials;
	normalize_server(normalized);

	bool updated = false;
	for (auto &server : settings.servers) {
		if (subsonic::strings_equal(server.server_id, normalized.server_id)) {
			server = normalized;
			updated = true;
			break;
		}
	}
	if (!updated) {
		settings.servers.push_back(normalized);
	}
	settings.selected_server_id = normalized.server_id;
	save_server_settings(settings);
}

pfc::string8 component_profile_directory() {
	pfc::string8 path = core_api::get_profile_path();
	path.add_filename(k_component_name);
	return path;
}

pfc::string8 cache_root_directory() {
	pfc::string8 path = component_profile_directory();
	path.add_filename("subsonic_cache");
	return path;
}

pfc::string8 artwork_cache_directory() {
	pfc::string8 path = cache_root_directory();
	path.add_filename("artwork");
	return path;
}

pfc::string8 database_path() {
	pfc::string8 path = cache_root_directory();
	path.add_filename("library_cache.sqlite3");
	return path;
}

pfc::string8 metadata_json_cache_path() {
	pfc::string8 path = cache_root_directory();
	path.add_filename("library_cache.json");
	return path;
}

void ensure_cache_layout(abort_callback &abort) {
	ensure_directory_exists(component_profile_directory(), abort);
	ensure_directory_exists(cache_root_directory(), abort);
	ensure_directory_exists(artwork_cache_directory(), abort);

	FB2K_console_formatter()
		<< "[foo_opensubsonic][config] cache layout ready at "
		<< cache_root_directory();
	abort.check();
}

} // namespace subsonic::config