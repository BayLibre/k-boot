#!/bin/bash

set -e
set -u
set -o pipefail

SRC=$(dirname "$(readlink -e "$0")")
source "${SRC}/utils.sh"

BUSYBOX="${ROOT}/busybox"

function build_busybox {
    local clean="$1"
    local mode="$2"

    display_current_build "busybox" "${mode}"

    pushd "${BUSYBOX}"

    export CROSS_COMPILE=aarch64-linux-musl-

    [[ "${clean}" == true ]] && make clean

    [ ! -f .config ] && make defconfig

    make -j"$(nproc)" LDFLAGS="--static"

    cp busybox "${INITRAMFS}/bin/"

    unset CROSS_COMPILE

    popd
}

if [ "$0" = "$BASH_SOURCE" ]; then
    main "$@"
fi
