# Packaging Plan

The C++/Qt port should keep packaging separate from application code.

## Windows

- Build with MSVC and Qt 6.
- Run `windeployqt` on `xlsOneQt.exe`.
- Package with NSIS or WiX.

## Linux x86_64 / Kylin

- Build on the oldest supported glibc baseline for the target distro family.
- Produce `.deb` for Debian/UOS/Kylin style systems.
- When not bundling Qt libs (e.g. native Kylin builds), declare system Qt
  dependencies (`libqt5core5a`, `libqt5gui5`, `libqt5widgets5`, `libqt5network5`).
- Kylin's GUI installer (`kylin-installer`) enforces package signature
  verification. Use `kylin-signtool` and a Kylin-issued developer certificate
  to sign the resulting `.deb`, or install from the terminal with
  `sudo dpkg -i <package>.deb`.

## UOS

- Produce `.deb` packages for:
  - x86_64
  - ARM64
  - LoongArch64
- Validate on real UOS/Kylin machines before release.
- Keep DTK optional unless product requirements demand native DDE-specific widgets.

