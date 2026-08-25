#pragma once

#include <cstddef>

namespace onion::overlay {

/* Approximate 18pt bold advances; padded per-core spaces are much narrower. */
inline constexpr float glyph_advance(unsigned char ch) {
  if (ch == ' ')
    return 5.0f;
  if (ch >= '0' && ch <= '9')
    return 11.0f;
  switch (ch) {
  case '%':
    return 15.0f;
  case '.':
  case '|':
    return 5.0f;
  case '-':
    return 8.0f;
  default:
    return 12.0f;
  }
}

inline float estimate_text_width(const char *text) {
  if (!text || !text[0])
    return 0.0f;

  float width = 6.0f;
  for (std::size_t i = 0; text[i]; ++i)
    width += glyph_advance(static_cast<unsigned char>(text[i]));

  return width < 32.0f ? 32.0f : width;
}

} // namespace onion::overlay
