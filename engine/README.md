# Shared C++ physics engine

This directory is the platform-independent source of truth for game physics.

- `include/voxelbench`: public API shared by native and WebAssembly builds
- `src/physics.cpp`: movement, collision, pushing, and Ice rules
- `src/wasm_api.cpp`: thin browser ABI only
- `tests`: native C++ and WebAssembly regression tests
- `benchmarks`: fixed-workload performance baselines

The future iOS/macOS application should link this library directly. The web app
uses the same core compiled to WebAssembly.

After every physics change, run the repository-level test suite. A change is
not complete until all native, WebAssembly, rotation, and web regression tests
pass.
