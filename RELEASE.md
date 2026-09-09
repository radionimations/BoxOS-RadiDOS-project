# BoxOS 2.0 (RadiDOS 2.0)

The first stable release of BoxOS — a hand-rolled 64-bit hobby OS that now
boots straight into a graphical desktop, runs a small suite of GUI apps, has
durable disk writes, and plays DOOM with sound. Drops below BIOS the moment
long-mode is up.

## What's new

**A real GUI**
* **BoxOS Executive** — a full-screen graphical file browser that replaces
  the old shell as the default landing page. Single-click selects,
  double-click opens, right-click pops a context menu, the menu bar has
  File / View / Special drop-downs, and there's a live clock in the top
  corner.
* **Window manager** — multiple dragable windows with title bars, close (X)
  buttons, save-under-content so dragging doesn't leave white holes, and
  proper redraw on close.
* **Software mouse cursor** — arrow over the desktop, hourglass while the
  loader is reading an app off disk, and clean save/restore as it moves
  over arbitrary backgrounds.

**GUI apps you can actually use**
* **WINPAINT** — paint with palette + brush sizes + a real **Save As**
  dialog (folder picker + filename input), writes 8-bit indexed BMPs that
  WINIMG and any host viewer can open.
* **WINMUS** — 16-step PC-speaker music sequencer. Each cell is one of
  8 pitches (C4..C5) or silence. PLAY loops the pattern at a configurable
  BPM; SAVE writes a `.MUS` file.
* **WINFLIP** — frame-by-frame animation builder with optional per-frame
  tones. Saves a `.ANI` file that WINIMG can play back.
* **WINIMG** — image viewer for 8-bit indexed BMP, baseline JPEG, and the
  `.ANI` format from WINFLIP.
* **WINNOTE** — tiny notepad with save.
* **WINCALC**, **WINCLOCK**, **WINFILES**, **WINHELLO**, **WINSNAKE** — pocket
  apps that round out the desktop.
* **WINSETT** — settings: theme picker, sound on/off, system info readout.

**Shell upgrades (RadiDOS 2.0)**
Tier 1+2 DOS-style commands shipped: PAUSE, REM, HOSTNAME, VOL, WHERE,
ATTRIB, CHOICE, SORT, FC, FINDSTR, plus a customisable PROMPT, TITLE, and
HOSTNAME. The kernel banner now reads `RadiDOS 2.0`.

**Print Screen**
F12 anywhere snapshots the framebuffer to an auto-numbered
`/DOCS/SCREENn.BMP`.

**Sound**
PC-speaker driver with tone-on / tone-off / beep / chord helpers. DOOM SFX
now play through it (mapped from doomgeneric's sound IDs to frequencies),
WINMUS uses it for both audition and playback, and short OK / error chords
chime on save success and failure.

**Bigger Disk Layout**
The bootable image now packs the bootloader, kernel, every app, and a
factory backup of the filesystem in a single 65 MB file — write it to USB
with Rufus in DD mode or attach it as a virtual hard disk in UTM /
VirtualBox / QEMU.

## What's fixed (the load-bearing ones)

These were all "you wouldn't notice it day-to-day but it breaks under load"
bugs that this version nails down:

* **ATA writes weren't durable.** The PIO driver issued `CMD_FLUSH` (and
  the next command) without waiting for BSY to clear. Real hardware and
  emulators silently drop a command sent during BSY, so the second-and-
  later writes inside `fs_create_file` (dir entry + FAT flush) vanished —
  files appeared in the dir but the FAT never recorded the cluster as
  allocated. Saves "looked OK" until the next reboot or a re-allocation
  corrupted the data. Now `ata_read` / `ata_write` poll until !BSY && !DRQ
  before every command and after every flush.
* **Kernel BSS was overlapping VGA memory.** The linker put large BSS
  arrays (notably the desktop's 1 MiB image-launch shuttle) at physical
  addresses `0xA0000..0xBFFFF`, which are mapped to VGA hardware planes,
  not RAM. Writes went into the video chip and reads returned `0xFF` —
  which is why every image showed up as "Unknown image format" until this
  release. Fixed by placing `.bss` above 1 MiB in `linker.ld`.
* **Closing the terminal broke clicks.** The shell consumed from the
  ASCII key ring but every keystroke also accumulated press+release events
  in the separate event ring that the desktop's event loop reads. After a
  long terminal session the desktop drained hundreds of stale key events
  one per `gui_poll_event` call before mouse buttons got a turn, so clicks
  felt frozen. Now the terminal drains both rings on exit.
* **The hourglass cursor was on screen the whole time an app ran.**
  `cursor_set_busy(1)` was scoped to the entire app launch instead of just
  the disk read; clicking around inside Paint, etc. was disorientingly
  hard. Now it flips only while the loader is actually reading the binary
  off disk.
* **Dragging a window left it blank.** The window-frame repaint cleared
  the content area on every move. The WM now keeps a per-window
  save-under-content buffer used during drag, so windows redraw with their
  pixels intact.
* **First-run install didn't always land.** `INSTALL.OK` and other first-
  boot writes silently went missing because of the same ATA bug above.
  Fixed in the same place.

## Known limitations (carried over to v1.1)

* JPEG decoder handles only a subset of baseline JPEGs — some valid files
  (including the bundled `HELLO.JPG`) still hit "decode failed". BMPs work
  fine. Replacing the hand-rolled decoder is on the v1.1 list.
* No real audio card driver. Sound is PC-speaker tones only; Sound Blaster
  emulation is reserved for v1.1.
* Legacy BIOS only — no UEFI.
* PS/2 keyboard and mouse only — no native USB stack (BIOS legacy
  emulation works on most real hardware).
* Single ATA disk, no SATA AHCI, FAT12 only, no networking,
  single-tasking.

## Try it

Download `boxos.iso` from the release.

* **UTM / VirtualBox / QEMU** — attach it as a **hard disk** (not as a
  CD/DVD). Enable the PC speaker / a sound card to get audio.
* **Real PC** — write it to a USB stick with **Rufus in DD mode** and boot
  with **Legacy BIOS / CSM** enabled.

Full instructions in `README.md`.

## License

GPL v2 (the bundled doomgeneric DOOM port is GPL v2, so the rest of the
codebase follows). `DOOM1.WAD` is the id Software shareware data file,
redistributed under id's shareware terms.
