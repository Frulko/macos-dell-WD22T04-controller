# dockctl — Dell WD22TB4 controller for macOS

[![Build and test](https://github.com/Frulko/macos-dell-WD22T04-controller/actions/workflows/ci.yml/badge.svg)](https://github.com/Frulko/macos-dell-WD22T04-controller/actions/workflows/ci.yml)

A native C CLI and reusable library for Dell WD22TB4 identification, thermal
telemetry, power diagnostics and experimental temperature-controlled silence.
No background service or runtime Homebrew dependency is required.

**Hardware-tested:** one WD22TB4, Apple Silicon Mac, AppleHPMLib v3, EC
01.01.00.03. EC 01.01.00.16 is recognized from firmware analysis, but has not
been tested physically. Intel builds are compilation-tested; the thermal
transport searches for `AppleHPMARMI2C` and is not established on Intel Macs.
The CLI's human-readable output currently remains French; JSON keys and the
public API are English.

## Install

### Homebrew

The project repository also contains its Homebrew tap. An explicit repository
URL is needed because its name does not start with `homebrew-`.

```sh
brew tap frulko/dockctl https://github.com/Frulko/macos-dell-WD22T04-controller
brew install frulko/dockctl/dockctl
# Later:
brew update
brew upgrade frulko/dockctl/dockctl
```

The formula builds the tagged source and installs the CLI, static/dynamic
libraries and public headers. To follow unreleased main instead:
`brew install --HEAD frulko/dockctl/dockctl`.

### Download a binary

Get an archive from [Releases](https://github.com/Frulko/macos-dell-WD22T04-controller/releases).
Choose `arm64` for Apple Silicon or `x86_64` for Intel. The archives target
macOS 13 or newer and include the CLI, libraries, headers and license notices.
They are ad-hoc signed, not Apple notarized. Verify the accompanying SHA-256:

```sh
# Replace v0.1.0 if downloading a newer release.
gh release download v0.1.0 --repo Frulko/macos-dell-WD22T04-controller \
  --pattern 'dockctl-v0.1.0-macos-arm64.tar.gz*'
shasum -a 256 -c dockctl-v0.1.0-macos-arm64.tar.gz.sha256
mkdir -p downloaded
tar -xzf dockctl-v0.1.0-macos-arm64.tar.gz -C downloaded
./downloaded/dockctl-v0.1.0-macos-arm64/bin/dockctl --help
```

### Build locally

Install Xcode Command Line Tools, then:

```sh
git clone https://github.com/Frulko/macos-dell-WD22T04-controller.git
cd macos-dell-WD22T04-controller
make
make check
./dockctl info
```

`make` produces `dockctl`, `libdock.a`, `libdock.dylib` and two examples.
`make check` uses Python 3 for CLI/research tests and never sends hardware commands.

## First use

```sh
dockctl info
dockctl features
dockctl host-power --json
sudo dockctl thermal
```

Use `./dockctl` in a source checkout instead of `dockctl`. If thermal reads
report an inactive Dell mode, run `sudo dockctl connect` explicitly, then retry.
This performs Discover Modes and Enter Mode. No command enters automatically,
and **no production command sends Exit Mode**: an early experimental Exit
coincided with lost displays. Enter can leave the Dell mode engaged after exit.

## Temperature-controlled silence

```sh
# A 15-minute session, with at most 5 minutes per silent period.
sudo dockctl watch --silence --seconds 900

# Customize thresholds and require stable conditions for one minute.
sudo dockctl watch --silence --seconds 900 \
  --ventilate-at 42,47,68 --resume-at 40,45,66 --stable-seconds 60
```

The watcher requires automatic mode and valid telemetry at startup. It observes
at least 30 seconds in automatic mode before stopping the fan. Silence becomes
eligible when all three temperatures stay at or below **40/45/66 °C** for
30 seconds and the speed class is not 2. The observation and minimum automatic
interval overlap.

During silence, any sensor continuously at or above its **42/47/68 °C** threshold
for 30 seconds requests automatic mode. An isolated spike resets the timer once
all sensors drop below their ventilation thresholds. Each silent period has a
**300-second maximum target**, even when temperatures stay low. Before another
silent period, the minimum automatic interval and stable cool conditions apply
again. The final automatic mode is verified.

| Option | Default | Allowed range / meaning |
|---|---|---|
| `--seconds` | 300 | Total session, 1–3600 seconds |
| `--silence-seconds` | 300 | Per silent period, 9–300 seconds |
| `--cooling-seconds` | 30 | Minimum automatic interval, 30–600 seconds |
| `--stable-seconds` | 30 | Continuous qualifying condition in either direction, 1–120 seconds |
| `--ventilate-at` | `42,47,68` | Local, remote, module thresholds in °C |
| `--resume-at` | `40,45,66` | Each must be below its ventilation threshold |

Critical thresholds **55/60/73 °C**, cancellation, invalid readings and I/O
errors bypass the stability delay. Configurable ventilation thresholds must
remain strictly below those critical values. These are experimental project
backstops, not Dell-certified operating limits.

Ctrl-C, SIGTERM and SIGHUP request restoration after any attempted write,
including an ambiguous failed write. Ctrl-C prints a message immediately;
restoration and verification normally need about 12 seconds and cannot be
cancelled. A blocked IOKit call can take longer. Logs show elapsed time,
pending confirmation and the triggering sample before each requested transition.

**Keep the Mac awake and the connection active.** A full thermal read takes
about six seconds; I/O and restoration can overrun software deadlines. There
is no independent dock watchdog: crashes, SIGKILL, sleep or disconnect can
prevent restoration. If restoration is unconfirmed, run `sudo dockctl auto`
when the connection responds again. `/var/run/dockctl-hpm.lock` prevents
competing sessions of this library/probe, but does not lock third-party tools.

Automatic mode, forced settings, a brief stop, a one-minute stop and Ctrl-C
restoration have been physically observed. A subsequent user trace confirms a silent period of 301.4 seconds, automatic
restoration at its deadline, filtering of isolated 68 °C module readings and
re-entry after stable temperatures. Maxima in the supplied trace were
36/37/68 °C. The complete 15-minute session and sustained over-threshold or
critical-threshold recovery have not been physically validated. These tests
do not establish safe permanent silence.

## CLI reference

| Command | Result | Root needed |
|---|---|---|
| `info`, `list` | Model, identifiers, declared supply, components, raw ports, host charger | No |
| `features` | Known, experimental and unknown capabilities | No |
| `host-power` | Mac input telemetry and advertised charging profiles | No |
| `thermal` | Fan mode, speed class, three temperatures | Yes |
| `power` | Source PDOs, inferred contract and raw HPM registers | Yes |
| `inspect` | Identity + thermal + registers + host power | Yes |
| `watch` | Read-only thermal observation | Yes |
| `watch --silence` | Experimental controlled silence | Yes |
| `auto` | Restore automatic mode and verify | Yes |
| `connect` | Explicit Discover + Enter, never Exit | Yes |
| `--help` | Usage and watcher options | No |

All commands accept `--json`. Watch output is newline-delimited JSON; other
successful commands return a single JSON document. Errors go to stderr.
Unknown measurements are `null`, not zero. Exit codes: 0 success, 1 runtime
error, 2 invalid arguments or unconfirmed watcher restoration, 130 interrupted
after the restoration attempt. Identity captures contain hardware identifiers.

## Available data and open questions

| Data | Status / interpretation |
|---|---|
| Model, Service Tag, board/module, serials, firmware, raw port status | Read from Dell HID |
| Declared supply wattage | 130 W on the tested dock; not measured consumption |
| Fan profile | Automatic or forced |
| Speed class | 0: stopped **or unavailable**; 1: 0 < RPM < 2750; 2: RPM ≥ 2750 |
| Local, remote, module temperatures | EC Attention replies |
| Host charging profiles | Observed 5 V/3 A and 19.5 V/4.5 A |
| PD contract | Locally inferred 19.5 V × 4.5 A = 87.75 W capacity, not consumption |
| Mac input voltage/current/power | Private AppleSmartBattery keys; inferred units, possibly cached |
| HPM diagnostics | Raw registers `03, 3f, 30–35, 48, 49`, including lengths/status |
| Exact RPM | Internal tachometer located in firmware; external read not established |
| Total dock / per-port consumption | Unknown |
| Port switching / internal fan curves | Research leads only; no public write commands |
| Firmware update | Deferred; no flash operation exposed |

Mac input measurements exclude the dock's losses and downstream peripherals.
The power source is not attributed to the dock by the host telemetry API.
PDO/RDO decoding accepts only corroborated fixed SPR layouts; it does not guess
PPS, EPR, GiveBack or unrecognized formats. The ten-byte Apple register layout
is explicitly marked inferred. Quiet/Optimized/Cool/Ultra Performance names
have not been mapped to the dock bytes.

## Library integration

See [API guide](docs/API.md), [public header](include/dock.h),
[identity example](examples/info.c) and [watcher example](examples/watch.c).
The C ABI is usable from C++, Swift modules or an FFI. API version **2** changes
the watcher callback/options; rebuild integrations with matching headers and
binaries. Calls are synchronous and callbacks must return promptly.

```sh
clang -Iinclude examples/info.c libdock.a \
  -framework IOKit -framework CoreFoundation -o /tmp/dock-info
```

## Resources and research

- [Research index and current protocol summary](research/README.md): evidence,
  confirmed packet layout, limits and reproducibility.
- [Resource links](docs/RESOURCES.md): Dell, TI, fwupd, Asahi and USB-PD references.
- [Detailed chronological thermal notebook](research/thermal-protocol.md) and
  [prototype guide](research/prototype-guide.md): original French lab records,
  including superseded experiments; follow this README for current operation.
- [Release and repository maintenance](docs/RELEASING.md): CI, artifacts, tags,
  Homebrew formula updates and `gh` commands.

Original code is MIT-licensed except the Apache-2.0 HPM-derived files. See
[LICENSE](LICENSE), [NOTICE](NOTICE) and the [vendored Apache license](research/macvdmtool/LICENSE).
Dell installers, firmware images and extracted vendor binaries stay local and
are not distributed. This is an independent project, not a Dell-supported tool.
