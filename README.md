# OS/2 Warp 4.52 for ARM64 (QEMU / Raspberry Pi 3+)

A **bare-metal**, freestanding OS/2-style operating system written in C11 +
AArch64 assembly, targeting the **QEMU virt** machine (Cortex-A72) and
**Raspberry Pi 3/3+** hardware. No libc, no MMU-backed process isolation —
everything (kernel, `CMD.EXE`, filesystem drivers) runs in one flat address
space.

It has a PL011 UART driver, a DOS-style command shell with DOSKEY-style
history/macros, a QuickBASIC-compatible interpreter, an ext4 (read-only) +
btrfs (read/write) filesystem stack behind a small VFS/IFS layer, an OS/2 LX
`.EXE` loader that can run a standalone `CMD.EXE`, a Wayland-style GUI
compositor with four switchable "workplaces", a DEBUG.COM-style memory
monitor, and a small on-box heuristic assistant (`AI`) — no network stack, so
it's not a real LLM, just something that watches what you type.

```
┌──────────────────────────────────────────────────────────────────────┐
│                       OS/2 Warp 4.52 ARM64                           │
│                                                                        │
│  bootloader/                     kernel/src/                         │
│  ├── boot.S      (QEMU entry)    ├── main.c        (kernel_main,     │
│  ├── rpi_boot.S  (RPi3 entry)    │                   shell loop)     │
│  └── linker scripts              ├── uart.c         (PL011)          │
│                                   ├── kprintf.c      (kernel printf)  │
│  os2api/                         ├── vfs.c          (VFS layer)      │
│  └── os2_api.c   Dos* calls      ├── ifs_loader.c   (IFS registry)   │
│      (DosOpen/DosRead/...)       ├── blkdev.c/gpt.c (block devices)  │
│                                   ├── lx_loader.c    (OS/2 LX loader) │
│  dos/                            ├── ai.c           (AI assistant)   │
│  ├── dos_commands.c  DIR/TYPE/   ├── basic.c        (QuickBASIC)     │
│  │   COPY/COMP/FC/DEL/REN/MD/    └── gui/            Wayland-style   │
│  │   RD/CD/VOL/ATTRIB/ECHO/          compositor, 4 workplaces,       │
│  │   CLS/DOSKEY/DEBUG/EXIT           virtio-gpu/ramfb/virtio-input   │
│  └── cmd_main.c  standalone                                          │
│      CMD.EXE entry point         fs/                                 │
│                                   ├── ext4/    read-only driver      │
│                                   └── btrfs/   read/write CoW driver │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 1. Prerequisites

### macOS (for building)

```bash
brew install llvm qemu cmake
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"

# Verify
clang --version            # must be LLVM clang, not Apple clang
llvm-objcopy --version
qemu-system-aarch64 --version
```

### Ubuntu / Debian

```bash
sudo apt update
sudo apt install clang lld llvm qemu-system-arm cmake
```

---

## 2. Build

### QEMU (default)

```bash
git clone <this repo>
cd os24arm

cmake -B build
cmake --build build --target os2warp.img

# Outputs:
#   build/os2warp_kernel  — ELF with debug symbols
#   build/os2warp.img     — raw binary QEMU boots directly
#   build/CMD.EXE         — standalone OS/2 LX executable (see §6)
```

`cmake --build build` alone (no `--target`) builds everything, including the
host-side LX toolchain (`lxbuild`/`makeexe`/`makedll`, built with your host
compiler since they run on the build machine, not the target) and `kcc`, the
Kayte C Compiler host tool used by the LX build pipeline. If you only want
the kernel image, target `os2warp.img` specifically as above — it's faster.

### Raspberry Pi 3+

```bash
cmake -B build-rpi -DTARGET_PLATFORM=RPI3
cmake --build build-rpi

# Outputs:
#   build-rpi/os2warp_kernel  — ELF with debug symbols
#   build-rpi/kernel8.img     — raw binary for the Pi's firmware to load
```

The RPi3 path shares the same kernel sources as QEMU where possible (see
`kernel/CMakeLists.txt`); `bootloader/rpi_boot.S` and `bootloader/rpi_main.c`
handle the Pi-specific entry sequence (dropping from EL2/EL3 to EL1, parking
secondary cores, etc.) before handing off. It hasn't been exercised as
heavily this session as the QEMU path — treat it as less battle-tested.

The toolchain is configured directly in `CMakeLists.txt` (no separate
toolchain file needed):

```cmake
-target aarch64-none-elf
-mcpu=cortex-a72
-ffreestanding -nostdlib
-fno-builtin -fno-stack-protector -fno-pie -fno-pic
```

---

## 3. Run in QEMU

There's no real VirtIO block driver yet (`blkdev.c`'s `virtio_blk_read/write`
are unused stubs) — disk I/O only works via a fixed-physical-address
"preloaded disk" window (`PRELOADED_DISK_ADDR = 0x44000000`, 24 MB) that
QEMU's `-device loader` injects a raw disk image into *before* boot. That's
what `run-testdisk` (below) sets up for you.

```bash
# Boot with just the kernel — no disk, serial console only
cmake --build build --target run

# Boot with a real display: type GUI at the prompt for the Wayland-style
# compositor; Cmd+Escape cycles the 4 workplaces; mouse is live
cmake --build build --target run-gui

# Build a small ext4 test disk (CMD.EXE, HELLO.EXE, README.TXT) and boot
# with it preloaded — makes DIR/TYPE/COPY/RUN actually have something to
# work with
cmake --build build --target run-testdisk

# GDB/LLDB debugging (QEMU pauses, waits for a debugger on :1234)
cmake --build build --target debug
```

Press **Ctrl-A X** to quit QEMU (nographic mode).

For a btrfs test disk (the read/write filesystem — see §7), build one with
`scripts/mkbtrfs_image.py` and inject it the same way `run-testdisk` does,
via `-device loader,file=...,addr=0x44000000,force-raw=on`; GPT-partition it
with a partition literally named `C` (`sgdisk --change-name=1:C`) so
`btrfs_init()` finds it (see `fs/btrfs/btrfs.c`).

---

## 4. Boot on Raspberry Pi 3+

### Hardware needed

- Raspberry Pi 3, 3+, or 3B+ (ARM64, Cortex-A53)
- MicroSD card (8 GB+, FAT32)
- USB-to-TTL serial cable for console output
- 5V power supply (2.5A minimum)

### SD card

```bash
# Format as FAT32, then copy the kernel image
diskutil eraseDisk FAT32 BOOT /dev/diskX   # macOS
cp build-rpi/kernel8.img /Volumes/BOOT/
diskutil eject /dev/diskX
```

You'll also need the Pi's own firmware files (`bootcode.bin`, `start.elf`,
`fixup.dat`) and a `config.txt` on the card — see `bootloader/config.txt` for
a starting point.

### Serial console

| Cable wire  | RPi pin | Function |
|-------------|---------|----------|
| Black (GND) | Pin 6   | Ground   |
| Green (RX)  | Pin 8   | UART TX  |
| White (TX)  | Pin 10  | UART RX  |
| Red (VCC)   | **do not connect** | the Pi powers itself |

```bash
screen /dev/tty.usbserial-* 115200   # macOS
screen /dev/ttyUSB0 115200           # Linux
```

---

## 5. Debug with GDB

```bash
# Terminal 1 — start QEMU paused, waiting on :1234
cmake --build build --target debug

# Terminal 2 — attach
gdb build/os2warp_kernel \
    -ex "target remote :1234" \
    -ex "break kernel_main" \
    -ex "continue"
```

(`aarch64-elf-gdb` if you have a cross-gdb installed; a host `gdb` built with
AArch64 support works too.)

---

## 6. The shell

Boots straight into a command prompt:

```
[C:\]>
```

Type `HELP` for the command list. Type `DOSKEY` once to turn on command
history and macros — until you do, the prompt is plain byte-at-a-time input,
matching how real DOSKEY only changes anything once loaded.

### DOSKEY

Based on real MS-DOS DOSKEY: Up/Down arrows recall history, `F7` lists it,
`F8` prefix-searches it, `F9` selects by number; Left/Right/Home/End/Delete/
Insert edit the line, `Esc` clears it. `$`-macros: `name=text`, with `$1`-`$9`
positional args, `$*` for all args, `$T` to chain multiple commands in one
macro, `$$` for a literal `$`. `DOSKEY /HISTORY`, `/MACROS`, `/LISTSIZE=n`,
`/REINSTALL`, `/MACROFILE=file` all work as in real DOSKEY.

### DEBUG

A partial DEBUG.COM-style memory monitor — its own `-` sub-prompt (`Q` to
exit back to the DOS prompt). `D`(ump)/`E`(nter)/`F`(ill)/`C`(ompare)/
`M`(ove)/`S`(earch) operate on real flat 64-bit addresses, parsed the DEBUG.COM
way (bare hex, `start end` or `start Llength` ranges). `H` does hex
arithmetic; `R` shows real AArch64 state (SP, exception level, DAIF) rather
than faking x86 registers. `?` lists every command DEBUG.COM had — the ones
that don't map to this platform (port I/O, EMS, an x86 assembler/
disassembler, breakpoint/trace) are recognized but say so rather than
pretending.

### AI

An on-box heuristic assistant — **not** a real LLM (there's no network
stack to reach one). `AI STATUS` gives a session summary and a suggestion,
`AI STATS` shows command-usage counts, `AI TIP` suggests a built-in command
you haven't tried yet. State is RAM-only and resets on reboot.

### Commands

| Command | Description |
|---|---|
| `VER` | Show OS/kernel version |
| `HELP` / `?` | List commands |
| `DIR [path]` | List directory contents |
| `TYPE <file>` | Print file contents |
| `COPY <src> <dst>` | Copy a file |
| `COMP <file1> <file2>` | Byte-for-byte compare, reports first differing offset |
| `FC <file1> <file2>` | Line-by-line compare, reports differing lines |
| `DEL <file>` / `ERASE` | Delete a file |
| `REN <old> <new>` / `RENAME` | Rename a file |
| `MD <dir>` / `MKDIR` | Create a directory |
| `RD <dir>` / `RMDIR` | Remove a directory |
| `CD [path]` / `CHDIR` | Show/change current directory |
| `VOL` | Show volume label |
| `ATTRIB <file>` | Show file attributes |
| `CHKDSK`, `FORMAT` | Stubbed — not supported |
| `ECHO <text>` | Print text |
| `CLS` | Clear screen |
| `DOSKEY` | Enable history/macros (see above) |
| `DEBUG` | Memory monitor sub-shell (see above) |
| `MEM` | Show heap usage |
| `IFS` | List loaded filesystem drivers |
| `RUN <file.EXE>` | Load and run an OS/2 LX executable (e.g. `RUN CMD.EXE`) |
| `BASIC` | Start the QuickBASIC interpreter |
| `AI [STATUS\|STATS\|TIP\|HELP]` | On-box assistant (see above) |
| `GUI` | Start the Wayland-style Workplace Shell desktop |
| `EXIT` | Halt the CPU (`wfi` loop) — see §8 for why there's nowhere to "return" to |

---

## 7. Filesystems

```
dos/dos_commands.c   (DIR, TYPE, COPY, DEL, REN, MD, RD, CD, ...)
        │
os2api/os2_api.c     (DosOpen/DosRead/DosWrite/DosFindFirst/... - the only
        │              way commands talk to storage, matching real DOSCALLS.DLL)
kernel/src/vfs.c      (mount table, fd table, path dispatch)
        │
kernel/src/ifs_loader.c  (installable-filesystem registry)
        │
   ┌────┴────┐
fs/ext4/   fs/btrfs/
(read-only) (read/write)
        │
kernel/src/blkdev.c + gpt.c   (block device registry, GPT partitions)
```

- **ext4** — read-only.
- **btrfs** — real copy-on-write writes: create/write/truncate/delete/mkdir/
  rename, with node/leaf splitting, CRC32C-checksummed nodes, and generation
  bumping, landing in a superblock rewrite as the atomic commit point. It
  does **not** maintain the extent tree or checksum tree (a much larger
  undertaking with no way to validate here — see `fs/btrfs/btrfs_write.c`'s
  header comment), and its leaf layout is a simplified, project-local
  dialect (items packed forward, not upstream btrfs's backward-growing
  layout). Images this driver writes to are safe for this OS to read/write
  across reboots, but shouldn't be handed to a real Linux `btrfs` or
  `btrfs-progs` afterward. `scripts/mkbtrfs_image.py` builds test images;
  `scripts/check_btrfs_image.py` is an independent structural verifier
  (checksums, key ordering, generation monotonicity) since there's no
  `btrfsck` available on macOS to check against.

---

## 8. CMD.EXE and the LX loader

`RUN <file.EXE>` (`kernel/src/lx_loader.c`) loads and executes a real OS/2 LX
(Linear eXecutable) binary in the kernel's own flat address space — there's
no process isolation or syscall boundary in this project, so this is the
actual command-interpreter logic, not a demo.

`dos/cmd_main.c` builds a standalone `CMD.EXE` from the exact same
`dos_commands.c`/`dos_shell_dispatch()` command set the in-kernel shell uses,
wrapped as a genuine LX executable by the host-side tools in `lx/`
(`lxbuild`, `makeexe`). `RUN CMD.EXE` loads and jumps into it directly. Since
there's no process boundary to return across, `CMD.EXE` is treated like the
in-kernel shell: `EXIT` halts the CPU rather than returning control to
whatever called `RUN`.

---

## 9. The GUI

`GUI` starts a small Wayland-style compositor (`kernel/src/gui/wayland.c`)
with four switchable "workplaces" (virtual desktops) — **Cmd+Escape** in the
display window cycles between them. Backed by `virtio-gpu`/`ramfb` for the
framebuffer and `virtio-input` for keyboard/pointer events (see `run-gui` in
§3 for the QEMU flags that wire this up). This is separate from the serial
console the DOS shell runs on.

---

## 10. QuickBASIC interpreter

Type `BASIC` at the shell. Type `BYE`, `QUIT`, or `SYSTEM` to return.

```basic
10 PRINT "Hello from OS/2 BASIC!"
20 FOR I = 1 TO 10
30   PRINT I * I
40 NEXT I
50 END
RUN
```

### Statements

| Statement | Notes |
|---|---|
| `PRINT expr / "str" [; ,]` | `TAB()`, `SPC()` supported |
| `LET var = expr` | `LET` is optional |
| `INPUT ["prompt";] var` | String or integer |
| `IF expr THEN … [ELSE …]` | Single-line form |
| `IF … / ELSEIF … / ELSE / END IF` | Block form |
| `SELECT CASE … / CASE … / END SELECT` | `CASE IS`, `CASE a TO b` |
| `FOR var = n TO m [STEP s] … NEXT [var]` | |
| `WHILE expr … WEND` | |
| `DO [WHILE\|UNTIL] … LOOP [WHILE\|UNTIL]` | |
| `GOTO line` | |
| `GOSUB line … RETURN` | |
| `ON expr GOTO line[,…]` | |
| `ON expr GOSUB line[,…]` | |
| `READ var / DATA val[,…] / RESTORE` | |
| `DIM var(n)` | 1-D integer arrays, 0-based |
| `CONST name = expr` | |
| `SWAP var, var` | |
| `CLS / LOCATE r,c / COLOR fg[,bg] / BEEP` | ANSI terminal |
| `SLEEP n` | Busy-wait seconds |
| `REM` or `'` | Comment |
| `END / STOP` | |
| `LIST [first[-last]] / RUN / NEW / RENUM` | Direct-mode commands |

### Functions

`ABS` `SGN` `INT` `FIX` `SQR` `RND` `VAL` `LEN` `ASC` `INSTR` `CHR$` `STR$`
`HEX$` `OCT$` `LEFT$` `RIGHT$` `MID$` `LTRIM$` `RTRIM$` `UCASE$` `LCASE$`
`SPACE$` `STRING$` `INKEY$`

### Variables

| Type | Example |
|---|---|
| Integer (`A`–`Z`) | `A = 42` |
| String (`A$`–`Z$`) | `A$ = "hi"` |
| Array (`A(n)`) | `DIM A(10)` |
| Hex literal | `&HFF` |
| Octal literal | `&O17` |

Multi-statement lines separated by `:`. `?` is shorthand for `PRINT`.

---

## 11. Other subprojects in this tree

Not all part of the default build (`add_subdirectory(...)` in the root
`CMakeLists.txt`) — noted here so their status is clear:

- **`rexx/`** — an ooRexx-flavored interpreter. Built and linked into the
  kernel, but not yet wired up to a shell command.
- **`lx/`** — the host-side OS/2 LX executable builder (`lxbuild`, `makeexe`,
  `makedll`) used to produce `CMD.EXE` and other `.EXE`/`.DLL` files; built
  automatically as part of the LX pipeline (§8).
- **`tools/kcc`** — the Kayte C Compiler, an alternative to clang for the
  cross-build (`cmake -DUSE_KCC=ON`), and a host tool in the LX pipeline.
- **`kayte/`** — a separate Free Pascal/Lazarus project (the Kayte language
  toolchain) living in this repo; not part of the CMake build.
- **`workplace/`** — a Qt6-based C++ Workplace Shell desktop/QPA platform
  plugin; not currently wired into `add_subdirectory(...)`, distinct from
  the actual GUI in §9 (`kernel/src/gui/`).
- **`installer/`** — a C++ installer wizard, separate from the kernel build.

---

## 12. Architecture reference

### Platform comparison

| Feature | QEMU virt | Raspberry Pi 3+ |
|---|---|---|
| CPU | Cortex-A72 | Cortex-A53 |
| RAM | Configurable (2 GB in the `run`/`run-gui` targets) | 1 GB |
| UART | PL011 @ 0x09000000 | PL011 @ 0x3F201000 |
| Load address | 0x40000000 | 0x80000 |
| Heap size | 32 MB (`kernel/src/main.c`; the GUI's window buffers need it — note this is more than the `cmake configure` status message says, which is stale) | 32 MB |
| Output file | `os2warp.img` | `kernel8.img` |
| Boot method | Direct kernel load (`-kernel`) | Pi firmware chain |
| Disk | Preloaded RAM window (§3) — no real VirtIO block driver yet | none yet |

### Boot sequence (QEMU)

```
QEMU loads os2warp.img → 0x40000000
  │
  ▼
boot.S  (_start)
  ├── Zero BSS
  ├── Set stack pointer (_stack_top, linker.ld)
  └── bl kernel_main
        │
        ▼
kernel/src/main.c  (kernel_main)
  ├── mem_init / scheduler_init
  ├── vfs_init → blk_init → GPT scan
  ├── ifs_loader_init → register ext4 + btrfs drivers → mount
  ├── kbd_init
  └── kernel_shell()  ──►  dos_read_line() (§6)
                              ├── DOSKEY history/macro expansion
                              ├── dos_shell_dispatch() → dos/dos_commands.c
                              └── kernel-local commands (VER, MEM, IFS,
                                  RUN, BASIC, AI, GUI, HELP)
```

---

## 13. Project structure

```
os24arm/
├── CMakeLists.txt          Root build: platform detection, all targets
├── bootloader/
│   ├── boot.S               QEMU entry point
│   ├── rpi_boot.S           Raspberry Pi entry point
│   └── linker.ld / rpi_linker.ld
├── kernel/
│   ├── CMakeLists.txt        Actual kernel source list (see below)
│   ├── linker.ld
│   ├── include/               types.h, uart.h, vfs.h, ifs_loader.h, ai.h, ...
│   └── src/
│       ├── main.c              kernel_main, shell loop, HELP/VER/MEM/IFS/...
│       ├── uart.c / kprintf.c   PL011 driver, kernel printf
│       ├── vfs.c / ifs_loader.c VFS layer, installable-FS registry
│       ├── blkdev.c / gpt.c     Block device registry, GPT partitions
│       ├── lx_loader.c          OS/2 LX .EXE loader (RUN)
│       ├── ai.c                 On-box AI assistant
│       ├── basic.c              QuickBASIC interpreter
│       └── gui/                 Wayland-style compositor (framebuffer,
│                                 wayland, virtio_gpu, virtio_input, ramfb)
├── dos/
│   ├── dos_commands.c        DIR/TYPE/COPY/COMP/FC/DEL/REN/MD/RD/CD/VOL/
│   │                         ATTRIB/ECHO/CLS/DOSKEY/DEBUG/EXIT + dos_read_line()
│   ├── cmd_main.c            Standalone CMD.EXE entry point
│   └── dos.h
├── os2api/
│   └── os2_api.c              DosOpen/DosRead/DosWrite/DosFindFirst/... bridge
├── fs/
│   ├── ext4/                  Read-only ext4 driver
│   └── btrfs/                 Read/write copy-on-write btrfs driver
├── lx/                        Host-side OS/2 LX builder tools
├── rexx/                      REXX interpreter (linked, not yet exposed)
├── tools/kcc/                 Kayte C Compiler (alternative cross-compiler)
├── kayte/                     Separate Free Pascal Kayte-language project
├── workplace/                 Qt6 Workplace Shell desktop (not in default build)
├── installer/                 C++ installer wizard
└── scripts/
    ├── mktestdisk.sh           ext4+btrfs GPT test disk builder
    ├── mkbtrfs_image.py        Minimal btrfs image builder (no mkfs.btrfs on macOS)
    ├── check_btrfs_image.py    Independent btrfs structural verifier
    └── build_iso.sh            Bootable ISO builder
```

---

## 14. Contributing

Areas of interest:

- **Storage:** a real VirtIO block driver (`blkdev.c`'s `virtio_blk_*` are
  still stubs); btrfs extent-tree/checksum-tree maintenance for real
  Linux/`btrfs-progs` interop.
- **DEBUG:** `A`/`U` (an AArch64 assembler/disassembler), `G`/`P`/`T`
  (breakpoint/single-step support in the exception vectors).
- **Shell:** wiring `rexx/` up to a command.
- **Networking:** there is none — no driver, no TCP/IP stack.
- **Documentation:** the Doxygen output in `docs/html/` (from the root
  `Doxyfile`) is the closest thing to an API reference right now.

---

## 15. License

GPLv3 — see `LICENSE`.

---

**Status:** Boots on QEMU virt (serial console + GUI) | Boots on Raspberry Pi
3+ (less exercised) | Active development.
