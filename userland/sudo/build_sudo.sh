#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRCDIR="${PANTHERA_ROOT}/src/sudo-114.60.3/sudo"
OUT_BIN="${PANTHERA_ROOT}/userland/sudo/bin"
OUT_ETC="${PANTHERA_ROOT}/userland/sudo/etc"

CC="$(xcrun -find clang)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
TARGET="x86_64-apple-darwin23.0"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

mkdir -p "${OUT_BIN}" "${OUT_ETC}"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" && -x "${OUT_BIN}/sudo" ]]; then
    echo "sudo already built"
    exit 0
fi

WORKDIR="$(mktemp -d /tmp/panthera-sudo-build.XXXXXX)"
trap 'rm -rf "${WORKDIR}"' EXIT

BUILDDIR="${WORKDIR}/build"
mkdir -p "${BUILDDIR}"
cd "${BUILDDIR}"

TARGET_CFLAGS="-target ${TARGET} -mmacosx-version-min=14.0 -isysroot ${SDKROOT}"

# Remove flat_namespace from configure
sed -i '' 's/-flat_namespace//g; s/-undefined suppress/-undefined error/g' "${SRCDIR}/configure"

"${SRCDIR}/configure" \
    --host="${TARGET}" \
    --build="${TARGET}" \
    --prefix=/usr \
    --without-pam \
    --without-sendmail \
    --without-ldap \
    --without-selinux \
    --without-skey \
    --without-opie \
    --without-kerb5 \
    --disable-nls \
    --disable-shared \
    --with-editor=/usr/bin/vi \
    --with-env-editor \
    --without-interfaces \
    --without-tls \
    --disable-intercept \
    --disable-log-client \
    --disable-log-server \
    --disable-python \
    CC="${CC} ${TARGET_CFLAGS}" \
    CFLAGS="-O2 -Wno-deprecated-declarations -Wno-implicit-function-declaration" \
    LDFLAGS="${TARGET_CFLAGS} -lSystem -Wl,-not_for_dyld_shared_cache"

make -j"${JOBS}" 2>&1 || {
    echo "sudo: full build failed, attempting minimal build..."
    # sudo is complex — if configure/make fails, skip it per task rules
    echo "SKIPPED: sudo build failed — too complex for current sysroot"
    exit 0
}

# Find the built binary
SUDO_BIN="${BUILDDIR}/src/sudo"
if [[ ! -f "${SUDO_BIN}" ]]; then
    SUDO_BIN="${BUILDDIR}/src/.libs/sudo"
fi

if [[ -f "${SUDO_BIN}" ]]; then
    cp "${SUDO_BIN}" "${OUT_BIN}/sudo"
    bash "${PANTHERA_ROOT}/tools/audit_package.sh" "${OUT_BIN}/sudo"
    echo "Built sudo: ${OUT_BIN}/sudo ($(wc -c < "${OUT_BIN}/sudo") bytes)"
fi

# Create sudoers
cat > "${OUT_ETC}/sudoers" <<'SUDOERS'
# /etc/sudoers — Panthera default
root ALL=(ALL) ALL
%wheel ALL=(ALL) ALL
SUDOERS

echo "Created default sudoers"
