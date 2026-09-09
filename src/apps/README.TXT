Welcome to RadiDOS.

This is a hand-rolled 64-bit operating system. Boots, shells, runs apps,
plays DOOM. No BIOS calls after boot.

First-time? Type SETUP for a hardware report and getting-started.

Try these:

  HELP            list every command
  DIR / LS        show what's here
  CD GAMES        descend into the games folder
  CD ../APPS      go up and back down
  CAT BOXOS.TXT   read a file (try this from /SYS)
  RUN CALC        a tiny calculator (in /APPS)
  RUN DOOM        play DOOM (from /GAMES)

Paths: every file command (CAT, COPY, MOVE, EDIT, ...) accepts
either a name in the current directory or a /absolute/path.

  COPY README.TXT /SYS/HI.TXT
  MOVE NOTES.TXT /SYS/
  CD /

Layout:

  /                root
    APPS/          ascii, calc, echo2, hello, plasma
    GAMES/         DOOM.BIN + DOOM1.WAD
    SYS/           BOXOS.TXT (more info)

Single-file install: 'boxos.iso' (or equivalently 'boxos-bootable.img';
they're literal copies of each other) bundles the kernel, every app,
and a factory backup in one file.

  - Rufus (Windows): pick "DD image" mode, point at boxos.iso, write
    to a USB stick.
  - dd:  sudo dd if=boxos.iso of=/dev/sdX bs=1M conv=fsync
  - UTM: attach as a hard drive (NOT as a CD-ROM). Single drive in the
    VM; remove any other disks/CDs.

The target machine needs legacy BIOS / CSM enabled. UEFI-only setups
can't load it.

Inside the OS: type FACTORY to roll back to the as-shipped state.
First boot (and any post-factory boot) auto-runs SETUP.

Hit ESC inside DOOM for the menu, then pick "Quit" to return here.
