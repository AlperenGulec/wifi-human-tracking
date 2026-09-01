SUMMARY = "CSI serial logger, camera recording, and session scripts for Wi-Fi CSI tracking"
DESCRIPTION = "Reads CSI CSV from the ESP32 RX over serial and timestamps each \
line on arrival with CLOCK_MONOTONIC (csi-logger). Also installs monoms (a \
CLOCK_MONOTONIC-in-milliseconds helper for the session scripts), the camera \
recording wrapper and PTS converter, the session start/stop/verify scripts, \
and the udev rule for a stable /dev/esp32-rx. See docs/RASPBERRY_PI_V1.md in \
the wifi-human-tracking repo for the full design."
LICENSE = "CLOSED"

# Source files live in the main wifi-human-tracking repo, not in this layer -
# raspberry-pi/yocto/README.md explains why (only recipes are committed to a
# Yocto layer; application source has one home). Point this at your checkout,
# e.g. in local.conf:
#
#   WIFI_TRACKING_REPO = "/home/you/wifi-human-tracking"
#
FILESEXTRAPATHS:prepend := "${WIFI_TRACKING_REPO}/raspberry-pi/logger:${WIFI_TRACKING_REPO}/raspberry-pi/camera:${WIFI_TRACKING_REPO}/raspberry-pi/session:${WIFI_TRACKING_REPO}/raspberry-pi/yocto:${THISDIR}:"

SRC_URI = " \
    file://csi_logger.c \
    file://monoms.c \
    file://Makefile \
    file://record.sh \
    file://pts_to_frames.sh \
    file://session_start.sh \
    file://session_stop.sh \
    file://session_verify.sh \
    file://99-esp32.rules \
"

S = "${WORKDIR}"

do_compile() {
    oe_runmake
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 csi-logger              ${D}${bindir}/csi-logger
    install -m 0755 monoms                  ${D}${bindir}/monoms
    install -m 0755 ${WORKDIR}/record.sh          ${D}${bindir}/record.sh
    install -m 0755 ${WORKDIR}/pts_to_frames.sh   ${D}${bindir}/pts_to_frames.sh
    install -m 0755 ${WORKDIR}/session_start.sh   ${D}${bindir}/session_start.sh
    install -m 0755 ${WORKDIR}/session_stop.sh    ${D}${bindir}/session_stop.sh
    install -m 0755 ${WORKDIR}/session_verify.sh  ${D}${bindir}/session_verify.sh

    install -d ${D}${sysconfdir}/udev/rules.d
    install -m 0644 ${WORKDIR}/99-esp32.rules ${D}${sysconfdir}/udev/rules.d/99-esp32.rules
}

FILES:${PN} += "${sysconfdir}/udev/rules.d/99-esp32.rules"

# The session scripts call rpicam-vid, which must already be in the image -
# see docs/YOCTO_BUILD.md's IMAGE_INSTALL (RASPBERRYPI_CAMERA_V2 = "1" plus
# the libcamera-apps package, which is what actually ships the rpicam-vid
# binary). Not a hard RDEPENDS: this package is also useful standalone
# (csi-logger alone needs no camera at all).
