// =====================================================================================================
// TAPESTRY · src/core/bytes.h · hex, base64, UTF-8 validation, and the chain hash
//
// The chain hash is the estate's, unchanged in arithmetic:  h = blake2b256(prev_hex ‖ body_bytes),
// where body_bytes are THE LITERAL ON-DISK BYTES OF THE ROW PRECEDING `,"prev":"` (blueprint §3.1,
// fusord.cpp chain_hash / Tape::put, convergence_tools/verify_chain.py). There is no canonicalization
// step in v0 and the word does not appear in this file.
//
// UTF-8 validation is new. fusord's `jesc` DELETES carriage returns and passes invalid UTF-8 through
// (fusord.cpp:177-186); §3.1 says that escaper is replaced, because a writer that drops a byte cannot
// be the truth. Validation here is the strict RFC 3629 form: no overlongs, no surrogates, no >U+10FFFF.
// =====================================================================================================
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include "blake2b.h"

namespace tapestry {

inline const char* const GENESIS =
    "0000000000000000000000000000000000000000000000000000000000000000";

// ---- hex --------------------------------------------------------------------------------------
inline std::string hex_of(const uint8_t* p, size_t n) {
    static const char* H = "0123456789abcdef";
    std::string s; s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) { s += H[p[i] >> 4]; s += H[p[i] & 15]; }
    return s;
}
inline int hex_nib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;   // accepted on read, never written
    return -1;
}
inline bool is_hex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s) if (hex_nib(c) < 0) return false;
    return true;
}

// ---- base64 (RFC 4648, padded) ----------------------------------------------------------------
// The carrier for a column value that is not valid UTF-8: §3.1 says such a value is carried base64
// with a type tag or refused with a typed reason. Base64 also keeps a shell literal safe, which is
// why the estate's transforms use it.
inline std::string b64_encode(const uint8_t* p, size_t n) {
    static const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o; o.reserve(((n + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 3 <= n; i += 3) {
        const uint32_t v = ((uint32_t)p[i] << 16) | ((uint32_t)p[i + 1] << 8) | (uint32_t)p[i + 2];
        o += A[(v >> 18) & 63]; o += A[(v >> 12) & 63]; o += A[(v >> 6) & 63]; o += A[v & 63];
    }
    if (i + 1 == n) {
        const uint32_t v = ((uint32_t)p[i] << 16);
        o += A[(v >> 18) & 63]; o += A[(v >> 12) & 63]; o += '='; o += '=';
    } else if (i + 2 == n) {
        const uint32_t v = ((uint32_t)p[i] << 16) | ((uint32_t)p[i + 1] << 8);
        o += A[(v >> 18) & 63]; o += A[(v >> 12) & 63]; o += A[(v >> 6) & 63]; o += '=';
    }
    return o;
}
inline std::string b64_encode(const std::string& s) {
    return b64_encode((const uint8_t*)s.data(), s.size());
}
inline bool b64_decode(const std::string& in, std::string& out) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    if (in.size() % 4 != 0) return false;
    out.clear(); out.reserve(in.size() / 4 * 3);
    for (size_t i = 0; i < in.size(); i += 4) {
        int q[4]; int pad = 0;
        for (int j = 0; j < 4; ++j) {
            const char c = in[i + j];
            if (c == '=') { if (i + 4 != in.size() || j < 2) return false; q[j] = 0; ++pad; }
            else { q[j] = val(c); if (q[j] < 0) return false; if (pad) return false; }
        }
        const uint32_t v = ((uint32_t)q[0] << 18) | ((uint32_t)q[1] << 12) |
                           ((uint32_t)q[2] << 6)  | (uint32_t)q[3];
        out += (char)((v >> 16) & 0xff);
        if (pad < 2) out += (char)((v >> 8) & 0xff);
        if (pad < 1) out += (char)(v & 0xff);
    }
    return true;
}

// ---- UTF-8 -------------------------------------------------------------------------------------
// Strict: returns false on an overlong form, a surrogate (U+D800..U+DFFF), a scalar above U+10FFFF,
// a truncated sequence or a stray continuation byte. `bad_at` receives the offset of the first bad
// byte so a refusal can name it.
inline bool utf8_valid(const char* p, size_t n, size_t* bad_at = nullptr) {
    const uint8_t* s = (const uint8_t*)p;
    size_t i = 0;
    while (i < n) {
        const uint8_t c = s[i];
        size_t need; uint32_t cp;
        if (c < 0x80)                       { i += 1; continue; }
        else if ((c & 0xE0) == 0xC0)        { need = 1; cp = c & 0x1Fu; }
        else if ((c & 0xF0) == 0xE0)        { need = 2; cp = c & 0x0Fu; }
        else if ((c & 0xF8) == 0xF0)        { need = 3; cp = c & 0x07u; }
        else                                { if (bad_at) *bad_at = i; return false; }
        if (i + need >= n) { if (bad_at) *bad_at = i; return false; }   // truncated sequence at the tail
        for (size_t j = 1; j <= need; ++j) {
            const uint8_t cc = s[i + j];
            if ((cc & 0xC0) != 0x80) { if (bad_at) *bad_at = i + j; return false; }
            cp = (cp << 6) | (uint32_t)(cc & 0x3Fu);
        }
        const bool overlong = (need == 1 && cp < 0x80u) ||
                              (need == 2 && cp < 0x800u) ||
                              (need == 3 && cp < 0x10000u);
        if (overlong)                        { if (bad_at) *bad_at = i; return false; }
        if (cp >= 0xD800u && cp <= 0xDFFFu)  { if (bad_at) *bad_at = i; return false; }
        if (cp > 0x10FFFFu)                  { if (bad_at) *bad_at = i; return false; }
        i += need + 1;
    }
    return true;
}
inline bool utf8_valid(const std::string& s, size_t* bad_at = nullptr) {
    return utf8_valid(s.data(), s.size(), bad_at);
}

// ---- the chain --------------------------------------------------------------------------------
// h = blake2b256(prev_hex ‖ body). `body` is the literal prefix of the on-disk row, up to but not
// including `,"prev":"`. Identical to fusord.cpp chain_hash and to verify_chain.py.
inline std::string chain_hash(const std::string& prev_hex, const std::string& body) {
    Blake2b b; b.init(32);
    b.update((const uint8_t*)prev_hex.data(), prev_hex.size());
    b.update((const uint8_t*)body.data(), body.size());
    uint8_t out[32]; b.finish(out);
    return hex_of(out, 32);
}

} // namespace tapestry
