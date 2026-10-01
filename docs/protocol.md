# HID LampArray, as seen on Linux

Windows "Dynamic Lighting" is built on the **HID LampArray** standard (HID Usage Page
`0x59`, "Lighting and Illumination"). Any keyboard, mouse or LED device that exposes a
LampArray collection can be driven by a generic host stack. Linux has no in-kernel
LampArray driver, so `lampctl` talks to the `hidraw` node directly.

The spec fixes the *contents* of each report but **not their report IDs**, so `lampctl`
parses the report descriptor at runtime instead of hardcoding IDs.

## Reports used

| Collection usage | Name                      | Purpose                                         |
|------------------|---------------------------|-------------------------------------------------|
| `0x02`           | LampArrayAttributesReport | lamp count, bounding box, kind, min update interval |
| `0x20`           | LampAttributesRequest     | select a lamp ID to query                       |
| `0x22`           | LampAttributesResponse    | position, latency, purposes of the selected lamp |
| `0x50`           | LampMultiUpdateReport     | set N lamps individually (N is device-specific) |
| `0x60`           | LampRangeUpdateReport     | set lamps `start..end` to one colour            |
| `0x70`           | LampArrayControlReport    | `AutonomousMode`: 1 = firmware effect, 0 = host |

Field usages: `0x03` LampCount, `0x21` LampId, `0x51..0x54` Red/Green/Blue/Intensity,
`0x55` LampUpdateFlags (bit 0 = LampUpdateComplete), `0x61/0x62` LampIdStart/End,
`0x71` AutonomousMode.

Flow: set `AutonomousMode = 0`, send updates with `LampUpdateComplete` set, and set
`AutonomousMode = 1` to hand control back to the firmware.

## Reference device: ITE `048d:c693` (Lenovo LOQ IRX10, 24-zone Spectrum keyboard)

The chip exposes two hidraw nodes:

* **LampArray node** (descriptor starts `05 59 09 01`). All reports are *Feature* reports:

  | ID   | Kind        | Size  |
  |------|-------------|-------|
  | 0x01 | Attributes  | 23 B  |
  | 0x02 | AttrRequest | 3 B   |
  | 0x03 | AttrResponse| 29 B  |
  | 0x04 | MultiUpdate | 51 B (8 lamps per report) |
  | 0x05 | RangeUpdate | 10 B  |
  | 0x06 | Control     | 2 B   |

* **Keyboard + vendor node**: standard boot keyboard, consumer control, and vendor pages
  `0xFF89` / `0xFF99` including a 32-byte feature report `0xCC`. That one is likely the
  Lenovo-specific channel (firmware profiles etc.) and is **not** used by `lampctl` yet.

Both descriptors are stored in `tests/descriptors/` and used by `make test`.

## Dumping a descriptor from your own machine

```sh
for d in /sys/class/hidraw/hidraw*; do
  echo "== $d  $(grep HID_NAME $d/device/uevent)"
  xxd -p $d/device/report_descriptor | tr -d '\n'; echo
done
./lampctl decode <hex>
```
