# VoxelBench project data

The local editor automatically reads and writes `voxelbench-project.json` in
this directory. The file contains physics roles, block definitions, suite
folders, and every authored start/expected test frame, so changes are visible
to Git and available to the native C++ test workflow.

Browser storage is retained only as a fallback and as a one-time migration
source when the repo-backed project file does not exist yet.
