#!/bin/bash
# scripts/mktestdisk.sh — build a real GPT test disk with BOOT (ext4) +
# C: (btrfs) partitions for QEMU
#
# blkdev.c has no real VirtIO block driver yet (virtio_blk_read/write are
# unused stubs) — it only reads a "disk" from a fixed physical RAM address
# (PRELOADED_DISK_ADDR = 0x44000000, 24MB window), meant to be populated by
# injecting a real disk image there before boot via QEMU's -device loader.
#
# The disk has two GPT partitions, matching this project's "safe BOOT
# partition" design (see ext4_init()/btrfs_init() in fs/ext4/ext4.c and
# fs/btrfs/btrfs.c): "BOOT" (ext4, mounted at /BOOT — a recovery area) and
# "C" (btrfs, mounted at / — the main drive, holding CMD.EXE/HELLO.EXE).
# No mkfs.btrfs exists on macOS, so the C: partition is built directly by
# scripts/mkbtrfs_image.py instead.
#
# Usage: scripts/mktestdisk.sh [output.img]

set -e

DISK="${1:-build/testdisk.img}"
BOOT_SIZE_MB=4
C_SIZE_MB=8
BOOT_START_SECTOR=2048
C_START_SECTOR=10240

# ── Locate tools ────────────────────────────────────────────────────────────
if command -v mkfs.ext4 >/dev/null 2>&1; then
    E2FS_BIN="$(dirname "$(command -v mkfs.ext4)")"
elif [ -x /opt/homebrew/opt/e2fsprogs/sbin/mkfs.ext4 ]; then
    E2FS_BIN=/opt/homebrew/opt/e2fsprogs/sbin
elif [ -x /usr/local/opt/e2fsprogs/sbin/mkfs.ext4 ]; then
    E2FS_BIN=/usr/local/opt/e2fsprogs/sbin
else
    echo "mkfs.ext4 not found. Install with: brew install e2fsprogs" >&2
    exit 1
fi

if ! command -v sgdisk >/dev/null 2>&1; then
    echo "sgdisk not found. Install with: brew install gptfdisk" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$(dirname "$DISK")"

# ── BOOT partition (ext4) ────────────────────────────────────────────────────
echo "Building BOOT partition (ext4, ${BOOT_SIZE_MB}MB)..."
dd if=/dev/zero of="$WORK/boot.img" bs=1M count=$BOOT_SIZE_MB status=none
"$E2FS_BIN/mkfs.ext4" -F -O ^64bit,^metadata_csum -b 1024 "$WORK/boot.img" >/dev/null

cat > "$WORK/bootinfo.txt" <<'EOF'
This is the BOOT partition (ext4) - a safety/recovery area.
The main C: drive (btrfs) holds the regular files.
EOF
"$E2FS_BIN/debugfs" -w -R "write $WORK/bootinfo.txt BOOTINFO.TXT" "$WORK/boot.img" >/dev/null 2>&1

# ── C: partition (btrfs) ─────────────────────────────────────────────────────
echo "Building C: partition (btrfs, ${C_SIZE_MB}MB)..."
BTRFS_ARGS=()
add_file() {
    if [ -f "$2" ]; then
        BTRFS_ARGS+=("$1:$2")
        echo "  + $1"
    else
        echo "  (skipped $1 — $2 not built yet)"
    fi
}
add_file CMD.EXE build/CMD.EXE
add_file HELLO.EXE build/HELLO.EXE

echo "Hello from OS/2 Warp ARM64 - btrfs C: drive!" > "$WORK/readme.txt"
BTRFS_ARGS+=("README.TXT:$WORK/readme.txt")

python3 "$SCRIPT_DIR/mkbtrfs_image.py" "$WORK/c.img" $((C_SIZE_MB * 1024 * 1024)) "${BTRFS_ARGS[@]}"

# ── GPT layout + assembly ────────────────────────────────────────────────────
echo "Building GPT disk..."
TOTAL_MB=$((BOOT_SIZE_MB + C_SIZE_MB + 4))
truncate -s "${TOTAL_MB}M" "$DISK"
sgdisk --clear \
    --new=1:${BOOT_START_SECTOR}:+${BOOT_SIZE_MB}M --typecode=1:8300 --change-name=1:BOOT \
    --new=2:${C_START_SECTOR}:+${C_SIZE_MB}M --typecode=2:8300 --change-name=2:C \
    "$DISK" >/dev/null

dd if="$WORK/boot.img" of="$DISK" bs=512 seek=$BOOT_START_SECTOR conv=notrunc status=none
dd if="$WORK/c.img" of="$DISK" bs=512 seek=$C_START_SECTOR conv=notrunc status=none

echo
echo "Done: $DISK ($(stat -f%z "$DISK" 2>/dev/null || stat -c%s "$DISK") bytes)"
echo "  Partition 1 BOOT (ext4, mounts at /BOOT)"
echo "  Partition 2 C    (btrfs, mounts at /  — the main drive)"
echo
echo "Boot with:"
echo "  qemu-system-aarch64 -machine virt,gic-version=3 -cpu max -accel hvf -m 2G -smp 1 \\"
echo "    -kernel build/os2warp.img \\"
echo "    -device loader,file=$DISK,addr=0x44000000,force-raw=on \\"
echo "    -serial mon:stdio -display none -nographic"
