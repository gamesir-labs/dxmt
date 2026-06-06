#!/bin/sh

UNIX_INSTALL_DIR=$2
INSTALL_DIR="${DESTDIR%%/}${MESON_INSTALL_PREFIX}/${UNIX_INSTALL_DIR}"

mkdir -p "${INSTALL_DIR}"
cp "${MESON_BUILD_ROOT}/src/winemetal4/unix/winemetal4.so" "${INSTALL_DIR}/winemetal4.so"


if [ $1 == "true" ]; then
    strip "${INSTALL_DIR}/winemetal4.so" -x
fi
