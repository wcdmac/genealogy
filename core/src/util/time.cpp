#include <genealogy/util/time.hpp>

#include <array>
#include <cstdio>
#include <ctime>
#include <string>

namespace genealogy {

std::string utc_now_iso() {
  const std::time_t now = std::time(nullptr);
  std::tm tm_utc{};
#if defined(_WIN32)
  gmtime_s(&tm_utc, &now);
#else
  gmtime_r(&now, &tm_utc);
#endif
  std::array<char, 32> buf{};
  std::snprintf(buf.data(), buf.size(), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
                tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
  return std::string(buf.data());
}

}  // namespace genealogy
