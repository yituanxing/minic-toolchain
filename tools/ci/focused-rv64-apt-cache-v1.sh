#!/usr/bin/env bash
# Reuse the same package archive cache as the certified Linux 3352 gate.
# A cold Ubuntu mirror must NOT consume the whole CI job before compilation.
set -Eeuo pipefail
cache="${RV64_APT_CACHE_DIR:-$HOME/.cache/minic-apt/archives}"
mkdir -p "$cache/partial"

cross_ready() {
  command -v riscv64-linux-gnu-gcc >/dev/null 2>&1 &&
  command -v riscv64-linux-gnu-as >/dev/null 2>&1 &&
  command -v riscv64-linux-gnu-nm >/dev/null 2>&1 &&
  command -v riscv64-linux-gnu-ld >/dev/null 2>&1 &&
  command -v qemu-riscv64 >/dev/null 2>&1 &&
  test -f /usr/riscv64-linux-gnu/include/stdio.h
}
if cross_ready; then
  echo 'FOCUSED_APT=PASS source=runner-preinstalled'
  exit 0
fi

# Previous Linux corpus/PR workflows persisted the dependency .debs under
# this exact cache key. Prefer local installation without an apt-get update.
if [[ "${RV64_CACHE_HIT:-false}" == true ]]; then
  mapfile -d '' -t debs < <(find "$cache" -maxdepth 1 -type f -name '*.deb' -print0 | sort -z)
  if (( ${#debs[@]} > 0 )); then
    echo "FOCUSED_APT=CACHE_RESTORE debs=${#debs[@]}"
    if ! timeout 85s sudo apt-get -o Acquire::Retries=0 \
        -o Acquire::http::Timeout=12 -o Acquire::https::Timeout=12 \
        -o DPkg::Lock::Timeout=15 \
        install -y --no-install-recommends "${debs[@]}"; then
      echo 'FOCUSED_APT=CACHE_INCOMPLETE local_debs_failed=1'
    fi
  else
    echo 'FOCUSED_APT=CACHE_EMPTY restore_hit_without_debs=1'
  fi
  if cross_ready; then
    echo 'FOCUSED_APT=PASS source=existing-rv64-package-archive-cache'
    exit 0
  fi
fi

# Cold cache or missing dependencies: one bounded attempt only. The old
# 360s apt install -> 300s apt update -> 480s apt install hid mirror stalls
# for 14m and never executed a single compiler test.
# GitHub's Azure mirror took over two minutes to transfer one 10MB
# cpp-riscv64 package (#39893228873), exhausting our bounded download.
# Keep the runner's pinned Ubuntu APT indexes and switch only the archive
# transport to the canonical Ubuntu origin. Package signatures remain APT
# verified. Avoid an expensive unconditional apt-get update.
if grep -qs 'azure.archive.ubuntu.com/ubuntu' /etc/apt/sources.list.d/ubuntu.sources; then
  sudo sed -i 's|http://azure.archive.ubuntu.com/ubuntu|http://archive.ubuntu.com/ubuntu|g' \
    /etc/apt/sources.list.d/ubuntu.sources
  echo 'FOCUSED_APT=MIRROR canonical_archive_ubuntu=1'
fi
echo 'FOCUSED_APT=COLD_DOWNLOAD timeout_s=170 mirror=ubuntu-canonical'
packages=(gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu
          libc6-dev-riscv64-cross qemu-user)
if ! timeout 170s sudo apt-get \
    -o Acquire::Retries=0 \
    -o Acquire::http::Timeout=20 -o Acquire::https::Timeout=20 \
    -o DPkg::Lock::Timeout=20 \
    -o Dir::Cache::archives="$cache" \
    -o APT::Keep-Downloaded-Packages=true \
    install -y --no-install-recommends "${packages[@]}"; then
  echo 'FOCUSED_APT=FAIL stage=cold_cross_tool_download reason=mirror_or_packages' >&2
  exit 1
fi
if ! cross_ready; then
  echo 'FOCUSED_APT=FAIL stage=toolchain_or_sysroot_incomplete' >&2
  exit 1
fi
sudo chown -R "$USER:$USER" "$cache" || true
echo 'FOCUSED_APT=PASS source=bounded-package-download'
