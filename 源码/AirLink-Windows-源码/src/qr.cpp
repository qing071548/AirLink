// qr.cpp - minimal QR code encoder (byte mode, error-correction level M).
// Written in-house so the app stays a single dependency-free executable.
#include "qr.h"
#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace {

// ---------------------------------------------------------------- GF(256)
struct GF256 {
    unsigned char exp[512];
    unsigned char log[256];
    GF256() {
        int x = 1;
        for (int i = 0; i < 255; i++) {
            exp[i] = (unsigned char)x;
            log[x] = (unsigned char)i;
            x <<= 1;
            if (x & 0x100) x ^= 0x11D;
        }
        for (int i = 255; i < 512; i++) exp[i] = exp[i - 255];
        log[0] = 0;
    }
};
const GF256 GF;

inline unsigned char gmul(unsigned char a, unsigned char b) {
    if (!a || !b) return 0;
    return GF.exp[(int)GF.log[a] + (int)GF.log[b]];
}

// Generator polynomial for `n` error-correction codewords, highest degree first.
std::vector<unsigned char> rs_generator(int n) {
    std::vector<unsigned char> g(1, 1);
    for (int i = 0; i < n; i++) {
        std::vector<unsigned char> ng(g.size() + 1, 0);
        unsigned char a = GF.exp[i];
        for (size_t j = 0; j < g.size(); j++) {
            ng[j] ^= g[j];                 // multiplied by x
            ng[j + 1] ^= gmul(g[j], a);    // multiplied by a
        }
        g.swap(ng);
    }
    return g;
}

std::vector<unsigned char> rs_ecc(const std::vector<unsigned char>& data, int ecLen) {
    std::vector<unsigned char> gen = rs_generator(ecLen);
    std::vector<unsigned char> rem(ecLen, 0);
    for (size_t i = 0; i < data.size(); i++) {
        unsigned char factor = data[i] ^ rem[0];
        for (int j = 0; j < ecLen - 1; j++) rem[j] = rem[j + 1];
        rem[ecLen - 1] = 0;
        if (factor) {
            for (int j = 0; j < ecLen; j++) rem[j] ^= gmul(gen[j + 1], factor);
        }
    }
    return rem;
}

// ---------------------------------------------------------------- layout tables
// Level M block structure per version: ec codewords per block, then up to two
// groups of (block count, data codewords per block).
struct VerSpec { int ec, b1, d1, b2, d2; };
const VerSpec kSpecM[11] = {
    { 0, 0,  0, 0,  0 },
    {10, 1, 16, 0,  0 },   // 1
    {16, 1, 28, 0,  0 },   // 2
    {26, 1, 44, 0,  0 },   // 3
    {18, 2, 32, 0,  0 },   // 4
    {24, 2, 43, 0,  0 },   // 5
    {16, 4, 27, 0,  0 },   // 6
    {18, 4, 31, 0,  0 },   // 7
    {22, 2, 38, 2, 39 },   // 8
    {22, 3, 36, 2, 37 },   // 9
    {26, 4, 43, 1, 44 },   // 10
};

// Alignment pattern centre coordinates.
int align_centres(int v, int* out) {
    static const int a2[] = { 6, 18 }, a3[] = { 6, 22 }, a4[] = { 6, 26 };
    static const int a5[] = { 6, 30 }, a6[] = { 6, 34 };
    static const int a7[] = { 6, 22, 38 }, a8[] = { 6, 24, 42 };
    static const int a9[] = { 6, 26, 46 }, a10[] = { 6, 28, 50 };
    const int* src = nullptr; int n = 0;
    switch (v) {
    case 2: src = a2; n = 2; break;
    case 3: src = a3; n = 2; break;
    case 4: src = a4; n = 2; break;
    case 5: src = a5; n = 2; break;
    case 6: src = a6; n = 2; break;
    case 7: src = a7; n = 3; break;
    case 8: src = a8; n = 3; break;
    case 9: src = a9; n = 3; break;
    case 10: src = a10; n = 3; break;
    default: break;
    }
    for (int i = 0; i < n; i++) out[i] = src[i];
    return n;
}

// ---------------------------------------------------------------- bit buffer
struct Bits {
    std::vector<unsigned char> b;
    int len = 0;
    void push(unsigned v, int n) {
        for (int i = n - 1; i >= 0; i--) {
            if ((len & 7) == 0) b.push_back(0);
            if ((v >> i) & 1u) b[len >> 3] |= (unsigned char)(0x80u >> (len & 7));
            len++;
        }
    }
};

struct Canvas {
    int n;
    std::vector<unsigned char> m;   // 1 = dark
    std::vector<unsigned char> fn;  // 1 = function module (not masked, not data)

    Canvas(int size) : n(size), m(size * size, 0), fn(size * size, 0) {}
    void set(int col, int row, bool dark) {
        if (col < 0 || row < 0 || col >= n || row >= n) return;
        m[row * n + col] = dark ? 1 : 0;
        fn[row * n + col] = 1;
    }
    bool isFn(int col, int row) const { return fn[row * n + col] != 0; }
};

void place_finder(Canvas& c, int cx, int cy) {
    for (int dr = -4; dr <= 4; dr++) {
        for (int dc = -4; dc <= 4; dc++) {
            int d = std::max(std::abs(dr), std::abs(dc));
            c.set(cx + dc, cy + dr, d != 2 && d != 4);
        }
    }
}

void place_alignment(Canvas& c, int cx, int cy) {
    for (int dr = -2; dr <= 2; dr++) {
        for (int dc = -2; dc <= 2; dc++) {
            int d = std::max(std::abs(dr), std::abs(dc));
            c.set(cx + dc, cy + dr, d != 1);
        }
    }
}

// 15-bit format information: 2 EC bits + 3 mask bits, BCH(15,5), masked.
unsigned format_bits(int mask) {
    int v = (0 << 3) | mask;              // 0b00 = level M
    int d = v << 10;
    for (int i = 14; i >= 10; i--)
        if (d & (1 << i)) d ^= 0x537 << (i - 10);
    return (unsigned)((((v << 10) | d) ^ 0x5412) & 0x7FFF);
}

// 18-bit version information, BCH(18,6).
unsigned version_bits(int v) {
    int d = v << 12;
    for (int i = 17; i >= 12; i--)
        if (d & (1 << i)) d ^= 0x1F25 << (i - 12);
    return (unsigned)(((v << 12) | d) & 0x3FFFF);
}

void draw_format(Canvas& c, unsigned bits) {
    int n = c.n;
    // first copy, around the top-left finder
    for (int i = 0; i <= 5; i++) c.set(8, i, (bits >> i) & 1);
    c.set(8, 7, (bits >> 6) & 1);
    c.set(8, 8, (bits >> 7) & 1);
    c.set(7, 8, (bits >> 8) & 1);
    for (int i = 9; i < 15; i++) c.set(14 - i, 8, (bits >> i) & 1);
    // second copy, split between the other two finders
    for (int i = 0; i < 8; i++) c.set(n - 1 - i, 8, (bits >> i) & 1);
    for (int i = 8; i < 15; i++) c.set(8, n - 15 + i, (bits >> i) & 1);
    c.set(8, n - 8, true);                 // the mandatory dark module
}

void draw_version(Canvas& c, int v) {
    unsigned bits = version_bits(v);
    int n = c.n;
    for (int i = 0; i < 18; i++) {
        int bit = (bits >> i) & 1;
        int a = i / 3, b = i % 3;          // 6x3 blocks
        c.set(n - 11 + b, a, bit);
        c.set(a, n - 11 + b, bit);
    }
}

bool mask_at(int mask, int row, int col) {
    switch (mask) {
    case 0: return ((row + col) & 1) == 0;
    case 1: return (row & 1) == 0;
    case 2: return col % 3 == 0;
    case 3: return (row + col) % 3 == 0;
    case 4: return ((row / 2) + (col / 3)) % 2 == 0;
    case 5: return ((row * col) % 2) + ((row * col) % 3) == 0;
    case 6: return (((row * col) % 2) + ((row * col) % 3)) % 2 == 0;
    default: return (((row + col) % 2) + ((row * col) % 3)) % 2 == 0;
    }
}

int penalty(const Canvas& c) {
    int n = c.n, score = 0;
    // rule 1: runs of five or more identical modules
    for (int pass = 0; pass < 2; pass++) {
        for (int a = 0; a < n; a++) {
            int run = 1;
            for (int b = 1; b < n; b++) {
                int prev = pass ? c.m[(b - 1) * n + a] : c.m[a * n + (b - 1)];
                int cur = pass ? c.m[b * n + a] : c.m[a * n + b];
                if (cur == prev) {
                    run++;
                } else {
                    if (run >= 5) score += 3 + (run - 5);
                    run = 1;
                }
            }
            if (run >= 5) score += 3 + (run - 5);
        }
    }
    // rule 2: 2x2 blocks of one colour
    for (int y = 0; y + 1 < n; y++) {
        for (int x = 0; x + 1 < n; x++) {
            int v = c.m[y * n + x];
            if (v == c.m[y * n + x + 1] && v == c.m[(y + 1) * n + x] && v == c.m[(y + 1) * n + x + 1])
                score += 3;
        }
    }
    // rule 3: finder-like 1:1:3:1:1 patterns with four light modules beside them
    static const int pat[11] = { 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0 };
    auto match_at = [&](int row, int col, int dr, int dc) {
        for (int i = 0; i < 11; i++) {
            int r = row + dr * i, cc = col + dc * i;
            if (r < 0 || cc < 0 || r >= n || cc >= n) return false;
            if ((c.m[r * n + cc] != 0) != (pat[i] != 0)) return false;
        }
        return true;
    };
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            if (match_at(y, x, 0, 1)) score += 40;
            if (match_at(y, x, 1, 0)) score += 40;
        }
    }
    // rule 4: overall dark/light balance
    int dark = 0;
    for (int i = 0; i < n * n; i++) dark += c.m[i] ? 1 : 0;
    int pct = dark * 100 / (n * n);
    score += std::abs(pct - 50) / 5 * 10;
    return score;
}

} // namespace

std::vector<unsigned char> qr_encode(const std::string& text, int* outSize) {
    if (outSize) *outSize = 0;
    if (text.empty() || text.size() > 200) return {};

    // ---- pick the smallest version that fits
    int version = 0, dataCodewords = 0;
    for (int v = 1; v <= 10; v++) {
        const VerSpec& s = kSpecM[v];
        int total = s.b1 * s.d1 + s.b2 * s.d2;
        int countBits = (v <= 9) ? 8 : 16;
        int usableBits = total * 8 - 4 - countBits;
        if ((int)text.size() * 8 <= usableBits) {
            version = v;
            dataCodewords = total;
            break;
        }
    }
    if (!version) return {};

    const VerSpec& sp = kSpecM[version];
    int countBits = (version <= 9) ? 8 : 16;

    // ---- data bit stream
    Bits bb;
    bb.push(0x4, 4);                               // byte mode
    bb.push((unsigned)text.size(), countBits);
    for (unsigned char ch : text) bb.push(ch, 8);

    int capBits = dataCodewords * 8;
    int term = std::min(4, capBits - bb.len);
    if (term > 0) bb.push(0, term);
    while (bb.len & 7) bb.push(0, 1);
    for (int pad = 0xEC; bb.len < capBits; pad ^= 0xEC ^ 0x11) bb.push((unsigned)pad, 8);

    // ---- split into blocks, compute ECC, interleave
    std::vector<std::vector<unsigned char>> blocks, eccs;
    int pos = 0;
    for (int g = 0; g < 2; g++) {
        int count = g == 0 ? sp.b1 : sp.b2;
        int dlen = g == 0 ? sp.d1 : sp.d2;
        for (int i = 0; i < count; i++) {
            std::vector<unsigned char> blk(bb.b.begin() + pos, bb.b.begin() + pos + dlen);
            pos += dlen;
            blocks.push_back(blk);
            eccs.push_back(rs_ecc(blk, sp.ec));
        }
    }
    std::vector<unsigned char> stream;
    size_t maxData = 0;
    for (auto& b : blocks) maxData = std::max(maxData, b.size());
    for (size_t i = 0; i < maxData; i++)
        for (auto& b : blocks) if (i < b.size()) stream.push_back(b[i]);
    for (int i = 0; i < sp.ec; i++)
        for (auto& e : eccs) stream.push_back(e[i]);

    // ---- paint everything except the data
    int n = 17 + 4 * version;
    Canvas c(n);
    place_finder(c, 3, 3);
    place_finder(c, n - 4, 3);
    place_finder(c, 3, n - 4);
    for (int i = 8; i < n - 8; i++) {                 // timing patterns
        bool dark = (i & 1) == 0;
        c.set(6, i, dark);
        c.set(i, 6, dark);
    }
    int centres[4], nc = align_centres(version, centres);
    for (int i = 0; i < nc; i++) {
        for (int j = 0; j < nc; j++) {
            bool corner = (i == 0 && j == 0) || (i == 0 && j == nc - 1) || (i == nc - 1 && j == 0);
            if (corner) continue;
            place_alignment(c, centres[i], centres[j]);
        }
    }
    // reserve the format areas (real bits are written after masking)
    for (int i = 0; i <= 8; i++) { c.set(8, i, 0); c.set(i, 8, 0); }
    for (int i = 0; i < 8; i++) { c.set(n - 1 - i, 8, 0); c.set(8, n - 1 - i, 0); }
    c.set(8, n - 8, true);
    if (version >= 7) draw_version(c, version);

    // ---- zigzag data placement
    int bitIdx = 0;
    int totalBits = (int)stream.size() * 8;
    bool upward = true;
    for (int right = n - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;                    // skip the vertical timing column
        for (int vert = 0; vert < n; vert++) {
            for (int j = 0; j < 2; j++) {
                int col = right - j;
                int row = upward ? (n - 1 - vert) : vert;
                if (c.isFn(col, row)) continue;
                bool bit = false;
                if (bitIdx < totalBits)
                    bit = ((stream[bitIdx >> 3] >> (7 - (bitIdx & 7))) & 1) != 0;
                c.m[row * n + col] = bit ? 1 : 0;
                bitIdx++;
            }
        }
        upward = !upward;
    }

    // ---- choose the mask with the lowest penalty
    int bestMask = 0, bestScore = -1;
    std::vector<unsigned char> best;
    for (int mask = 0; mask < 8; mask++) {
        Canvas t = c;
        for (int row = 0; row < n; row++) {
            for (int col = 0; col < n; col++) {
                if (c.fn[row * n + col]) continue;
                if (mask_at(mask, row, col)) t.m[row * n + col] ^= 1;
            }
        }
        draw_format(t, format_bits(mask));
        int s = penalty(t);
        if (bestScore < 0 || s < bestScore) { bestScore = s; bestMask = mask; best = t.m; }
    }

    if (outSize) *outSize = n;
    return best;
}
