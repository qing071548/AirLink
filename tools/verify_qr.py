"""
Independent QR decoder used to validate AirLink's in-house encoder.

It re-derives everything from the published spec rather than reusing the C++
code: format-info BCH, unmasking, zigzag placement, block de-interleaving and
Reed-Solomon syndrome checks. If this reproduces the original text, the symbol
is structurally sound and a phone camera will read it.

Usage: python verify_qr.py <png> <expected-text> [quiet-modules] [pixel-scale]
"""
import sys
import math

# ---------------------------------------------------------------- GF(256)
EXP = [0] * 512
LOG = [0] * 256


def _init_gf():
    x = 1
    for i in range(255):
        EXP[i] = x
        LOG[x] = i
        x <<= 1
        if x & 0x100:
            x ^= 0x11D
    for i in range(255, 512):
        EXP[i] = EXP[i - 255]


_init_gf()


def gmul(a, b):
    if a == 0 or b == 0:
        return 0
    return EXP[LOG[a] + LOG[b]]


# ---------------------------------------------------------------- tables
# level M: (ec codewords per block, [(block count, data codewords per block), ...])
SPEC_M = {
    1: (10, [(1, 16)]),
    2: (16, [(1, 28)]),
    3: (26, [(1, 44)]),
    4: (18, [(2, 32)]),
    5: (24, [(2, 43)]),
    6: (16, [(4, 27)]),
    7: (18, [(4, 31)]),
    8: (22, [(2, 38), (2, 39)]),
    9: (22, [(3, 36), (2, 37)]),
    10: (26, [(4, 43), (1, 44)]),
}

ALIGN = {
    1: [], 2: [6, 18], 3: [6, 22], 4: [6, 26], 5: [6, 30], 6: [6, 34],
    7: [6, 22, 38], 8: [6, 24, 42], 9: [6, 26, 46], 10: [6, 28, 50],
}


def mask_pred(mask, r, c):
    if mask == 0:
        return (r + c) % 2 == 0
    if mask == 1:
        return r % 2 == 0
    if mask == 2:
        return c % 3 == 0
    if mask == 3:
        return (r + c) % 3 == 0
    if mask == 4:
        return (r // 2 + c // 3) % 2 == 0
    if mask == 5:
        return (r * c) % 2 + (r * c) % 3 == 0
    if mask == 6:
        return ((r * c) % 2 + (r * c) % 3) % 2 == 0
    return ((r + c) % 2 + (r * c) % 3) % 2 == 0


def function_map(version):
    """Which modules are reserved for function patterns (not data)."""
    n = 17 + 4 * version
    fn = [[False] * n for _ in range(n)]

    def mark(r, c):
        if 0 <= r < n and 0 <= c < n:
            fn[r][c] = True

    # finders + separators (8x8 each, clipped at the border)
    for (br, bc) in ((0, 0), (0, n - 7), (n - 7, 0)):
        for dr in range(-1, 8):
            for dc in range(-1, 8):
                mark(br + dr, bc + dc)
    # timing
    for i in range(n):
        mark(6, i)
        mark(i, 6)
    # alignment
    cen = ALIGN[version]
    for i, ri in enumerate(cen):
        for j, ci in enumerate(cen):
            if (i, j) in ((0, 0), (0, len(cen) - 1), (len(cen) - 1, 0)):
                continue
            for dr in range(-2, 3):
                for dc in range(-2, 3):
                    mark(ri + dr, ci + dc)
    # format areas + dark module
    for i in range(9):
        mark(8, i)
        mark(i, 8)
    for i in range(8):
        mark(8, n - 1 - i)
        mark(n - 1 - i, 8)
    mark(n - 8, 8)
    # version info blocks
    if version >= 7:
        for a in range(6):
            for b in range(3):
                mark(a, n - 11 + b)
                mark(n - 11 + b, a)
    return fn


def read_format(m):
    """Recover the 5 data bits of the format information (EC level + mask)."""
    n = len(m)
    bits1 = 0
    order1 = [(8, i) for i in range(6)] + [(8, 7), (8, 8), (7, 8)] + [(14 - i, 8) for i in range(9, 15)]
    for k, (c, r) in enumerate(order1):
        if m[r][c]:
            bits1 |= 1 << k
    # the same 15 bits live mirrored near the other two finders
    bits2 = 0
    order2 = [(n - 1 - i, 8) for i in range(8)] + [(8, n - 15 + i) for i in range(8, 15)]
    for k, (c, r) in enumerate(order2):
        if m[r][c]:
            bits2 |= 1 << k

    def decode(raw):
        v = raw ^ 0x5412
        data = v >> 10
        d = v
        for i in range(14, 9, -1):
            if d & (1 << i):
                d ^= 0x537 << (i - 10)
        return data, d == 0

    d1, ok1 = decode(bits1)
    d2, ok2 = decode(bits2)
    if ok1 and ok2 and d1 != d2:
        raise ValueError("the two format-info copies disagree")
    if not (ok1 or ok2):
        raise ValueError("format info failed its BCH check")
    data = d1 if ok1 else d2
    ec = (data >> 3) & 0x3
    mask = data & 0x7
    return ec, mask


def extract_codewords(m, version):
    n = len(m)
    fn = function_map(version)
    bits = []
    upward = True
    right = n - 1
    while right >= 1:
        if right == 6:
            right = 5
        for vert in range(n):
            for j in range(2):
                c = right - j
                r = (n - 1 - vert) if upward else vert
                if not fn[r][c]:
                    bits.append(1 if m[r][c] else 0)
        upward = not upward
        right -= 2
    out = bytearray()
    for i in range(0, len(bits) - 7, 8):
        byte = 0
        for b in bits[i:i + 8]:
            byte = (byte << 1) | b
        out.append(byte)
    return out


def deinterleave(stream, version):
    ec, groups = SPEC_M[version]
    sizes = []
    for count, dlen in groups:
        sizes.extend([dlen] * count)
    nblocks = len(sizes)
    blocks = [bytearray() for _ in range(nblocks)]
    pos = 0
    for i in range(max(sizes)):
        for b in range(nblocks):
            if i < sizes[b]:
                blocks[b].append(stream[pos])
                pos += 1
    eccs = [bytearray() for _ in range(nblocks)]
    for i in range(ec):
        for b in range(nblocks):
            eccs[b].append(stream[pos])
            pos += 1
    return blocks, eccs


def syndromes_zero(block, ecc):
    """A valid RS codeword evaluates to zero at alpha^0 .. alpha^(ec-1)."""
    ec = len(ecc)
    full = list(block) + list(ecc)
    for i in range(ec):
        s = 0
        for coef in full:
            s = gmul(s, EXP[i]) ^ coef
        if s != 0:
            return False
    return True


def decode_png(path):
    from PIL import Image
    img = Image.open(path).convert("L")
    return img


def main():
    if len(sys.argv) < 3:
        print("usage: verify_qr.py <png> <expected> [quiet] [scale]")
        return 2
    png, expected = sys.argv[1], sys.argv[2]
    quiet = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    scale_arg = sys.argv[4] if len(sys.argv) > 4 else "8"

    img = decode_png(png)
    dim = img.size[0]

    if "." in scale_arg:
        # Non-integer scale (a screenshot of the real UI): work out the module
        # size from the total dimension and sample each module's centre.
        scale = float(scale_arg)
        if quiet == 0:
            n = int(round(dim / scale))
            quiet_px = 0.0
        else:
            n = int(round(dim / scale)) - 2 * quiet
            quiet_px = quiet * scale
    else:
        scale = float(scale_arg)
        if quiet == 0:
            n = int(round(dim / scale))
            quiet_px = 0.0
        else:
            n = int(round(dim / scale)) - 2 * quiet
            quiet_px = quiet * scale

    if (n - 17) % 4 != 0 or n < 21:
        print(f"FAIL: {n} modules is not a valid QR size")
        return 1
    version = (n - 17) // 4
    print(f"dimension={dim}px  modules={n}x{n}  version={version}  scale={scale}")

    px = img.load()
    m = [[0] * n for _ in range(n)]
    for r in range(n):
        for c in range(n):
            x = int(quiet_px + (c + 0.5) * scale)
            y = int(quiet_px + (r + 0.5) * scale)
            x = max(0, min(dim - 1, x))
            y = max(0, min(dim - 1, y))
            m[r][c] = 1 if px[x, y] < 128 else 0

    ec_level, mask = read_format(m)
    print(f"format: ec_bits={ec_level:02b} mask={mask}")
    if ec_level != 0:
        print("FAIL: expected error-correction level M (bits 00)")
        return 1

    # Undo the mask over the data region first - codewords are only meaningful
    # once the mask is removed.
    fn = function_map(version)
    unmasked = [row[:] for row in m]
    for r in range(n):
        for c in range(n):
            if not fn[r][c] and mask_pred(mask, r, c):
                unmasked[r][c] ^= 1

    stream = extract_codewords(unmasked, version)
    blocks, eccs = deinterleave(stream, version)

    bad = [i for i in range(len(blocks)) if not syndromes_zero(blocks[i], eccs[i])]
    print(f"blocks={len(blocks)}  RS syndrome check: {'PASS' if not bad else 'FAIL ' + str(bad)}")
    if bad:
        return 1

    payload = bytearray()
    for b in blocks:
        payload.extend(b)

    bitpos = 0

    def take(k):
        nonlocal bitpos
        v = 0
        for _ in range(k):
            byte = payload[bitpos >> 3]
            v = (v << 1) | ((byte >> (7 - (bitpos & 7))) & 1)
            bitpos += 1
        return v

    mode = take(4)
    if mode != 0x4:
        print(f"FAIL: mode indicator {mode} is not byte mode (0100)")
        return 1
    count_bits = 8 if version <= 9 else 16
    length = take(count_bits)
    text = bytes(take(8) for _ in range(length)).decode("utf-8", "replace")

    print(f"payload ({length} bytes): {text!r}")
    if text == expected:
        print("RESULT: OK - decoded text matches")
        return 0
    print(f"RESULT: MISMATCH - expected {expected!r}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
