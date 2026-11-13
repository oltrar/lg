# Looking Glass B7 Linux guest — KDE and GNOME Wayland

## Install and set up

For a CachyOS/Arch **Linux guest** with a passed-through NVIDIA GPU and dummy
plug, and the existing Looking Glass **B7** client on the physical Linux host.
Use **main**. No feature-branch switching or GPU/Looking Glass driver reinstall
is needed. Video uses plain IVSHMEM; input and audio use SPICE.

### If you already downloaded this project

Open a terminal **inside the guest KDE/GNOME Wayland desktop**, in your `lg`
directory. Stop any manually running capture with Ctrl+C, then paste this block:

```bash
git fetch origin
git switch main
git pull --ff-only origin main
bash install.sh
```

The switch to `main` above is a one-time return from the previous testing
branches. **For future updates, run only `bash update.sh`.**

### New installation

First configure the VM and the physical host as described below. Then, inside
the guest desktop:

```bash
git clone --branch main https://github.com/oltrar/lg.git
cd lg
bash install.sh
```

Run without sudo. The installer asks for sudo only when installing missing
CachyOS/Arch packages or the IVSHMEM permission helper. It then:

1. Builds and tests the guest application.
2. Installs persistent IVSHMEM permissions, including migration from the older
   rule that stopped matching if removing QXL changed the device's PCI address.
3. Repairs and registers login startup.
4. Opens a **fresh display chooser**. Choose the NVIDIA/dummy-plug monitor.
5. Waits for a complete B7 raw frame through shared memory before reporting
   setup complete. You do not need to start the physical-host client for this
   check. The service performs the same check after future logins.

Keep QXL available during setup. Put different content on the two extended
displays so you can recognize the NVIDIA screen. After setup succeeds, start
your physical-host B7 client and confirm it shows that content. You can then
shut down the VM, change its emulated video model to **None**, and boot again.
The guest must automatically log into its Wayland desktop for unattended use.

The selected portal/PipeWire session now survives the B7 host's idle period
and client reconnects. Closing the client does not close the sharing session or
open another chooser. Logging out or stopping the guest service closes it.

**A QXL picture alone is not proof of Looking Glass video.** B7 can show a
SPICE fallback picture while raw capture is broken. Setup now checks the raw
path directly. If it fails, it prints the service output and exits with an
error; do not substitute `build.sh` and treat compilation as successful setup.

This is experimental SDR capture. Tests exercise the real PipeWire and B7
transport, but this development environment has no passed-through NVIDIA GPU.
See [VALIDATION.md](VALIDATION.md) for exactly what was tested.

This update also fixes a frame-publication crash: the previous heap snapshot
could be aligned to only 16 bytes, while B7's AVX2 copy requires 32. Capture
could therefore die after selecting a display and receiving its first frame.
Snapshots now satisfy the actual B7 writer's alignment requirements.

## VM configuration (physical Linux host)

Shut down the guest. Inside its libvirt `<devices>`, use:

```xml
<shmem name='looking-glass'>
  <model type='ivshmem-plain'/>
  <size unit='M'>64</size>
</shmem>
```

This creates `/dev/shm/looking-glass` on the physical host. Inside the guest it
is a PCI device (`1af4:1110`), not a `/dev/shm` file. Neither side needs KVMFR
for this path. Use **128 MiB for 4K SDR**; 64 MiB is suitable for initial
1080p/1440p setup. B7 needs space for two frames and metadata.

Keep SPICE for input and audio, even with video None. The relevant example is
[config/libvirt-devices.xml](config/libvirt-devices.xml), including:

```xml
<graphics type='spice' autoport='no' port='5901'>
  <listen type='address' address='127.0.0.1'/>
  <mouse mode='server'/>
</graphics>
<video><model type='none'/></video>
<sound model='ich9'><audio id='1'/></sound>
<audio id='1' type='spice'/>
```

Merge the example into your existing XML; keep your GPU passthrough devices.
Use QXL temporarily during setup and None afterward. The NVIDIA/dummy-plug
output must be enabled in KDE Display Configuration or GNOME Settings → Displays.
Use SDR. This application does not configure GPU drivers or enable displays.

Give your **physical-host** desktop user access to the backing file. For example,
after the VM creates it, run on the physical host:

```bash
sudo setfacl -m u:"$(id -un)":rw /dev/shm/looking-glass
```

Repeat if libvirt recreates the file. Use separate shared-memory names and
SPICE ports if the Windows and Linux VMs run at the same time.

## Physical-host client, keyboard, mouse and audio

Copy [config/client-linux.ini](config/client-linux.ini) to the physical Linux
host. Set its shared-memory filename and SPICE port to match this VM. Run your
existing B7 client there:

```bash
looking-glass-client -C /absolute/path/to/client-linux.ini
```

The sample enables SPICE keyboard/mouse, sound and microphone. Scroll Lock is
B7's default input capture/release key. Use captured relative mouse input.

For microphone support, the physical-host client must report
`Using AudioDev: PipeWire`. B7's PulseAudio backend supports playback but not
microphone recording. Keep the microphone connected to the **physical host**
and use its PipeWire audio server to share it with host applications and LG
concurrently. Do not USB/PCI-pass through that microphone to the guest.

The sample sets `micDefault=allow` and shows B7's recording indicator. You can
use `micDefault=prompt` instead. Scroll Lock+E toggles microphone recording.
To select a particular host microphone, set `[pipewire] recDevice=` to its
PipeWire `node.name`; otherwise the default input is used.

In the guest, choose the emulated **ICH9/Intel HDA** card for output and input,
including in the application that records. NVIDIA HDMI audio is a different
device. Leave SPICE enabled in the client; `-s` disables it.

## Startup and changing the display

`install.sh` enables startup after login and starts capture immediately. Do not
run another manual copy at the same time. Keep the project in the same directory;
run `bash install.sh` again if you move it.

To select a different monitor and keep startup configured, run:

```bash
bash install.sh
```

To stop or restart capture:

```bash
bash setup-startup.sh stop
bash setup-startup.sh restart
```

To remove automatic startup:

```bash
bash setup-startup.sh uninstall
```

Uninstall stops the managed service and removes its unit/autostart entry. It
keeps the repository, build, settings, saved display permission and reports.
Run `bash install.sh` to set it up again. There is no capture driver to uninstall.

Wayland capture starts **after desktop login**; it cannot display the login or
secure/lock screen. Configure automatic login separately if needed. Desktop
policy can require a new sharing confirmation after permissions/output changes.
The application cannot bypass that policy.

## If setup or video fails

The installer now repairs stale/missing systemd unit state, avoids resetting an
unloaded service, and fixes the older quoted `EnvironmentFile=` directive.
Switching source branches or building alone did not repair installed units.
Use `bash install.sh` from main to perform all steps together.

View the current service and journal inside the guest:

```bash
bash setup-startup.sh status
journalctl --user -u looking-glass-host.service -b --no-pager
```

`RAW VIDEO VERIFIED` means a B7 client received a complete raw frame from shared
memory. `First raw frame written to IVSHMEM` is the capture host's corresponding
marker. An open monitor chooser, a running process, or a QXL fallback picture
does not establish this.

Every launch saves private `capture.log` and `diagnostics.log` files under:

```text
~/.local/state/looking-glass-linux-wayland/reports/
```

If QXL None leaves you without a usable screen, shut down the guest, restore
QXL, and retrieve the timestamped report from the **failed boot**. Its UTC time
and boot ID are in `capture.log`. Copy both logs somewhere you can read after
shutting down the VM. You can return to this chat with those logs later;
simultaneous VM/chat access and SSH are not required. A boot with no report
means the launcher did not run far enough to create it (or could not save it).

`bash version.sh` shows the checkout, successful build and installed service.
`bash doctor.sh` gathers read-only desktop/GPU/connector/IVSHMEM diagnostics.
Do not share files named `restore-*`; those are private portal permission tokens.

## Advanced setup

CachyOS/Arch dependencies are installed as needed by `install.sh`. If preparing
them manually:

```bash
sudo pacman -S --needed base-devel git cmake ninja pkgconf pipewire glib2 dbus \
  xdg-desktop-portal xdg-desktop-portal-kde pciutils desktop-file-utils
```

For GNOME use `xdg-desktop-portal-gnome` instead of the KDE backend. Both may
coexist. Other distributions need equivalent compiler, CMake/Ninja, GLib/GIO,
PipeWire/SPA development headers, D-Bus, systemd and desktop-entry tools, plus
their desktop's portal backend.

The default permission rule auto-discovers exactly one IVSHMEM device, so it
continues to work if removing QXL renumbers PCI devices. Multiple IVSHMEM devices
require explicit selection:

```bash
sudo bash setup-ivshmem.sh --user "$USER" --device 0000:06:01.0 --install
```

Explicit selection stays pinned to that BDF. Set `LG_SHM=ivshmem:YOUR_BDF` in
`~/.config/looking-glass-linux-wayland/autostart.env`, then use
`bash install.sh --skip-device-setup` to preserve custom transport setup.
The same flag is available for the [optional KVMFR path](docs/optional-kvmfr.md).
The helper refuses ambiguity, non-IVSHMEM devices and bound drivers. It never
unbinds a GPU. Kernel lockdown can prohibit raw PCI mapping despite permissions;
capture reports the error rather than bypassing it.

Startup settings use systemd EnvironmentFile syntax:

```ini
LG_SHM=ivshmem
LG_FPS=60
LG_CAPTURE_RESTORE=yes
```

Keep restore=yes for unattended login. `LG_FPS` accepts 1–360; actual cadence is
compositor-dependent. After editing, run `bash setup-startup.sh restart`.

Manual capture remains available with `bash run-guest.sh`, or
`bash run-guest.sh --choose-display` for a fresh chooser. Stop the service first.
Manual runs use your terminal's `LG_SHM`/`LG_FPS` values, not the service options
file. `bash check-video.sh` can verify raw publication while capture is running;
set `LG_SHM` when using a custom device.

To remove the persistent permission helper, delete only
`/etc/udev/rules.d/70-lg-ivshmem.rules` and
`/usr/local/libexec/lg-ivshmem-permissions`, reload udev rules, and reboot.

## Implementation, tests and limits

The build fetches pinned upstream B7 commit
`27fe47cbe2a3a8da986d310ab866f0b646ed68f5`, initializes its submodules and
applies this GPL-2.0-or-later overlay. The executable is
`_build/host/looking-glass-host`; `_build/lg-frame-check` is a B7 LGMP verifier.
Build records identify source and binary changes. Unknown edits in the upstream
build tree are preserved and stop overlay upgrades.

Guest KWin/Mutter → local ScreenCast portal/PipeWire → owned raw pixel snapshot
→ IVSHMEM/B7 LGMP → existing physical-host B7 client. This path uses no video
codec or network video stream. SPICE supplies input/audio separately.

Tests cover the old AVX2 alignment crash using the real B7 writer, raw format
negotiation, padded/offset MemFd buffers, ownership,
portal responses/restoration, unit generation/repair, installer flow and actual
QEMU PCI BAR2 coherence without KVMFR. Additional CI runs a real PipeWire daemon,
the full guest executable and a B7 LGMP client with no QXL, checks every received
pixel and reconnects clients repeatedly. Its compositor/portal UI is a fixture;
it does not test an NVIDIA GPU or real KDE/GNOME session. See
[VALIDATION.md](VALIDATION.md).

Material limits: one monitor, SDR, CPU copies, embedded cursor, no native
Wayland clipboard, no automatic guest resolution changes, no login-screen
capture. No zero-copy/performance guarantee is made. Actual NVIDIA/Wayland
hardware, sound and concurrent host/guest microphone use need guest validation.

- [Upstream B7 source](https://github.com/gnif/LookingGlass/tree/B7)
- [Looking Glass and SPICE fallback](https://looking-glass.io/docs/B7/install_libvirt/)
- [ScreenCast portal API](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html)
- [QEMU IVSHMEM](https://www.qemu.org/docs/master/system/devices/ivshmem.html)
- [Linux PCI sysfs mapping](https://www.kernel.org/doc/html/latest/PCI/sysfs-pci.html)
- [PipeWire threaded loop](https://docs.pipewire.org/page_thread_loop.html)

See [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md).
