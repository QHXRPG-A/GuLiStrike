"""Build the runtime alpha atlas from the four HUDuiPRO sources used by the approved HUD."""
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter


ROOT = Path(__file__).resolve().parents[3]
SRC = ROOT / "ArtSource/UI/HUDuiPRO/Exports/Textures/Others"
OUT = ROOT / "ArtSource/UI/GroundMech/AIEngineHUD_Reference_20261121/T_AIEngine_Optics.png"


def light(path, exponent=.76, gain=1.12):
    r, g, b, a = Image.open(SRC / path).convert("RGBA").split()
    value = ImageChops.multiply(ImageChops.lighter(ImageChops.lighter(r, g), b), a)
    return value.point([min(255, round(255 * (i / 255) ** exponent * gain)) for i in range(256)])


def main():
    alpha = Image.new("L", (1024, 1024))
    circles = "Basic_Elements/Circles/"
    alpha.paste(light(circles + "Circle_Elements/T_HUD_Circle_Elements_03.png"), (6, 6))
    alpha.paste(light(circles + "Circle_Elements_Tech/T_HUD_Circle_Elements_Tech_15.png", .60), (518, 6))
    hexagon = light("Basic_Elements/Polygons/T_HUD_Polygon_05.png")
    hexagon = ImageChops.lighter(hexagon, hexagon.filter(ImageFilter.MaxFilter(3)).point(lambda x: round(.72*x)))
    inner_boundary = Image.new("L", (500, 500))
    ImageDraw.Draw(inner_boundary).ellipse((94, 94, 406, 406), fill=255)
    alpha.paste(ImageChops.subtract(hexagon, inner_boundary), (6, 518))
    ImageDraw.Draw(inner_boundary).ellipse((187, 187, 313, 313), fill=0)
    inner = ImageChops.multiply(hexagon, inner_boundary).crop((92, 92, 408, 408))
    alpha.paste(inner, (518, 518))
    rail = light("HUD_SPECIAL_GADGETS/Gadgets_HUD_UI/T_HUD_Special_Gadget_04.png", .50, 1.38)
    alpha.paste(rail.crop((0, 345, 263, 455)), (518, 850))
    ImageDraw.Draw(alpha).rectangle((1014, 1008, 1022, 1016), fill=255)
    atlas = Image.new("RGBA", alpha.size, "white")
    atlas.putalpha(alpha)
    atlas.save(OUT, optimize=True)


if __name__ == "__main__":
    main()
