# Looking Glass B7 Linux guest for KDE and GNOME Wayland

## Setup

For CachyOS/Arch guests with NVIDIA GPU passthrough, KDE or GNOME
Wayland, and a Looking Glass **B7** client on the physical Linux host.
**Experimental:** CPU-copy SDR capture; native Wayland clipboard is not
implemented. See [limitations](#material-limitations) and
[validation](VALIDATION.md) before using it.

## 1. Guest dependencies — no KVMFR required

The preferred video transport is **plain IVSHMEM**, using the VM XML in step 3.
The Linux guest maps the PCI device's BAR2 directly through sysfs; the physical
host's existing B7 client reads the corresponding `/dev/shm/looking-glass` file.
Neither side needs KVMFR for this path. Keyboard/mouse, guest playback and host
microphone capture use B7's existing SPICE channels; SPICE video is disabled.

Inside an up-to-date CachyOS/Arch **KDE Wayland guest**:

```bash
sudo pacman -S --needed base-devel git cmake ninja pkgconf pipewire glib2 \
  xdg-desktop-portal xdg-desktop-portal-kde pciutils
```

For a GNOME guest/session also install:

```bash
sudo pacman -S --needed xdg-desktop-portal-gnome
```

Both portal backends may coexist. Log into the intended desktop normally and
let its portal backend handle capture. Do not run the capture application as
root or from a plain SSH login. Kernel headers and DKMS are only needed for the
[optional KVMFR path](docs/optional-kvmfr.md), not the preferred IVSHMEM path.

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

## 3. VM XML: plain IVSHMEM, input, sound and microphone

On the **physical Linux host**, shut down the Linux VM and use this exact
shared-memory device inside `<devices>`:

```xml
<shmem name='looking-glass'>
  <model type='ivshmem-plain'/>
  <size unit='M'>64</size>
</shmem>
```

Libvirt exposes its backing file on the physical host as
`/dev/shm/looking-glass`. Inside the guest it is a PCI device (`1af4:1110`),
not a `/dev/shm` file. The guest app now discovers and maps this device directly.

Merge [config/libvirt-devices.xml](config/libvirt-devices.xml) into your existing
definition, replacing matching elements. It includes SPICE on **127.0.0.1:5901**,
an ICH9 sound card explicitly linked to the SPICE audio backend, and the final
`<video><model type='none'/></video>` configuration. This allows keyboard/mouse,
playback and microphone capture without a SPICE display feed. Existing NVIDIA
VFIO passthrough stays in the definition.

**64 MiB is supported**, with the unchanged B7 raw-frame protocol. It is suitable
for an initial 1080p/1440p SDR setup. For a full 4K SDR frame, use **128 MiB**:
B7 keeps two frame buffers plus protocol/cursor metadata, so a 64 MiB region
cannot reliably hold full 4K frames. Refresh-rate changes alone do not increase
the number of frame buffers. The client can display truncated frames if you
exceed capacity; increase the region before using a larger output.

If your Windows VM also uses `name='looking-glass'`, do not run both VMs with the
same backing file. Either run them separately or rename one allocation (for
example `looking-glass-linux`) and update only that VM's client `shmFile` path.
The guest discovers by PCI ID/address, not by the libvirt shmem name. Use distinct
SPICE ports for concurrently running VMs as well.

Keep a way to operate the first guest screen-sharing chooser, such as a physical
display on the NVIDIA GPU or a temporary console. Choose the NVIDIA-driven
monitor. After capture is verified, shut down the VM and remove its emulated
video device to prevent SPICE video fallback. Future portal prompts may still
need interaction in the guest; login-screen capture is not implemented.

After VM startup, grant the physical host user access to the existing backing
file using your usual Looking Glass permission setup. A temporary ACL preserves
QEMU's ownership:

```bash
sudo setfacl -m u:"$USER":rw /dev/shm/looking-glass
```

This command runs on the **physical host**, not in the guest. It may need
repeating if libvirt recreates the file. Do not make the file world-writable.

## 4. Guest IVSHMEM permissions — no kernel module

Inside the Linux guest, check that the XML created the device:

```bash
lspci -nn | grep '1af4:1110'
```

Run the scoped setup helper once with privilege, using your guest desktop user:

```bash
sudo bash setup-ivshmem.sh --user "$USER" --install
```

It verifies the IVSHMEM vendor/device IDs, enables that device if needed, and
grants only this account access to **BAR2**. `--install` adds a root-owned helper
and a udev rule for the selected PCI address so the permission survives reboot.
Without `--install`, the change lasts until the sysfs device is recreated.
The capture application itself continues to run without root or capabilities.

If there are multiple IVSHMEM devices, select the guest's address explicitly:

```bash
sudo bash setup-ivshmem.sh --user "$USER" --device 0000:06:01.0 --install
```

Use the address reported by `lspci`, not the example address. The helper refuses
non-IVSHMEM devices and devices already bound to a driver. It never unbinds a
GPU or a driver automatically. If you previously installed guest KVMFR, you can
keep using it explicitly, or unbind **only the IVSHMEM device** before switching
to raw mapping. Do not alter the NVIDIA device's binding.

If the guest kernel's lockdown policy prohibits PCI resource mappings, this
method can be denied despite file permissions. The program reports the mapping
error; it does not bypass the policy or silently switch transports. See
[optional KVMFR](docs/optional-kvmfr.md) if that environment requires a driver.

To remove persistent raw-device setup, delete
`/etc/udev/rules.d/70-lg-ivshmem.rules` and
`/usr/local/libexec/lg-ivshmem-permissions`, reload udev rules, and reboot. Re-run
setup if the IVSHMEM PCI address changes after editing the domain.

## 5. Capture, keyboard/mouse, playback and shared microphone

In a terminal in the **guest KDE/GNOME Wayland desktop**, as your normal user:

```bash
bash run-guest.sh
```

The default is `LG_SHM=ivshmem`: discover exactly one IVSHMEM PCI device, validate
its BAR2 and map it. With multiple devices, select one explicitly:

```bash
LG_SHM=ivshmem:0000:06:01.0 LG_FPS=120 bash run-guest.sh
```

An ambiguous or inaccessible device produces a clear error, not a KVMFR
fallback. Accept the portal chooser and select the monitor on the NVIDIA GPU.

On the **physical Linux host**, copy the supplied client INI there, adjust its
SPICE port if needed, and run your existing B7 client:

```bash
looking-glass-client -C /absolute/path/to/config/client-linux.ini
```

The INI reads `/dev/shm/looking-glass`, enables SPICE input/audio, captures input
on startup, and enables microphone capture when a guest application requests it:

```ini
[audio]
micDefault=allow
micShowIndicator=yes
syncVolume=no
```

**The physical host microphone stays attached to the host.** Use its PipeWire
audio server to feed Looking Glass and host applications concurrently. Do not
USB/PCI-pass through the microphone or open its ALSA hardware device exclusively.
B7's PipeWire recording stream is non-exclusive; it does not take the microphone
away from other PipeWire/PulseAudio clients. The included config allows guest
microphone requests automatically and keeps B7's recording indicator enabled.
Use `micDefault=prompt` instead if you prefer approval for each request.

Verify the physical host client log reports **`Using AudioDev: PipeWire`**.
B7's PulseAudio backend implements playback but not microphone recording. If
your client lacks PipeWire support, rebuild B7 with its PipeWire audio backend
enabled; this does not change the B7 protocol or require KVMFR.

By default B7 uses the host's default PipeWire input source. To choose another
microphone, set the **host client** option below to its PipeWire `node.name`
(obtain it with `wpctl status` and `wpctl inspect NODE_ID`):

```ini
[pipewire]
recDevice=alsa_input.YOUR_MICROPHONE_NODE
```

Inside the guest select the **ICH9/Intel HDA** card for both output and input,
and select its input in the guest application that needs the microphone.
NVIDIA HDMI audio is a separate device and is not automatically carried by
SPICE. The SPICE listener must be on the physical host, and `spice:audio=yes`
must remain enabled. Do not start the B7 client with `-s`, which disables SPICE.

B7's default input capture/release key is Scroll Lock. Scroll Lock+E toggles
microphone recording; check your own escape-key customization. Captured relative
input uses QEMU's virtual input devices and works independently of the guest's
Wayland input-injection APIs. Adjust guest mouse acceleration if needed.

Stop capture with Ctrl+C. For autostart, add `run-guest.sh` to the guest desktop's
Autostart settings with `/usr/bin/bash`. Portal restoration remains subject to
desktop policy. Keep the project in the same path, and run it within the
graphical user session rather than as a root/system capture service.

## 6. Verify the actual VM

Under **both KDE and GNOME**, using a fresh login to each:

1. Check raw mapping works without a loaded `kvmfr` module and the guest log
   reports `PCI IVSHMEM BAR2` with **64 MiB**. The client should report Linux
   guest frames. With emulated video removed, no SPICE picture should appear
   if the guest capture application is stopped.
2. Type, click, drag and scroll. Capture/release input and check the embedded
   cursor and your important shortcuts.
3. Test your output resolution and scaling. Increase shared memory to 128 MiB
   before full 4K SDR capture. Measure actual cadence and CPU usage; requested
   FPS is not a performance guarantee.
4. Play sound in the guest using its emulated sound card.
5. Open a guest recorder and a **physical-host recorder at the same time**,
   using the same host PipeWire microphone. Verify both receive your voice,
   then close either application and verify the other still records. Check
   the B7 microphone indicator and the guest application's selected input.
6. Restart capture/client, cancel or revoke portal access, lock/unlock, and log
   out. Verify per-desktop source restoration and clear failure handling.
7. Keep allocations/ports separate if running Windows and Linux VMs together.

Run `bash doctor.sh` in the guest for read-only diagnostics. Keep guest and
physical-host client logs separate, and do not share the portal restore tokens
in `~/.config/looking-glass-linux-wayland/`.

`bash test.sh` tests layout and PCI discovery. `bash build.sh` also tests real
shared-memory mappings and backend publication. CI additionally boots a minimal
QEMU guest and checks **unprivileged 64 MiB PCI BAR2 mapping and guest-to-host
writes with no KVMFR driver**. This does not replace real NVIDIA/Wayland/audio
testing in your VM. See [VALIDATION.md](VALIDATION.md).

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
Playback: guest sound card -> QEMU SPICE audio -> client -> host PipeWire
Microphone: host PipeWire -> client -> QEMU SPICE record -> guest sound card
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

- B7-compatible guest frame publication using upstream LGMP with raw PCI IVSHMEM preferred; KVMFR stays optional.
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
- Raw PCI sysfs mapping depends on guest kernel policy and resource mapping
  support. Its memory caching/performance can differ from KVMFR, and the host
  client uses a CPU copy rather than KVMFR DMA-BUF import. No kernel module or
  rebuild is required for the default raw path. The optional KVMFR path still
  requires a module compatible with your guest kernel.

## Source, licensing and references

This is an adaptation of the existing Linux host, not a clean-room replacement.
The capture and PCI transport overlay is GPL-2.0-or-later, preserving upstream licensing. The complete
base source is the pinned upstream commit fetched by `build.sh`; LGMP retains
its upstream license. See `LICENSE` and `NOTICE.md`.

- [Upstream Looking Glass B7 source](https://github.com/gnif/LookingGlass/tree/B7)
- [Upstream Linux guest status](https://looking-glass.io/docs/B7/install_host/#for-linux)
- [Upstream discussion of Linux capture and CPU copies](https://github.com/gnif/LookingGlass/issues/1265)
- [ScreenCast portal API](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html)
- [QEMU IVSHMEM](https://www.qemu.org/docs/master/system/devices/ivshmem.html)
- [Linux PCI sysfs resource mapping](https://www.kernel.org/doc/html/latest/PCI/sysfs-pci.html)
- [PipeWire threaded-loop locking](https://docs.pipewire.org/page_thread_loop.html)
- [Arch portal backend configuration](https://wiki.archlinux.org/title/XDG_Desktop_Portal)
