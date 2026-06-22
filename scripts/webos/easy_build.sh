#!/usr/bin/env bash

set -e

#!/usr/bin/env bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"

SDK_VERSION="webos-a38c582"

case "$(uname -m)" in
    aarch64|arm64)
        SDK_ARCHIVE="arm-webos-linux-gnueabi_sdk-buildroot-aarch64.tar.gz"
        SDK_DIR="${WEBOS_SDK_DIR:-/tmp/arm-webos-linux-gnueabi_sdk-buildroot}"
        ;;
    x86_64|amd64)
        SDK_ARCHIVE="arm-webos-linux-gnueabi_sdk-buildroot.tar.gz"
        SDK_DIR="${WEBOS_SDK_DIR:-/tmp/arm-webos-linux-gnueabi_sdk-buildroot}"
        ;;
    *)
        echo "Unsupported host architecture: $(uname -m)"
        exit 1
        ;;
esac

SDK_URL="https://github.com/openlgtv/buildroot-nc4/releases/download/${SDK_VERSION}/${SDK_ARCHIVE}"

cd "${PROJECT_ROOT}"

echo "=== ADB.webOS - Build for LG webOS (LG C9, webOS 4 and higher) ==="
echo "Project: ${PROJECT_ROOT}"
echo "Build type: ${BUILD_TYPE}"
echo ""

if [ ! -f "scripts/webos/build.sh" ]; then
    echo "Error: Run this script from the ADB.webOS project root"
    exit 1
fi

for cmd in cmake awk gawk; do
    if ! command -v $cmd &>/dev/null; then
        echo "Error: $cmd not found. Install it with: sudo apt-get install cmake gawk"
        exit 1
    fi
done

echo "Updating submodules..."
git submodule update --init --recursive

if [ ! -f "${SDK_DIR}/share/buildroot/toolchainfile.cmake" ]; then
    echo ""
    echo "WebOS SDK not found in ${SDK_DIR}"
    echo "Downloading SDK (buildroot-nc4 ${SDK_VERSION})..."
    
    mkdir -p /tmp
    cd /tmp
    
    if [ ! -f "${SDK_ARCHIVE}" ]; then
        if command -v curl &>/dev/null; then
            curl -L -O "${SDK_URL}"
        elif command -v wget &>/dev/null; then
            wget "${SDK_URL}"
        else
            echo "Error: curl or wget is required to download the SDK"
            exit 1
        fi
    fi
    
    echo "Extracting SDK..."
    tar -xzf "${SDK_ARCHIVE}"
    
    EXTRACTED_SDK_DIR=$(tar -tf "${SDK_ARCHIVE}" | head -1 | cut -f1 -d"/")
    if [ -d "${EXTRACTED_SDK_DIR}" ]; then
        echo "Relocating SDK..."
        ./"${EXTRACTED_SDK_DIR}"/relocate-sdk.sh
        SDK_DIR="/tmp/${EXTRACTED_SDK_DIR}"
    else
        echo "Error: Unexpected SDK structure after extraction"
        exit 1
    fi
    
    cd "${PROJECT_ROOT}"
else
    echo "WebOS SDK found in ${SDK_DIR}"
fi

TOOLCHAIN_FILE="${SDK_DIR}/share/buildroot/toolchainfile.cmake"

if [ ! -f "${TOOLCHAIN_FILE}" ]; then
    echo "Error: Toolchain not found in ${TOOLCHAIN_FILE}"
    exit 1
fi

echo ""
echo "Running build..."
export TOOLCHAIN_FILE
./scripts/webos/build.sh -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

echo ""
echo "=== Build complete! ==="
echo "IPK package generated in: ${PROJECT_ROOT}/dist/"
ls -la "${PROJECT_ROOT}/dist/"/*.ipk 2>/dev/null || true
echo ""
echo "To install on LG C9 and higher:"
echo "  1. use ares-install from webOS sdk"
echo "  2. or use dev-manager-desktop for easy installation"
echo ""