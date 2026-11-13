# Validation record

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
**KVMFR kernel builds, portal request execution and end-to-end display/input/
audio tests remain unverified.** Build and unit-test success do not establish
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
