#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output_dir="$project_root/apps/web/public/physics"
zig_cache_dir="$project_root/.zig-cache"
zig_global_cache_dir="$project_root/.zig-global-cache"
mkdir -p "$output_dir" "$zig_cache_dir" "$zig_global_cache_dir"

ZIG_LOCAL_CACHE_DIR="$zig_cache_dir" \
ZIG_GLOBAL_CACHE_DIR="$zig_global_cache_dir" \
"$project_root/node_modules/.bin/zig" c++ \
  -target wasm32-freestanding \
  -std=c++20 \
  -O3 \
  -nostdlib \
  -Wl,--no-entry \
  -Wl,--export=physics_abi_version \
  -Wl,--export=voxel_stride \
  -Wl,--export=voxel_capacity \
  -Wl,--export=voxel_buffer \
  -Wl,--export=role_buffer_capacity \
  -Wl,--export=role_buffer \
  -Wl,--export=role_code \
  -Wl,--export=simulate_turn \
  -Wl,--export-memory \
  -Wl,--initial-memory=8388608 \
  -Wl,--max-memory=8388608 \
  -o "$output_dir/voxel_physics.wasm" \
  -I"$project_root/engine/include" \
  "$project_root/engine/src/physics.cpp" \
  "$project_root/engine/src/wasm_api.cpp"
