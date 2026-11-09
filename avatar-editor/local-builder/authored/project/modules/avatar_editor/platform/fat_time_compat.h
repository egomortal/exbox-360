#pragma once
#include <chrono>
#include <cstdint>
#include <ctime>
namespace av2::platform {
// Only the bounded FAT timestamp fields used by STFS: years 1980..2107,
// months -1..14, days 0..31, hours 0..31, minutes 0..63, seconds 0..62.
// Normalize from January, then add days/time so malformed-but-representable
// FAT values retain timegm's normalization. No timezone or process-global TZ.
inline std::int64_t fat_utc_seconds(const std::tm& value) {
  using namespace std::chrono;
  const auto month_start = year{value.tm_year + 1900}/January + months{value.tm_mon};
  const auto date = sys_days{month_start/1} + days{value.tm_mday - 1};
  return duration_cast<seconds>(date.time_since_epoch()).count()
      + std::int64_t(value.tm_hour)*3600 + std::int64_t(value.tm_min)*60 + value.tm_sec;
}
}
