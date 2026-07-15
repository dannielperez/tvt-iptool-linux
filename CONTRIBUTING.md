# Contributing

Contributions are welcome through pull requests.

Before submitting:

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

Do not commit proprietary SDK binaries, headers, firmware, vendor artwork, credentials, customer addresses, or captures from customer networks. Tests should use synthetic XML and mock libraries only.

Protocol changes should include a synthetic fixture and a short evidence note explaining how the behavior was observed without copying vendor source code.
