// PCH disabled for utils folder

#include "crypto_util.h"

#include <SDK/hasher_md5.h>

namespace subsonic {
namespace crypto_util {

pfc::string8 md5_hex(const char *input) noexcept {
	const auto hash = static_api_ptr_t<hasher_md5>()->process_single_string(
		input != nullptr ? input : "");
	return pfc::format_hexdump_lowercase(hash.m_data, sizeof(hash.m_data), "");
}

pfc::string8 generate_salt() noexcept {
	pfc::string8 salt = pfc::print_guid(pfc::createGUID());
	salt.replace_string("{", "");
	salt.replace_string("}", "");
	salt.replace_string("-", "");
	return salt;
}

pfc::string8 make_auth_token(const char *password, const char *salt) noexcept {
	pfc::string8 input = password != nullptr ? password : "";
	input += salt != nullptr ? salt : "";
	return md5_hex(input);
}

} // namespace crypto_util
} // namespace subsonic
