# Validation record

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

## Checks that still require Linux / the actual VM

The execution environment here is Windows and has no installed Linux runtime,
Wayland compositor, passed-through VM GPU, IVSHMEM device or usable Linux guest.
Consequently **a complete application link/native build, KVMFR kernel build,
portal request execution and end-to-end display/input/audio tests were not
performed**. The source object checks do not establish runtime correctness,
performance or compatibility with current CachyOS kernels or NVIDIA drivers.

The included GitHub Actions workflow runs native Arch and Debian build
checks on pushes and pull requests. The checks above describe the local
validation performed before opening the PR; consult the PR checks for current
CI results. CI does not establish KDE/GNOME/NVIDIA VM runtime compatibility.

Use the README's integration checks under both desktop sessions. Report the
guest and client logs if a native build or VM test fails. Keep restore tokens
private. This is an experimental implementation with explicitly scoped
limitations, not a claim of complete Windows feature/performance parity.
