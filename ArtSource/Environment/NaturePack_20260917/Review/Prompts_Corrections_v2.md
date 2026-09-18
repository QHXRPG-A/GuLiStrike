# Nature Pack · Design A v2 targeted corrections

以下为内置image_gen的实际修订提示词。花卉修订针对平面花冠、平花心和视图一致性；岩石修订针对去掉亮白边线、改为少量深蓝灰线。

## Flowers

```text
Use case: precise-object-edit. Correct ONLY the construction and projection of the four standalone flowers N23, N24, N25, N28 on this reference sheet. Keep all labels, dimensions, budgets, row order, column layout, overall color scheme, N26 and N27 purple-spike rows unchanged. Keep title DESIGN A v2.
The user explicitly said flowers look too polygon-heavy. These four flowers must visibly look like flat cut-paper quadrilateral polygons with NO thickness. Each petal is a single flat FOUR-CORNERED pointed diamond or kite (exactly 2 triangles worth of geometry), NO rounded multi-segment edges, NO smooth oval petals, NO bevel, NO rolled rim. Their petals radiate around a small perfectly FLAT HEXAGON center: one unthickened planar six-cornered yellow/orange disk, no circular ring, no rim, no sphere, no raised button, no domed surface, no underside green calyx geometry. Green backside may only be a flat material color on the SAME flat center polygon. Stems must be visibly simple triangular prisms without extra rings; two leaves are each one unthickened pointed flat quadrilateral. No drawn outlines.
Exact petal counts per single bloom and per corresponding view: N23 WHITE = 8 petals; N24 YELLOW = 6 petals; N25 RED = 10 petals; N28 PINK = 5 petals.
CRITICAL SAME OBJECT IN EVERY VIEW: for these four flowers, orient the whole flat flower-head plane tilted 45 degrees from horizontal toward the FRONT camera, so FRONT and TOP are both correctly foreshortened ellipses (apparent height approx 0.707 x apparent width), NOT two contradictory full circles. SIDE shows an almost zero-thickness diagonal line of petals at 45 degrees and thin stem behind; no protruding button. BACK sees the underside of the same tilted flat flower with matching petal positions. Keep same head tilt in all four columns, do not change orientation just to show its face. FRONT/SIDE/BACK remain orthographic elevations. TOP really looks straight downward, not another front view.
Keep original three-tone green stems/leaves and warm flower colors, clean pale grey background, no new text or decorative elements. The geometry must look physically simple enough for the written budgets: white flower32, yellow28, red40, pink28 triangles total including stem/leaves. Do not add topology wireframes.
```

## Rocks

```text
Use case: precise-object-edit. Make one precise rendering correction to this GuLiStrike rock design sheet; keep ALL text, all dimensions, labels, layout, all rock silhouettes, topology shapes, positions and camera views exactly unchanged.
Remove the bright WHITE edge tracing currently drawn on rock faces. These are matte grey-lavender cel-shaded rocks matching the project's fine dark blue-grey ink language, not glossy crystal or shiny chamfered stone.
Use a very thin muted DARK blue-grey outer contour on the silhouettes, and only TWO or THREE deliberately selected principal structural crease strokes on each rock view. Keep remaining face boundaries readable ONLY by broad color values, without drawn lines. Do not trace every facet, do not trace triangulation, no cracks, no wireframe, no white highlights along edges. Three broad cel-shading values, no texture, same base palette as input. Ink subtle and subordinate to silhouette.
This is a concept drawing, not a measured mesh; all existing labels stay exactly unchanged.
```

