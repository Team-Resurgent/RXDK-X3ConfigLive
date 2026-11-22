"""Put a Config Live XBE into an X3 BIOS image (replacing the one in it).

usage: python -I bios/pack_bios.py <x3 bios.bin> <ConfigLive.xbe> <out bios.bin>
       python -I bios/pack_bios.py --extract <x3 bios.bin> <out.xbe>

How the X3 kernel finds Config Live (per 1 MB image; a 2 MB image is the same 1 MB twice):
  0x51F0  u32 version (must stay 0xCDE: the kernel checks it), u32 compressed size,
          u32 uncompressed size, 'l33t'
  0x5200  RC4 (key below, from the X3 kernel .data at 0x80045FF0) of a zlib stream of the XBE
The slot ends at 0x73EEE, where an unidentified high-entropy region starts, so the compressed
stream must fit in 0x6ECEE bytes. Only bytes 0x51F0..0x73EEE change; the boot front-end
(xcodes, 2BL), the kernel and everything else stay byte-identical (checked below).

zlib level 9 is tried first; if that does not fit, 7-Zip's deflate (-mx=9 -mfb=258 -mpass=15,
about 4% smaller) is wrapped in a zlib header + Adler-32. Both are plain deflate streams, and the
result is always decompressed again and compared with the XBE.
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

KEY = bytes.fromhex('17a8d084a3867c406e6c2aadfb7c8671')
MB = 0x100000
HDR = 0x51F0
STREAM = 0x5200
SLOT_END = 0x73EEE
SLOT = SLOT_END - STREAM
VERSION = 0xCDE
def find_7zip():
    """SEVENZIP, else 7z / 7zz / 7za on PATH, else the Windows default install."""
    if os.environ.get('SEVENZIP'):
        return os.environ['SEVENZIP']
    for name in ('7z', '7zz', '7za'):
        p = shutil.which(name)
        if p:
            return p
    return 'C:/Program Files/7-Zip/7z.exe'


SEVENZIP = find_7zip()


def rc4(key, data):
    S = list(range(256))
    j = 0
    for i in range(256):
        j = (j + S[i] + key[i % len(key)]) & 0xFF
        S[i], S[j] = S[j], S[i]
    out = bytearray(len(data))
    i = j = 0
    for n, b in enumerate(data):
        i = (i + 1) & 0xFF
        j = (j + S[i]) & 0xFF
        S[i], S[j] = S[j], S[i]
        out[n] = b ^ S[(S[i] + S[j]) & 0xFF]
    return bytes(out)


def extract(img):
    ver, cs, us, magic = struct.unpack_from('<III4s', img, HDR)
    if magic != b'l33t' or ver != VERSION:
        sys.exit(f'no Config Live header at 0x{HDR:X} (magic {magic!r}, version 0x{ver:X})')
    x = zlib.decompress(rc4(KEY, img[STREAM:STREAM + cs]))
    if len(x) != us or x[:4] != b'XBEH':
        sys.exit('Config Live stream does not decode to an XBE')
    return x


def deflate_7z(xbe):
    """7-Zip's deflate as a zlib stream (gzip container -> raw deflate -> zlib wrapper)."""
    if not os.path.isfile(SEVENZIP):
        return None
    with tempfile.TemporaryDirectory() as d:
        src, gz = os.path.join(d, 'x.xbe'), os.path.join(d, 'x.gz')
        open(src, 'wb').write(xbe)
        subprocess.run([SEVENZIP, 'a', '-tgzip', '-mx=9', '-mfb=258', '-mpass=15', gz, src],
                       check=True, stdout=subprocess.DEVNULL)
        g = open(gz, 'rb').read()
    if g[:3] != b'\x1f\x8b\x08':
        sys.exit('7-Zip did not write a gzip file')
    flg, p = g[3], 10
    if flg & 4:
        p += 2 + struct.unpack_from('<H', g, p)[0]
    for bit in (8, 16):  # FNAME, FCOMMENT: zero-terminated
        if flg & bit:
            p = g.index(b'\0', p) + 1
    if flg & 2:
        p += 2
    raw = g[p:-8]
    return b'\x78\xda' + raw + struct.pack('>I', zlib.adler32(xbe))


def pack(img1, xbe):
    cands = [('zlib -9', zlib.compress(xbe, 9))]
    if len(cands[0][1]) > SLOT:
        z7 = deflate_7z(xbe)
        if z7 is None:
            sys.exit(f'zlib -9 gives 0x{len(cands[0][1]):X} bytes, slot is 0x{SLOT:X}, and no 7-Zip '
                     f'was found (install 7-Zip / p7zip, or set SEVENZIP)')
        cands.append(('7-Zip deflate', z7))
    how, z = min(cands, key=lambda c: len(c[1]))
    if zlib.decompress(z) != xbe:
        sys.exit(f'{how}: stream does not decompress to the XBE')
    if len(z) > SLOT:
        sys.exit(f'compressed XBE is 0x{len(z):X} bytes, the flash slot holds 0x{SLOT:X}')
    out = bytearray(img1)
    struct.pack_into('<III4s', out, HDR, VERSION, len(z), len(xbe), b'l33t')
    out[STREAM:SLOT_END] = rc4(KEY, z) + b'\xff' * (SLOT - len(z))
    return bytes(out), how, len(z)


def main(a):
    if a and a[0] == '--extract' and len(a) == 3:
        open(a[2], 'wb').write(extract(open(a[1], 'rb').read()[:MB]))
        return
    if len(a) != 3:
        sys.exit(__doc__)
    bios, xbe = open(a[0], 'rb').read(), open(a[1], 'rb').read()
    if len(bios) not in (MB, 2 * MB):
        sys.exit(f'{a[0]}: expected a 1 MB or 2 MB image, got {len(bios)} bytes')
    halves = [bios[i:i + MB] for i in range(0, len(bios), MB)]
    if any(h != halves[0] for h in halves):
        sys.exit(f'{a[0]}: the 1 MB halves differ')
    extract(halves[0])  # the input really is an X3 image with Config Live in it
    new1, how, zlen = pack(halves[0], xbe)
    # checks: only the Config Live header + slot changed, and the new image gives back the XBE
    if new1[:HDR] != halves[0][:HDR] or new1[SLOT_END:] != halves[0][SLOT_END:]:
        sys.exit('internal error: bytes outside the Config Live slot changed')
    if extract(new1) != xbe:
        sys.exit('internal error: the new image does not decode to the XBE')
    open(a[2], 'wb').write(new1 * len(halves))
    print(f'{a[2]}: Config Live 0x{len(xbe):X} bytes -> {how} 0x{zlen:X} of 0x{SLOT:X} '
          f'(0x{SLOT - zlen:X} free); rest of the image unchanged')


if __name__ == '__main__':
    main(sys.argv[1:])
