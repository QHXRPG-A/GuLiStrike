"""Export the WM01 card textures through Unreal when their original PNGs are unavailable."""

import json
import os
from pathlib import Path
from tempfile import gettempdir

import unreal


def main():
    root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    tables = root / "data/Json"
    destination = Path(os.environ.get("GULI_GRAPH_TEXTURE_CACHE", Path(gettempdir()) / "GuLiStrike-GraphTextures"))
    destination.mkdir(parents=True, exist_ok=True)

    def rows(name):
        return json.loads((tables / f"DT_GuLiStrikeCardArtwork_{name}.json").read_text(encoding="utf-8-sig"))

    wm_art = {row["Id"] for row in rows("Artworks") if row["Name"].startswith("WM01_")}
    layer_keys = {(row["ArtworkId"], row["TextureParameter"]) for row in rows("Layers")
                  if row["ArtworkId"] in wm_art and row["Opacity"] > 0}
    textures = {row["Texture"] for row in rows("Parameters") if (row["ArtworkId"], row["Key"]) in layer_keys
                or (row["ArtworkId"] in wm_art and row["Key"] == "Background Margin Map")}
    manifest = {}
    for path in sorted(textures):
        texture = unreal.load_asset(path)
        if not isinstance(texture, unreal.Texture2D):
            raise ValueError(f"Expected a Texture2D at {path}")
        name = texture.get_name() + ".tga"
        task = unreal.AssetExportTask()
        for key, value in dict(object=texture, filename=str(destination / name), automated=True,
                               replace_identical=True, prompt=False, exporter=unreal.TextureExporterTGA()).items():
            task.set_editor_property(key, value)
        if not unreal.Exporter.run_asset_export_task(task):
            raise RuntimeError(f"Could not export {path}")
        manifest[path] = name
    (destination / "textures.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    unreal.log(f"Exported {len(manifest)} card textures to {destination}")


if __name__ == "__main__":
    main()
