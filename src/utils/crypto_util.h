#pragma once

#include <SDK/foobar2000.h>

namespace subsonic {
namespace crypto_util {

// Compute MD5 hash of input string, return hex representation
// Uses foobar2000 SDK's hasher_md5
// Note: MD5 is weak but required by OpenSubsonic API spec
[[nodiscard]] pfc::string8 md5_hex(const char *input);

// Generate random salt for authentication
// Uses foobar2000 SDK's GUID generator for randomness
// Returns 32-character hex string (no hyphens or braces)
[[nodiscard]] pfc::string8 generate_salt();

// Create OpenSubsonic auth token: token = md5(password + salt)
// This is the token-based authentication method required by OpenSubsonic API
// Security: Password never sent in plaintext, only the hashed token
[[nodiscard]] pfc::string8 make_auth_token(const char *password,
											const char *salt);

} // namespace crypto_util
} // namespace subsonic
