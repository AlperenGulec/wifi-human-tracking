# pc/labeling

Turns recorded video into `labels.csv`.

- Person detection on recorded frames (YOLO or MobileNet-SSD)
- Homography: image pixel → floor (x, y), using `config/rooms/*.yaml`
- Floor (x, y) → zone id
