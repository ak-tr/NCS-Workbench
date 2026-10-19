// SHA-256 (FIPS 180-4) for session snapshots. Pure C++, no Windows headers:
// XP before SP3 has no SHA-256 in CryptoAPI.

#pragma once

#include <cstddef>
#include <cstdint>

#include "xpstd.h"

class Sha256 {
public:
    Sha256() { Reset(); }

    void Reset()
    {
        static const uint32_t init[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
        for (int i = 0; i < 8; ++i)
            h_[i] = init[i];
        length_ = 0;
        used_ = 0;
    }

    void Update(const void* data, size_t size)
    {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        length_ += size;
        while (size) {
            size_t n = 64 - used_ < size ? 64 - used_ : size;
            for (size_t i = 0; i < n; ++i)
                block_[used_ + i] = p[i];
            used_ += n;
            p += n;
            size -= n;
            if (used_ == 64) {
                Compress();
                used_ = 0;
            }
        }
    }

    // 64 lowercase hex digits. Resets the hash.
    String HexDigest()
    {
        uint64_t bits = length_ * 8;
        unsigned char pad = 0x80;
        Update(&pad, 1);
        unsigned char zero = 0;
        while (used_ != 56)
            Update(&zero, 1);
        unsigned char len[8];
        for (int i = 0; i < 8; ++i)
            len[i] = (unsigned char)(bits >> (56 - 8 * i));
        Update(len, 8);

        static const char hex[] = "0123456789abcdef";
        String out;
        for (int i = 0; i < 8; ++i) {
            for (int shift = 28; shift >= 0; shift -= 4)
                out += hex[(h_[i] >> shift) & 15];
        }
        Reset();
        return out;
    }

private:
    static uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void Compress()
    {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
        };
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t)block_[4 * i] << 24 | (uint32_t)block_[4 * i + 1] << 16 |
                   (uint32_t)block_[4 * i + 2] << 8 | block_[4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = h + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h_[0] += a;
        h_[1] += b;
        h_[2] += c;
        h_[3] += d;
        h_[4] += e;
        h_[5] += f;
        h_[6] += g;
        h_[7] += h;
    }

    uint32_t h_[8];
    uint64_t length_;
    unsigned char block_[64];
    size_t used_;
};

inline String Sha256Hex(const char* data, size_t size)
{
    Sha256 s;
    s.Update(data, size);
    return s.HexDigest();
}
