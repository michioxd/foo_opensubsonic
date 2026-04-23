#include "stdafx.h"

#include "config.h"

#include "utils/utils.h"

namespace {

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

cfg_string g_cfg_server_url(guid_cfg_server_url, "");
cfg_string g_cfg_username(guid_cfg_username, "");
cfg_string g_cfg_password(guid_cfg_password, "");
cfg_string g_cfg_api_version(guid_cfg_api_version,
							 subsonic::k_default_api_version);
cfg_string g_cfg_client_name(guid_cfg_client_name,
							 subsonic::k_default_client_name);
cfg_bool g_cfg_allow_insecure_tls(guid_cfg_allow_insecure_tls, false);

} // namespace

namespace subsonic::config {

server_credentials load_server_credentials() {
	server_credentials credentials;
	credentials.base_url = normalize_base_url(g_cfg_server_url.get());
	credentials.username = g_cfg_username.get();
	credentials.password = g_cfg_password.get();
	credentials.api_version = g_cfg_api_version.get();
	credentials.client_name = g_cfg_client_name.get();
	credentials.allow_insecure_tls = g_cfg_allow_insecure_tls.get();
	return credentials;
}

void save_server_credentials(const server_credentials &credentials) {
	g_cfg_server_url = normalize_base_url(credentials.base_url);
	g_cfg_username = credentials.username;
	g_cfg_password = credentials.password;
	g_cfg_api_version = credentials.api_version.is_empty()
							? k_default_api_version
							: credentials.api_version.c_str();
	g_cfg_client_name = credentials.client_name.is_empty()
							? k_default_client_name
							: credentials.client_name.c_str();
	g_cfg_allow_insecure_tls = credentials.allow_insecure_tls;

	FB2K_console_formatter()
		<< "[foo_opensubsonic][config] updated server settings for user '"
		<< credentials.username << "'";
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