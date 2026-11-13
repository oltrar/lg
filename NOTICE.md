This source project adapts Looking Glass B7 by The Looking Glass Authors:
https://github.com/gnif/LookingGlass

Base revision: 27fe47cbe2a3a8da986d310ab866f0b646ed68f5 (B7).
The overlay replaces the Linux PipeWire capture and desktop-portal source
files, adds frame-layout helpers and a direct PCI IVSHMEM mapping path. Existing source copyright and GPL terms
are retained. Added source, tests, scripts, examples and documentation are
provided under GPL-2.0-or-later.

The vendored zkde-screencast-unstable-v1.xml protocol retains its original
SPDX copyright (Aleix Pol Gonzalez, 2020-2021) and LGPL-2.1-or-later license.
It is copied from KDE plasma-wayland-protocols revision
1db07441870d54157e937674b99380a7dcc0fb32:
https://github.com/KDE/plasma-wayland-protocols/blob/1db07441870d54157e937674b99380a7dcc0fb32/src/protocols/zkde-screencast-unstable-v1.xml
See licenses/LGPL-2.1-or-later.txt. The KDE native integration is specific to
the compositor protocol and does not claim upstream KDE compatibility guarantees.
The non-wire deprecated-since attribute is omitted so Debian's older
wayland-scanner can validate the XML; the event description retains that notice.

The build downloads Looking Glass and its pinned LGMP submodule. Those sources
and their respective license files remain in _upstream/LookingGlass. Include
the complete corresponding source and applicable notices if redistributing a
resulting binary. This archive does not contain prebuilt binaries.

This is a community adaptation prepared for the stated VM configuration. It
is not an official Looking Glass release or an upstream support commitment.
