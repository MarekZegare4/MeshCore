#pragma once
// Minimal native stand-in for densaugeo/base64's base64.hpp. Real board envs
// pull in the whole library via lib_deps; src/helpers/BaseChatMesh.cpp
// (built for every board, including the sim, whenever MAX_GROUP_CHANNELS is
// defined) only ever calls decode_base64() -- to decode a channel's
// user-supplied PSK -- so that's the only function reproduced here, as a
// plain standard base64 decoder (skips '=' padding/whitespace/invalid chars
// rather than erroring, same permissive behaviour the real library has).

static inline int _sim_b64_val(unsigned char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

inline unsigned int decode_base64(const unsigned char input[], unsigned int inputLength, unsigned char output[]) {
  unsigned int out_len = 0;
  int val = 0, bits = -8;
  for (unsigned int i = 0; i < inputLength; i++) {
    unsigned char c = input[i];
    if (c == '=') break;
    int v = _sim_b64_val(c);
    if (v < 0) continue;   // skip whitespace/invalid characters
    val = (val << 6) + v;
    bits += 6;
    if (bits >= 0) {
      output[out_len++] = (unsigned char)((val >> bits) & 0xFF);
      bits -= 8;
    }
  }
  return out_len;
}
