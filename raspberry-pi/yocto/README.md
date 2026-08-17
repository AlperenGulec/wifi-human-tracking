# Yocto

Recipes and layer notes for the Pi image. The build tree itself lives outside
this repo — only recipes, bblayers notes, and documented local.conf settings
are committed here.

Layers required: meta-raspberrypi, meta-openembedded (meta-oe, meta-python,
meta-multimedia). meta-multimedia is required for the camera recipe
(`rpi-libcamera-apps`) to be visible at all. See
[../../docs/RASPBERRY_PI_V1.md](../../docs/RASPBERRY_PI_V1.md#yocto-configuration).
