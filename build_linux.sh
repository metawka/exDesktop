#!/usr/bin/env bash
# exDesktop Linux Native Build & Validation Script
# Supports:
#   1. Native Host Build (using system Qt6, Python3-dev and dependencies)
#   2. Docker Hermetic Build (official tdesktop container with full WebRTC/Qt6 stack)
#   3. Test Suite Execution (verification of extera_plugins engine)
set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Your own credentials from https://my.telegram.org. Without them the build
# uses tdesktop's TEST ONLY credentials.
if [ -n "${TDESKTOP_API_ID:-}" ]; then
    API_ARGS=(-DTDESKTOP_API_ID="${TDESKTOP_API_ID}" -DTDESKTOP_API_HASH="${TDESKTOP_API_HASH}")
else
    API_ARGS=(-DTDESKTOP_API_TEST=ON)
fi

MODE="${1:---help}"

show_help() {
    cat <<EOF
exDesktop Linux Native Porter & Builder

Usage:
  ./build_linux.sh [command] [options]

Commands:
  test                Run native extera_plugins C API verification suite
  configure           Configure CMake for native host build
  build               Build Telegram binary on host
  docker              Build Telegram in official tdesktop Linux container
  patch               Generate or check linux compatibility patch
  help                Show this message

Environment Variables:
  TDESKTOP_API_ID     Telegram API ID (default: test credentials)
  TDESKTOP_API_HASH   Telegram API Hash
  BUILD_DIR           Build directory (default: out)
  CONFIG              Build configuration: Release or Debug (default: Release)
EOF
}

run_test() {
    echo "==> Running extera_plugins Linux Native Engine Test..."
    local CFLAGS
    local LDFLAGS
    CFLAGS="$(python3-config --cflags --embed 2>/dev/null || python3-config --cflags)"
    LDFLAGS="$(python3-config --ldflags --embed 2>/dev/null || python3-config --ldflags)"
    
    mkdir -p tests
    clang++ -std=c++17 -O2 tests/test_extera_plugins.cpp ${CFLAGS} ${LDFLAGS} -o tests/test_extera_plugins
    ./tests/test_extera_plugins
    echo "==> Plugin engine test succeeded!"
}

run_configure() {
    local BUILD_DIR="${BUILD_DIR:-out}"
    local CONFIG="${CONFIG:-Release}"
    echo "==> Initializing submodules (lib_ui, cmake)..."
    git submodule update --init Telegram/lib_ui cmake
    
        local CACHE_ARGS=()
    if command -v sccache >/dev/null 2>&1; then
        echo "==> Using sccache compiler cache for accelerated rebuilds."
        CACHE_ARGS=(-DDESKTOP_APP_USE_COMPILER_CACHE=ON)
    elif command -v ccache >/dev/null 2>&1; then
        echo "==> Using ccache compiler cache for accelerated rebuilds."
        CACHE_ARGS=(-DDESKTOP_APP_USE_COMPILER_CACHE=ON)
    fi

    echo "==> Configuring CMake in ${BUILD_DIR} (${CONFIG})..."
    cmake -B "${BUILD_DIR}" -G Ninja \
        -DCMAKE_BUILD_TYPE="${CONFIG}" \
        "${API_ARGS[@]}" \
        "${CACHE_ARGS[@]}" \
        -DDESKTOP_APP_DISABLE_X11_INTEGRATION=OFF \
        -DDESKTOP_APP_DISABLE_WAYLAND_INTEGRATION=OFF \
        "${@:2}" 
}

run_build() {
    local BUILD_DIR="${BUILD_DIR:-out}"
    local CONFIG="${CONFIG:-Release}"
    if [ ! -f "${BUILD_DIR}/build.ninja" ] && [ ! -f "${BUILD_DIR}/Makefile" ]; then
        run_configure "$@"
    fi
    echo "==> Compiling exDesktop (${CONFIG})..."
    cmake --build "${BUILD_DIR}" --config "${CONFIG}" --target Telegram
    echo "==> Build complete: ${BUILD_DIR}/bin/Telegram"
}

run_docker() {
    local CONFIG="${CONFIG:-Release}"
    local DOCKER_CLI
    if command -v docker >/dev/null 2>&1; then
        DOCKER_CLI="docker"
    elif command -v podman >/dev/null 2>&1; then
        DOCKER_CLI="podman"
    else
        echo "[ERROR] Neither docker nor podman found in PATH." >&2
        exit 1
    fi

    echo "==> Preparing official Docker build environment..."
    git submodule update --init Telegram/lib_ui cmake
    
    local IMAGE_TAG="tdesktop:centos_env"
    if ! ${DOCKER_CLI} image inspect "${IMAGE_TAG}" >/dev/null 2>&1; then
        echo "==> Building ${IMAGE_TAG} image..."
        cd Telegram/build/docker/centos_env
        if command -v poetry >/dev/null 2>&1; then
            poetry install
            poetry run python gen_dockerfile.py | DOCKER_BUILDKIT=1 ${DOCKER_CLI} build -t "${IMAGE_TAG}" -
        else
            python3 -m pip install jinja2
            python3 gen_dockerfile.py | DOCKER_BUILDKIT=1 ${DOCKER_CLI} build -t "${IMAGE_TAG}" -
        fi
        cd "$SCRIPT_DIR"
    fi

    echo "==> Running build in container..."
    local DOCKER_TTY=""
    [ -t 0 ] && DOCKER_TTY="-it"
    ${DOCKER_CLI} run --rm ${DOCKER_TTY} \
        -u "$(id -u):$(id -g)" \
        -v "$PWD:/usr/src/tdesktop" \
        -e CONFIG="${CONFIG}" \
        "${IMAGE_TAG}" \
        /usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh \
        "${API_ARGS[@]}"

    echo "==> Success! Binary output: out/Release/root/Telegram"
}

run_patch() {
    mkdir -p patches
    git diff main > patches/exdesktop_linux_port.patch || git diff HEAD~1 > patches/exdesktop_linux_port.patch
    echo "==> Patch saved to patches/exdesktop_linux_port.patch ($(wc -l < patches/exdesktop_linux_port.patch) lines)"
}

case "$MODE" in
    test)
        run_test
        ;;
    configure)
        run_configure "$@"
        ;;
    build)
        run_build "$@"
        ;;
    docker)
        run_docker "$@"
        ;;
    patch)
        run_patch
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        echo "[ERROR] Unknown command: $MODE"
        show_help
        exit 1
        ;;
esac
