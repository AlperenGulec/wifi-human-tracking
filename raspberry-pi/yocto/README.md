# Yocto

Recipes and layer notes for the Pi image. The build tree itself lives outside
this repo — only recipes, bblayers notes, and documented local.conf settings
are committed here.

Layers required: meta-raspberrypi, meta-openembedded (meta-oe, meta-python,
meta-multimedia). meta-multimedia is required for the camera recipe
(`libcamera-apps`) to be visible at all. See
[../../docs/RASPBERRY_PI_V1.md](../../docs/RASPBERRY_PI_V1.md#yocto-configuration)
and [../../docs/YOCTO_BUILD.md](../../docs/YOCTO_BUILD.md).

## `csi-logger_1.0.bb`

Builds and installs everything in `raspberry-pi/logger`, `raspberry-pi/camera`
and `raspberry-pi/session`, plus `99-esp32.rules`. Application source lives in
this repo, not duplicated into the layer — set in your `local.conf`:

```
WIFI_TRACKING_REPO = "/path/to/your/wifi-human-tracking/checkout"
```

Then drop `csi-logger_1.0.bb` into a `recipes-csi/csi-logger/` directory in
your own layer (or `meta-raspberrypi`'s local additions) and add it to
`IMAGE_INSTALL:append` alongside the camera packages, e.g.:

```
IMAGE_INSTALL:append = " \
  ...
  csi-logger \
  "
```

`core-image-minimal` ships no toolchain, so `csi_logger.c` and `monoms.c` are
cross-compiled by this recipe — they cannot be built on the Pi itself.
