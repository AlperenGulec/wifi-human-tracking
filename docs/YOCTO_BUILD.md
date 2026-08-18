# Yocto Build Guide — Raspberry Pi 2 + Camera Module (IMX219)

Host: Ubuntu 24.04 LTS (x86-64). Target: `MACHINE = "raspberrypi2"`. Release: Yocto 5.0 **scarthgap** (LTS).
Scope: camera capture only. No ESP32, no serial logger.

Requirements: ~90 GB free disk, 8 GB RAM, do not build as root, do not build on the Pi.

---

## 1. Host setup

```bash
sudo apt update
sudo apt install -y build-essential chrpath cpio debianutils diffstat file \
  gawk gcc git iputils-ping libacl1 libcrypt-dev locales python3 python3-git \
  python3-jinja2 python3-pexpect python3-pip python3-subunit socat texinfo \
  unzip wget xz-utils zstd liblz4-tool bmap-tools
```

Locale (BitBake requires UTF-8):

```bash
sudo locale-gen en_US.UTF-8
sudo update-locale LANG=en_US.UTF-8
export LANG=en_US.UTF-8
export LC_ALL=en_US.UTF-8
```

AppArmor fix (mandatory on 24.04, otherwise BitBake fails immediately):

```bash
echo 'kernel.apparmor_restrict_unprivileged_userns=0' | \
  sudo tee /etc/sysctl.d/60-apparmor-namespace.conf
sudo sysctl -p /etc/sysctl.d/60-apparmor-namespace.conf
```

## 2. Clone layers (all on the same branch)

```bash
mkdir -p ~/rpi2-yocto && cd ~/rpi2-yocto
git clone -b scarthgap git://git.yoctoproject.org/poky
git clone -b scarthgap git://git.openembedded.org/meta-openembedded
git clone -b scarthgap git://git.yoctoproject.org/meta-raspberrypi
```

`meta-multimedia` is required — `libcamera-apps` lives in `meta-raspberrypi/dynamic-layers/multimedia-layer/`.

## 3. Init build directory

```bash
cd ~/rpi2-yocto
source poky/oe-init-build-env build
```

Re-run this same command in every new shell.

## 4. Add layers

```bash
bitbake-layers add-layer ../meta-openembedded/meta-oe
bitbake-layers add-layer ../meta-openembedded/meta-python
bitbake-layers add-layer ../meta-openembedded/meta-multimedia
bitbake-layers add-layer ../meta-openembedded/meta-networking
bitbake-layers add-layer ../meta-raspberrypi
bitbake-layers show-layers   # verify
```

Order matters (meta-multimedia depends on meta-oe + meta-python).

## 5. local.conf

Edit `build/conf/local.conf`:

```
MACHINE = "raspberrypi2"
DISTRO = "poky"

# Serial console
ENABLE_UART = "1"

# Camera v2.1 / IMX219 -> adds dtoverlay=imx219 to config.txt
RASPBERRYPI_CAMERA_V2 = "1"
# Do NOT set VIDEO_CAMERA, start_x, or camera_auto_detect.

IMAGE_FSTYPES = "wic.bz2 wic.bmap"
IMAGE_ROOTFS_EXTRA_SPACE = "4194304"

BB_NUMBER_THREADS = "8"
PARALLEL_MAKE = "-j 8"

DL_DIR ?= "${TOPDIR}/../downloads"
SSTATE_DIR ?= "${TOPDIR}/../sstate-cache"

EXTRA_IMAGE_FEATURES = "debug-tweaks ssh-server-dropbear"

IMAGE_INSTALL:append = " \
  kernel-modules \
  libcamera libcamera-apps \
  v4l-utils \
  coreutils util-linux \
  e2fsprogs e2fsprogs-resize2fs parted \
  rsync \
  tzdata \
  "
```

Notes:

- Keep the default SysVinit/BusyBox init — lighter on the 1 GB Pi 2.
- `kernel-modules` ships `imx219`, `bcm2835-isp`, `bcm2835-codec`.
- Leave `GPU_MEM` at the default 64 MB.

## 6. Build

```bash
cd ~/rpi2-yocto/build
bitbake core-image-minimal
```

First build: a few hours. Rebuilds with sstate: minutes.

## 7. Flash

```bash
cd tmp/deploy/images/raspberrypi2
lsblk                                   # identify the card, e.g. /dev/sdX
sudo bmaptool copy core-image-minimal-raspberrypi2.wic.bz2 /dev/sdX
```

dd fallback:

```bash
bzcat core-image-minimal-raspberrypi2.wic.bz2 | \
  sudo dd of=/dev/sdX bs=4M conv=fsync status=progress
sync
```

## 8. First boot

- Login: `root`, no password.
- UART console: 115200 8N1 — GND pin 6, Pi TXD (GPIO14, pin 8) → adapter RX, Pi RXD (GPIO15, pin 10) → adapter TX.
- Ethernet comes up via DHCP; check `ip addr`. SSH via dropbear.

Expand rootfs to the whole card:

```bash
parted -s /dev/mmcblk0 resizepart 2 100%
resize2fs /dev/mmcblk0p2
```

## 9. Camera test

```bash
rpicam-hello --list-cameras
# expect: 0 : imx219 [3280x2464] (...)

rpicam-vid -t 5000 --width 1280 --height 720 --framerate 10 \
  --codec h264 --save-pts frames.csv -o test.h264

ls -l /dev/video11        # hardware H.264 encoder (bcm2835-codec)
top -d1                   # rpicam-vid should stay well under one core
lsmod | grep -E 'imx219|bcm2835'
```

Play back `test.h264` on the PC (`ffmpeg -i test.h264` or `mpv`), not with VLC.

High CPU means it fell back to software encode — check `/dev/video11` exists and `CONFIG_VIDEO_BCM2835_CODEC` is enabled.

## 10. Troubleshooting

| Symptom | Fix |
|---|---|
| `User namespaces are not usable by BitBake` | AppArmor sysctl (step 1) |
| `Layer X depends on layer Y, but not enabled` | add missing meta-openembedded sublayer (step 4) |
| `not compatible ... only supports these series` | put ALL layers on `scarthgap` |
| `rpicam-hello: Illegal instruction` | old `LIBCAMERA_ARCH=armv8-neon`; fixed in scarthgap |
| `no cameras available` | `RASPBERRYPI_CAMERA_V2 = "1"`, no `start_x`, no legacy stack |
| high CPU during recording | software encode; confirm `--codec h264` and `/dev/video11` |
| rootfs full quickly | expand partition (step 8) |
| build halts on disk space | free space or move `TMPDIR` |

## 11. Image location

```
tmp/deploy/images/raspberrypi2/core-image-minimal-raspberrypi2.wic.bz2
```

Use the symlink — it always points at the latest build.
