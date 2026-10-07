# WD22TB4 research

This is the English entry point to the lab records. The detailed chronological
notebook and prototype guide preserve their original French wording. Historical
commands and hypotheses may be superseded; use the top-level README for current
operation. Vendor downloads/extractions and generated disassembly remain local.

## Confirmed on the tested setup

WD22TB4, Apple Silicon, AppleHPMLib v3, EC 01.01.00.03. HID `413c:b06e`
identifies the dock; HPM partner identity is Dell `413c:b070`. RID is discovered,
not hard-coded. The thermal session uses Dell SVID `0x413c`, Discover Modes and
explicit Enter Mode 1. Exit is excluded after an earlier display interruption.

| Request | Encoding / observed reply |
|---|---|
| Profile | 28 bytes beginning `12 a1 3c 41 02 0f`; Attention `06 a1 3c 41 01 81 0b mode class` |
| Temperatures | Same envelope, opcode `11`; Attention `01 81 0c local remote module` after its four-byte header |
| Fan setting | Opcode `10`, payload mode/class; status Attention begins `01 80 01` |
| Receipt | `13 a1 3c 41`, acknowledging each new recognized Attention, maximum 16 per exchange |

Automatic mode is 0; forced mode is 1. Established classes: 0 stop, 1 target
1900 RPM, 2 target 3600 RPM. Readback reports coarse measured classes rather than
exact RPM. A class of 0 can also mean measurement unavailable. Padding can retain
old bytes and must not be interpreted as additional thermal data.

The production API exposes automatic restoration and bounded monitored silence,
not arbitrary VDM/register writes. Prototype speed tests are intentionally
separate. Identity/partner checks precede sends and sessions share a process lock.

## Evidence versus inference

- Physically observed: profile/temperature reads, audible 3600 RPM acceleration,
  brief fan stop, one-minute stop, automatic restoration and Ctrl-C restoration.
- Inferred from static firmware: exact tachometer conversion, EC curves, and port
  command handlers. Port commands can reset PD and disrupt power/video; their
  mapping is not established and they have not been sent.
- Correlated local PD capture: fixed 19.5 V/4.5 A offer and matching selected PDO,
  operating capacity 87.75 W. This is contract capacity, not measured consumption.
- Unknown: dock-wide/per-port power, external exact RPM, named thermal profiles,
  configurable internal curves and safe long-term passive operation.
- Not hardware-validated yet: the five-minute silence policy and new configurable
  stability thresholds. Software tests cover those decisions and restoration.

## Files

| File | Purpose |
|---|---|
| [thermal-protocol.md](thermal-protocol.md) | Detailed evidence, offsets, hashes, captures and chronological corrections |
| [prototype-guide.md](prototype-guide.md) | Historical probing and firmware preparation |
| [power-capture.json](power-capture.json) | Original register capture used by pure PD decoder tests |
| [dockctl-probe.c](dockctl-probe.c) | Original bounded HID experiments and self-test |
| [hpm-probe.c](hpm-probe.c) | HPM experiments, bounded speed/stop tests and self-test |
| [unpack-ti-pd.py](unpack-ti-pd.py) | Static TI ROM/RAM reconstruction; no hardware communication |
| [macvdmtool](macvdmtool) | Vendored upstream ABI reference, with original license |

Run hardware-free checks with `make check`. To test only static unpacking:
`python3 research/unpack-ti-pd.py --self-test`. The unpacker's normal mode expects
local vendor extraction files under `research/extracted/dock`; those are not
included. Official source links and download hashes are retained in the notebook.

See [Resources](../docs/RESOURCES.md) for primary references. Capture identity data
can include serials and Service Tags; remove these before sharing fresh logs.
