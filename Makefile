# BoxOS build
# -------------------------------------------------------------
# Layout of the resulting raw disk image (boxos.img):
#   LBA 0       boot.bin   (1 sector,  512 B)
#   LBA 1..8    stage2.bin (8 sectors, 4 KiB - padded by NASM)
#   LBA 9..199  kernel.bin (up to 191 sectors / 95 KiB; stage 1 does
#               two INT 13h reads to cover the 127-sector per-call cap)
# -------------------------------------------------------------

ASM      := nasm
QEMU     := qemu-system-x86_64

# Toolchain. macOS has no ELF-capable native toolchain, so it needs the
# x86_64-elf-* cross tools from Homebrew. On Linux (and WSL) the native
# gcc/ld/objcopy already emit 64-bit ELF, which is exactly what
# linker.ld and the objcopy -O binary step expect. Prefer the cross
# tools when they're installed, otherwise fall back to the native ones.
# Override on the command line to force either: `make CC=clang`.
HAVE_CROSS := $(shell command -v x86_64-elf-gcc >/dev/null 2>&1 && echo yes)
ifeq ($(HAVE_CROSS),yes)
CC       := x86_64-elf-gcc
LD       := x86_64-elf-ld
OBJCOPY  := x86_64-elf-objcopy
else
CC       := gcc
LD       := ld
OBJCOPY  := objcopy
endif

# Per-build tag stamped into the boot banner so it's obvious which
# build is actually running on the target. Updated every `make`.
BUILD_TAG := $(shell date +%H:%M:%S)

CFLAGS   := -ffreestanding -nostdlib -fno-stack-protector -fno-pie -fno-pic \
            -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mno-3dnow \
            -mcmodel=large -fno-builtin -fno-asynchronous-unwind-tables \
            -fcf-protection=none -m64 -O2 -Wall -Wextra \
            -Isrc/kernel/include
LDFLAGS  := -nostdlib -static -z noexecstack -T linker.ld

BUILD    := build
BOOT     := $(BUILD)/boot.bin
STAGE2   := $(BUILD)/stage2.bin
KASMOBJS := $(BUILD)/kentry.o $(BUILD)/idt_stubs.o $(BUILD)/context.o
KOBJS    := $(BUILD)/kernel.o $(BUILD)/vga.o $(BUILD)/string.o \
            $(BUILD)/idt.o $(BUILD)/pic.o $(BUILD)/keyboard.o \
            $(BUILD)/mouse.o \
            $(BUILD)/shell.o $(BUILD)/ata.o $(BUILD)/fat12.o \
            $(BUILD)/loader.o $(BUILD)/timer.o $(BUILD)/heap.o \
            $(BUILD)/gfx.o $(BUILD)/fb.o $(BUILD)/fbcon.o \
            $(BUILD)/gui.o $(BUILD)/cursor.o $(BUILD)/wm.o \
            $(BUILD)/desktop.o $(BUILD)/exe.o $(BUILD)/sound.o \
            $(BUILD)/theme.o $(BUILD)/font_8x8.o $(BUILD)/task.o \
            $(BUILD)/sb16.o
KELF     := $(BUILD)/kernel.elf
KBIN     := $(BUILD)/kernel.bin
IMG      := $(BUILD)/boxos.img
# IMPORTANT: The .iso and .img paths are intentionally stable at the
# project root so UTM never has to be re-pointed after a rebuild.
ISO      := boxos.iso
FSIMG    := boxos-fs.img
ISO_ROOT := $(BUILD)/iso_root

# Apps. Two flavours:
#   src/apps/<name>.asm  -> flat-binary built directly with NASM
#   src/apps/<name>.c    -> C compiled with the cross-compiler,
#                            linked with crt0 + lib at 0x200000,
#                            objcopy'd to flat binary.
APPSRC_ASM := $(wildcard src/apps/*.asm)
APPSRC_C   := $(wildcard src/apps/*.c)
APPS_ASM   := $(APPSRC_ASM:src/apps/%.asm=$(BUILD)/apps/%.bin)
APPS_C     := $(APPSRC_C:src/apps/%.c=$(BUILD)/apps/%.bin)
APPS       := $(APPS_ASM) $(APPS_C)

# Raw data files dropped into src/apps/ (e.g. DOOM1.WAD). These are
# copied verbatim into the FAT12 image alongside the built binaries.
APP_ASSETS := $(wildcard src/apps/*.WAD) $(wildcard src/apps/*.wad) \
              $(wildcard src/apps/*.DAT) $(wildcard src/apps/*.dat) \
              $(wildcard src/apps/*.TXT) $(wildcard src/apps/*.txt)

# Shared C-app runtime: crt0.o + libc-ish helpers.
APPLIB_SRCS := $(wildcard src/apps/lib/*.c)
APPLIB_OBJS := $(BUILD)/apps/lib/app_crt.o \
               $(APPLIB_SRCS:src/apps/lib/%.c=$(BUILD)/apps/lib/%.o)

APP_CFLAGS  := -ffreestanding -nostdlib -fno-stack-protector \
               -fno-pie -fno-pic -mno-red-zone \
               -mno-mmx -mno-sse -mno-sse2 -mno-3dnow \
               -mcmodel=large -fno-builtin \
               -fno-asynchronous-unwind-tables -fcf-protection=none \
               -m64 -O2 -Wall -Wextra \
               -Isrc/apps/include
APP_LDFLAGS := -nostdlib -static -z noexecstack -T src/apps/app.ld

# ----- DOOM (FastDoom) app ---------------------------------------
# A "fat" app: ~60 .c files in src/apps/doom/ link together into a
# single DOOM.BIN. Built with extra warning suppression because the
# upstream source is C89-style and was written for OpenWatcom, not GCC.
# ----- doomgeneric port (replaces FastDoom) -----------------------
# Cleaner DOOM port: ~80 .c files, no .asm, no DOS-isms. Engine
# compiles against std-C; we only implement the 6 DG_* functions.
DOOM_DIR    := src/apps/doomgen
DOOM_SHIM   := $(DOOM_DIR)/dosshim
DOOM_SRCS   := $(wildcard $(DOOM_DIR)/*.c)
DOOM_OBJS   := $(DOOM_SRCS:$(DOOM_DIR)/%.c=$(BUILD)/apps/doomgen/%.o)
DOOM_LIBC_OBJ  := $(BUILD)/apps/doomgen/libc.o
DOOM_STUBS_OBJ := $(BUILD)/apps/doomgen/asm_stubs.o
DOOM_SETJMP_OBJ:= $(BUILD)/apps/doomgen/setjmp.o
DOOM_BIN    := $(BUILD)/apps/DOOM.BIN

# Build-target macro tells FastDoom's options.h to pick mode 13h.
# -include the BoxOS compat header so DOS keywords (__far, etc.)
# evaporate before any FastDoom .c gets to see them.
DOOM_CFLAGS := $(APP_CFLAGS) -DBOXOS -DCMAP256 \
               -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 \
               -I$(DOOM_SHIM) -I$(DOOM_DIR) -Isrc/apps/include \
               -include $(DOOM_DIR)/boxos_compat.h \
               -std=gnu99 -fcommon \
               -Wno-implicit-function-declaration \
               -Wno-implicit-int \
               -Wno-int-conversion \
               -Wno-incompatible-pointer-types \
               -Wno-builtin-declaration-mismatch \
               -Wno-format -Wno-parentheses -Wno-pointer-sign \
               -Wno-unused-but-set-variable -Wno-unused-variable \
               -Wno-unused-function -Wno-unused-parameter \
               -Wno-sign-compare -Wno-misleading-indentation \
               -Wno-strict-aliasing

.PHONY: all iso fs run run-iso run-bootable debug clean tools bootable

all: $(ISO) $(FSIMG) bootable

# ----- one-file bootable for USB / Rufus / real hardware ---------
# Layout in 'boxos-bootable.img':
#   LBA 0..2047       : boot sector + stage2 + kernel (with growth headroom)
#   LBA 2048..67583   : the LIVE FAT12 filesystem (32 MiB)
#   LBA 67584..133119 : a read-only FACTORY copy of the same FAT12 (32 MiB)
# `FACTORY` in the shell copies LBA 67584+ over LBA 2048+ to undo
# everything the user has done since first boot. fs_mount() probes
# both LBA 0 (legacy two-disk setup) and LBA 2048 (combined image).
BOOTABLE       := boxos-bootable.img
FS_LIVE_LBA    := 2048
FS_FACTORY_LBA := 67584
FS_SECTORS     := 65536

bootable: $(BOOTABLE)

$(BOOTABLE): $(IMG) $(FSIMG)
	@dd if=/dev/zero of=$@ bs=512 count=$$(($(FS_LIVE_LBA) + $(FS_SECTORS) + $(FS_SECTORS))) status=none
	@dd if=$(IMG)   of=$@ bs=512                                  conv=notrunc status=none
	@dd if=$(FSIMG) of=$@ bs=512 seek=$(FS_LIVE_LBA)              conv=notrunc status=none
	@dd if=$(FSIMG) of=$@ bs=512 seek=$(FS_FACTORY_LBA)           conv=notrunc status=none
	@printf "wrote %s (%s sectors total: 0..%d boot+kernel, %d+ live FAT12, %d+ factory copy)\n" \
	    $@ "$$(($$(wc -c < $@) / 512))" $$(($(FS_LIVE_LBA) - 1)) $(FS_LIVE_LBA) $(FS_FACTORY_LBA)

# Audio flags for PC speaker. coreaudio on macOS, PulseAudio elsewhere;
# override with `make run AUDIO_BACKEND=alsa` if your box wants ALSA.
# Without these, QEMU silently accepts the speaker IO writes but
# produces no output — Settings > Sound > Test will look broken.
ifeq ($(shell uname -s),Darwin)
AUDIO_BACKEND ?= coreaudio
else
AUDIO_BACKEND ?= pa
endif
QEMU_AUDIO    := -audiodev $(AUDIO_BACKEND),id=snd \
                 -machine type=pc,pcspk-audiodev=snd \
                 -device sb16,audiodev=snd

# Boot the combined image directly.
run-bootable: $(BOOTABLE)
	$(QEMU) $(QEMU_AUDIO) \
	  -drive format=raw,file=$(BOOTABLE),if=ide,index=0 \
	  -no-reboot -no-shutdown

$(BUILD):
	@mkdir -p $(BUILD)

$(BOOT): src/boot/boot.asm | $(BUILD)
	$(ASM) -f bin $< -o $@

$(STAGE2): src/boot/stage2.asm | $(BUILD)
	$(ASM) -f bin $< -o $@

$(BUILD)/%.o: src/kernel/%.asm | $(BUILD)
	$(ASM) -f elf64 $< -o $@

$(BUILD)/%.o: src/kernel/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# kernel.o always rebuilds: it embeds BUILD_TAG so we can confirm a
# fresh sync from the boot banner. .PHONY dep forces the recompile.
.PHONY: force-kernel-build-tag
force-kernel-build-tag: ;
# force-kernel-build-tag is a normal (not order-only) prereq, so the
# phony recipe always runs and kernel.o always rebuilds, picking up
# a fresh BUILD_TAG every make invocation.
$(BUILD)/kernel.o: src/kernel/kernel.c force-kernel-build-tag | $(BUILD)
	$(CC) $(CFLAGS) -DBUILD_TAG='"$(BUILD_TAG)"' -c $< -o $@

$(KELF): $(KASMOBJS) $(KOBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(KASMOBJS) $(KOBJS)

$(KBIN): $(KELF)
	$(OBJCOPY) -O binary $< $@

# Assemble the final raw disk image. We pre-create a 1.44 MiB blank
# image so dd's `seek=` writes don't stop short of the kernel.
$(IMG): $(BOOT) $(STAGE2) $(KBIN)
	@dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	@dd if=$(BOOT)   of=$@ bs=512 seek=0 conv=notrunc status=none
	@dd if=$(STAGE2) of=$@ bs=512 seek=1 conv=notrunc status=none
	@dd if=$(KBIN)   of=$@ bs=512 seek=9 conv=notrunc status=none
	@echo "built $@ ($$(wc -c < $@) bytes)"
	@printf "  boot.bin   %6d bytes\n" "$$(wc -c < $(BOOT))"
	@printf "  stage2.bin %6d bytes\n" "$$(wc -c < $(STAGE2))"
	@printf "  kernel.bin %6d bytes\n" "$$(wc -c < $(KBIN))"

# `boxos.iso` — the canonical, FINAL format: a real bootable El
# Torito ISO 9660. The raw boxos-bootable.img is embedded as
# BOXOS.IMG; SeaBIOS preloads 260 sectors (boot + stage 2 + kernel)
# via no-emulation El Torito; our boot sector spots the BOX2 marker
# and skips the disk read; the kernel's ATAPI + ISO 9660 parser
# locates BOXOS.IMG on the CD and mounts the FAT12 inside it.
#
# UTM: attach as "CD/DVD (ISO) Image" — this is the only attach type
# that boots it. The Setup wizard runs; on its Install Target page
# pick a *second, non-removable Disk Image* drive (a CD can't store
# an install — it's read-only). After Setup finishes, eject the CD
# and the OS boots from the target drive.
#
# `boxos-bootable.img` (also built) is the raw HDD variant for
# people who'd rather attach a single writable Disk Image and do an
# in-place install with no second drive.
$(ISO): $(BOOTABLE)
	@command -v xorriso >/dev/null || { \
	    echo "error: xorriso not found. brew install xorriso"; \
	    exit 1; \
	}
	@rm -rf $(BUILD)/iso_root
	@mkdir -p $(BUILD)/iso_root
	@cp $(BOOTABLE) $(BUILD)/iso_root/BOXOS.IMG
	@xorriso -as mkisofs \
	    -V BOXOS_ARISE \
	    -b BOXOS.IMG \
	    -no-emul-boot \
	    -boot-load-size 260 \
	    -o $@ \
	    $(BUILD)/iso_root/ >/dev/null 2>&1
	@echo "wrote $@ ($$(wc -c < $@) bytes)  <-- bootable ISO 9660 + El Torito (attach as CD/DVD)"

iso: $(ISO)
fs:  $(FSIMG)

# ----- apps -------------------------------------------------------
$(BUILD)/apps:
	@mkdir -p $@

$(BUILD)/apps/lib:
	@mkdir -p $@

# ASM apps -> direct flat binary
$(BUILD)/apps/%.bin: src/apps/%.asm src/apps/_app_template.inc | $(BUILD)/apps
	$(ASM) -f bin -I src/apps/ $< -o $@

# C apps: compile, link with crt0+lib via app.ld, then objcopy to bin.
$(BUILD)/apps/lib/app_crt.o: src/apps/lib/app_crt.asm | $(BUILD)/apps/lib
	$(ASM) -f elf64 $< -o $@

$(BUILD)/apps/lib/%.o: src/apps/lib/%.c | $(BUILD)/apps/lib
	$(CC) $(APP_CFLAGS) -c $< -o $@

$(BUILD)/apps/%.o: src/apps/%.c | $(BUILD)/apps
	$(CC) $(APP_CFLAGS) -c $< -o $@

$(BUILD)/apps/%.elf: $(BUILD)/apps/%.o $(APPLIB_OBJS) src/apps/app.ld
	$(LD) $(APP_LDFLAGS) -o $@ $(APPLIB_OBJS) $<

$(BUILD)/apps/%.bin: $(BUILD)/apps/%.elf
	$(OBJCOPY) -O binary $< $@

# DOOM: each .c -> .o, then all .o + applib link into one big flat binary.
$(BUILD)/apps/doomgen:
	@mkdir -p $@

# Static pattern rule: explicitly list every doom .o so this rule
# beats the broader $(BUILD)/apps/%.o pattern that uses APP_CFLAGS.
$(DOOM_OBJS): $(BUILD)/apps/doomgen/%.o: $(DOOM_DIR)/%.c | $(BUILD)/apps/doomgen
	$(CC) $(DOOM_CFLAGS) -c $< -o $@

$(DOOM_LIBC_OBJ): $(DOOM_SHIM)/libc.c | $(BUILD)/apps/doomgen
	$(CC) $(DOOM_CFLAGS) -c $< -o $@

# asm_stubs.o not needed for doomgeneric — left over from FastDoom.

$(DOOM_SETJMP_OBJ): $(DOOM_SHIM)/setjmp.asm | $(BUILD)/apps/doomgen
	$(ASM) -f elf64 $< -o $@

# applib minus string.o (libc.c provides memset/memcpy/etc).
DOOM_APPLIB := $(BUILD)/apps/lib/app_crt.o $(BUILD)/apps/lib/syscalls.o

$(BUILD)/apps/doomgen.elf: $(DOOM_OBJS) $(DOOM_LIBC_OBJ) $(DOOM_SETJMP_OBJ) $(DOOM_APPLIB) src/apps/app.ld
	$(LD) $(APP_LDFLAGS) -o $@ $(DOOM_APPLIB) $(DOOM_SETJMP_OBJ) $(DOOM_LIBC_OBJ) $(DOOM_OBJS)

$(DOOM_BIN): $(BUILD)/apps/doomgen.elf
	$(OBJCOPY) -O binary $< $@

.PHONY: doom doom-compile-test
doom: $(DOOM_BIN)

# Compile every .c independently. Useful for triaging what compiles
# vs what doesn't during the multi-session port. Counts errors.
doom-compile-test: | $(BUILD)/apps/doomgen $(DOOM_LIBC_OBJ)
	@total=0; ok=0; bad=0; \
	for f in $(DOOM_SRCS); do \
	  total=$$((total+1)); \
	  o=$(BUILD)/apps/doomgen/$$(basename $$f .c).o; \
	  if $(CC) $(DOOM_CFLAGS) -c $$f -o $$o 2>/dev/null; then \
	    ok=$$((ok+1)); \
	  else \
	    bad=$$((bad+1)); \
	    echo "FAIL: $$f"; \
	  fi; \
	done; \
	echo "----"; \
	echo "compiled: $$ok / $$total  (failed: $$bad)"

# ----- FAT12 filesystem image ------------------------------------
# 32 MiB FAT12 disk holding all app .bin files plus raw assets like
# DOOM1.WAD. Geometry: 1024 cyl x 16 heads x 4 sectors = 65536 sectors
# (32 MiB). Cluster size = 32 sectors (16 KiB) keeps cluster count
# ~2048, well under FAT12's 4084 limit.
$(BUILD)/make_test_exes: tools/make_test_exes.c | $(BUILD)
	@cc -O2 -Wall -o $@ $<

$(BUILD)/test_exes/HELLODOS.EXE \
$(BUILD)/test_exes/WIN1HELL.EXE \
$(BUILD)/test_exes/HELLO32.EXE: $(BUILD)/make_test_exes
	@mkdir -p $(BUILD)/test_exes
	@$(BUILD)/make_test_exes $(BUILD)/test_exes

# Best-effort test JPEG: convert any handy PNG via macOS's `sips`.
# If sips isn't available (other host), we just skip and the FAT12
# layout will not include HELLO.JPG -- the JPEG decoder still loads,
# users can paste their own .JPG files in via mcopy or via Paint.
$(BUILD)/test_jpg/HELLO.JPG:
	@mkdir -p $(BUILD)/test_jpg
	@if command -v sips >/dev/null 2>&1; then \
	   sips -s format jpeg -s formatOptions 80 -Z 240 \
	     /System/Library/Desktop\ Pictures/Solid\ Colors/Teal.png \
	     --out $@ >/dev/null 2>&1 || \
	   sips -s format jpeg -s formatOptions 80 -Z 240 \
	     /System/Library/CoreServices/DefaultDesktop.heic \
	     --out $@ >/dev/null 2>&1 || \
	   true; \
	fi
	@if [ ! -f $@ ]; then \
	   printf "%s" "(no sips JPEG available — skipping)" > $@; \
	fi

$(FSIMG): $(APPS) $(APP_ASSETS) $(DOOM_BIN) \
          src/apps/README.TXT src/apps/BOXOS.TXT \
          src/apps/SYSINFO.TXT src/apps/CONFIG.SYS \
          src/apps/SAMPLE.TXT  src/apps/MEDIA.TXT \
          src/apps/SAMPLE.MP3  src/apps/SAMPLE.MP4 \
          $(BOOT) $(STAGE2) $(KBIN) $(IMG) \
          $(BUILD)/test_exes/HELLODOS.EXE \
          $(BUILD)/test_exes/WIN1HELL.EXE \
          $(BUILD)/test_exes/HELLO32.EXE \
          $(BUILD)/test_jpg/HELLO.JPG
	@dd if=/dev/zero of=$@ bs=512 count=65536 status=none
	@mformat -i $@ -t 1024 -h 16 -s 4 -c 32 -v BOXOS ::
	@mmd -i $@ ::/APPS ::/GAMES ::/SYS ::/SYS/SYSTEM32 ::/DOCS
	@mcopy -i $@ -o src/apps/README.TXT  ::/
	@mcopy -i $@ -o src/apps/BOXOS.TXT   ::/SYS/SYSTEM32/
	@mcopy -i $@ -o src/apps/SYSINFO.TXT ::/SYS/SYSTEM32/
	@mcopy -i $@ -o src/apps/CONFIG.SYS  ::/SYS/SYSTEM32/
	@mcopy -i $@ -o $(BOOT)              ::/SYS/SYSTEM32/BOOT.BIN
	@mcopy -i $@ -o $(STAGE2)            ::/SYS/SYSTEM32/STAGE2.BIN
	@mcopy -i $@ -o $(KBIN)              ::/SYS/SYSTEM32/KERNEL.BIN
	@mcopy -i $@ -o $(IMG)               ::/SYS/SYSTEM32/BOXOS.IMG
	@# Apps go to /APPS — except game-flavoured ones, which live in /GAMES.
	@for app in $(APPS); do \
	   case "$$app" in \
	     */winsnake.bin|*/winmines.bin|*/winpong.bin|*/winrvrs.bin|*/wintris.bin|*/winsoli.bin) \
	         mcopy -i $@ -o $$app ::/GAMES/;; \
	     *) mcopy -i $@ -o $$app ::/APPS/;; \
	   esac; \
	 done
	@mcopy -i $@ -o $(BUILD)/test_exes/HELLODOS.EXE ::/APPS/
	@mcopy -i $@ -o $(BUILD)/test_exes/WIN1HELL.EXE ::/APPS/
	@mcopy -i $@ -o $(BUILD)/test_exes/HELLO32.EXE  ::/APPS/
	@mcopy -i $@ -o src/apps/SAMPLE.TXT             ::/DOCS/
	@mcopy -i $@ -o src/apps/MEDIA.TXT              ::/DOCS/
	@mcopy -i $@ -o src/apps/SAMPLE.MP3             ::/DOCS/
	@mcopy -i $@ -o src/apps/SAMPLE.MP4             ::/DOCS/
	@mcopy -i $@ -o $(BUILD)/test_jpg/HELLO.JPG     ::/DOCS/ || true
	@for asset in $(filter-out %.TXT %.txt,$(APP_ASSETS)); do mcopy -i $@ -o $$asset ::/GAMES/; done
	@if [ -f $(DOOM_BIN) ]; then mcopy -i $@ -o $(DOOM_BIN) ::/GAMES/; fi
	@echo "wrote $@ — layout:"
	@mdir -i $@ -b ::/ ::/APPS ::/GAMES ::/SYS ::/SYS/SYSTEM32 ::/DOCS

# ----- run targets -----------------------------------------------
# `make run` boots the .img directly with the FS as a second IDE disk.
run: $(IMG) $(FSIMG)
	$(QEMU) $(QEMU_AUDIO) \
	  -drive format=raw,file=$(IMG),if=ide,index=0 \
	  -drive format=raw,file=$(FSIMG),if=ide,index=1 \
	  -no-reboot -no-shutdown

# `make run-iso` mirrors what UTM does: boot from CD, with the FS as
# the primary IDE drive (which is what UTM's "first disk" becomes).
run-iso: $(ISO) $(FSIMG)
	$(QEMU) $(QEMU_AUDIO) \
	  -cdrom $(ISO) -boot d \
	  -drive format=raw,file=$(FSIMG),if=ide,index=0 \
	  -no-reboot -no-shutdown

debug: $(IMG) $(FSIMG)
	$(QEMU) $(QEMU_AUDIO) \
	  -drive format=raw,file=$(IMG),if=ide,index=0 \
	  -drive format=raw,file=$(FSIMG),if=ide,index=1 \
	  -no-reboot -no-shutdown -s -S

clean:
	rm -rf $(BUILD) $(ISO) $(FSIMG) $(BOOTABLE)

# ----- sync to UTM ------------------------------------------------
# UTM copies imported drives into its VM bundle, so rebuilds on disk
# don't reach the running VM. This target replaces UTM's cached
# copies of the boot ISO and the FAT12 disk in-place.
#
# Usage:  make sync-utm           (assumes the VM is called "BoxOS")
#         make sync-utm UTM_VM=Foo (otherwise)
UTM_VM  ?= BoxOS
UTM_DIR := $(HOME)/Library/Containers/com.utmapp.UTM/Data/Documents/$(UTM_VM).utm/Data

.PHONY: sync-utm
# Each artifact is synced into the file UTM actually uses. UTM may
# rename imported disks (e.g. boxos-fs.qcow2 -> boxos-fs-2.qcow2)
# when re-imported, so we glob by name pattern and write whichever
# matching file exists. We never write to the wrong file because the
# patterns ('*-fs*.qcow2' vs '*.iso' vs the specific boot-disk path)
# are disjoint.
#
# Also stops the VM before swapping the disk and starts it again
# after — UTM caches the qcow2 while a VM is running, so without the
# stop/start the running VM keeps showing the *old* contents.
sync-utm: $(BOOTABLE) $(ISO)
	@test -d "$(UTM_DIR)" || { \
	    echo "UTM VM data folder not found:"; \
	    echo "  $(UTM_DIR)"; \
	    echo "Pass UTM_VM=<vm-name> if yours isn't 'BoxOS'."; \
	    exit 1; }
	@# Stop the VM if it's running so UTM releases its handle on the
	@# qcow2 and the convert below actually replaces what the VM uses.
	@was_running=$$(osascript -e "tell application \"UTM\" to status of virtual machine named \"$(UTM_VM)\"" 2>/dev/null); \
	if [ "$$was_running" = "started" ]; then \
	    echo "  vm:    stopping '$(UTM_VM)' before sync"; \
	    osascript -e "tell application \"UTM\" to stop virtual machine named \"$(UTM_VM)\"" >/dev/null 2>&1 || true; \
	    sleep 2; \
	fi
	@# The VM is the El Torito install layout: a writable target HDD
	@# (boxos-target.qcow2, left untouched — Setup writes the install
	@# onto it) plus boxos.iso in the CD slot. We refresh the ISO so
	@# the installer always carries the latest kernel + apps. A blank
	@# target qcow2 is created on first sync if one isn't present.
	@cp $(ISO) "$(UTM_DIR)/boxos.iso"
	@echo "  iso:   $(ISO) -> $(UTM_DIR)/boxos.iso (installer media)"
	@if [ ! -f "$(UTM_DIR)/boxos-target.qcow2" ]; then \
	    qemu-img create -f qcow2 -o preallocation=full "$(UTM_DIR)/boxos-target.qcow2" 64M >/dev/null; \
	    echo "  hdd:   created blank boxos-target.qcow2 (64 MiB, preallocated) — Setup installs here"; \
	else \
	    echo "  hdd:   boxos-target.qcow2 left as-is (your installed disk)"; \
	fi
	@# Legacy single-HDD VMs: keep any *fs*.qcow2 in sync with the raw
	@# bootable image (never touch *target* — that's the install dest).
	@fs=$$(find "$(UTM_DIR)" -maxdepth 1 -name '*fs*.qcow2' ! -name '*target*' | head -n 1); \
	if [ -n "$$fs" ]; then \
	    qemu-img convert -f raw -O qcow2 $(BOOTABLE) "$$fs.new" && mv "$$fs.new" "$$fs"; \
	    echo "  hdd:   $(BOOTABLE) -> $$fs (legacy single-disk VM)"; \
	fi
	@# Restart the VM so it picks up the newly-written disk. We then
	@# poll the status for a few seconds to confirm UTM actually
	@# brought it up (osascript "start" can return before the VM is
	@# really running, and silently fail if the previous stop hasn't
	@# fully released the disk yet).
	@osascript -e "tell application \"UTM\" to start virtual machine named \"$(UTM_VM)\"" >/dev/null 2>&1 || true
	@for try in 1 2 3 4 5 6; do \
	    sleep 1; \
	    st=$$(osascript -e "tell application \"UTM\" to status of virtual machine named \"$(UTM_VM)\"" 2>/dev/null); \
	    if [ "$$st" = "started" ]; then \
	        echo "  vm:    started '$(UTM_VM)' with new disk"; \
	        break; \
	    fi; \
	    if [ "$$try" = "3" ]; then \
	        echo "  vm:    retry start (status='$$st')"; \
	        osascript -e "tell application \"UTM\" to start virtual machine named \"$(UTM_VM)\"" >/dev/null 2>&1 || true; \
	    fi; \
	    if [ "$$try" = "6" ]; then \
	        echo "  vm:    WARNING — '$(UTM_VM)' is '$$st' after start attempts; check UTM manually"; \
	    fi; \
	done
	@echo ""
	@echo "sync-utm done."

# A blank, writable HDD for the El Torito Setup to install onto.
# Attach this in UTM as a second drive (Image Type "Disk Image",
# Read Only OFF) alongside boxos.iso, then run Setup and pick it.
# Fully preallocated so the install copy never stalls on qcow2-style
# on-demand cluster allocation (which can trip the ATA write timeout).
.PHONY: target-disk
target-disk:
	@qemu-img create -f raw boxos-target.img 64M >/dev/null
	@echo "wrote boxos-target.img (64 MiB blank) — attach as a Disk Image in UTM"

tools:
	@echo "Required: $(ASM) $(CC) $(LD) $(OBJCOPY) $(QEMU)"
	@which $(ASM) $(CC) $(LD) $(OBJCOPY) $(QEMU)
