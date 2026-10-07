# Resources

These references explain individual protocol or product claims. They do not
establish universal compatibility or validate unattended fan stopping.

## Dell product and software

- [WD22TB4 specifications](https://www.dell.com/support/manuals/en-ca/wd22tb4-dock/dell_wd22tb4_userguide/product-specifications?guid=guid-bb63fcdf-357e-408c-82f1-c7c56e8d5b4e&lang=en-us): ports and rated host power; the 180 W supply specification does not directly describe the tested 130 W supply.
- [Dell Thunderbolt docks with Apple hosts](https://www.dell.com/support/kbdoc/en-us/000124312/dell-thunderbolt-dock-wd19tb-and-apple-usb-c-hosts): display and feature compatibility. macOS does not provide extended desktops through MST; host capability and wiring matter.
- [Dell Power Manager 3.13](https://www.dell.com/support/home/en-us/drivers/driversdetails?driverid=pv7r1): Windows software inspected for thermal-profile paths.
- [Dell dock firmware updater](https://www.dell.com/support/home/en-us/drivers/driversdetails?driverid=xvxn7): Windows update components inspected statically; no update is performed by this project.
- [LVFS firmware metadata](https://cdn.fwupd.org/downloads/firmware.xml.gz): public firmware discovery source. Downloaded packages are excluded from this repository.

## Protocol implementations and hardware references

- [fwupd Dell dock plugin](https://github.com/fwupd/fwupd/tree/main/plugins/dell-dock): HID transport, EC framing and component descriptors.
- [AsahiLinux/macvdmtool](https://github.com/AsahiLinux/macvdmtool): AppleHPMLib interface; vendored revision `b22ae51eb43a0e1daa21d41616ac899f28e7bf8a`, Apache-2.0.
- [TI USB-PD host interface, SLVUAN1A](https://www.ti.com/lit/ug/slvuan1a/slvuan1a.pdf): controller registers. Apple wrapper layouts are separately inferred, not assumed identical.
- [TI AMC6821 datasheet](https://www.ti.com/lit/ds/symlink/amc6821.pdf): tachometer and fan controller located through EC analysis; an external exact-RPM read is not established.
- [Linux USB-PD definitions](https://github.com/torvalds/linux/blob/master/include/linux/usb/pd.h) and [VDM definitions](https://github.com/torvalds/linux/blob/master/include/linux/usb/pd_vdo.h): PDO/RDO units, fields and structured VDM headers.
- [CableScope](https://github.com/tzzs/cablescope): macOS charging-property exploration; its per-port power-source class produced no entries on the tested Mac.
- [Reported WD22TB4 fan noise](https://www.reddit.com/r/Dell/comments/y9rmaa/docking_station_wd22tb4_annoying_fan_noise/): motivation and anecdotal Power Manager profile changes, not protocol proof.

## Distribution

- [GitHub runner reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners): `macos-14` ARM64 and `macos-15-intel` x86_64 build runners.
- [Homebrew taps](https://docs.brew.sh/Taps.html) and [formula cookbook](https://docs.brew.sh/Formula-Cookbook.html): explicit tap URL and source formula packaging.
