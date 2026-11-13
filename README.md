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
sudo pacman -S --needed base-devel git cmake ninja pkgconf pipewire glib2 dbus \
  xdg-desktop-portal xdg-desktop-portal-kde pciutils desktop-file-utils
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

After updating this repository, run `bash build.sh` again before running
`bash run-guest.sh`. The build now records the source overlay it applied and
can replace an exact older managed overlay. For older builds without that
record, it recognizes exact overlay versions in this repository's Git history.
Unrecognized local source edits are preserved and stop the update. If you
upgrade from downloaded archives without Git history and see that message,
extract/build the new project in a separate directory instead of deleting edits.

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

During setup, keep QXL/the virtual console available to operate the guest's
screen-sharing chooser. Enable the NVIDIA/dummy-plug monitor and follow
[Choose the NVIDIA/dummy-plug display](#choose-the-nvidiadummy-plug-display)
below. Verify that Looking Glass captures that output and restores it after
restarting capture **before** removing QXL. Then shut down the VM and use the
final `video type='none'` configuration. Future portal prompts may still need
interaction in the guest; login-screen capture is not implemented.

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

Stop manual capture with Ctrl+C. The optional startup setup below runs capture
in your graphical user session.

### Choose the NVIDIA/dummy-plug display

If Looking Glass shows QXL's desktop, it may have saved that monitor as its
capture source. Removing QXL does not select the NVIDIA monitor automatically.
The capture application requests monitors from KDE/GNOME's ScreenCast portal;
the **monitor you choose** determines which display is captured. A dummy plug
provides an output, but that output must also be enabled in the guest desktop.

While QXL is still available:

1. In KDE **Display Configuration** or GNOME **Settings → Displays**, enable
   the monitor connected to the passed-through NVIDIA GPU. Use an SDR mode.
   Temporarily extend the desktop rather than mirroring QXL, so the two screens
   can be distinguished. KDE's `kscreen-doctor -o` lists outputs and their state;
   `bash doctor.sh` also maps DRM connectors to their PCI GPU/driver.
2. Stop any separately launched manual capture with Ctrl+C. From a guest desktop
   terminal, run this without sudo:

   ```bash
   bash setup-startup.sh choose-display
   ```

   This stops this project's managed startup service if installed, then runs
   capture in the foreground with a **fresh monitor-selection request**. Choose
   the NVIDIA/dummy-plug monitor in the guest sharing dialog, rather than QXL's
   virtual monitor. The old restore token is omitted for this run; a successful
   new choice replaces it when the portal grants persistence. If persistence is
   denied, the old selection is removed so future startup asks again instead of
   switching back to QXL. Cancelling leaves the previous saved selection intact.
3. Verify the LG client shows the NVIDIA output. For example, place a terminal
   on that output and confirm it appears in LG while QXL shows a different area.
   Press Ctrl+C after verifying. If startup is enabled, run:

   ```bash
   bash setup-startup.sh start
   ```

   Confirm the same monitor is restored. Otherwise, run `bash run-guest.sh`
   normally to verify restoration before stopping it again.
4. Shut down the VM, change its emulated video model from QXL to **None**, boot
   and log in. Automatic startup now requests the saved NVIDIA selection.

For manual capture, the equivalent option is:

```bash
bash run-guest.sh --choose-display
```

Both chooser commands use the normal manual-run defaults. If your setup needs
a particular IVSHMEM device or frame rate, supply the same overrides, for example
`LG_SHM=ivshmem:0000:09:02.0 LG_FPS=120 bash setup-startup.sh choose-display`.
The chooser command preserves `autostart.env`; it does not copy its settings
into your terminal. Ordinary future runs restore the new selection by default.
`LG_CAPTURE_RESTORE=no` (or the binary option `pipewire:restore=no`) requests a
new choice on every run; keep the default `yes` for unattended startup.

The portal does not provide a portable connector-name/GPU-index selection
argument. Choosing through its monitor dialog works with both KDE and GNOME.
Its saved stream ID is opaque and is not necessarily an HDMI/DP connector name.
The log now reports whether a saved choice is requested, the selected stream's
ID/logical size when supplied, and whether a new restore token was saved.
Restore tokens remain private and are not printed.

If the NVIDIA output is missing from the chooser, check that it is enabled in
the active Wayland desktop; capture cannot enable a display or change which
GPU the compositor uses. If capture still fails with QXL removed, retain the
service journal and `bash doctor.sh` output. A missing active desktop, a new
portal prompt, and a PipeWire format error require different fixes. Re-enable
the temporary console or use your existing way to operate the GPU desktop to
handle any prompt. Desktop policy may require consent again after a display
topology change; this option does not bypass it.

## 6. Optional: start automatically after guest login

After building and confirming `bash run-guest.sh` works, run this **inside the
guest Wayland desktop**, as your normal user, **without sudo**:

```bash
bash setup-startup.sh enable
```

Capture will start automatically at the next KDE/GNOME Wayland login, including
after reboot. This installs a per-user XDG Autostart entry and a
`looking-glass-host.service` **user** unit. The desktop entry imports the current
session's Wayland/portal environment before starting the unit. The unit stops
with the graphical session. It is intentionally not an early-boot system/root
service: Wayland screen capture needs your logged-in desktop. If you want it to
start without typing a login password after boot, configure desktop auto-login
separately in your guest display manager.

Enabling startup leaves your current manual capture running. To try the service
immediately, **first stop manual capture with Ctrl+C**, then run:

```bash
bash setup-startup.sh start
bash setup-startup.sh status
journalctl --user -u looking-glass-host.service -b --no-pager
```

Remove any older Looking Glass entry you added manually in the desktop's
Autostart settings, so only one capture process starts. Keep this project at
the same path; if you move it, run `bash setup-startup.sh enable` from its new
location. Re-running enable updates the managed startup files and keeps settings.

Optional startup settings are saved in
`${XDG_CONFIG_HOME:-$HOME/.config}/looking-glass-linux-wayland/autostart.env`:

```ini
LG_SHM=ivshmem
LG_FPS=60
LG_CAPTURE_RESTORE=yes
```

For example, use `LG_SHM=ivshmem:0000:09:02.0` to select a particular device and
`LG_FPS=120` for its requested capture rate. These settings use systemd
`EnvironmentFile` syntax, not shell commands. After editing, restart from a
terminal in the guest desktop:

```bash
bash setup-startup.sh restart
```

Stop until the next login, or remove startup entirely:

```bash
bash setup-startup.sh stop
bash setup-startup.sh disable
```

Disable stops the service and removes its managed desktop entry/unit, keeping
your settings and portal restore tokens. Use `status` and the journal command
above to inspect failures. To switch back to manual capture, stop the service
first. The service does not automatically retry on failure or portal cancellation,
so it will not repeatedly open permission prompts; use `restart` when ready.

Startup uses the same saved per-desktop portal permission as manual capture.
KDE/GNOME may still show a monitor chooser after login if permission cannot be
restored, the output changes, or desktop policy requires confirmation. This does
not capture the login/lock screen or bypass consent. Persistent IVSHMEM permissions
from step 4 must also be installed so BAR2 remains accessible after reboot.
One graphical session per guest user is supported; log out before switching
between KDE and GNOME.

## 7. Verify the actual VM

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
8. With startup enabled, reboot and log in. Check that the service starts once,
   restore/accept portal permission if prompted, and verify frames. Log out and
   check capture stops; repeat after logging into the other desktop.
9. With QXL still present, choose the NVIDIA output, distinguish the two extended
   desktops in LG, and restart capture to verify the saved choice. Then test a
   reboot with QXL removed. Keep this separate from merely seeing the GPU output
   in another application.

Run `bash doctor.sh` in the guest for read-only diagnostics. Keep guest and
physical-host client logs separate, and do not share the portal restore tokens
in `~/.config/looking-glass-linux-wayland/`.

`bash test.sh` tests layout and PCI discovery. `bash build.sh` also tests real
shared-memory mappings and backend publication. CI additionally boots a minimal
QEMU guest and checks **unprivileged 64 MiB PCI BAR2 mapping and guest-to-host
writes with no KVMFR driver**. This does not replace real NVIDIA/Wayland/audio
testing in your VM. See [VALIDATION.md](VALIDATION.md).

## Troubleshooting capture startup

If the guest log shows `PCI IVSHMEM BAR2` and `IVSHMEM Size: 64 MiB`, the
shared-memory mapping succeeded. A subsequent `Failed to initialize the capture
device` message points to screen capture rather than a missing KVMFR module.
`KVMFR Version: 20` names B7's frame protocol version; it is not a module check.
The missing `~/looking-glass-host.ini` message is also normal when using the
supplied launcher, which supplies its options on the command line.

Earlier versions of this project rejected the standard ScreenCast
`CreateSession` response because `session_handle` is a D-Bus **string** (`s`)
even though its contents are an object path. This is now parsed and validated
correctly, with a real private-D-Bus regression test. Rebuild after updating.

Capture startup errors now name the failed stage (portal session creation,
source selection, monitor chooser, PipeWire remote, context/stream creation,
or raw video negotiation). If capture still fails, run `bash doctor.sh` in the
same guest desktop session and keep the full new capture output, including
the stage-specific error. Run the application without `sudo`.

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
complete guest application and execute all test suites. The
optional KVMFR module and actual KDE/GNOME/NVIDIA desktop/audio integration
still need broader validation. The user has confirmed screen capture, keyboard,
mouse and login startup in their CachyOS guest with plain IVSHMEM, with capture
currently showing the QXL display. Capturing the NVIDIA/dummy-plug output after
reselection/removing QXL still needs a retry; sound and shared microphone remain
untested. The default raw PCI path also passed a QEMU guest test without KVMFR.
Read `VALIDATION.md` for evidence and limits.

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
- Optional KDE/GNOME login startup with a user service, persistent settings,
  journal logs, and enable/disable/start/restart/stop/status commands.
- A one-time monitor chooser that skips a saved QXL selection and saves the new
  monitor for subsequent startup, with selected-stream diagnostics.

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
- [XDG desktop autostart](https://specifications.freedesktop.org/autostart/latest/)
- [systemd graphical user sessions](https://www.freedesktop.org/software/systemd/man/latest/systemd.special.html#graphical-session.target)
