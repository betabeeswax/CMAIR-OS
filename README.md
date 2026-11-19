![MCAI OS logo](assets/mcai-os-logo.svg)

# MCAI-OS

[🌐 Open the MCAI-OS Easy Flasher](https://betabeeswax.github.io/MCAI-OS/) · [📦 View the repository](https://github.com/betabeeswax/MCAI-OS) · [⬇️ Firmware releases](https://github.com/betabeeswax/MCAI-OS/releases)

**MCAI-OS stands for “M5Stack Cardputer but AI”** — a real, native operating-system project for the M5Stack Cardputer ADV, with a companion desktop edition.

MCAI-OS is being developed as a small, modular system rather than a web page pretending to be an OS. The Cardputer build must respect its 240 × 135 display, limited internal flash/RAM, keyboard, battery, wireless hardware and microSD storage. Desktop builds can provide richer versions of the same apps.

> **Project status:** early foundation. This repository is the starting point, not a claim that all 109 apps or hardware drivers are already complete.

## Targets

- **Cardputer ADV:** native firmware, compact UI, hardware services and microSD-backed app/data storage.
- **Desktop:** a companion environment and simulator for developing and testing apps.
- **Shared app model:** app catalogue, metadata, permissions and portable project formats where practical.

## App catalogue

The complete 109-entry list is in [`apps/catalogue.json`](apps/catalogue.json). The separate [MCAI-OS Apps repository](https://github.com/betabeeswax/MCAI-OS-APPS) is the home for app packages, manifests, developer format documentation and the app catalogue mirror. Names are kept as specified. In particular, the media player is named **MLC**; app names are not automatically prefixed with “AIR”.

The catalogue is a roadmap. Entries are not represented as implemented until their code and tests exist.

## Repository layout

- `kernel/core` — portable operating-system core and service interfaces.
- `firmware/cardputer-adv` — ESP-IDF firmware entry point and board integration.
- `desktop` — desktop host/simulator target.
- `apps` — app catalogue and app packaging contract.
- `sdk` — app developer API and project format documentation.
- `benchmarks` — repeatable performance test plan and result format.
- `docs` — architecture, storage, security and hardware notes.

## Browser flasher

**Quick link:** [Open the MCAI-OS installer website](https://betabeeswax.github.io/MCAI-OS/). You can always find this link at the top of this README. The installer also has a **Go to MCAI-OS repository** button so you can return here.

A browser-based installer for the M5Stack Cardputer ADV is in [`docs/index.html`](docs/index.html), with its ESP Web Tools manifest in [`docs/manifest.json`](docs/manifest.json).

To publish the website, open **Settings → Pages**, choose **Deploy from a branch**, select branch **main** and folder **/docs**, then save. The site will be available at:

`https://betabeeswax.github.io/MCAI-OS/`

Use current Chrome or Edge on a computer with a USB data cable. The installer checks GitHub Releases and only enables the flash button when a release contains `cmair-os-cardputer-adv.bin`. The firmware workflow builds a merged ESP32-S3 image on pushes to `main`, uploads the build artifact, and publishes/updates the `v0.1.0-alpha` prerelease with the firmware binary. Version tags beginning with `v` publish versioned releases.

**Current development status:** the Cardputer ADV firmware includes a launcher and built-in screens for File Manager, editable Notes, Calculator, Settings, System Info, App Store, Clock, Timer, Keyboard Test, Storage, Diagnostics, a Games hub (with Snake), About, Stopwatch and an OS Updater. The keyboard driver now follows M5Stack's TCA8418 7×8 matrix setup and supports the ADV arrow keys, Caps Lock and Fn symbol layer. Settings includes on-device Wi-Fi credential entry, network time sync, BLE device scanning, display options and sound controls. The ES8311/I2S audio path provides startup, keypress and timer tones. The launcher supports a saved app order and a user folder on the microSD card. The OS Updater uses HTTPS OTA with two firmware slots and rollback protection; because this changes the flash partition layout, the first installation of this OTA-capable build still requires the browser flasher once. Later compatible updates can be installed from the OS Updater. The App Store reads the MCAI-OS-APPS catalogue, opens built-in apps, and can install verified declarative packages. Installed packages are shown at the bottom of the launcher. Planned catalogue entries are not executable apps until their packages and runtime support exist. Bluetooth LE scanning is implemented in firmware; BLE pairing and connected-device profiles remain in development. Many catalogue entries remain planned, and the desktop shell is still a core-validation host rather than a finished desktop OS. **CI builds do not prove physical hardware operation**: display, keyboard, Wi-Fi, BLE, speaker, microSD and every app still require testing on the actual Cardputer ADV. The installer checks GitHub Releases (including prereleases); the microSD card is separate from internal flash and is not formatted by the installer.

## Build

### Portable core and desktop host

Requires CMake 3.20+ and a C++17 compiler.

```sh
cmake -S . -B build
cmake --build build --config Release
./build/desktop/cmair-desktop
```

On Windows, run `build\\desktop\\Release\\cmair-desktop.exe` when using a multi-configuration generator.

### Cardputer ADV firmware

Requires Espressif ESP-IDF 5.x and the correct board configuration. The initial firmware target is intentionally conservative; board-specific display, keyboard, battery, SD and wireless drivers must be validated against the exact Cardputer ADV hardware revision before being enabled.

```sh
cd firmware/cardputer-adv
idf.py set-target esp32s3
idf.py build
```

## Design principles

1. Keep the kernel and hardware services small.
2. Load optional apps and large assets from microSD where possible.
3. Keep essential boot, recovery and device services in internal flash.
4. Make offline operation a first-class requirement.
5. Expose hardware through capability-checked APIs rather than letting every app access peripherals directly.
6. Benchmark memory, startup time, frame timing, storage and power before optimizing.
7. Clearly label planned, experimental and working features.

## Safety and reliability

Apps should run with limited permissions. Network access, microphone, camera, IR, Bluetooth, file access and remote control should require explicit capabilities. Updates should be verifiable and recoverable. Never store passwords or cloud tokens in plain-text app data.

## Contributing

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) and [`sdk/README.md`](sdk/README.md) before adding a service or app. Hardware changes must include the exact board revision and a test procedure.

## Cardputer ADV App Store

The firmware App Store reads the live catalogue from
[`betabeeswax/MCAI-OS-APPS`](https://github.com/betabeeswax/MCAI-OS-APPS)
over HTTPS. It does not install roadmap entries or pretend that planned apps
are downloadable.

### Wi-Fi setup

Open **Settings → Wi-Fi Setup** on the Cardputer ADV. Type the network name, press Enter, type the password, and press Enter again to save and connect. Credentials are stored in `/sdcard/MCAI-OS/wifi.cfg` on the microSD card. Keep the card private because this file contains your Wi-Fi password.

The App Store downloads the repository catalogue and supports verified `mcai-app-v1` JSON packages. It checks the package's HTTPS response, exact size, SHA-256 digest, app ID and Cardputer target before saving it under `/sdcard/MCAI-OS/apps/`. The first package, Tap Counter, is marked `experimental` until it is tested on physical Cardputer ADV hardware. The initial runtime is deliberately limited to declarative counter apps; it does not execute downloaded native code. Entries marked `planned` cannot be installed.
