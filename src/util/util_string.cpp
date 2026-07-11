/*
 * This file is part of DXMT, Copyright (c) 2023 Feifan He
 *
 * Derived from a part of DXVK (originally under zlib License),
 * Copyright (c) 2017 Philip Rebohle
 * Copyright (c) 2019 Joshua Ashton
 *
 * See <https://github.com/doitsujin/dxvk/blob/master/LICENSE>
 */

#include "util_string.hpp"
#include "util_likely.hpp"

namespace dxmt::str {

const uint8_t *decodeTypedChar(const uint8_t *begin, const uint8_t *end,
                               uint32_t &ch) {
  if (begin >= end) {
    ch = uint32_t('?');
    return end;
  }

  uint32_t first = begin[0];

  if (likely(first < 0x80)) {
    ch = uint32_t(first);
    return begin + 1;
  }

  size_t length = 0;
  uint32_t minimum = 0;
  if (first >= 0xc2 && first <= 0xdf) {
    length = 2;
    minimum = 0x80;
    ch = first & 0x1f;
  } else if (first >= 0xe0 && first <= 0xef) {
    length = 3;
    minimum = 0x800;
    ch = first & 0x0f;
  } else if (first >= 0xf0 && first <= 0xf4) {
    length = 4;
    minimum = 0x10000;
    ch = first & 0x07;
  } else {
    ch = uint32_t('?');
    return begin + 1;
  }

  if (size_t(end - begin) < length) {
    ch = uint32_t('?');
    return end;
  }

  for (size_t i = 1; i < length; i++) {
    if ((begin[i] & 0xc0) != 0x80) {
      ch = uint32_t('?');
      return begin + 1;
    }
    ch = (ch << 6) | (begin[i] & 0x3f);
  }

  if (ch < minimum || (ch >= 0xd800 && ch <= 0xdfff) || ch > 0x10ffff)
    ch = uint32_t('?');
  return begin + length;
}

const uint16_t *decodeTypedChar(const uint16_t *begin, const uint16_t *end,
                                uint32_t &ch) {
  if (begin >= end) {
    ch = uint32_t('?');
    return end;
  }

  uint32_t first = begin[0];

  if (likely(first < 0xD800)) {
    ch = first;
    return begin + 1;
  } else if (first < 0xDC00) {
    if (unlikely(begin + 2 > end || begin[1] < 0xdc00 || begin[1] >= 0xe000)) {
      ch = uint32_t('?');
      return begin + 1;
    }

    ch = 0x10000 + ((uint32_t(begin[0]) & 0x3FF) << 10) +
         ((uint32_t(begin[1]) & 0x3FF));
    return begin + 2;
  } else if (unlikely(first < 0xE000)) {
    // Stray low surrogate
    ch = uint32_t('?');
    return begin + 1;
  } else {
    ch = first;
    return begin + 1;
  }
}

const uint32_t *decodeTypedChar(const uint32_t *begin, const uint32_t *end,
                                uint32_t &ch) {
  if (begin >= end) {
    ch = uint32_t('?');
    return end;
  }

  ch = begin[0];
  if ((ch >= 0xd800 && ch <= 0xdfff) || ch > 0x10ffff)
    ch = uint32_t('?');
  return begin + 1;
}

size_t encodeTypedChar(uint8_t *begin, uint8_t *end, uint32_t ch) {
  if ((ch >= 0xd800 && ch <= 0xdfff) || ch > 0x10ffff)
    return 0;

  if (likely(ch < 0x80)) {
    if (begin) {
      if (unlikely(begin + 1 > end))
        return 0;

      begin[0] = uint8_t(ch);
    }

    return 1;
  } else if (ch < 0x800) {
    if (begin) {
      if (unlikely(begin + 2 > end))
        return 0;

      begin[0] = uint8_t(0xC0 | (ch >> 6));
      begin[1] = uint8_t(0x80 | (ch & 0x3F));
    }

    return 2;
  } else if (ch < 0x10000) {
    if (begin) {
      if (unlikely(begin + 3 > end))
        return 0;

      begin[0] = uint8_t(0xE0 | ((ch >> 12)));
      begin[1] = uint8_t(0x80 | ((ch >> 6) & 0x3F));
      begin[2] = uint8_t(0x80 | ((ch >> 0) & 0x3F));
    }

    return 3;
  } else {
    if (begin) {
      if (unlikely(begin + 4 > end))
        return 0;

      begin[0] = uint8_t(0xF0 | ((ch >> 18)));
      begin[1] = uint8_t(0x80 | ((ch >> 12) & 0x3F));
      begin[2] = uint8_t(0x80 | ((ch >> 6) & 0x3F));
      begin[3] = uint8_t(0x80 | ((ch >> 0) & 0x3F));
    }
    return 4;
  }
}

size_t encodeTypedChar(uint16_t *begin, uint16_t *end, uint32_t ch) {
  if ((ch >= 0xd800 && ch <= 0xdfff) || ch > 0x10ffff)
    return 0;

  if (likely(ch < 0xD800)) {
    if (begin) {
      if (unlikely(begin + 1 > end))
        return 0;

      begin[0] = ch;
    }

    return 1;
  } else if (ch < 0x10000) {
    if (begin) {
      if (unlikely(begin + 1 > end))
        return 0;

      begin[0] = ch;
    }

    return 1;
  } else {
    if (begin) {
      if (unlikely(begin + 2 > end))
        return 0;

      ch -= 0x10000;
      begin[0] = uint16_t(0xD800 + (ch >> 10));
      begin[1] = uint16_t(0xDC00 + (ch & 0x3FF));
    }
    return 2;
  }
}

size_t encodeTypedChar(uint32_t *begin, uint32_t *end, uint32_t ch) {
  if ((ch >= 0xd800 && ch <= 0xdfff) || ch > 0x10ffff)
    return 0;

  if (begin) {
    if (unlikely(begin + 1 > end))
      return 0;

    begin[0] = ch;
  }

  return 1;
}

std::string fromws(const WCHAR *ws) {
  size_t srcLen = length(ws);
  size_t dstLen = transcodeString<char>(nullptr, 0, ws, srcLen);

  std::string result;
  result.resize(dstLen);

  transcodeString(result.data(), dstLen, ws, srcLen);

  return result;
}

std::wstring tows(const char *mbs) {
  size_t srcLen = length(mbs);
  size_t dstLen = transcodeString<wchar_t>(nullptr, 0, mbs, srcLen);

  std::wstring result;
  result.resize(dstLen);

  transcodeString(result.data(), dstLen, mbs, srcLen);

  return result;
}

} // namespace dxmt::str
