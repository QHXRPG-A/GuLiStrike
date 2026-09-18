# Nature Pack · Design A v2 prompts

生成方式：内置image_gen。v2根据用户对草和花面数的反馈减少造型复杂度；实际网格预算以LowGeometry_v2.md为准。

## 00 Overview — low geometry revision

```text
Use case: precise-object-edit / stylized-concept.
Edit this GuLiStrike 34-asset concept sheet into DESIGN A v2 after the user's feedback: both grass and flowers look too polygon-heavy. Keep the exact 7x5 grid, exact N01-N34 IDs and names, same botanical families, same green/cool-shadow three-tone anime palette, same grey-lavender rocks and same pale neutral background. Change header "DESIGN A v1" to "DESIGN A v2". Replace the upper right subtitle with "34 VARIANTS / 10x SCALE / LOW GEOMETRY / CONCEPT ONLY".
IMPORTANT VISUAL REDUCTION: this is extremely economical game geometry intended for dense instancing. Show clearly fewer, flatter, larger, readable pieces. Do not make illustrations lush, bushy, polished, beveled or complex in contradiction to these counts. Use broad flat untextured polygon planes; all plant outlines remain completely absent. Reduce plant subdivisions and stem roundness. Do not show topology wireframes.
Grass:
N01 exactly 8 tall slim ribbon blades. N02 exactly 12 tall broad blades. N03 exactly 16 slim blades. Each is a simple flat tapered ribbon, no thickness, no leaf-edge cuts, at most one bend. No hidden extra blades/filler.
N04/N06/N08/N10 exactly 6 blades each: respectively fine upright, wide upright, broad sideways fan, low radial. N05/N07/N09/N11 exactly 12 blades each, their dense siblings but still sparse enough to count. Each short blade has 2 triangles worth of shape, one straight broad taper, no curved segmentation.
N12 exactly 6 short straight straw blades. N13 exactly 12 straw blades. Keep dry gold color.
Flowers:
N23 white daisy has 8 FLAT angular petals, one flat small yellow center, one extremely simple triangular-section straight stem and 2 flat leaves. N24 yellow flower has 6 flat polygon petals. N25 red flower has 10 flat polygon petals. N28 pink flower has 5 flat polygon petals and a flat center, NO thick cup. No domed centers, no rolled petals, no modeled petal thickness, no scalloping, no ribs or vein grooves.
N26 three bare stalks each with a very simple purple tapered spike of 3 angular broad lobes. N27 five stalks each with a simple purple tapered spike, and exactly 6 flat green basal leaves. No intricate lavender florets.
N31 EXACTLY 12 flat little cream-white daisies in a loose low patch, minimal short stems and a few large flat green leaves. N32 same 12-flower composition and shape, change ONLY petals to pale blue. Each tiny groundcover flower is simpler than the large N23 standalone flower.
Also simplify ferns N17/N18 into large flat angular paired leaflets, no rounded multi-segment leaflets, not a fluffy bush. N17 five fronds / N18 nine fronds.
N14/N15 narrow stem plants simple flat lanceolate leaves. N16 exactly three broad leaves. N19 exactly seven large broad simple leaves. N20 exactly seven flat pointed rosette leaves. No tiny edge cuts or serrations.
N21 five straw stalks each has a single simple golden seed-head silhouette with broad segments, not dozens of modeled kernels. N22 three green stalks with simple flat alternating seed leaves.
N29 at most 20 broad flat ground leaves. N30 18 simple three-lobed flat clover leaves, no thickness.
Keep N33 upright rock and N34 low rock large simple planar faces, light contour and few principal creases, no new cracks.
The intended LOD0 modeling budgets are 12-48 triangles per grass clump, 24-40 per single flower, no more than 256 for the entire white/blue flower patch. Do NOT print budget annotations on the art; these limits guide the simplicity of the image. No extra decorative foliage to fill empty space. Embrace the clean open low-poly silhouettes.
```
