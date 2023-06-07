#!/bin/bash

set -e
set -u
set -o pipefail

SRC=$(dirname "$(readlink -e "$0")")
source "${SRC}/utils.sh"

KBOOTD="${ROOT}/kbootd"

function build_kbootd {
    local clean="$1"
    local mode="$2"

    display_current_build "kbootd" "${mode}"

    pushd "${KBOOTD}"

    export CROSS_COMPILE=aarch64-linux-musl-

    [[ "${clean}" == true ]] && rm -rf build

    if [ ! -d build ]; then
        meson setup build --prefix "${INITRAMFS}" --cross-file meson.cross
    fi

    ninja -C build install

    unset CROSS_COMPILE

    popd
}

if [ "$0" = "$BASH_SOURCE" ]; then
    main "$@"
fi
