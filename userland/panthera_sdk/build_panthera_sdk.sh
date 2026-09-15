#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SYSROOT="${PANTHERA_SYSROOT:-${PANTHERA_ROOT}/userland/libsystem/build/sysroot}"
OUT_ROOT="${PANTHERA_ROOT}/userland/panthera_sdk"
SDK_NAME="${PANTHERA_SDK_NAME:-Panthera.sdk}"
SDK_DIR="${OUT_ROOT}/${SDK_NAME}"
MANIFEST="${OUT_ROOT}/Panthera.sdk.manifest.tsv"

if [[ ! -d "${SYSROOT}/usr/include" || ! -d "${SYSROOT}/usr/lib" ]]; then
  echo "Panthera sysroot is incomplete: ${SYSROOT}" >&2
  exit 1
fi
if [[ ! -f "${SYSROOT}/usr/lib/libSystem.B.dylib" ]]; then
  echo "Panthera sysroot is missing canonical libSystem.B.dylib: ${SYSROOT}/usr/lib/libSystem.B.dylib" >&2
  exit 1
fi

rm -rf "${SDK_DIR}.tmp"
mkdir -p "${SDK_DIR}.tmp/usr" "${SDK_DIR}.tmp/System/Library/Frameworks"

cp -R "${SYSROOT}/usr/include" "${SDK_DIR}.tmp/usr/include"
cp -R "${SYSROOT}/usr/lib" "${SDK_DIR}.tmp/usr/lib"
if [[ ! -f "${SDK_DIR}.tmp/usr/lib/libSystem.B.dylib" ]]; then
  echo "Error: missing required libSystem runtime library: ${SDK_DIR}.tmp/usr/lib/libSystem.B.dylib" >&2
  exit 1
fi
ln -sf libSystem.B.dylib "${SDK_DIR}.tmp/usr/lib/libSystem.dylib"
if [[ ! -L "${SDK_DIR}.tmp/usr/lib/libSystem.dylib" && ! -f "${SDK_DIR}.tmp/usr/lib/libSystem.dylib" ]]; then
  echo "Error: failed to create canonical libSystem link: ${SDK_DIR}.tmp/usr/lib/libSystem.dylib" >&2
  exit 1
fi
mkdir -p \
  "${SDK_DIR}.tmp/usr/bin" \
  "${SDK_DIR}.tmp/usr/local/bin" \
  "${SDK_DIR}.tmp/usr/local/include" \
  "${SDK_DIR}.tmp/usr/local/lib" \
  "${SDK_DIR}.tmp/usr/local/libexec" \
  "${SDK_DIR}.tmp/usr/share/man"

copy_header_if_present() {
  local source="$1"
  local dest="$2"
  if [[ -f "${source}" ]]; then
    mkdir -p "$(dirname "${dest}")"
    rm -f "${dest}"
    cp -f "${source}" "${dest}"
    chmod 0644 "${dest}"
  fi
}
stage_required_header() {
  local source="$1"
  local dest="$2"
  if [[ ! -f "${source}" ]]; then
    echo "Error: missing required header: ${source}" >&2
    exit 1
  fi
  mkdir -p "$(dirname "${dest}")"
  rm -f "${dest}"
  cp -f "${source}" "${dest}"
  chmod 0644 "${dest}"
  if [[ ! -f "${dest}" ]]; then
    echo "Error: failed to stage required header to ${dest}" >&2
    exit 1
  fi
}


symlink_header_if_present() {
  local target="$1"
  local link="$2"
  if [[ -e "${SDK_DIR}.tmp/usr/include/${target}" && ! -e "${link}" ]]; then
    ln -sf "${target}" "${link}"
  fi
}

AVDIR="${PANTHERA_ROOT}/src/AvailabilityVersions-137.4"
if [[ ! -d "${AVDIR}" ]]; then
  echo "Error: missing required AvailabilityVersions directory: ${AVDIR}" >&2
  exit 1
fi

stage_availability_header() {
  local template_name="$1"
  local dest="$2"
  local src_template="${AVDIR}/templates/${template_name}"
  local prebuilt_obj="${AVDIR}/obj/${template_name}"

  mkdir -p "$(dirname "${dest}")"
  rm -f "${dest}"
  if [[ -f "${prebuilt_obj}" ]]; then
    cp -f "${prebuilt_obj}" "${dest}"
  elif [[ -f "${src_template}" ]]; then
    if [[ ! -f "${AVDIR}/availability" ]]; then
      echo "Error: missing availability generator at ${AVDIR}/availability" >&2
      exit 1
    fi
    python3 "${AVDIR}/availability" --preprocess "${src_template}" "${dest}"
  else
    echo "Error: missing required availability template or prebuilt header: ${template_name}" >&2
    exit 1
  fi
  chmod 0644 "${dest}"

  if [[ ! -f "${dest}" ]]; then
    echo "Error: failed to stage required availability header to ${dest}" >&2
    exit 1
  fi
}

stage_availability_tool() {
  local dest="$1"
  local prebuilt_obj="${AVDIR}/obj/availability"
  local src_script="${AVDIR}/availability"

  mkdir -p "$(dirname "${dest}")"
  rm -f "${dest}"
  if [[ -f "${prebuilt_obj}" ]]; then
    cp -f "${prebuilt_obj}" "${dest}"
  elif [[ -f "${src_script}" ]]; then
    python3 "${AVDIR}/availability" --preprocess "${src_script}" "${dest}"
  else
    echo "Error: missing availability tool source at ${src_script}" >&2
    exit 1
  fi
  chmod 0755 "${dest}"
  if [[ ! -f "${dest}" ]]; then
    echo "Error: failed to stage availability tool to ${dest}" >&2
    exit 1
  fi
}

stage_availability_header "Availability.h" "${SDK_DIR}.tmp/usr/include/Availability.h"
stage_availability_header "AvailabilityInternal.h" "${SDK_DIR}.tmp/usr/include/AvailabilityInternal.h"
stage_availability_header "AvailabilityInternalLegacy.h" "${SDK_DIR}.tmp/usr/include/AvailabilityInternalLegacy.h"
stage_availability_header "AvailabilityMacros.h" "${SDK_DIR}.tmp/usr/include/AvailabilityMacros.h"
stage_availability_header "AvailabilityVersions.h" "${SDK_DIR}.tmp/usr/include/AvailabilityVersions.h"
stage_availability_header "os_availability.h" "${SDK_DIR}.tmp/usr/include/os/availability.h"
stage_availability_header "AvailabilityInternalPrivate.h" "${SDK_DIR}.tmp/usr/local/include/AvailabilityInternalPrivate.h"
stage_availability_header "AvailabilityProhibitedInternal.h" "${SDK_DIR}.tmp/usr/local/include/AvailabilityProhibitedInternal.h"
stage_availability_header "AvailabilityPrivate.modulemap" "${SDK_DIR}.tmp/usr/local/include/AvailabilityPrivate.modulemap"
stage_availability_header "VersionMap.h" "${SDK_DIR}.tmp/usr/local/include/dyld/VersionMap.h"
stage_availability_header "for_dyld_priv.inc" "${SDK_DIR}.tmp/usr/local/include/dyld/for_dyld_priv.inc"
stage_availability_tool "${SDK_DIR}.tmp/usr/local/libexec/availability.pl"

stage_required_header "${PANTHERA_ROOT}/src/xnu-10002.41.9/EXTERNAL_HEADERS/TargetConditionals.h" "${SDK_DIR}.tmp/usr/include/TargetConditionals.h"
stage_required_header "${PANTHERA_ROOT}/src/xnu-10002.41.9/EXTERNAL_HEADERS/AssertMacros.h" "${SDK_DIR}.tmp/usr/include/AssertMacros.h"
copy_header_if_present "${PANTHERA_ROOT}/src/xnu-10002.41.9/EXTERNAL_HEADERS/ptrcheck.h" "${SDK_DIR}.tmp/usr/include/ptrcheck.h"
stage_required_header "${PANTHERA_ROOT}/src/dyld-1122.1.2/include/dlfcn.h" "${SDK_DIR}.tmp/usr/include/dlfcn.h"
copy_header_if_present "${PANTHERA_ROOT}/src/dyld-1122.1.2/include/dlfcn_private.h" "${SDK_DIR}.tmp/usr/include/dlfcn_private.h"
stage_required_header "${PANTHERA_ROOT}/src/xnu-10002.41.9/libsyscall/mach/mach/vm_page_size.h" "${SDK_DIR}.tmp/usr/include/mach/vm_page_size.h"
stage_required_header "${PANTHERA_ROOT}/src/xnu-10002.41.9/libsyscall/mach/mach/mach_error.h" "${SDK_DIR}.tmp/usr/include/mach/mach_error.h"
copy_header_if_present "${PANTHERA_ROOT}/src/xnu-10002.41.9/libsyscall/mach/mach/sync.h" "${SDK_DIR}.tmp/usr/include/mach/sync.h"
copy_header_if_present "${PANTHERA_ROOT}/src/xnu-10002.41.9/libsyscall/mach/mach/vm_task.h" "${SDK_DIR}.tmp/usr/include/mach/vm_task.h"
copy_header_if_present "${PANTHERA_ROOT}/src/xnu-10002.41.9/libsyscall/mach/mach/mach_right.h" "${SDK_DIR}.tmp/usr/include/mach/mach_right.h"
copy_header_if_present "${PANTHERA_ROOT}/src/xnu-10002.41.9/libsyscall/mach/mach/port_obj.h" "${SDK_DIR}.tmp/usr/include/mach/port_obj.h"
copy_header_if_present "${PANTHERA_ROOT}/src/xnu-10002.41.9/libsyscall/mach/mach/mach_right_private.h" "${SDK_DIR}.tmp/usr/local/include/mach/mach_right_private.h"
copy_header_if_present "${PANTHERA_ROOT}/src/xnu-10002.41.9/libsyscall/mach/mach/port_descriptions.h" "${SDK_DIR}.tmp/usr/local/include/mach/port_descriptions.h"
copy_header_if_present "${PANTHERA_ROOT}/src/xnu-10002.41.9/libsyscall/mach/mach/mach_sync_ipc.h" "${SDK_DIR}.tmp/usr/local/include/mach/mach_sync_ipc.h"
copy_header_if_present "${PANTHERA_ROOT}/src/sys/x86/include/float.h" "${SDK_DIR}.tmp/usr/include/float.h"
copy_header_if_present "${PANTHERA_ROOT}/src/Libnotify-317/notify.h" "${SDK_DIR}.tmp/usr/include/notify.h"
copy_header_if_present "${PANTHERA_ROOT}/src/dyld-1122.1.2/include/objc-shared-cache.h" "${SDK_DIR}.tmp/usr/include/objc-shared-cache.h"
copy_header_if_present "${PANTHERA_ROOT}/src/Libinfo-583.0.1/lookup.subproj/pwd.h" "${SDK_DIR}.tmp/usr/include/pwd.h"
stage_required_header "${PANTHERA_ROOT}/userland/panthera_sdk/include/resolv.h" "${SDK_DIR}.tmp/usr/include/resolv.h"
stage_required_header "${PANTHERA_ROOT}/userland/panthera_sdk/include/arpa/nameser.h" "${SDK_DIR}.tmp/usr/include/arpa/nameser.h"
stage_required_header "${PANTHERA_ROOT}/userland/panthera_sdk/include/arpa/nameser_compat.h" "${SDK_DIR}.tmp/usr/include/arpa/nameser_compat.h"
copy_header_if_present "${PANTHERA_ROOT}/src/Libc-1583.40.7/stdtime/FreeBSD/tzfile.h" "${SDK_DIR}.tmp/usr/include/tzfile.h"
copy_header_if_present "${PANTHERA_ROOT}/src/xnu-10002.41.9/EXTERNAL_HEADERS/stdarg.h" "${SDK_DIR}.tmp/usr/include/stdarg.h"
copy_header_if_present "${PANTHERA_ROOT}/src/xnu-10002.41.9/EXTERNAL_HEADERS/stdatomic.h" "${SDK_DIR}.tmp/usr/include/stdatomic.h"
stage_required_header "${PANTHERA_ROOT}/src/libdispatch-1462.0.4/os/object.h" "${SDK_DIR}.tmp/usr/include/os/object.h"
stage_required_header "${PANTHERA_ROOT}/src/libdispatch-1462.0.4/os/object_private.h" "${SDK_DIR}.tmp/usr/include/os/object_private.h"
stage_required_header "${PANTHERA_ROOT}/src/xnu-10002.41.9/libkern/os/base.h" "${SDK_DIR}.tmp/usr/include/os/base.h"
copy_header_if_present "${PANTHERA_ROOT}/src/xnu-10002.41.9/libkern/os/base_private.h" "${SDK_DIR}.tmp/usr/include/os/base_private.h"
copy_header_if_present "${PANTHERA_ROOT}/src/libplatform-306.0.1/include/os/lock.h" "${SDK_DIR}.tmp/usr/include/os/lock.h"
copy_header_if_present "${PANTHERA_ROOT}/src/libplatform-306.0.1/private/os/lock_private.h" "${SDK_DIR}.tmp/usr/include/os/lock_private.h"

for asl_header in asl_client.h asl_core.h asl_file.h asl_legacy1.h asl_msg.h asl_msg_list.h asl_object.h asl_private.h asl_store.h asl_string.h; do
  copy_header_if_present "${PANTHERA_ROOT}/src/syslog-406/libsystem_asl.tproj/include/${asl_header}" "${SDK_DIR}.tmp/usr/include/${asl_header}"
done

symlink_header_if_present "libkern/Block.h" "${SDK_DIR}.tmp/usr/include/Block.h"
symlink_header_if_present "FreeBSD/nl_types.h" "${SDK_DIR}.tmp/usr/include/nl_types.h"
symlink_header_if_present "mach/mach.h" "${SDK_DIR}.tmp/usr/include/mach.h"
symlink_header_if_present "pthread/pthread.h" "${SDK_DIR}.tmp/usr/include/pthread.h"
symlink_header_if_present "pthread/pthread_impl.h" "${SDK_DIR}.tmp/usr/include/pthread_impl.h"
symlink_header_if_present "pthread/pthread_spis.h" "${SDK_DIR}.tmp/usr/include/pthread_spis.h"
symlink_header_if_present "pthread/sched.h" "${SDK_DIR}.tmp/usr/include/sched.h"
symlink_header_if_present "NetBSD/utmpx.h" "${SDK_DIR}.tmp/usr/include/utmpx.h"

make_simple_framework_link() {
  local name="$1"
  local dylib="$2"
  local framework="${SDK_DIR}.tmp/System/Library/Frameworks/${name}.framework"
  mkdir -p "${framework}/Versions/A"
  ln -sf A "${framework}/Versions/Current"
  ln -sf "Versions/Current/${name}" "${framework}/${name}"
  ln -sf "Versions/Current/Headers" "${framework}/Headers"
  if [[ -d "${SDK_DIR}.tmp/usr/include/${name}" ]]; then
    ln -sf "../../../../../../usr/include/${name}" "${framework}/Versions/A/Headers"
  else
    mkdir -p "${framework}/Versions/A/Headers"
  fi
  ln -sf "../../../../../../usr/lib/${dylib}" "${framework}/Versions/A/${name}"
}

make_simple_framework_link "CoreFoundation" "libCoreFoundation.dylib"
make_simple_framework_link "IOKit" "libIOKit.dylib"
make_simple_framework_link "SystemConfiguration" "libSystemConfiguration.dylib"

system_framework="${SDK_DIR}.tmp/System/Library/Frameworks/System.framework"
mkdir -p "${system_framework}/Versions/B/Resources"
ln -sf B "${system_framework}/Versions/Current"
ln -sf "Versions/Current/System" "${system_framework}/System"
ln -sf "Versions/Current/Headers" "${system_framework}/Headers"
ln -sf "Versions/Current/PrivateHeaders" "${system_framework}/PrivateHeaders"
ln -sf "Versions/Current/Resources" "${system_framework}/Resources"
ln -sf "../../../../../../usr/include" "${system_framework}/Versions/B/Headers"
ln -sf "../../../../../../usr/include" "${system_framework}/Versions/B/PrivateHeaders"
ln -sf "../../../../../../usr/lib/libSystem.B.dylib" "${system_framework}/Versions/B/System"

kernel_framework="${SDK_DIR}.tmp/System/Library/Frameworks/Kernel.framework"
mkdir -p "${kernel_framework}/Versions/A"
ln -sf A "${kernel_framework}/Versions/Current"
ln -sf "Versions/Current/Headers" "${kernel_framework}/Headers"
ln -sf "Versions/Current/PrivateHeaders" "${kernel_framework}/PrivateHeaders"
ln -sf "../../../../../../usr/include" "${kernel_framework}/Versions/A/Headers"
ln -sf "../../../../../../usr/include" "${kernel_framework}/Versions/A/PrivateHeaders"

cat > "${SDK_DIR}.tmp/SDKSettings.json" <<EOF
{
  "CanonicalName": "macosx14.0",
  "CustomProperties": {
    "KERNEL_EXTENSION_HEADER_SEARCH_PATHS": "\$(KERNEL_FRAMEWORK)/PrivateHeaders \$(KERNEL_FRAMEWORK_HEADERS)"
  },
  "DebuggerOptions": {
    "SupportsViewDebugging": "YES"
  },
  "DefaultProperties": {
    "AD_HOC_CODE_SIGNING_ALLOWED": "YES",
    "CODE_SIGN_ENTITLEMENTS": "",
    "CODE_SIGN_IDENTITY": "-",
    "CODE_SIGNING_REQUIRED": "NO",
    "DEFAULT_COMPILER": "com.apple.compilers.llvm.clang.1_0",
    "DEPLOYMENT_TARGET_SUGGESTED_VALUES": ["14.0"],
    "ENTITLEMENTS_DESTINATION": "Signature",
    "ENTITLEMENTS_REQUIRED": "NO",
    "MACOSX_DEPLOYMENT_TARGET": "14.0",
    "PLATFORM_NAME": "macosx",
    "TEST_FRAMEWORK_SEARCH_PATHS": "\$(inherited) \$(PLATFORM_DIR)/Developer/Library/Frameworks",
    "TEST_LIBRARY_SEARCH_PATHS": "\$(inherited) \$(PLATFORM_DIR)/Developer/usr/lib"
  },
  "DefaultVariant": "macos",
  "DisplayName": "Panthera SDK",
  "DefaultDeploymentTarget": "14.0",
  "IsBaseSDK": "YES",
  "MaximumDeploymentTarget": "14.0",
  "MinimalDisplayName": "0.1",
  "MinimumDeploymentTarget": "14.0",
  "PropertyConditionFallbackNames": [],
  "SupportedTargets": {
    "macosx": {
      "Archs": ["x86_64"],
      "BuildVersionPlatformID": "1",
      "ClangRuntimeLibraryPlatformName": "osx",
      "DefaultDeploymentTarget": "14.0",
      "DeploymentTargetSettingName": "MACOSX_DEPLOYMENT_TARGET",
      "DeviceFamilies": [{"Name": "mac", "DisplayName": "Mac"}],
      "LLVMTargetTripleEnvironment": "",
      "LLVMTargetTripleSys": "macos",
      "LLVMTargetTripleVendor": "apple",
      "MaximumDeploymentTarget": "14.0",
      "MinimumDeploymentTarget": "14.0",
      "PlatformFamilyName": "macOS",
      "RecommendedDeploymentTarget": "14.0",
      "SystemPrefix": "",
      "ValidDeploymentTargets": ["14.0"]
    }
  },
  "Variants": [{
    "Name": "macos",
    "BuildSettings": {
      "LLVM_TARGET_TRIPLE_OS_VERSION": "macos14.0",
      "LLVM_TARGET_TRIPLE_SUFFIX": ""
    }
  }],
  "Version": "14.0"
}
EOF

cat > "${SDK_DIR}.tmp/SDKSettings.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
 "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CanonicalName</key>
  <string>macosx14.0</string>
  <key>DefaultDeploymentTarget</key>
  <string>14.0</string>
  <key>DefaultProperties</key>
  <dict>
    <key>AD_HOC_CODE_SIGNING_ALLOWED</key>
    <string>YES</string>
    <key>CODE_SIGN_IDENTITY</key>
    <string>-</string>
    <key>CODE_SIGNING_REQUIRED</key>
    <string>NO</string>
    <key>DEFAULT_COMPILER</key>
    <string>com.apple.compilers.llvm.clang.1_0</string>
    <key>DEPLOYMENT_TARGET_SUGGESTED_VALUES</key>
    <array><string>14.0</string></array>
    <key>MACOSX_DEPLOYMENT_TARGET</key>
    <string>14.0</string>
    <key>PLATFORM_NAME</key>
    <string>macosx</string>
  </dict>
  <key>DefaultVariant</key>
  <string>macos</string>
  <key>DisplayName</key>
  <string>Panthera SDK</string>
  <key>IsBaseSDK</key>
  <string>YES</string>
  <key>MaximumDeploymentTarget</key>
  <string>14.0</string>
  <key>MinimalDisplayName</key>
  <string>0.1</string>
  <key>MinimumDeploymentTarget</key>
  <string>14.0</string>
  <key>SupportedTargets</key>
  <dict>
    <key>macosx</key>
    <dict>
      <key>Archs</key>
      <array><string>x86_64</string></array>
      <key>BuildVersionPlatformID</key>
      <string>1</string>
      <key>ClangRuntimeLibraryPlatformName</key>
      <string>osx</string>
      <key>DefaultDeploymentTarget</key>
      <string>14.0</string>
      <key>DeploymentTargetSettingName</key>
      <string>MACOSX_DEPLOYMENT_TARGET</string>
      <key>LLVMTargetTripleEnvironment</key>
      <string></string>
      <key>LLVMTargetTripleSys</key>
      <string>macos</string>
      <key>LLVMTargetTripleVendor</key>
      <string>apple</string>
      <key>PlatformFamilyName</key>
      <string>macOS</string>
      <key>SystemPrefix</key>
      <string></string>
      <key>ValidDeploymentTargets</key>
      <array><string>14.0</string></array>
    </dict>
  </dict>
  <key>Version</key>
  <string>14.0</string>
</dict>
</plist>
EOF

cat > "${SDK_DIR}.tmp/SDKInfo.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
 "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CanonicalName</key>
  <string>panthera</string>
  <key>DisplayName</key>
  <string>Panthera SDK</string>
  <key>Version</key>
  <string>0.1</string>
</dict>
</plist>
EOF

cat > "${SDK_DIR}.tmp/Entitlements.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
 "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>com.apple.application-identifier</key>
  <string>\$(AppIdentifierPrefix)\$(CFBundleIdentifier)</string>
</dict>
</plist>
EOF

cat > "${SDK_DIR}.tmp/_PROVENANCE" <<EOF
Generated from Panthera sysroot:
  ${SYSROOT}

SDK generator:
  userland/panthera_sdk/build_panthera_sdk.sh

Supplemental headers are copied only from Panthera source inputs already
present in this workspace. Generated SDK contents must not be edited by hand.
EOF

rm -rf "${SDK_DIR}"
mv "${SDK_DIR}.tmp" "${SDK_DIR}"

(
  cd "${SDK_DIR}"
  find . \( -type f -o -type l \) -print | LC_ALL=C sort | while IFS= read -r path; do
    if [[ -L "${path}" ]]; then
      printf '%s\t%s\t%s\n' "${path#./}" "symlink" "$(readlink "${path}")"
    else
      shasum -a 256 "${path}" | awk -v p="${path#./}" '{ print p "\tfile\t" $1 }'
    fi
  done
) > "${MANIFEST}"

echo "Built Panthera SDK: ${SDK_DIR}"
echo "Manifest: ${MANIFEST}"
echo "Files: $(wc -l < "${MANIFEST}" | tr -d ' ')"
