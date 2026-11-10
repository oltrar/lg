# Looking Glass B7 Linux guest for KDE and GNOME Wayland

## Setup

For CachyOS/Arch guests with NVIDIA GPU passthrough, KDE or GNOME
Wayland, and a Looking Glass **B7** client on the physical Linux host.
**Experimental:** CPU-copy SDR capture; native Wayland clipboard is not
implemented. See [limitations](#material-limitations) and
[validation](VALIDATION.md) before using it.

## 1. Guest dependencies

Use an up-to-date CachyOS guest. In its KDE Wayland session:

```bash
sudo pacman -S --needed base-devel git cmake ninja pkgconf pipewire glib2 \
  xdg-desktop-portal xdg-desktop-portal-kde dkms pciutils
```

For a GNOME guest/session, use `xdg-desktop-portal-gnome` as well:

```bash
sudo pacman -S --needed xdg-desktop-portal-gnome
```

Both backend packages may coexist when both desktops are installed. The active
desktop should select its own backend. Avoid forcing a KDE-only portal backend
globally for GNOME. Log into the intended desktop normally; do not launch the
guest capture program with `sudo` or from a plain SSH session.

Install headers matching **the exact running guest kernel**. For the standard
CachyOS kernel this is usually `linux-cachyos-headers`; LTS and other variants
need their matching header package. Check:

```bash
uname -r
ls -ld /usr/lib/modules/"$(uname -r)"/build
```

## 2. Build the guest program

Clone this repository inside the guest (or extract the branch archive),
then build from its root:

```bash
git clone https://github.com/oltrar/lg.git
cd lg
bash build.sh
```

When reviewing an unmerged PR, check out its branch before running `build.sh`.

The script fetches upstream B7 commit
`27fe47cbe2a3a8da986d310ab866f0b646ed68f5`, initializes its pinned submodules
(required by B7's configure checks), applies the source overlay, runs the tests, and
builds only the Linux guest host with PipeWire capture. It does not replace
your client, install system files or change the VM configuration.

Binary: `_build/host/looking-glass-host`.

## 3. Give the Linux VM its own IVSHMEM allocation

On the **physical Linux host**, shut down the Linux VM and merge
`config/libvirt-devices.xml` into its existing libvirt definition. It is a
snippet, not a full VM definition. Replace matching existing graphics/audio/
video elements; do not paste duplicate ones.

The example uses:

- Shared memory: `/dev/shm/looking-glass-linux`, 128 MiB, suitable as an initial
  4K SDR allocation. Higher maximum dimensions can need more memory.
- SPICE input/audio listener: **127.0.0.1:5901**.
- Existing NVIDIA VFIO passthrough stays in the domain.

Use a different memory allocation and SPICE port from your Windows VM. Two
running VMs must never write to the same Looking Glass shared-memory region.

For initial setup, retain a way to log into and operate the guest's portal
chooser, such as a physical display on the NVIDIA GPU or a temporary console.
Choose the display rendered by that GPU, not an emulated console display.
After first capture is verified, use the snippet's final:

```xml
<video><model type='none'/></video>
```

This removes the emulated display and prevents SPICE video fallback. Make that
change with the VM shut down. Future screen-sharing prompts may still need
interaction inside the guest; an unattended login-screen workflow is not
implemented here.

Libvirt creates the host-side memory file after VM startup. Give the physical
host user running the client read/write access through your normal Looking
Glass permissions setup, using the new filename. A temporary ACL after the
file exists preserves QEMU's existing ownership:

```bash
sudo setfacl -m u:"$USER":rw /dev/shm/looking-glass-linux
```

This command runs on the **physical host**, and may need repeating if libvirt
recreates the file. Its persistent equivalent depends on your existing
QEMU/libvirt permissions configuration. Do not make it world-writable.

## 4. Guest KVMFR driver

Inside the **Linux guest**, after step 2 and with the IVSHMEM PCI device present:

```bash
lspci -nn | grep '1af4:1110'
(
  cd _upstream/LookingGlass/module
  sudo dkms install .
)
sudo install -Dm644 config/70-kvmfr.rules /etc/udev/rules.d/70-kvmfr.rules
sudo install -Dm644 config/kvmfr-guest.conf /etc/modules-load.d/kvmfr.conf
sudo udevadm control --reload-rules
sudo modprobe kvmfr
sudo udevadm trigger --subsystem-match=kvmfr
ls -l /dev/kvmfr*
```

If DKMS reports an existing `kvmfr/0.0.12` installation, inspect that existing
installation before changing it. This project does not overwrite a different
DKMS source with the same version. If the kernel rejects the module, inspect
`dmesg`; signing may be required by the guest's Secure Boot configuration.

**In the guest, load `kvmfr` without `static_size_mb`.** It must bind to the
PCI IVSHMEM device supplied by QEMU. A static allocation inside the guest
creates unrelated RAM, not the shared region the physical host sees. If you
already configured static devices there, correct that configuration and
reboot before continuing. Guest and physical host `/dev/kvmfr0` names refer to
different devices and their indices need not match.

The active graphical session should obtain access via the included `uaccess`
rule. Check `test -r /dev/kvmfr0 && test -w /dev/kvmfr0`. Use `bash doctor.sh`
if it fails. Do not run the capture application as root to bypass this.

## 5. Start capture and your existing B7 client

Inside a terminal in the **guest KDE or GNOME Wayland desktop**:

```bash
bash run-guest.sh
```

Accept the screen-sharing prompt and select the NVIDIA-driven monitor.
For a different guest KVMFR device or requested rate:

```bash
LG_SHM=/dev/kvmfr1 LG_FPS=120 bash run-guest.sh
```

Inside the **physical Linux host**:

```bash
looking-glass-client -C /absolute/path/to/config/client-linux.ini
```

Copy the supplied client INI there first, and adjust its shared-memory path
and SPICE port if you changed them. Keep it separate from your Windows-VM
client configuration. Use B7; do not substitute B8's Windows IDD workflow.
The sample captures input on startup. B7's default capture/release key is
Scroll Lock; verify your own client key configuration first. Relative mouse
input continues to work on Wayland through QEMU's guest input devices, without
a compositor-specific input-injection backend. Adjust guest acceleration in
desktop settings if needed.

For audio, select the emulated ICH9/Intel HDA sound card in the guest. NVIDIA
HDMI audio is a different device and is not automatically carried by SPICE.

Stop guest capture with Ctrl+C. For optional autostart, add `run-guest.sh` in
KDE's Autostart settings, or add a GNOME desktop autostart entry that runs the
script with `/usr/bin/bash`. Keep the project at the same path. A restored
portal selection is subject to desktop permission policy. This is deliberately
a graphical-session program, not a system service capturing the login screen.

## 6. Verify the actual VM

Perform each check under **both KDE and GNOME**, using a fresh login to each:

1. With emulated video disabled, capture starts and the B7 log reports frames
   and `OS: Linux`. Without IVSHMEM capture there should be no SPICE picture.
2. Type, click, drag, scroll, and capture/release input. Check the embedded
   cursor and any desktop shortcuts important to you.
3. Resize/change the guest display resolution within allocated memory limits;
   verify a clean image and correct colors. Test scaling separately.
4. Play guest audio through the emulated sound card.
5. Stop/start the client and capture program. Verify portal selection restore,
   refusal/cancellation, lock/unlock, and logout behavior.
6. Measure your desired resolution and refresh rate in B7's statistics, while
   observing guest CPU usage. A configured 120 fps is a request, not a result.
7. Verify that Windows and Linux VMs can run with independent memory regions,
   ports and client configurations.

For diagnostics, run `bash doctor.sh` in the guest. Capture stderr/stdout from
`run-guest.sh` and `looking-glass-client` separately. Do not share restore-token
files from `~/.config/looking-glass-linux-wayland/`.

## How it works

An experimental, source-based Linux guest integration for a Linux QEMU/KVM
host, an NVIDIA GPU passed into a CachyOS/Arch guest, and the existing Looking
Glass **B7** client. It uses the standard ScreenCast portal and PipeWire inside
the guest, then publishes **uncompressed pixels through IVSHMEM/LGMP**.

```text
Guest KWin / GNOME Mutter
         | local ScreenCast portal + raw PipeWire buffers
         v
Patched looking-glass-host inside the guest
         | IVSHMEM / B7 LGMP shared memory
         v
Existing looking-glass-client B7 on the physical Linux host

Keyboard / mouse: client -> QEMU SPICE input -> guest virtual input devices
Audio: guest emulated sound card -> QEMU SPICE audio -> client
```

There is no video codec or network video transport in the intended display
path. The supplied final VM configuration removes the emulated video device
so B7 cannot silently fall back to a SPICE display. Input and audio can still
use SPICE. The NVIDIA GPU renders in the guest; this does not require NvFBC,
NVENC or a professional NVIDIA card.

**Status:** experimental source project, not a production-validated replacement
for the Windows guest software. Native Arch and Debian CI compile and link the
complete guest application and execute both test suites. The guest kernel
module and actual KDE/GNOME/NVIDIA/IVSHMEM integration still need validation in
your VM. Read `VALIDATION.md` for the exact evidence and limits.

## What is implemented

- B7-compatible guest frame publication using upstream LGMP and KVMFR code.
- A Wayland-native capture path shared by KDE and GNOME portal backends.
- Latest-frame selection, proper loop locking, producer-buffer ownership,
  padded source strides, chunk offsets and bounds checks.
- An owned frame snapshot so format changes cannot alter a frame being sent.
- Embedded guest cursor, SDR BGRA/RGBA, and a configurable requested capture
  rate from 1 to 360 fps (default 60). Actual cadence depends on the compositor.
- Bounded portal requests, cancellation handling, corrected session cleanup,
  and per-desktop portal restore tokens stored with mode 0600.
- Scripts, a separate Linux-VM client configuration, VM XML examples and tests.

## Material limitations

- **CPU-copy capture:** pixels are copied from mapped PipeWire memory to an
  owned snapshot and then to IVSHMEM. This is not Windows D12-style direct GPU
  copying. No zero-copy or frame-rate/latency claim is made.
- **Clipboard:** not implemented by this integration. It is disabled in the
  sample client configuration. Standard `spice-vdagent` is X11-oriented;
  installing it alone does not establish native clipboard sync on both KDE and
  GNOME Wayland. Files can be shared using your existing virtiofs setup.
- One selected monitor, SDR only. HDR, multiple simultaneous displays, automatic
  guest resolution changes, secure/login desktop capture and seamless absolute
  cursor positioning are outside this version. Use captured relative input.
- The guest must have an active desktop on the passed-through GPU. A real
  monitor, EDID emulator/dummy plug or separately configured headless output may
  be required. The Windows IDD is not a Linux display driver.
- First use requires accepting the guest's screen-sharing chooser. Restore
  tokens can reduce later prompts when supported; they cannot bypass desktop
  policy. Changing desktop/session/output or revoking permission may prompt
  again. Locking/logging out may pause or end capture.
- A guest kernel update can require a successful KVMFR rebuild. No claim is
  made that B7's module builds against every current CachyOS kernel.

## Source, licensing and references

This is an adaptation of the existing Linux host, not a clean-room replacement.
The overlay is GPL-2.0-or-later, preserving upstream licensing. The complete
base source is the pinned upstream commit fetched by `build.sh`; LGMP retains
its upstream license. See `LICENSE` and `NOTICE.md`.

- [Upstream Looking Glass B7 source](https://github.com/gnif/LookingGlass/tree/B7)
- [Upstream Linux guest status](https://looking-glass.io/docs/B7/install_host/#for-linux)
- [Upstream discussion of Linux capture and CPU copies](https://github.com/gnif/LookingGlass/issues/1265)
- [ScreenCast portal API](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html)
- [PipeWire threaded-loop locking](https://docs.pipewire.org/page_thread_loop.html)
- [Arch portal backend configuration](https://wiki.archlinux.org/title/XDG_Desktop_Portal)
