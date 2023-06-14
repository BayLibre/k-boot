#!/bin/bash

set -e
set -u
set -o pipefail

SRC=$(dirname "$(readlink -e "$0")")
source "${SRC}/utils.sh"

KEXEC="${ROOT}/kexec-tools"

function build_kexec {
    local clean="$1"
    local mode="$2"

    display_current_build "kexec" "${mode}"

    pushd "${KEXEC}"

    export CROSS_COMPILE=aarch64-linux-musl-

    if [[ "${clean}" == true ]] && [ -f Makefile ]; then
        make clean
    fi

    [ ! -f configure ] && ./bootstrap

    [ ! -f Makefile ] && LDFLAGS=-static ./configure --host=aarch64-linux-musl

    make -j"$(nproc)"

    aarch64-linux-musl-strip build/sbin/kexec
    cp build/sbin/kexec "${INITRAMFS}/bin/"

    unset CROSS_COMPILE

    popd
}

if [ "$0" = "$BASH_SOURCE" ]; then
    main "$@"
fi
