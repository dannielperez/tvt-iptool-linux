# TVT IPTool for Linux

A clean-room C/GTK utility for finding TVT-family cameras, NVRs, and DVRs on a local network and safely changing device network settings.

The project is intentionally buildable and useful without proprietary software:

- LAN discovery implements the vendor Layer-2 multicast protocol and its SSDP/UPnP response format directly.
- The GUI, parser, validation, tests, desktop files, and icons are open source.
- No TVT SDK binary, header, credential, firmware, or copied application asset is included.
- IP modification is enabled only when the operator supplies a compatible Linux `libdvrnetsdk.so` at runtime.

TVT and related product names are trademarks of their respective owners. This project is independent and is not an official vendor release.

## Features

- Immediate or periodic same-broadcast-domain discovery, including devices configured on a different IP subnet
- Device type, port-range, and free-text filters
- IP, MAC, model, name, firmware, data-port, and HTTP-port inventory
- Validated static IPv4, subnet-mask, and gateway editor
- Duplicate-IP detection against the current discovery set
- Runtime-loaded optional SDK; the SDK is never linked into or packaged with the app
- Post-change verification: the same MAC must reappear at the requested IP
- Two C frontends over the same core: GTK 3 for Ubuntu 18.04-era systems and GTK 4 for current desktops

## Build

Install a C toolchain, Meson, Ninja, and the development files for the frontend you want.

Published binaries use explicit variant names:

| Release asset suffix | Built on | Intended runtime |
|---|---|---|
| `linux-x86_64-gtk3` | Ubuntu 18.04 / glibc 2.27 | Legacy and field workstations |
| `linux-x86_64-gtk4` | Ubuntu 24.04 | Current Linux desktops |

Both variants expose the same commands and device-management behavior.

### GTK 3 compatibility build

Use this variant for Ubuntu 18.04-era workstations:

Ubuntu 24.04 / Debian testing:

```sh
sudo apt install build-essential meson ninja-build libgtk-3-dev
meson setup build-gtk3 -Dgui=gtk3
meson compile -C build-gtk3
meson test -C build-gtk3 --print-errorlogs
./build-gtk3/src/tvt-iptool
```

Fedora GTK 3 dependencies:

```sh
sudo dnf install gcc meson ninja-build gtk3-devel
meson setup build-gtk3 -Dgui=gtk3
meson compile -C build-gtk3
meson test -C build-gtk3 --print-errorlogs
./build-gtk3/src/tvt-iptool
```

### GTK 4 modern build

```sh
# Ubuntu 24.04 / current Debian
sudo apt install build-essential meson ninja-build libgtk-4-dev
meson setup build-gtk4 -Dgui=gtk4
meson compile -C build-gtk4
meson test -C build-gtk4 --print-errorlogs
./build-gtk4/src/tvt-iptool
```

Install system-wide with:

```sh
sudo meson install -C build-gtk3
```

## Discovery

The default search sends the original TVT Layer-2 discovery request to `234.55.55.55:23456`, listens on `234.55.55.56:23456`, and also sends an SSDP `M-SEARCH` request to `239.255.255.250:1900`. The binary Layer-2 path finds supported devices on the same Ethernet/VLAN even when their configured IP subnet differs from the workstation. Newer XML responses are parsed with GLib's bounded markup parser.

If the host has multiple network interfaces, select the outgoing IPv4 interface explicitly:

```sh
tvt-iptool --bind-address 192.168.1.20
```

Layer-2 discovery stays on the local broadcast domain. Routed/VPN subnet sweeps are deliberately outside the first release because they have different performance and authorization expectations.

## Deliberate first-release limits

- No factory reset or password-reset action. Those are destructive and vary by firmware.
- No activation, firmware upgrade, or bulk network mutation.
- No routed-subnet sweep; discovery is local multicast only.
- IPv4 provisioning only. IPv6 values may be displayed in a future release once verified against current devices.

These limits keep the initial public tool focused on the two observed, testable workflows: discovery and one-device-at-a-time network configuration.

## Optional SDK setup

Discovery does not need an SDK. Network modification does.

Obtain the Linux device SDK through your vendor or authorized distributor. Keep it outside this repository. The default private runtime location is:

```text
/opt/tvt-iptool/sdk/libdvrnetsdk.so
```

Place its required companion libraries in the same private directory and launch the app with that directory on the dynamic-loader path, for example:

```sh
LD_LIBRARY_PATH=/opt/tvt-iptool/sdk tvt-iptool
```

The installed field launcher may set this automatically. An explicit path still overrides the default:

```sh
export TVT_SDK_PATH=/opt/tvt-sdk/libdvrnetsdk.so
tvt-iptool
```

or:

```sh
tvt-iptool --sdk-path /opt/tvt-sdk
```

The path may name the library or a directory containing `libdvrnetsdk.so`. The SDK's companion libraries must also be visible to the dynamic loader, commonly through its own runtime layout or `LD_LIBRARY_PATH`.

Validate the SDK loader and required symbols without opening the GUI:

```sh
tvt-iptool --sdk-path /opt/tvt-sdk --check-sdk
```

The loader accepts these provisioning surfaces:

1. `NET_SDK_SetDeviceIP`, when present.
2. `NET_SDK_ModifyDeviceNetInfo`, for older SDK builds.

The repository does not download the SDK and the build system never searches the source tree for it.

## Safety model

Changing a camera IP can interrupt recording or make the device unreachable. The GUI therefore:

1. requires a selected device with a MAC address;
2. validates the new IP, mask, gateway, host bits, and special-address exclusions;
3. checks for a duplicate IP in the current discovery set;
4. displays an exact confirmation before mutation;
5. invokes one SDK operation for one MAC;
6. rescans and reports success only if the same MAC appears at the new IP.

If verification fails, treat the operation as indeterminate. Do not update NVR channel mappings or router settings until the device is independently located.

## Architecture

```text
GTK 3 or GTK 4 application
  ├── GListStore device inventory + filters
  ├── POSIX UDP multicast discovery
  │     └── GLib GMarkup response parser
  ├── IPv4 validation and conflict checks
  └── optional runtime SDK loader (dlopen)
        ├── NET_SDK_SetDeviceIP
        └── NET_SDK_ModifyDeviceNetInfo
```

Network and SDK work runs outside the GTK main thread. Secrets are held only for the duration of the operation, are not logged, and are cleared before release.

## CLI options

```text
--sdk-path PATH       SDK library or SDK directory
--bind-address IP     Local IPv4 address for multicast discovery
--timeout-ms N        Per-attempt discovery timeout (250–30000)
--check-sdk           Validate SDK loading and exit
--discover-only       Print a tab-separated LAN inventory and exit
--version             Print the application version
```

## Development

```sh
meson setup build-gtk3 -Dgui=gtk3 -Dwerror=true
meson compile -C build-gtk3
meson test -C build-gtk3 --print-errorlogs

meson setup build-gtk4 -Dgui=gtk4 -Dwerror=true
meson compile -C build-gtk4
meson test -C build-gtk4 --print-errorlogs
```

The `no-vendor-sdk-artifacts` test rejects known vendor SDK filenames, shared libraries, and unexpectedly large files. Review every release archive before publishing.

## License

MIT. See [LICENSE](LICENSE).
