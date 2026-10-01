# lampctl

Control keyboard (and other) RGB lighting on Linux through the standard **HID LampArray**
interface, the same one Windows uses for *Dynamic Lighting*. No Python, no dependencies:
one C file.

> ## ⚠️ This project is vibe-coded
> `lampctl` was written by an AI (Claude, by Anthropic) in a conversation with a user who
> wanted RGB control on a Lenovo LOQ under Arch Linux. The code has **not been audited**
> by a human expert and **has not yet been verified on real hardware**. Read the source
> before running it as root, and use it at your own risk. Bug reports and reviews are very
> welcome.

## Status

| Piece                                              | State                                   |
|----------------------------------------------------|-----------------------------------------|
| Descriptor parser (`lampctl decode`, `make test`)  | tested on the real ITE `048d:c693` descriptors |
| Setting colours / autonomous mode on real hardware | **untested, awaiting first report**     |
| systemd unit, udev rule                            | **untested**                            |

Target hardware: Lenovo LOQ IRX10, 24-zone "Spectrum" keyboard, ITE `048d:c693`.
Other LampArray devices should work in theory because nothing is hardcoded per model,
but that is also unverified. Please send a
[compatibility report](.github/ISSUE_TEMPLATE/compatibility-report.md).

## How it works

Linux has no generic LampArray driver. `lampctl` scans `/dev/hidraw*`, parses each HID
report descriptor, and keeps nodes that contain LampArray reports (usage page `0x59`).
Report IDs, sizes and field offsets come from the descriptor, so devices that number their
reports differently are handled. Details: [docs/protocol.md](docs/protocol.md).

## Build

```sh
make            # or: gcc -O2 -Wall -o lampctl src/lampctl.c
make test       # offline parser tests, no hardware needed
sudo make install
```

Needs a C compiler and Linux kernel headers (`base-devel` + `linux-api-headers` on Arch).

## Usage

```sh
sudo lampctl probe            # host model, LampArray devices found, report layout, lamp count
sudo lampctl lamps            # per-lamp positions
sudo lampctl set ff0000       # every lamp on every device
sudo lampctl zone 3 00ff00    # one lamp
sudo lampctl range 0 7 0000ff # lamps 0..7
sudo lampctl off
sudo lampctl auto             # give control back to the firmware effect/profile
lampctl decode <hex>          # parse a descriptor offline
sudo lampctl -d /dev/hidraw1 set ffffff   # restrict to one node
```

Setting a colour switches the device out of firmware (autonomous) mode; `auto` switches it back.

## Running without sudo

See [udev/99-lampctl.rules.example](udev/99-lampctl.rules.example). You must restrict the rule
to the lighting interface; otherwise the keyboard's other hidraw node (raw keystrokes) gets
exposed to your session.

## Apply a colour at boot and after resume

```sh
sudoedit /etc/lampctl.conf          # COLOR=rrggbb
sudo systemctl enable lampctl.service
```

If the colour is not applied at boot, the HID device may not be ready yet; this is an open
question to verify on hardware.

## Not implemented (yet)

* Switching the 6 firmware profiles. This is probably done through the vendor `0xCC` report
  on the other hidraw node and is not part of LampArray.
* Effects/animations, config profiles, a GUI.
* Anything that is not Linux `hidraw`.

## Safety

`lampctl` only sends LampArray HID reports (colour updates and the autonomous-mode switch)
to nodes whose descriptor declares them. It does not flash firmware. There is still no
guarantee; see the vibe-coded notice above and the licence.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). The most useful contribution is a compatibility report.

## Disclaimer

Not affiliated with or endorsed by Lenovo, Microsoft, ITE or Anthropic. Product names are
used only to describe compatibility.

## Licence

MIT, see [LICENSE](LICENSE). Replace the placeholder name in it with your own.
