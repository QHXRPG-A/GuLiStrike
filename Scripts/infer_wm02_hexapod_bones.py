"""Infer candidate six-legged WM02 joint centers from Unreal-exported mesh data."""

import json
from pathlib import Path

import numpy as np


PROJECT = Path("D:/UE5.7/test1")
VERTICES_PATH = PROJECT / "Saved/VibeUE/wm02_vertices.json"
COMPONENTS_PATH = PROJECT / "Saved/VibeUE/wm02_components.json"


def kmeans(points, count, seed=7, iterations=80):
    rng = np.random.default_rng(seed)
    centers = [points[rng.integers(len(points))]]
    for _ in range(count - 1):
        existing = np.asarray(centers)
        distance2 = ((points[:, None, :] - existing[None, :, :]) ** 2).sum(axis=2)
        centers.append(points[np.argmax(distance2.min(axis=1))])
    centers = np.asarray(centers, dtype=np.float64)
    for _ in range(iterations):
        distance2 = ((points[:, None, :] - centers[None, :, :]) ** 2).sum(axis=2)
        labels = distance2.argmin(axis=1)
        updated = centers.copy()
        for index in range(count):
            if np.any(labels == index):
                updated[index] = points[labels == index].mean(axis=0)
        if np.allclose(updated, centers):
            break
        centers = updated
    return centers, labels


def main():
    vertices = np.asarray(
        json.loads(VERTICES_PATH.read_text(encoding="utf-8"))["points"],
        dtype=np.float64,
    )
    components = json.loads(COMPONENTS_PATH.read_text(encoding="utf-8"))["components"]

    feet_points = vertices[vertices[:, 2] < -5000.0, :2]
    foot_centers, _ = kmeans(feet_points, 6)
    foot_centers = foot_centers[np.lexsort((foot_centers[:, 0], foot_centers[:, 1]))]
    print("FOOT XY CENTERS (Z is near mesh bottom)")
    for center in foot_centers:
        print(f"  X={center[0]:9.1f}  Y={center[1]:9.1f}")

    candidates = [
        component
        for component in components
        if 40 <= component["vertices"] <= 120
        and component["center"][2] < -700.0
        and min(component["size"]) > 40.0
    ]
    candidates.sort(
        key=lambda item: (
            round(item["center"][2] / 100.0) * 100.0,
            item["center"][1],
            item["center"][0],
        )
    )
    print(f"\nROUND/JOINT-LIKE COMPONENT CANDIDATES: {len(candidates)}")
    for item in candidates[:250]:
        center = np.round(item["center"], 1)
        size = np.round(item["size"], 1)
        print(f"  v{item['vertices']:3d} center={center} size={size}")


if __name__ == "__main__":
    main()
