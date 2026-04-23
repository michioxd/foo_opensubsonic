#include "../stdafx.h"

#include "time_utils.h"

#include <chrono>

namespace subsonic::time_utils {

std::uint64_t current_unix_time_ms() noexcept {
	using namespace std::chrono;
	return static_cast<std::uint64_t>(
		duration_cast<milliseconds>(system_clock::now().time_since_epoch())
			.count());
}

} // namespace subsonic::time_utils
