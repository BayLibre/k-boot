#!/bin/bash

set -e
set -u
set -o pipefail

SRC=$(dirname "$(readlink -e "$0")")
source "${SRC}/build_busybox.sh"
source "${SRC}/build_kbootd.sh"
source "${SRC}/build_kexec.sh"
source "${SRC}/build_linux.sh"
source "${SRC}/utils.sh"

function build_all {
    local clean="$1"
    local mode="$2"
    local linux="$3"
    local defconfig="$4"

    build_busybox "${clean}" "${mode}"

    build_kexec "${clean}" "${mode}"

    build_kbootd "${clean}" "${mode}"

    build_linux "$@"
    cp "${linux}/arch/arm64/boot/Image" "${ROOT}/k-boot-${mode}.bin"
}

function usage {
    cat <<DELIM__
usage: $(basename "$0") [options]

$ $(basename "$0") --linux=~/src/linux --defconfig=kboot_defconfig

Options:
  --linux       Linux project path
  --defconfig   Linux defconfig
  --clean       (OPTIONAL) clean before build
  --mode        (OPTIONAL) [release|debug|factory] mode (default: release)
  --help        (OPTIONAL) display usage
DELIM__
}

function main {
    local script=$(basename "$0")
    local build="${script%.*}"
    local clean=false
    local defconfig=""
    local linux=""
    local mode="release"

    local opts_args="clean,defconfig:,help,linux:,mode:"
    local opts=$(getopt -o '' -l "${opts_args}" -- "$@")
    eval set -- "${opts}"

    while true; do
        case "$1" in
            --clean) clean=true; shift ;;
            --defconfig) defconfig="$2"; shift 2 ;;
            --linux) linux=$(find_path "$2"); shift 2 ;;
            --mode) mode="$2"; shift 2 ;;
            --help) usage; exit 0 ;;
            --) shift; break ;;
        esac
    done

    # check arguments
    [ -z "${linux}" ] && error_usage_exit "Cannot find linux project"
    [ -z "${defconfig}" ] && error_usage_exit "defconfig not provided"
    ! [[ " ${MODES[*]} " =~ " ${mode} " ]] && error_usage_exit "${mode} mode not supported"

    # build
    build_all "${clean}" "${mode}" "${linux}" "${defconfig}"
}

if [ "$0" = "$BASH_SOURCE" ]; then
    main "$@"
fi
