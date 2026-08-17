# pc/dataset

Joins `csi.csv` and `labels.csv` by nearest timestamp (±50 ms), builds
windowed features, and produces train/test splits by session.

Output: `data/processed/<session>/dataset.npz`
