#!/bin/sh
# SPDX-License-Identifier: MIT
# Install the tested Rust compiler/source locally; never change shell profiles.
set -eu
cd "$(dirname "$0")/.."
task_root=$(pwd)
if [ "$(uname -s):$(uname -m)" != Darwin:arm64 ]; then
    echo 'Install Rust 1.100.0 with rust-src for this host and set DM32_RUST_BIN.' >&2
    exit 1
fi
mkdir -p research/toolchain/downloads toolchain
task_installer="$task_root/research/toolchain/downloads/rustup-init-1.29.3"
if [ ! -f "$task_installer" ]; then
    curl -fL https://static.rust-lang.org/rustup/archive/1.29.3/aarch64-apple-darwin/rustup-init -o "$task_installer"
fi
python3 - "$task_installer" <<'PY'
import hashlib,sys
assert hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest()=='63752a1919a3a4793173ec70f8f212621a7ce38ba6a246095984482f4de20f9f', 'Rust installer hash mismatch'
PY
chmod +x "$task_installer"
export CARGO_HOME="$task_root/toolchain/rust-cargo"
export RUSTUP_HOME="$task_root/toolchain/rustup"
"$task_installer" -y --no-modify-path --profile minimal --default-toolchain 1.100.0 --component rust-src
task_compiler="$RUSTUP_HOME/toolchains/1.100.0-aarch64-apple-darwin"
if [ ! -e toolchain/rust ]; then
    ln -s "$task_compiler" toolchain/rust
elif [ "$(cd toolchain/rust && pwd -P)" != "$task_compiler" ]; then
    echo 'Preserving existing toolchain/rust; select it with DM32_RUST_BIN.' >&2
    exit 1
fi
echo 'Installed Rust 1.100.0 and rust-src internally; no radio was opened.'
