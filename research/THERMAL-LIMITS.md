# Thermal limits: what a reported temperature does and does not prove

## Manufacturer evidence

[Dell specifies 0–35 °C operating environment](https://www.dell.com/support/manuals/en-ph/wd22tb4-dock/dell_wd22tb4_userguide/docking-specifications?guid=guid-a66fb04b-6aa4-4a5f-921e-72f06a9ed360&lang=en-us).
This is an ambient/environment rating, not an internal sensor limit. It must
not be compared directly to the reported module temperature.

[TI TPS65994AD datasheet, section 6.3](https://www.ti.com/lit/ds/symlink/tps65994ad.pdf)
specifies a recommended operating junction range of -40–125 °C; ambient limits
also depend on current. This is an example of semiconductor tolerance, not a
confirmed identification of every component or of the module sensor in this dock.
The 175 °C absolute maximum in section 6.1 is a stress rating, not an operating
target. Its availability does not establish system-level thermal safety.

[TI AMC6821 datasheet](https://www.ti.com/lit/ds/symlink/amc6821.pdf) specifies
sensor accuracy and a wide device temperature range. The EC's local and remote
readings come from this controller, but that does not make all neighboring parts
safe up to the controller's own rating. [TI thermal metrics](https://www.ti.com/lit/an/spra953d/spra953d.pdf)
distinguish ambient, case, board and junction temperatures. A sensor reading is
not necessarily the hottest semiconductor junction temperature.

## Firmware evidence, EC 01.01.00.03, module type 8

See the detailed notebook for disassembly addresses and hysteresis:

| Rising temperature | Local | Remote | Module |
|---|---:|---:|---:|
| Leave zero-RPM curve entry | 45 °C | 58 °C | 66 °C |
| Leave 1900-RPM curve entry | 62 °C | 70 °C | 73 °C |

The normal EC decision takes the maximum requirement across the three curves.
Forced mode replaces that decision. The separate protection function
`0x9d013600` in the analyzed firmware checks two 100 °C thresholds and one
105 °C threshold and initiates a delayed shutdown sequence. This path has not
been physically tested. It is not a comfort-control fallback or proof that
keeping the fan off below those temperatures is safe.

## Assessment of proposed 40 / 50 / 75 °C thresholds

- 40 °C local would be lower than the original 42 °C ventilation threshold.
- 50 °C remote would be higher than the original 47 °C threshold, but below the
  normal remote curve's first rising threshold, 58 °C.
- 75 °C module exceeds both the normal 66 °C fan-start threshold and the 73 °C
  next-speed threshold. It also exceeds the CLI's current 73 °C critical backstop
  and is rejected by validation.

A 75 °C reported value alone does not demonstrate damage, nor does it prove
safe fanless operation. The exact physical module sensor and temperature-to-hotspot
relationship have not been established. The latest user trace had maxima
36/37/68 °C and ended its first silent period due to the five-minute deadline,
not a sustained thermal threshold. Raising thresholds would not remove that
explicitly requested time cap. No higher thresholds or longer silent periods
were applied during this research.

## Raised defaults in v0.1.1

At the user's request, ventilation defaults are now 45/50/70 °C, confirmed for
30 seconds. Resume thresholds remain 40/45/66 °C, the silence cap remains
300 seconds, and critical backstops remain 55/60/73 °C without stability delay.
These are experimental choices, not certified limits. The module default is
below the next factory curve step at 73 °C, while still overriding its initial
fan-start decision at 66 °C. No physical test of the raised defaults has yet
been supplied. Existing custom CLI values still override defaults.
