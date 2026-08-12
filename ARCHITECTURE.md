# Repository architecture

```text
apps/
  web/       Browser editor and MazeBench renderer integration
  apple/     Reserved for the shared iOS/macOS client
engine/      Platform-independent C++ physics, tests, and benchmarks
scripts/     Repository build, test, and benchmark entry points
```

The dependency direction is one-way: platform apps consume `engine`; the C++
engine never imports web or Apple code. Project JSON is the portable scene and
test format shared by every client.
