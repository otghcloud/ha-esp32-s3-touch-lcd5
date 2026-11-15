#pragma once
#include <array>
#include <cstdint>
#include <string>
#include "release_version.h"

namespace wallpanel {
inline bool read_release(const std::string &text, std::array<uint32_t, 3> &out) {
  size_t position = 0;
  for (size_t part = 0; part < out.size(); part++) {
    const size_t start = position;
    uint32_t value = 0;
    while (position < text.size() && text[position] >= '0' && text[position] <= '9') {
      const uint32_t digit = text[position++] - '0';
      if (value > (UINT32_MAX - digit) / 10) return false;
      value = value * 10 + digit;
    }
    if (position == start || (position - start > 1 && text[start] == '0')) return false;
    out[part] = value;
    if (part + 1 < out.size() && (position == text.size() || text[position++] != '.')) return false;
  }
  return position == text.size();
}
}  // namespace wallpanel
