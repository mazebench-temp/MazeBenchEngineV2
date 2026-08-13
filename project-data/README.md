# VoxelBench project data

The local editor automatically reads and writes `project.json` plus the files
under `tests/`. The manifest contains physics roles, block definitions, suite
folders, searches, and the ordered test index. Every test owns one compact
file containing a palette, its full Start frame, and lossless add/remove deltas
for subsequent ticks and Expected. Static floor and terrain are therefore not
repeated in every frame.

`npm run migrate:project-data` converts the legacy monolithic
`voxelbench-project.json` after verifying every test, frame, and voxel. The web
importer still accepts old monolithic exports, while new browser backups and
exports use the same self-contained compact bundle representation.

Browser storage is retained only as a fallback and as a one-time migration
source when the repo-backed project file does not exist yet.
