// =====================================================================================================
// TAPESTRY · src/core/blake2b.h · BLAKE2b-256 (RFC 7693, unkeyed)
//
// Reused verbatim in algorithm from C:\fusor1\converge\src\fusord.cpp §2 (the estate's one hash
// family), lifted into its own header so the tape, the transactor and the tools share one copy and
// one set of test vectors. §12 of the blueprint: "Blake2b, chain_hash ... holds".
//
// The only changes from fusord: the struct is namespaced, `hex_of` moved to bytes.h, and one-shot
// helpers added. The compression function, the IV, SIGMA and the buffering rule are byte-for-byte
// the estate's, so every chain receipt written by fusord verifies under this header.
// =====================================================================================================
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <algorithm>

namespace tapestry {

struct Blake2b {
    uint64_t h[8]; uint64_t t[2]; uint8_t buf[128]; size_t buflen; size_t outlen;

    static const uint64_t IV[8];
    static const uint8_t  SIGMA[12][16];

    static inline uint64_t rotr64(uint64_t x, unsigned n) { return (x >> n) | (x << (64u - n)); }
    static inline uint64_t load64(const uint8_t* p) {
        uint64_t v = 0; for (int i = 7; i >= 0; --i) v = (v << 8) | (uint64_t)p[i]; return v;
    }
    static inline void G(uint64_t* v, const uint64_t* m, uint8_t x, uint8_t y,
                         int a, int b, int c, int d) {
        v[a] = v[a] + v[b] + m[x]; v[d] = rotr64(v[d] ^ v[a], 32);
        v[c] = v[c] + v[d];        v[b] = rotr64(v[b] ^ v[c], 24);
        v[a] = v[a] + v[b] + m[y]; v[d] = rotr64(v[d] ^ v[a], 16);
        v[c] = v[c] + v[d];        v[b] = rotr64(v[b] ^ v[c], 63);
    }
    void init(size_t out) {
        outlen = out;
        for (int i = 0; i < 8; ++i) h[i] = IV[i];
        h[0] ^= 0x01010000ull ^ (uint64_t)out;   // param block: fanout=1, depth=1, keylen=0, outlen
        t[0] = t[1] = 0; buflen = 0;
        std::memset(buf, 0, sizeof(buf));
    }
    void compress(const uint8_t* block, bool last) {
        uint64_t m[16], v[16];
        for (int i = 0; i < 16; ++i) m[i] = load64(block + 8 * i);
        for (int i = 0; i < 8; ++i) { v[i] = h[i]; v[i + 8] = IV[i]; }
        v[12] ^= t[0]; v[13] ^= t[1];
        if (last) v[14] = ~v[14];
        for (int r = 0; r < 12; ++r) {
            const uint8_t* s = SIGMA[r];
            G(v, m, s[0],  s[1],  0, 4,  8, 12);
            G(v, m, s[2],  s[3],  1, 5,  9, 13);
            G(v, m, s[4],  s[5],  2, 6, 10, 14);
            G(v, m, s[6],  s[7],  3, 7, 11, 15);
            G(v, m, s[8],  s[9],  0, 5, 10, 15);
            G(v, m, s[10], s[11], 1, 6, 11, 12);
            G(v, m, s[12], s[13], 2, 7,  8, 13);
            G(v, m, s[14], s[15], 3, 4,  9, 14);
        }
        for (int i = 0; i < 8; ++i) h[i] ^= v[i] ^ v[i + 8];
    }
    void update(const uint8_t* p, size_t n) {
        while (n > 0) {
            if (buflen == 128) {   // a full block is compressed only when MORE data follows it
                t[0] += 128; if (t[0] < 128) ++t[1];
                compress(buf, false); buflen = 0;
            }
            const size_t take = (std::min)((size_t)128 - buflen, n);
            std::memcpy(buf + buflen, p, take); buflen += take; p += take; n -= take;
        }
    }
    void update(const std::string& s) { update((const uint8_t*)s.data(), s.size()); }
    void finish(uint8_t* out) {
        t[0] += (uint64_t)buflen; if (t[0] < (uint64_t)buflen) ++t[1];
        std::memset(buf + buflen, 0, 128 - buflen);
        compress(buf, true);
        for (size_t i = 0; i < outlen; ++i) out[i] = (uint8_t)(h[i / 8] >> (8 * (i % 8)));
    }
};

// The two constant tables. `inline` so the header carries its own definition under C++17 and every
// translation unit in the estate links against one copy.
inline const uint64_t Blake2b::IV[8] = {
    0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
    0x510e527fade682d1ull, 0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull };
inline const uint8_t Blake2b::SIGMA[12][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15},
    {14,10, 4, 8, 9,15,13, 6, 1,12, 0, 2,11, 7, 5, 3},
    {11, 8,12, 0, 5, 2,15,13,10,14, 3, 6, 7, 1, 9, 4},
    { 7, 9, 3, 1,13,12,11,14, 2, 6, 5,10, 4, 0,15, 8},
    { 9, 0, 5, 7, 2, 4,10,15,14, 1,11,12, 6, 8, 3,13},
    { 2,12, 6,10, 0,11, 8, 3, 4,13, 7, 5,15,14, 1, 9},
    {12, 5, 1,15,14,13, 4,10, 0, 7, 6, 3, 9, 2, 8,11},
    {13,11, 7,14,12, 1, 3, 9, 5, 0,15, 4, 8, 6, 2,10},
    { 6,15,14, 9,11, 3, 0, 8,12, 2,13, 7, 1, 4,10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5,15,11, 9,14, 3,12,13, 0},
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15},
    {14,10, 4, 8, 9,15,13, 6, 1,12, 0, 2,11, 7, 5, 3} };

// One-shot: blake2b-256 of a byte range into 32 raw bytes.
inline void blake2b256_raw(const void* p, size_t n, uint8_t out[32]) {
    Blake2b b; b.init(32); b.update((const uint8_t*)p, n); b.finish(out);
}

} // namespace tapestry
