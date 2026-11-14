# Architecture

## Overview

CMAIR-OS has three layers:

1. **Kernel/core:** portable C++17 services, lifecycle, event routing, app metadata and resource accounting.
2. **Platform layer:** Cardputer ADV firmware (ESP-IDF) and desktop host. Platform code owns display, input, storage, power and network drivers.
3. **Apps:** isolated modules that request capabilities through the app SDK.

The first implementation is a foundation. It does not yet include a complete scheduler, graphics compositor, filesystem driver stack or all board peripherals.

## Cardputer resource strategy

- Keep boot and recovery code in internal flash.
- Treat microSD as removable and potentially unavailable or corrupt.
- Keep app packages, media, projects, caches and user files on microSD when inserted.
- Never assume the full advertised card capacity is writable; check free space and handle I/O errors.
- Avoid loading large media files fully into RAM; stream in bounded chunks.
- Keep the UI adapted to the 240 × 135 display.

## Hardware boundary

Only the platform layer may talk directly to device drivers. Apps use services such as Display, Input, Storage, Audio, Network, Bluetooth and Infrared. Each service must report whether the capability exists on the current target.

Pin mappings and peripheral initialization must be verified against the exact Cardputer ADV board revision. Do not copy pin assignments from the original Cardputer without checking.

## App lifecycle

An app is discovered from its manifest, checked for supported target and requested permissions, then launched by the app manager. Apps must release resources when paused or closed. Apps should save state atomically and tolerate missing microSD storage.

## Desktop edition

The desktop target is a development host and simulator. It should exercise shared core logic while exposing desktop-specific input, display and filesystem backends. It is not a replacement for testing on real hardware.

## AI features

AI apps may use a remote provider when online or a local model when hardware permits. Network calls must be optional, visible to the user and disabled by default for offline-only workflows. Never bundle private API keys in the firmware or app packages.
