# BoxOS 2.1 (RadiDOS 2.1)

A hand-rolled 64-bit hobby operating system. Boots from BIOS, lands on a
Windows-3.1-style desktop with a Program Manager, runs cooperative
multitasking with per-task page tables, and plays DOOM. No BIOS calls
after boot.

* x86_64 long mode kernel, custom bootloader (16-bit stage 1, 32→64-bit stage 2)
* PS/2 keyboard and mouse, ATA PIO disk, FAT12 read/write
* BGA (Bochs/QEMU/UTM) 640×480×8 framebuffer with a window manager, software
  mouse cursor, XP-style outline window drag, and z-order pixel clipping
* Cooperative multitasking with per-task PML4 page tables and per-task cwd —
  multiple apps run concurrently, each in its own private 2 MiB user-space
* VGA text mode 03h and graphics mode 13h (320×200×256) for legacy apps
* Optional **Sound Blaster 16** driver: 8-bit DMA PCM playback (set the VM's
  sound model to SB16 to enable). Falls back to PC speaker otherwise.
* GUI **Program Manager** desktop with grouped icons (Main, Accessories,
  Games), backed by a co-running **WinFiles** file browser
* **RadiDOS 2.1** shell with file commands, MORE / HEXDUMP / EDIT,
  scrollback, factory reset, configurable PROMPT / TITLE / HOSTNAME, plus
  DOS-style PAUSE / REM / VOL / WHERE / ATTRIB / CHOICE / SORT / FC / FINDSTR
* Print Screen (F12) saves the full framebuffer to `/DOCS/SCREENn.BMP`

## What's bundled

GUI apps (launch from Program Manager — single click an icon):

| App        | What it does                                                |
|------------|-------------------------------------------------------------|
| WINFILES   | File browser (navigate folders, launch .BIN, open images)   |
| WINSETT    | Control Panel: theme, sound, system info, drivers, power    |
| WINIMG     | Image viewer — 8-bit BMP, baseline JPEG, plays `.ANI`       |
| WINNOTE    | Notepad with Save                                           |
| WINCALC    | Pop-up calculator                                           |
| WINPAINT   | Paint with colour palette, brush sizes, **Save As** dialog  |
| WINCLOCK   | Analogue clock                                              |
| WINMUS     | Multi-page step sequencer for PC speaker, drag-to-paint     |
| WINFLIP    | Frame-by-frame animation builder, saves `.ANI` (with sound) |
| WINHELLO   | "Hello, world" demo                                         |

Games:

| Game        | What it does                                              |
|-------------|-----------------------------------------------------------|
| WINSNAKE    | Classic snake                                             |
| WINMINES    | Minesweeper (9×9, 10 mines)                               |
| WINPONG     | Two-paddle pong vs AI                                     |
| WINRVRS     | Reversi / Othello (you = Black vs greedy AI)              |
| WINTRIS     | Tetris (10×20, 7 tetrominoes, level-up gravity)           |
| WINSOLI     | Klondike Solitaire (1-card draw, drag-and-drop)           |
| DOOM        | doomgeneric port, plays `DOOM1.WAD`                       |

CLI / fullscreen demos (run via the shell):

| App     | What it does                                            |
|---------|---------------------------------------------------------|
| CALC    | Command-line calculator                                 |
| ASCII   | ASCII reference chart                                   |
| HELLO   | "Hello, world" stdout demo                              |
| ECHO2   | Echo back stdin                                         |
| PLASMA  | Fullscreen 256-colour plasma effect (mode 13h)          |

## What you download

There is one file you actually need:

```
boxos.iso
```

Despite the name it is **not** an ISO 9660 image. It is a raw hard-disk image
that contains the bootloader, kernel, every app, and a factory backup of the
filesystem in a single file. The `.iso` extension is kept only because most
USB flashers expect it.

That means:

* Write it to a USB stick with **Rufus in DD mode**, or
* Attach it as a **hard drive** in **UTM** or **VirtualBox**.

It will not work if you attach it as a CD/DVD drive.

## Requirements

* The target machine must support **legacy BIOS / CSM**. Pure UEFI firmware
  cannot boot this. On most desktops there is a BIOS setting called
  "Legacy Boot" or "CSM" that you turn on.
* USB peripherals work only if the BIOS exposes them as PS/2 (most do, via
  "USB Legacy Support"). BoxOS does not have its own USB stack yet.
* For sound, set the VM's audio model to **Sound Blaster 16** for real PCM
  (BoxOS will detect it and show `SB16 OK` in Settings → Drivers). Leaving
  it on **PC Speaker** also works — DOOM SFX, WINMUS, and save chimes use
  PIT-channel-2 square waves.

## Running it on a real PC with Rufus

You need a USB stick of any size (the image is around 65 MB). Everything on
the stick will be erased.

1. Download `boxos.iso`.
2. Open **Rufus**.
3. Device: pick your USB stick.
4. Boot selection: click `SELECT` and choose `boxos.iso`.
5. When Rufus asks "ISOHybrid image detected", choose **Write in DD Image
   mode**. This is important. ISO mode will not work.
6. Partition scheme: **MBR**. Target system: **BIOS (or UEFI-CSM)**.
7. Click `START` and wait for it to finish.
8. Reboot the target PC, open the boot menu (usually F12, F11, F8, or Esc),
   and pick the USB stick.
9. If it does not boot, go into BIOS setup and turn on **Legacy Boot** or
   **CSM**, then disable Secure Boot.

You should see the BoxOS splash and then drop into the desktop.

## Running it on macOS with UTM

1. Download `boxos.iso`.
2. Open **UTM** and click `Create a New Virtual Machine`.
3. Pick **Emulate** (not Virtualize, you need x86_64 emulation on Apple
   Silicon).
4. Operating System: **Other**.
5. Boot ISO Image: leave **empty** for now and skip.
6. Hardware: Architecture `x86_64`, System `Standard PC (i440FX)`, Memory 256
   MB is plenty.
7. Storage: 100 MB is fine. We will not actually use this disk.
8. Finish the wizard.
9. Right-click the new VM, choose `Edit`.
10. Go to the **Drives** section. Delete any existing drive entries.
11. Click `New Drive`. Set:
    * Image Type: **Disk Image** (NOT CD/DVD ISO).
    * Interface: **IDE**.
    * **Removable**: unchecked.
    * **Read Only**: unchecked.
    Import `boxos.iso` as the drive image.
12. Go to the **Display** section and set Emulated Display Card to plain
    `vga` or `VGA`. Avoid Cirrus, it can show stale frame fragments in DOOM.
13. Go to the **Sound** section. To get real PCM through the SB16 driver
    set **Hardware → Emulated Audio Card** to `Creative Sound Blaster 16
    (sb16)`. The PC Speaker model also works (no SB16 driver, but DOOM
    SFX + WINMUS still play).
14. Save and start the VM.

If UTM complains "Could not read from CDROM", the drive is still set as a
CD/DVD type. Open Edit again and switch it to Disk Image.

## Running it on Windows with VirtualBox

1. Download `boxos.iso`.
2. Open **VirtualBox** and click `New`.
3. Name: `BoxOS`. Type: `Other`. Version: `Other/Unknown (64-bit)`.
4. Memory: 256 MB.
5. Hard disk: choose **Do not add a virtual hard disk** for now. Click
   `Create`.
6. Select the new VM and click `Settings`.
7. Go to **Storage**.
8. Under the IDE controller, **remove the empty optical drive** if present.
9. Click the small disk icon next to the IDE controller and choose
   `Add Hard Disk` then `Add` and pick `boxos.iso`. VirtualBox accepts .iso
   files as raw disk images here.
   * If VirtualBox refuses to attach it because of the extension, rename the
     file to `boxos.img` first, then attach it.
10. Go to **System** and make sure `Enable EFI` is **off**. BoxOS needs
    legacy BIOS.
11. Go to **Display** and set Graphics Controller to `VBoxVGA` or `VMSVGA`.
12. Go to **Audio** and enable it so the PC speaker tones come through.
    SB16 is not exposed by VirtualBox; you'll get the PC speaker path only.
13. Click `OK` and start the VM.

## Running it from the command line

If you have QEMU installed:

```
qemu-system-x86_64 \
  -drive format=raw,file=boxos.iso,if=ide,index=0 \
  -audiodev coreaudio,id=snd \
  -machine type=pc,pcspk-audiodev=snd \
  -device sb16,audiodev=snd
```

(Swap `coreaudio` for `pa` on Linux/PulseAudio or `alsa` on ALSA.)

If you want to write to a real USB stick from the terminal on Linux or
macOS:

```
sudo dd if=boxos.iso of=/dev/sdX bs=1M conv=fsync
```

Replace `/dev/sdX` with your actual USB device. **Double check this**, dd
will overwrite anything you point it at.

## First boot

You land on the **BoxOS desktop** — wallpaper, menu bar, and the **Program
Manager** window with three icon groups: *Main*, *Accessories*, *Games*.

* Click any icon to launch its app. Each runs in its own task — Program
  Manager stays open, so you can keep launching things or click back to it.
* Open **File Mgr** for a Windows-3.1-style file browser:
  * Single click a row to highlight, double-click (or Enter) to open.
  * Folders descend in. Double-click `..` (or press Backspace) to go up.
  * `.BIN` files launch directly (the kernel auto-searches `/APPS` and
    `/GAMES`).
  * `.BMP` / `.JPG` / `.JPEG` / `.ANI` open in WinImg.
* Drag a window by its title bar — XP-style outline drag, with the actual
  window stamping to the new position on release.
* Click outside a window to deselect; click another window's chrome to
  raise it.
* Press **F12** anywhere for a screenshot to `/DOCS/SCREENn.BMP`.
* Open **Control Panel** for theme, sound, drivers, and power options.

The classic RadiDOS shell is still in the box — drop to it from the Files
menu or via `EXIT` in any terminal. Try:

```
HELP            list every command
DIR             show what's in the current folder
CD GAMES        descend into the games folder
RUN DOOM        play DOOM
CD /SYS
CAT BOXOS.TXT   read the internals doc
```

If you ever want to wipe everything back to the as-shipped state, type
`FACTORY` and confirm twice. The OS will reboot back into SETUP.

## Layout inside the OS

```
/                root
  APPS/          GUI + CLI apps (.BIN, plus a few test .EXE)
  GAMES/         DOOM.BIN, DOOM1.WAD, WINSNAKE, WINMINES, WINPONG,
                 WINRVRS, WINTRIS, WINSOLI
  SYS/SYSTEM32/  BOXOS.TXT, SYSINFO.TXT, CONFIG.SYS,
                 BOOT.BIN, STAGE2.BIN, KERNEL.BIN, BOXOS.IMG
  DOCS/          SAMPLE.TXT, MEDIA.TXT, SAMPLE.MP3, SAMPLE.MP4,
                 HELLO.JPG, plus your own saves and SCREEN*.BMP
```

## Building from source

You need:

* `nasm`
* a cross-compiling `gcc` and `ld` that can produce 64-bit ELF (Linux native
  toolchain works, on macOS use a `x86_64-elf` cross toolchain from Homebrew)
* `dd` and `mtools`
* `qemu-system-x86_64` for testing

Then:

```
make
make run
```

This produces:

* `boxos-bootable.img` and `boxos.iso` (identical bytes, the all-in-one image)
* `boxos.img` (kernel-only legacy image)
* `boxos-fs.img` (the filesystem on its own)

`make run` boots the all-in-one image in QEMU with the PC speaker and an
emulated Sound Blaster 16 both wired up.

## Known limitations

* Legacy BIOS only, no UEFI.
* No real USB stack. PS/2 keyboard and mouse only (BIOS legacy emulation
  works on most hardware).
* Single ATA disk, no SATA AHCI driver.
* FAT12 only. No FAT16 / FAT32 / ext.
* No networking.
* Cooperative multitasking only — no preemption. A misbehaving app can
  starve every other task.
* SB16 driver supports 8-bit single-cycle PCM only. No 16-bit or auto-init
  yet, and the existing apps haven't been ported off the PC speaker, so
  setting SB16 currently just silences the speaker beeps.

## License

BoxOS is **MIT-licensed** for everything except the DOOM port. The DOOM
sources in `src/apps/doom/` and `src/apps/doomgen/` remain under GPL v2
or later (id Software + Simon Howard's headers). `DOOM1.WAD` is the
shareware DOOM data file, redistributed under id's shareware terms.

See [`LICENSE`](LICENSE) for the full text.
