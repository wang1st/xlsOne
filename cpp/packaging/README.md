# Packaging Plan

The C++/Qt port should keep packaging separate from application code.

## Windows

- Build with MSVC and Qt 6.
- Run `windeployqt` on `xlsOneQt.exe`.
- Package with NSIS or WiX.

## Linux x86_64

- Build on the oldest supported glibc baseline for the target distro family.
- Produce `.deb` for Debian/UOS style systems.
- Optionally produce AppImage for generic desktop Linux.

## UOS

- Produce `.deb` packages for:
  - x86_64
  - ARM64
  - LoongArch64
- Validate on real UOS machines before release.
- Keep DTK optional unless product requirements demand native DDE-specific widgets.

