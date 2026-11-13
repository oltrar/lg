# Validation record

## Unattended KDE and upload performance — 2026-11-13

The user now confirms the NVIDIA/dummy-plug screen is selected and video works
after setup. Rebooting with QXL removed still leaves a sharing chooser they
cannot operate. They report about 24 UPS and 50 client FPS during video/gaming,
at 2560x1440 with both 120 Hz and 165 Hz desktop modes. A 24 Hz display mode is
therefore ruled out; the full remaining performance bottleneck is not measured
on their hardware yet.

KDE portal source review shows outputs can be restored by screen position,
which can change when removing QXL. Add a KDE-specific native KWin capture
integration, selected explicitly by wl_output connector name during setup and
saved privately. At login it requests that same output, without portal tokens
or a hidden chooser. Missing/disabled outputs fail rather than falling back to
QXL. Installed per-executable desktop descriptors declare the capture protocol
for KWin's normal capability checks; global permission checks are never disabled.
GNOME retains the portal backend and its normal permission policy.

Plain PCI mapping previously always used ordinary resource2. Linux exposes a
write-combining resource2_wc alias for prefetchable BARs on supported platforms.
Prefer that alias, grant its scoped permissions during install/udev setup, and
report any ordinary-mapping fallback. Add SFENCE before publishing B7 ready
byte counts so NT/WC pixel stores complete first. This preserves the B7 protocol.
The QEMU test has actually mapped the WC alias as UID 1000, without KVMFR,
and verified guest-to-physical-host coherence. No NVIDIA FPS claim is inferred
from QEMU/TCG timings.

Automatic capture FPS now follows KDE's selected output refresh (up to 360);
portal fallback defaults to 60. Selecting an output migrates the previous exact
LG_FPS=60 default to auto; other explicit caps are preserved. The source mode
and resolved requested maximum are logged. Every five active seconds, rate
and copy/upload timing counters distinguish source, capture and publication.
B7 client FPS counts rendering; UPS counts guest frame uploads. A video with
24 changing frames/sec can naturally produce 24 UPS, but that does not explain
the user's gaming result without measurements.

Tests use a real Wayland client/server exchange for enumeration and selected
connector capture, different positions/120-165 Hz modes, compositor restart and
removing QXL. They check missing-output refusal and clean session destruction.
Full PipeWire-to-B7 pixel verification also passes on both portal and native
KDE paths, with idle/reconnect handling, on native Arch and Debian CI. Those
compositor/portal endpoints are fixtures. Additional real KWin software/virtual
compositor validation across two starts is run separately; its final result is
recorded on the PR. This test targets current Arch/KWin, matching CachyOS. The
older Debian KWin 5.27 virtual EGL backend omits outputChange emission, which
its output screen-cast source requires, so it cannot validate pixel delivery
in that artificial environment. Debian still builds/runs the protocol/transport
fixtures. This environment still has no NVIDIA passthrough VM or
physical-host renderer and cannot certify a hardware upload rate.

## Main installation, missing units, topology changes and real video — 2026-11-13

The new full PipeWire-to-LGMP integration exposed a publication failure that
the earlier stubbed framebuffer test could not detect. Production snapshots
used realloc, which guarantees only 16-byte alignment on this platform. B7's
framebuffer_write_avx2 uses _mm256_stream_load_si256 and requires 32-byte source
alignment. Large allocations commonly have page+16 addresses. This can fault
after the first successful capture copy, before the IVSHMEM-write log marker.
Snapshots now use posix_memalign(32). A negative-control test forks the real B7
writer with a deliberately 16-but-not-32-aligned input and requires SIGSEGV on
AVX2 systems; the aligned input must publish matching bytes. Backend ingestion
also asserts alignment, and the full live integration exercises actual writes.
This is a demonstrated code defect, not hardware-specific proof from the user's
VM. The old QXL image can still have been SPICE fallback throughout.

The latest user retry still showed QXL and no NVIDIA feed after removing QXL.
They also reported install.sh stopped with a service-not-found error. No actual
capture-stage log from that retry is available; hardware causation is unproven.

Three additional code paths could independently break this workflow:

- start-session.sh called reset-failed before the unit had ever been loaded;
  unlike start, that operation does not load the unit. It now resets only an
  existing failed unit. The installer reloads before stopping old managed units
  and handles an actual not-found result without swallowing other stop failures.
- B7 initializes capture before its first client, deinitializes when idle, then
  initializes again when a client connects. The backend previously closed the
  entire portal session each time. A fresh-choice run requested another chooser
  on every initialization. Deinit now pauses the stream, init resumes the same
  selected monitor, and final free closes the session once.
- The permission rule was pinned to a guest BDF even in auto-discovery mode.
  Removing QXL can renumber PCI devices. Auto mode now re-discovers exactly one
  IVSHMEM device; explicit device selection remains pinned and ambiguous/bound
  devices are refused. install.sh replaces the older rule.

Installation now installs missing Arch dependencies, builds, configures raw PCI
permissions, registers startup and selects a fresh monitor. A new ExecStartPost
checker acts as a real B7 LGMP subscriber against the same mapping. Startup is
successful only after a complete nontruncated raw SDR frame is received. It has
a bounded timeout and cannot mistake a SPICE/QXL image for capture success.
The README has one main installation path; update.sh always updates from main.

Regression fixtures reproduce missing/unloaded-unit failures, preserve genuine
stop errors, exercise one-time selection and the actual permission helper across
PCI renumbering. New integration tests launch a real private PipeWire daemon,
a padded BGRA/MemFd producer, the real full guest executable, and the production
B7 LGMP verifier. They check every pixel, initial idle and three reconnects,
requiring only one portal session until shutdown. There is no QXL in this test.
The portal consent UI and compositor producer are fixtures, not KDE/GNOME/NVIDIA.
Native CI results are recorded with the change; hardware success is not claimed.


## Service options path, repair and build identity — 2026-11-13

The user's actual journal showed `EnvironmentFile= path is not absolute,
ignoring:` because the generated directive surrounded its absolute filename
with literal quotes. This bug was introduced in the startup service generator
and persisted across later source updates. It is not a GPU driver issue.
The warning confirms ignored settings, but the supplied output does not show
whether capture subsequently started or why video failed.

The corrected directive uses an unquoted absolute pattern with escaped systemd
specifiers/glob characters. Tests reproduce the quoted-path warning with real
systemd parsing, verify the corrected unit has no diagnostics, and use real
glob/file opening to check special-character paths resolve to exactly the
saved settings. The installer now validates staged units and treats warnings
as failures even if `systemd-analyze verify` exits zero. A regression test
checks warning refusal preserves the previously installed unit.

Added service `repair` and startup `uninstall` commands, an explicit
stop/build/register/start installer, a successful-build record, and `version.sh`.
Tests cover repair of an old unit, saved-options preservation, installer flow,
and source/binary mismatch reporting. Test service operations are still mocked;
the real systemd parser and glob implementation are exercised. Native results
are recorded on the PR. Actual NVIDIA-only capture remains unverified here.

## BGRA negotiation and persistent failure reports — 2026-11-13

The user reproduced missing video on a fresh CachyOS/NVIDIA/dummy-plug VM,
including after selecting the correct monitor and removing QXL. SPICE input
still works. The previous chooser change did not resolve this. The user cannot
run the VM while using this chat and cannot currently collect a live failure
log. The actual failure stage is still unknown; a QXL/SPICE fallback picture
must not be treated as proof of LG raw-frame capture.

Found a definite bug independently: the SPA enum advertised BGRA as the default
but omitted it from its allowed alternatives. A local executable using the real
SPA filter reproduced `-EINVAL` (-22) for a BGRA-only producer. The corrected
production format helper passed real negotiation tests with BGRA-only, RGBA,
BGRx and RGBx producers, geometry, maximum-rate constraints, and both CPU buffer
types. Those pure-header tests were actually executed locally on Windows.

Added explicit first-buffer/copy/IVSHMEM-write diagnostics, detailed rejection
metadata and a one-time no-usable-frame warning. Backend tests now ingest a
real mapped MemFd with offsets/padding, verify owned pixels survive producer
reuse, and exercise publication and rejected-buffer paths. This does not
exercise a real compositor or NVIDIA GPU.

The launcher now retains a private report for every capture attempt, with a
bounded read-only diagnostic snapshot and its normal console/journal output.
Startup tests cover report permissions, separate reports, failure retention
and XDG state location. Native Arch/Debian and QEMU results are recorded on the
PR. The real NVIDIA-only failure remains unconfirmed until a saved report and
guest retry are available. No new DMA-BUF import support is claimed.

## NVIDIA monitor selection — 2026-11-13

The user confirmed screen, keyboard, mouse and automatic login startup work,
but observed Looking Glass capturing QXL rather than the NVIDIA/dummy-plug
display. The NVIDIA display is available in the guest and visible through
their PCVR setup. Removing QXL led to no LG feed; the corresponding startup
log is not available yet, so the exact failure stage remains unconfirmed.

Added `run-guest.sh --choose-display`, `LG_CAPTURE_RESTORE=yes|no`, and
`setup-startup.sh choose-display`. The latter stops the managed service and
runs the one-time chooser in the foreground. No saved restore token is sent
for a fresh choice; a successful choice is saved for future normal startup.
Cancellation/empty-stream responses preserve the prior token. A successful
choice without a reusable token removes the old selection instead of allowing
a later switch back to QXL. New diagnostics
report restoration, selected-stream metadata and DRM connector/GPU state.

The real private-D-Bus test now checks monitor-only selection, omission of an
existing QXL token, replacement with a new selection, subsequent restoration
and token rotation, denied persistence, mode 0600, and cancellation/empty-stream preservation.
Launcher tests verify the flag, environment setting and startup chooser without
changing saved startup preferences. Native CI results are recorded on the PR.
Actual NVIDIA-output capture with QXL removed still needs a guest retry;
sound and concurrent microphone use remain untested.

## Optional desktop startup and user feedback — 2026-11-13

The user confirmed that screen passthrough works in their CachyOS guest with
NVIDIA passthrough after the portal parser fix. At that point keyboard, mouse,
audio and concurrent host/guest microphone use had not been tested by the user.
The later report above confirms keyboard/mouse/startup and clarifies that the
captured output was QXL. Audio/microphone remain untested.
This report does not establish capture on both desktop environments.

Added opt-in KDE/GNOME Wayland login startup using an XDG desktop entry and a
systemd user service. Tests exercise a real GLib desktop-entry launch with
special characters in project/config paths, systemd unit validation, scoped
session-environment import, X11/missing-session handling, settings retention,
unmanaged-file/symlink preservation, and root-install refusal. `systemctl`
is mocked: these tests do not establish an actual compositor login/logout or
reboot lifecycle. Check the startup PR's native Arch/Debian results and perform
the README's reboot/login/logout checks in the real guest.

## Portal session startup fix — 2026-11-13

Fixed the ScreenCast `CreateSession` parser: the documented `session_handle`
result is a string (`s`), not an object-path variant (`o`). The parser validates
the string as an object path before using it in later calls. Added named
capture-startup stages and concrete PipeWire allocation/connect errors.

The regression test uses the actual portal client against a private D-Bus
service: a standards-compliant string response, SelectSources/Start, Unix FD
transfer, session close, cancellation, and missing/invalid result values.
Build-upgrade tests cover managed source updates, migration from prior Git
overlay versions, and refusal to overwrite local edits. See the fix PR's checks
for native results. The later user report above confirms screen capture in their
guest, but does not establish both desktop environments or other channels.

## Plain IVSHMEM update — 2026-11-13

This update changes the preferred guest path to direct PCI IVSHMEM BAR2,
supports the supplied 64 MiB `ivshmem-plain` XML, keeps KVMFR optional, and adds
microphone configuration/verification while preserving B7 compatibility.

The new transport and test sources were cross-compiled to Linux objects with
strict warnings. On implementation commit
`132c47379774c72282a3a7af937ca23b1d7d2109`, the
[feature PR workflow](https://github.com/oltrar/lg/actions/runs/43467267371)
passed all three jobs: Arch build/tests, Debian build/tests, and QEMU IVSHMEM.
Native tests cover PCI selection/validation and real cross-process 64 MiB
mappings. The QEMU smoke test booted a Linux guest without KVMFR, dropped the
mapper to UID 1000, and verified actual 64 MiB PCI BAR2 mapping, a host-written
marker read by the guest, and guest-written bytes read from the host backing
file. This validates the production transport path on emulated PCI hardware.
KDE/GNOME/NVIDIA capture,
actual sound/input devices, microphone capture in both systems concurrently,
and guest kernel lockdown configurations still require real-VM verification.

The entries below describe the earlier B7 capture implementation's checks.

Prepared 2026-11-10 for CachyOS/Arch guests, KDE Plasma and GNOME on Wayland,
NVIDIA GPU passthrough, and an existing B7 client.

## Checks completed here

- Fetched upstream B7 and checked the base revision:
  `27fe47cbe2a3a8da986d310ab866f0b646ed68f5`.
- Compiled the modified `pipewire.c` and `portal.c` to **Linux x86-64 ELF
  object files**, using Zig 0.13.0's Clang 18.1.6, real PipeWire/SPA 0.3.65
  and GLib 2.74.6 development headers, and
  `-std=gnu11 -D_GNU_SOURCE -Wall -Werror -Wstrict-prototypes`.
  The capture source was also compiled with `-O3`.
- Built and **executed** the portable `frame_layout_test.c` on Windows x86-64.
  Assertions are explicitly enabled in the source. It passed layout edge
  cases and 2,000 padded/offset frame copies with input/output bounds checks
  and output canaries.
- Compiled `backend_frame_test.c` to a Linux object. This test uses the actual
  backend's frame metadata/publication functions and supplies only the final
  framebuffer writer; it covers full/truncated frames, snapshot geometry,
  color metadata, bounds and write failures. **It was not executed here**.
  `build.sh` runs it natively in Linux before building the application.
- Checked every supplied shell script with Bash's syntax checker.
- Checked the changes for whitespace errors and the supplied XML/INI syntax.
- Checked the archive contents against the source files and their SHA-256
  manifest.

## Native Linux CI

On implementation commit `ebd9bcc8345758d78ba905bbf07f91b3925c2892`,
the [PR workflow](https://github.com/oltrar/lg/actions/runs/42887642488)
passed both jobs:

- Arch Linux: complete guest application compilation/link and both test suites.
- Debian bookworm: complete guest application compilation/link and both test suites.

This includes native execution of the backend frame-publication tests described
above. The build fetches every pinned B7 submodule because upstream's configure
checks require them even for host-only builds.

## Checks that still require the actual VM

The execution environment here is Windows and has no installed Linux runtime,
Wayland compositor, passed-through VM GPU, IVSHMEM device or usable Linux guest.
The local environment could not perform a native build; CI subsequently
validated the complete user-space application build as recorded above.
**KVMFR kernel builds and end-to-end audio tests remain unverified.**
The real private-D-Bus portal test and the user's successful screen capture
report supersede the earlier portal/display limitation. Build and unit-test success do not establish
runtime correctness, performance or compatibility with current CachyOS kernels
or NVIDIA drivers.

The included GitHub Actions workflow runs native Arch and Debian build
checks on pushes and pull requests. The checks above describe the local
validation performed before opening the PR; consult the PR checks for current
CI results. CI does not establish KDE/GNOME/NVIDIA VM runtime compatibility.

Use the README's integration checks under both desktop sessions. Report the
guest and client logs if a native build or VM test fails. Keep restore tokens
private. This is an experimental implementation with explicitly scoped
limitations, not a claim of complete Windows feature/performance parity.
