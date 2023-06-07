#!/bin/bash

BUILD=$(dirname "$(readlink -e "$0")")
ROOT=$(readlink -e "${BUILD}/../")
INITRAMFS="${ROOT}/initramfs"
MODES=("release" "debug" "factory")

function pushd {
    command pushd "$@" > /dev/null
}

function popd {
    command popd > /dev/null
}

function find_path {
    local path="$1"
    local real_path=""
    if [ -e "${path}" ]; then
        real_path=$(readlink -e "${path}")
    fi
    echo "${real_path}"
}

function display_current_build {
    local build="$1"
    local mode="$2"

    printf "\n"
    printf "%0.s-" {1..20}
    printf "> Build %s: %s <" "${build}" "${mode}"
    printf "%0.s-" {1..20}
    printf "\n"
}

function usage {
    cat <<DELIM__
usage: $(basename "$0") [options]

$ $(basename "$0")

Options:
  --clean    (OPTIONAL) clean before build
  --mode     (OPTIONAL) [release|debug|factory] mode (default: release)
  --help     (OPTIONAL) display usage
DELIM__
}

function warning {
    local warning="$1"
    printf "\033[0;33mWARNING:\033[0m ${warning}\n\n"
}

function warning_exit {
    warning "$1"
    exit 0
}

function error {
    local error="$1"
    printf "\033[0;31mERROR:\033[0m ${error}\n\n"
}

function error_exit {
    error "$1"
    exit 1
}

function error_usage_exit {
    error "$1"
    usage
    exit 1
}

function main {
    local script=$(basename "$0")
    local build="${script%.*}"
    local clean=false
    local mode="release"

    local opts_args="clean,help,mode:"
    local opts=$(getopt -o '' -l "${opts_args}" -- "$@")
    eval set -- "${opts}"

    while true; do
        case "$1" in
            --clean) clean=true; shift ;;
            --mode) mode="$2"; shift 2 ;;
            --help) usage; exit 0 ;;
            --) shift; break ;;
        esac
    done

    # check arguments
    ! [[ " ${MODES[*]} " =~ " ${mode} " ]] && error_usage_exit "${mode} mode not supported"

    # build
    ${build} "${clean}" "${mode}"
}
