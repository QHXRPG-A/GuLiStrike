"""Build six complete, reference-registered layers on the common canvas."""

import json

import numpy as np
from PIL import Image

from prepare_layers import ROOT, SIZE, WINDOW, sha
from reference_fit import original_position, reconstruct, structural_similarity


REVISION = 4
SIMILARITY_MINIMUM = 0.95
TRAVEL = (36, 44)
DEPTH = {1: 1.0, 2: 0.70, 3: 0.44, 4: 0.44, 5: 0.44, 6: 0.0}
BACKGROUND_RGBA = (8, 64, 102, 255)
BACKGROUND_SWATCH = [200, 48, 580, 245]
# Baked placement of the complete painted sheets and assembly.
DUST_NEAR = (1.1667588583860533, (8, -120))
DUST_BEHIND = (1.192955383162053, (0, -184))
MECH = (1.03575, (40, 72))
BORE_SOURCE = (676, 716)
FLAME_ROOT_SOURCE = (563, 755)
FLAME_SCALE = 1.10


def read_json(relative):
    return json.loads((ROOT / relative).read_text(encoding="utf-8-sig"))


def flame_placement():
    scale, offset = MECH
    bore = [coordinate * scale + origin for coordinate, origin in zip(BORE_SOURCE, offset)]
    root_offset = [target - source * FLAME_SCALE for target, source in zip(bore, FLAME_ROOT_SOURCE)]
    return FLAME_SCALE, root_offset


def fit_complete_source(raw, scale, offset):
    if raw.mode != "RGBA" or raw.getchannel("A").getextrema()[0] != 0:
        raise ValueError("The source needs a genuine transparent background")
    x, y = offset
    result = raw.transform(
        SIZE, Image.Transform.AFFINE,
        (1 / scale, 0, -x / scale, 0, 1 / scale, -y / scale),
        Image.Resampling.BICUBIC,
    )
    if result.getchannel("A").getextrema()[1] < 200:
        raise ValueError("The completed object has no solid foreground")
    return result


def input_records(inputs):
    """Keep external historical inputs under the bundle's relative paths."""
    records = []
    for file in inputs:
        relative = file.replace("\\", "/")
        if ":/" in relative:
            name = relative.rsplit("/", 1)[-1]
            relative = f"Inputs/{name}" if name.endswith("_Edit.png") else name
        if not (ROOT / relative).is_file():
            raise FileNotFoundError(relative)
        records.append({"file": relative, "sha256": sha(ROOT / relative)})
    return records


def main():
    request = read_json("requests.json")
    originals = {item["slug"]: item for item in read_json("Raw/origins.json")}
    first_revisions = {item["slug"]: item for item in read_json("Raw/revisions.json")}
    completed = {item["slug"]: item for item in read_json("Raw/complete-revisions.json")}
    master = ROOT / "Master.png"
    if sha(master) != request["reference_sha256"]:
        raise ValueError("The frozen reference changed")
    placements = {1: DUST_NEAR, 2: DUST_BEHIND, 3: (1.125, (0, 0)), 4: MECH, 5: flame_placement()}
    revisions = {2: first_revisions["02_DustBehind"], 3: completed["03_Lightning"],
                 5: completed["05_MuzzleFlame"]}
    raw_suffix = {2: "_v2", 3: "_v3", 5: "_v3"}
    layers = []
    painted = {}

    for row in request["layers"]:
        index, slug = row["index"], row["slug"]
        if index == 6:
            image = Image.new("RGBA", SIZE, BACKGROUND_RGBA)
            provenance = {"method": "solid_color", "rgba": list(BACKGROUND_RGBA),
                          "reference_swatch": BACKGROUND_SWATCH}
        else:
            edit = revisions.get(index)
            raw_path = ROOT / "Raw" / f"{slug}{raw_suffix.get(index, '')}.png"
            prompt = edit["prompt"] if edit else f"Prompts/{slug}.txt"
            inputs = edit["inputs"] if edit else row["referenced_image_paths"]
            scale, offset = placements[index]
            with Image.open(raw_path) as raw:
                image = fit_complete_source(raw, scale, offset)
                provenance = {
                    "method": "reference_matte_with_painted_completion",
                    "prompt": prompt,
                    "prompt_sha256": sha(ROOT / prompt),
                    "raw_file": raw_path.relative_to(ROOT).as_posix(),
                    "raw_origin": edit["path"] if edit else originals[slug]["path"],
                    "raw_size": list(raw.size),
                    "raw_alpha_range": list(raw.getchannel("A").getextrema()),
                    "raw_sha256": sha(raw_path),
                    "inputs": input_records(inputs),
                    "placement": {"scale": scale, "offset": list(offset)},
                }
            if index == 5:
                provenance["attachment"] = {
                    "flame_root_source": list(FLAME_ROOT_SOURCE),
                    "mech_bore_source": list(BORE_SOURCE),
                }

        painted[index] = np.asarray(image, dtype=np.float32) / 255
        layers.append({
            "index": index,
            "name": row["title"],
            "file": f"Layers/{slug}.png",
            **provenance,
            "depth": DEPTH[index],
        })

    with Image.open(master) as source:
        reference = np.asarray(source.convert("RGB"), dtype=np.float32) / 255
    fitted, rear = reconstruct(reference, painted, WINDOW)
    encoded = {index: np.rint(np.clip(image, 0, 1) * 255).astype(np.uint8)
               for index, image in fitted.items()}
    sampled = {index: image.astype(np.float32) / 255 for index, image in encoded.items()}
    score = structural_similarity(reference, original_position(sampled, rear, WINDOW))
    if score < SIMILARITY_MINIMUM:
        raise ValueError(f"Original position SSIM {score:.4f} is below {SIMILARITY_MINIMUM:.2f}")

    for row in layers:
        image = Image.fromarray(encoded[row["index"]])
        path = ROOT / row["file"]
        image.save(path)
        row["sha256"] = sha(path)
        row["alpha_range"] = list(image.getchannel("A").getextrema())
        print(row["index"], row["file"], image.size, flush=True)

    mask = np.where(rear, 0, 255).astype(np.uint8)
    mask_path = ROOT / "Masks/LightningFront.png"
    Image.fromarray(np.dstack((mask, mask))).save(mask_path)

    manifest = {
        "card_id": request["card_id"],
        "title": "增加攻速",
        "model_id": request["model_id"],
        "source": "Master.png",
        "source_reference": request["reference"],
        "source_sha256": request["reference_sha256"],
        "canvas": list(SIZE),
        "window": list(WINDOW),
        "travel_px": list(TRAVEL),
        "revision": REVISION,
        "lightning_front_mask": mask_path.relative_to(ROOT).as_posix(),
        "draw_order": [6, "3-back", 4, 5, "3-front", 2, 1],
        "generation": "Original-coordinate mattes with separate imagegen completions, solid RGBA background",
        "reference_similarity": {
            "metric": "RGB SSIM",
            "window": list(WINDOW),
            "gaussian_sigma": 1.5,
            "gaussian_width": 11,
            "minimum": SIMILARITY_MINIMUM,
            "value": score,
            "source": "Six saved-equivalent RGBA planes at zero displacement and full opacity",
        },
        "latest_generation_receipts": "Raw/complete-revisions.json",
        "visual_acceptance": "待用户验收",
        "layers": layers,
    }
    (ROOT / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    browser_data = {key: manifest[key] for key in (
        "source", "canvas", "window", "travel_px", "lightning_front_mask", "draw_order", "revision"
    )}
    browser_data["layers"] = [
        {key: layer[key] for key in ("index", "name", "file", "depth")}
        for layer in layers
    ]
    (ROOT / "card-data.js").write_text(
        "window.cardData = " + json.dumps(browser_data, ensure_ascii=False, indent=2) + ";\n",
        encoding="utf-8",
    )
    print(f"Original-position RGB SSIM: {score:.6f}", flush=True)


if __name__ == "__main__":
    main()

