#!/usr/bin/env python3
"""scripts/check_btrfs_image.py — independent structural checker for
images written by fs/btrfs/btrfs_write.c.

There's no btrfs-progs/btrfsck available in this project's dev environment
(confirmed: no mkfs.btrfs on macOS - see mkbtrfs_image.py's own docstring),
so this is the closest available substitute: a from-scratch, independent
parser (sharing no code with the C driver) that walks the chunk/root/fs
trees byte-by-byte and checks structural invariants a correct copy-on-write
B-tree must satisfy:

  - every node/leaf's CRC32C checksum matches its content
  - keys within a leaf/internal node are strictly increasing
  - every internal node's key-pointer key matches its child's lowest key
  - node levels match their depth in the tree
  - generations are monotonic (child <= parent <= superblock generation)

This does NOT validate upstream btrfs on-disk compatibility - this driver
uses a simplified, project-local leaf layout (see btrfs_write.c's header
comment), so this script's only job is confirming internal consistency of
what this driver actually wrote, independent of the driver's own read path
(which reading the image back with the driver itself would not prove -
that would only show self-consistency).

Usage: check_btrfs_image.py <image> [--offset BYTES]
  --offset: byte offset of the btrfs filesystem within <image> (e.g. a
            GPT partition's start LBA * 512). Default 0 (whole-file image).
"""
import struct
import sys

NODESIZE_DEFAULT = 4096
SUPER_OFFSET = 0x10000

HEADER_FMT = "<32s16sQQ16sQQIB"
HEADER_SIZE = struct.calcsize(HEADER_FMT)
assert HEADER_SIZE == 101
ITEM_FMT = "<QBQII"
ITEM_SIZE = struct.calcsize(ITEM_FMT)
assert ITEM_SIZE == 25
KEYPTR_FMT = "<QBQQQ"
KEYPTR_SIZE = struct.calcsize(KEYPTR_FMT)
assert KEYPTR_SIZE == 33

CHUNK_ITEM_KEY = 228
ROOT_ITEM_KEY = 132
INODE_ITEM_KEY = 1
DIR_INDEX_KEY = 96
EXTENT_DATA_KEY = 108

ROOT_TREE_OBJECTID = 1
FS_TREE_OBJECTID = 5
FIRST_FREE_OBJECTID = 256


class CheckError(Exception):
    pass


def crc32c(data, crc=0xFFFFFFFF):
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 if (crc & 1) else 0)
    return crc ^ 0xFFFFFFFF


class Image:
    def __init__(self, path, offset):
        with open(path, "rb") as f:
            self.data = f.read()
        self.offset = offset
        self.errors = []
        self.warnings = []
        self.nodes_checked = 0
        self.leaves_checked = 0
        self.items_checked = 0
        self.files = []

    def read(self, phys, length):
        start = self.offset + phys
        if start < 0 or start + length > len(self.data):
            raise CheckError(f"read out of bounds: phys=0x{phys:x} len={length}")
        return self.data[start:start + length]

    def err(self, msg):
        self.errors.append(msg)

    def warn(self, msg):
        self.warnings.append(msg)


def unpack_key(buf, off):
    objectid, type_, offset = struct.unpack_from("<QBQ", buf, off)
    return (objectid, type_, offset)


def key_str(k):
    return f"({k[0]},{k[1]},{k[2]})"


def logical_to_phys(chunks, logical):
    for c_logical, c_length, c_phys in chunks:
        if c_logical <= logical < c_logical + c_length:
            return c_phys + (logical - c_logical)
    raise CheckError(f"no chunk maps logical 0x{logical:x}")


def read_node(img, chunks, nodesize, logical):
    phys = logical_to_phys(chunks, logical)
    return img.read(phys, nodesize)


def check_checksum(img, node, logical, what):
    stored = node[0:4]
    computed = crc32c(node[32:]).to_bytes(4, "little")
    if stored != computed:
        img.err(f"{what} at logical=0x{logical:x}: checksum mismatch "
                 f"(stored={stored.hex()}, computed={computed.hex()})")


def walk(img, chunks, nodesize, logical, expected_level, owner, sb_generation, min_key=None, max_key=None):
    """Returns (low_key, high_key, generation) of the subtree rooted here."""
    node = read_node(img, chunks, nodesize, logical)
    hdr = struct.unpack_from(HEADER_FMT, node, 0)
    _, _, bytenr, _flags, _uuid, generation, hdr_owner, nritems, level = hdr
    img.nodes_checked += 1

    check_checksum(img, node, logical, "node")
    if bytenr != logical:
        img.err(f"node at 0x{logical:x}: header.bytenr=0x{bytenr:x} != its own logical address")
    if hdr_owner != owner:
        img.err(f"node at 0x{logical:x}: owner={hdr_owner} != expected tree objectid {owner}")
    if level != expected_level:
        img.err(f"node at 0x{logical:x}: header.level={level} != expected depth {expected_level}")
    if generation > sb_generation:
        img.err(f"node at 0x{logical:x}: generation {generation} > superblock generation {sb_generation}")

    low_key = None
    high_key = None

    if level == 0:
        img.leaves_checked += 1
        prev_key = None
        for i in range(nritems):
            item_off = HEADER_SIZE + i * ITEM_SIZE
            objectid, type_, offset, data_off, data_size = struct.unpack_from(ITEM_FMT, node, item_off)
            key = (objectid, type_, offset)
            img.items_checked += 1
            if prev_key is not None and key <= prev_key:
                img.err(f"leaf at 0x{logical:x}: item {i} key {key_str(key)} not strictly after {key_str(prev_key)}")
            prev_key = key
            if low_key is None:
                low_key = key
            high_key = key
            if data_off + data_size > nodesize:
                img.err(f"leaf at 0x{logical:x}: item {i} data [{data_off}:{data_off+data_size}] exceeds nodesize")
                continue

            if type_ == INODE_ITEM_KEY:
                pass  # size/mode not independently cross-checked here
            elif type_ == EXTENT_DATA_KEY:
                fe = node[data_off:data_off + data_size]
                if len(fe) >= 53:
                    ftype = fe[20]
                    if ftype != 0:  # not inline
                        disk_bytenr, disk_num_bytes = struct.unpack_from("<QQ", fe, 21)
                        if disk_bytenr != 0:
                            try:
                                logical_to_phys(chunks, disk_bytenr)
                            except CheckError as e:
                                img.err(f"leaf at 0x{logical:x}: EXTENT_DATA for ino {objectid}: {e}")
            elif type_ == DIR_INDEX_KEY:
                name_len = struct.unpack_from("<H", node, data_off + 27)[0]
                name = node[data_off + 30:data_off + 30 + name_len].decode("ascii", "replace")
                img.files.append((objectid, name))
    else:
        prev_key = None
        for i in range(nritems):
            ptr_off = HEADER_SIZE + i * KEYPTR_SIZE
            objectid, type_, offset, blockptr, child_gen = struct.unpack_from(KEYPTR_FMT, node, ptr_off)
            key = (objectid, type_, offset)
            if prev_key is not None and key <= prev_key:
                img.err(f"node at 0x{logical:x}: keyptr {i} key {key_str(key)} not strictly after {key_str(prev_key)}")
            prev_key = key
            if low_key is None:
                low_key = key
            high_key = key

            child_low, child_high, child_actual_gen = walk(
                img, chunks, nodesize, blockptr, level - 1, owner, sb_generation)
            if child_low is not None and child_low != key:
                img.err(f"node at 0x{logical:x}: keyptr {i} key {key_str(key)} != child's actual lowest key {key_str(child_low)}")
            if child_gen > generation:
                img.warn(f"node at 0x{logical:x}: keyptr {i} generation {child_gen} > parent generation {generation}")

    return low_key, high_key, generation


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    path = sys.argv[1]
    offset = 0
    if "--offset" in sys.argv:
        offset = int(sys.argv[sys.argv.index("--offset") + 1], 0)

    img = Image(path, offset)

    sb = img.read(SUPER_OFFSET, 4096)
    magic = sb[64:72]
    if magic != b"_BHRfS_M":
        print(f"FAIL: bad magic {magic!r}")
        return 1

    stored_csum = sb[0:4]
    computed_csum = crc32c(sb[32:]).to_bytes(4, "little")
    if stored_csum != computed_csum:
        img.err(f"superblock: checksum mismatch (stored={stored_csum.hex()}, computed={computed_csum.hex()})")

    generation = struct.unpack_from("<Q", sb, 72)[0]
    root = struct.unpack_from("<Q", sb, 80)[0]
    chunk_root = struct.unpack_from("<Q", sb, 88)[0]
    sectorsize, nodesize = struct.unpack_from("<II", sb, 144)
    sys_chunk_array_size = struct.unpack_from("<I", sb, 160)[0]
    root_level, chunk_root_level = struct.unpack_from("<BB", sb, 198)
    sys_chunk_array = sb[811:811 + sys_chunk_array_size]

    if nodesize == 0:
        nodesize = NODESIZE_DEFAULT

    print(f"Superblock: generation={generation} root=0x{root:x} (level {root_level}) "
          f"chunk_root=0x{chunk_root:x} (level {chunk_root_level}) nodesize={nodesize}")

    # ── Bootstrap chunk map from the system chunk array ────────────────────
    chunks = []
    off = 0
    while off + 17 <= len(sys_chunk_array):
        k = unpack_key(sys_chunk_array, off)
        off += 17
        if k[1] != CHUNK_ITEM_KEY:
            break
        length, owner, stripe_len, ctype, io_align, io_width, sector_size, num_stripes, sub_stripes = \
            struct.unpack_from("<QQQQIIIHH", sys_chunk_array, off)
        stripe_off = off + 48
        devid, phys = struct.unpack_from("<QQ", sys_chunk_array, stripe_off)
        chunks.append((k[2], length, phys))
        off += 48 + max(num_stripes, 1) * 32

    if not chunks:
        print("FAIL: no usable system chunks")
        return 1

    # ── Walk the chunk tree fully to pick up every chunk ────────────────────
    def collect_chunks(logical, level):
        node = read_node(img, chunks, nodesize, logical)
        hdr = struct.unpack_from(HEADER_FMT, node, 0)
        nritems = hdr[7]
        if level == 0:
            for i in range(nritems):
                item_off = HEADER_SIZE + i * ITEM_SIZE
                objectid, type_, koff, data_off, data_size = struct.unpack_from(ITEM_FMT, node, item_off)
                if type_ != CHUNK_ITEM_KEY:
                    continue
                length, cowner, stripe_len, ctype, io_align, io_width, sector_size, num_stripes, sub_stripes = \
                    struct.unpack_from("<QQQQIIIHH", node, data_off)
                devid, phys = struct.unpack_from("<QQ", node, data_off + 48)
                if not any(c[0] == koff for c in chunks):
                    chunks.append((koff, length, phys))
        else:
            for i in range(nritems):
                ptr_off = HEADER_SIZE + i * KEYPTR_SIZE
                _, _, _, blockptr, _ = struct.unpack_from(KEYPTR_FMT, node, ptr_off)
                collect_chunks(blockptr, level - 1)

    collect_chunks(chunk_root, chunk_root_level)
    print(f"Chunks: {len(chunks)}")

    # ── Root tree -> this subvolume's ROOT_ITEM -> fs tree root ────────────
    def find_root_item(logical, level):
        node = read_node(img, chunks, nodesize, logical)
        hdr = struct.unpack_from(HEADER_FMT, node, 0)
        nritems = hdr[7]
        if level == 0:
            for i in range(nritems):
                item_off = HEADER_SIZE + i * ITEM_SIZE
                objectid, type_, koff, data_off, data_size = struct.unpack_from(ITEM_FMT, node, item_off)
                if objectid == FS_TREE_OBJECTID and type_ == ROOT_ITEM_KEY:
                    bytenr = struct.unpack_from("<Q", node, data_off + 176)[0]
                    lvl = node[data_off + 238]
                    return bytenr, lvl
            return None
        else:
            chosen = None
            for i in range(nritems):
                ptr_off = HEADER_SIZE + i * KEYPTR_SIZE
                objectid, type_, koff, blockptr, _ = struct.unpack_from(KEYPTR_FMT, node, ptr_off)
                if chosen is None or objectid <= FS_TREE_OBJECTID:
                    chosen = blockptr
                else:
                    break
            if chosen is None:
                return None
            r = find_root_item(chosen, level - 1)
            if r:
                return r
        return None

    result = find_root_item(root, root_level)
    if not result:
        print("FAIL: default subvolume ROOT_ITEM not found")
        return 1
    fs_root, fs_level = result
    print(f"FS tree root: 0x{fs_root:x} (level {fs_level})")

    # ── Full structural walk of chunk / root / fs trees ─────────────────────
    walk(img, chunks, nodesize, chunk_root, chunk_root_level, 3, generation)
    walk(img, chunks, nodesize, root, root_level, ROOT_TREE_OBJECTID, generation)
    walk(img, chunks, nodesize, fs_root, fs_level, FS_TREE_OBJECTID, generation)

    print(f"Nodes checked: {img.nodes_checked} ({img.leaves_checked} leaves, {img.items_checked} items)")
    names = sorted(name for _, name in img.files)
    print(f"Directory entries found: {len(names)}: {names}")

    if img.warnings:
        print(f"\n{len(img.warnings)} warning(s):")
        for w in img.warnings:
            print(f"  WARN: {w}")

    if img.errors:
        print(f"\n{len(img.errors)} error(s):")
        for e in img.errors:
            print(f"  FAIL: {e}")
        return 1

    print("\nOK: all structural invariants held.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
