#pragma once

#include <SDK/foobar2000.h>

#include <cstdint>
#include <vector>

namespace subsonic {

inline constexpr const char *k_component_name = "foo_opensubsonic";
inline constexpr const char *k_scheme = "subsonic://";
inline constexpr const char *k_default_api_version = "1.16.1";
inline constexpr const char *k_default_client_name = "foo_opensubsonic";

struct server_credentials {
  pfc::string8 base_url;
  pfc::string8 username;
  pfc::string8 password;
  pfc::string8 api_version = k_default_api_version;
  pfc::string8 client_name = k_default_client_name;
  bool allow_insecure_tls = false;

  [[nodiscard]] bool is_configured() const noexcept {
    return !base_url.is_empty() && !username.is_empty() && !password.is_empty();
  }
};

struct query_param {
  pfc::string8 key;
  pfc::string8 value;

  query_param() = default;
  query_param(const char *p_key, const char *p_value)
      : key(p_key), value(p_value) {}
  query_param(pfc::string8 p_key, pfc::string8 p_value)
      : key(std::move(p_key)), value(std::move(p_value)) {}
  [[nodiscard]] bool is_valid() const noexcept { return !key.is_empty(); }
};

struct track_identity {
  pfc::string8 track_id;
  pfc::string8 path;

  [[nodiscard]] bool is_valid() const noexcept {
    return !track_id.is_empty() && !path.is_empty();
  }
};

struct track_metadata_field {
  pfc::string8 key;
  pfc::string8 value;

  [[nodiscard]] bool is_valid() const noexcept { return !key.is_empty(); }
};

struct cached_track_metadata {
  pfc::string8 track_id;
  pfc::string8 artist;
  pfc::string8 title;
  pfc::string8 album;
  pfc::string8 cover_art_id;
  pfc::string8 stream_mime_type;
  pfc::string8 suffix;
  pfc::string8 track_number;
  pfc::string8 disc_number;
  pfc::string8 year;
  pfc::string8 genre;
  pfc::string8 bitrate;
  double duration_seconds = 0.0;
  std::vector<track_metadata_field> extra_fields;

  [[nodiscard]] bool is_valid() const noexcept { return !track_id.is_empty(); }
};

struct artwork_cache_entry {
  pfc::string8 cover_art_id;
  pfc::string8 mime_type;
  pfc::string8 local_path;
  pfc::string8 content_hash;
  std::uint64_t last_access_unix_ms = 0;

  [[nodiscard]] bool is_valid() const noexcept {
    return !cover_art_id.is_empty() && !local_path.is_empty();
  }
};

} // namespace subsonic