# config

Per-room configuration: zone grid, homography points, node positions.
Read by `pc/labeling` (homography → zone id) and `pc/training` (zone layout).

One YAML file per room, e.g. `rooms/office.yaml`. Committed to git — this is
data the pipeline needs, not generated output.
