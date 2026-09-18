# Nature Pack · Design A v2 prompts

生成方式：内置image_gen。以下完整提示词用于低几何候选；实际面数需在建模后读回。

## 00 Overview

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

## 01_TallGrass_Dry

```text
Use case: stylized-concept. Production orthographic reference sheet derived from the provided GuLiStrike NATURE PACK Design A v2 overview.
Input image role: approved production INTENT and current CANDIDATE silhouettes/palette; this is not a user-approved final model. Develop ONLY the specified assets from that image into clear modeling reference views, preserve the simplified low-geometry style. This output is 2D concept art for review, not a Blender or Unreal screenshot.
Canvas: large high-resolution portrait sheet, pale warm grey background, understated charcoal sans-serif labels. Header exactly "GuLiStrike | TALL GRASS + DRY TUFTS | DESIGN A v2". Small subtitle "10x SCALE / LOW GEOMETRY / CONCEPT ONLY".
Layout: exactly 5 rows by 3 view columns. Each ROW is one SINGLE asset, same size and same design across all views, correctly rotated rather than different variations. Row label to left contains asset ID, simple English key, height in meters and LOD0 triangle-budget target. A thin common ground baseline across the three elevation views. Do not crop tips or roots. Separate rows generously.
Column headings exactly "FRONT", "SIDE", "BACK". Elevation views orthographic, not perspective. Front looking -Y, side looking -X, rear looking +Y. For carpets the front/side/back show their true thin low silhouette; the TOP column reveals flower/leaf layout. For rocks a 3/4 column reveals the mass.
Render language: extremely economical flat broad polygon surfaces and exactly three clean illumination values, bright yellow-green highlights, medium green, cool readable shadows; matte surfaces, no textured detail, no rounded bevels, no microfacets. PLANTS HAVE NO CONTOUR INK, NO INTERNAL LINEART, NO BLACK VEINS. Folded color planes can express a leaf rib. ROCKS gray-lavender with very fine blue-grey contour and at most 2-3 intentional principal creases, NOT triangulation wireframe. Keep restrained cream/gold/red/purple/pink/blue flower accents from reference.
Low geometry is essential. Grass blades are single thin polygon ribbons without thickness, each only 2-4 triangles worth of bends. Short grass has exactly 6 or 12 blades; do not add hidden filler. Flowers have flat angular petal polygons, a flat center, triangular-section simple stem, and flat leaves; no thick cups, rolled petals, high-segment cylinders, spheres or tiny florets. Fern leaflets are flat, angular, with few coarse segments. Wheat is 5 stems with simple angular whole seed-head shapes rather than many separate kernels.
Specific rows in this EXACT order:
N01 "GrassTallThin" | total target size X width 8m, Y depth 8m, Z height 7m | label "H 7 m / LOD0 <= 24 tris" | shape instruction: 8片纤细直立尖叶，每片最多3个三角面。 EXACTLY 8 main blades; same blades in every view.
N02 "GrassTallWide" | total target size X width 9m, Y depth 9m, Z height 10m | label "H 10 m / LOD0 <= 48 tris" | shape instruction: 12片较宽折叶，每片最多4个三角面。 EXACTLY 12 main blades; same blades in every view.
N03 "GrassTallDense" | total target size X width 10m, Y depth 10m, Z height 8m | label "H 8 m / LOD0 <= 48 tris" | shape instruction: 16片简洁细叶，每片最多3个三角面；不增加隐藏填充叶。 EXACTLY 16 main blades; same blades in every view.
N12 "GrassDrySmall" | total target size X width 5m, Y depth 4m, Z height 3m | label "H 3 m / LOD0 <= 12 tris" | shape instruction: 短直稻草金叶片，斜向齐整顶缘。 主叶6片，每片最多2个三角面。 EXACTLY 6 main blades; same blades in every view.
N13 "GrassDryWide" | total target size X width 7m, Y depth 5m, Z height 4m | label "H 4 m / LOD0 <= 24 tris" | shape instruction: 宽厚干草簇，浅金色，轮廓略不齐。 主叶12片，每片最多2个三角面。 EXACTLY 12 main blades; same blades in every view.

Topology budgets are design targets, not claims of measured meshes. Use them to keep SHAPES very simple, don't draw wireframes. View consistency matters more than adding detail. No additional assets, platforms, pots, ground dirt, humans, decorative scene, watermark or logos.
```

## 02_ShortGrass_Upright

```text
Use case: stylized-concept. Production orthographic reference sheet derived from the provided GuLiStrike NATURE PACK Design A v2 overview.
Input image role: approved production INTENT and current CANDIDATE silhouettes/palette; this is not a user-approved final model. Develop ONLY the specified assets from that image into clear modeling reference views, preserve the simplified low-geometry style. This output is 2D concept art for review, not a Blender or Unreal screenshot.
Canvas: large high-resolution landscape sheet, pale warm grey background, understated charcoal sans-serif labels. Header exactly "GuLiStrike | SHORT GRASS / UPRIGHT | DESIGN A v2". Small subtitle "10x SCALE / LOW GEOMETRY / CONCEPT ONLY".
Layout: exactly 4 rows by 3 view columns. Each ROW is one SINGLE asset, same size and same design across all views, correctly rotated rather than different variations. Row label to left contains asset ID, simple English key, height in meters and LOD0 triangle-budget target. A thin common ground baseline across the three elevation views. Do not crop tips or roots. Separate rows generously.
Column headings exactly "FRONT", "SIDE", "BACK". Elevation views orthographic, not perspective. Front looking -Y, side looking -X, rear looking +Y. For carpets the front/side/back show their true thin low silhouette; the TOP column reveals flower/leaf layout. For rocks a 3/4 column reveals the mass.
Render language: extremely economical flat broad polygon surfaces and exactly three clean illumination values, bright yellow-green highlights, medium green, cool readable shadows; matte surfaces, no textured detail, no rounded bevels, no microfacets. PLANTS HAVE NO CONTOUR INK, NO INTERNAL LINEART, NO BLACK VEINS. Folded color planes can express a leaf rib. ROCKS gray-lavender with very fine blue-grey contour and at most 2-3 intentional principal creases, NOT triangulation wireframe. Keep restrained cream/gold/red/purple/pink/blue flower accents from reference.
Low geometry is essential. Grass blades are single thin polygon ribbons without thickness, each only 2-4 triangles worth of bends. Short grass has exactly 6 or 12 blades; do not add hidden filler. Flowers have flat angular petal polygons, a flat center, triangular-section simple stem, and flat leaves; no thick cups, rolled petals, high-segment cylinders, spheres or tiny florets. Fern leaflets are flat, angular, with few coarse segments. Wheat is 5 stems with simple angular whole seed-head shapes rather than many separate kernels.
Specific rows in this EXACT order:
N04 "GrassNeedleSparse" | total target size X width 7m, Y depth 6m, Z height 2.5m | label "H 2.5 m / LOD0 <= 12 tris" | shape instruction: 细直叶；稀疏约7片主叶。 主叶6片，每片最多2个三角面。 EXACTLY 6 main blades; same blades in every view.
N05 "GrassNeedleDense" | total target size X width 8m, Y depth 7m, Z height 3m | label "H 3 m / LOD0 <= 24 tris" | shape instruction: 与N04同族，叶片数量与交叠层次增加。 主叶12片，每片最多2个三角面。 EXACTLY 12 main blades; same blades in every view.
N06 "GrassBroadSparse" | total target size X width 8m, Y depth 7m, Z height 3m | label "H 3 m / LOD0 <= 12 tris" | shape instruction: 短宽折叶，叶间留空。 主叶6片，每片最多2个三角面。 EXACTLY 6 main blades; same blades in every view.
N07 "GrassBroadDense" | total target size X width 10m, Y depth 8m, Z height 3.5m | label "H 3.5 m / LOD0 <= 24 tris" | shape instruction: 同族短宽折叶，密簇圆拱。 主叶12片，每片最多2个三角面。 EXACTLY 12 main blades; same blades in every view.

Topology budgets are design targets, not claims of measured meshes. Use them to keep SHAPES very simple, don't draw wireframes. View consistency matters more than adding detail. No additional assets, platforms, pots, ground dirt, humans, decorative scene, watermark or logos.
```

## 03_ShortGrass_FanRadial

```text
Use case: stylized-concept. Production orthographic reference sheet derived from the provided GuLiStrike NATURE PACK Design A v2 overview.
Input image role: approved production INTENT and current CANDIDATE silhouettes/palette; this is not a user-approved final model. Develop ONLY the specified assets from that image into clear modeling reference views, preserve the simplified low-geometry style. This output is 2D concept art for review, not a Blender or Unreal screenshot.
Canvas: large high-resolution landscape sheet, pale warm grey background, understated charcoal sans-serif labels. Header exactly "GuLiStrike | SHORT GRASS / FAN + RADIAL | DESIGN A v2". Small subtitle "10x SCALE / LOW GEOMETRY / CONCEPT ONLY".
Layout: exactly 4 rows by 3 view columns. Each ROW is one SINGLE asset, same size and same design across all views, correctly rotated rather than different variations. Row label to left contains asset ID, simple English key, height in meters and LOD0 triangle-budget target. A thin common ground baseline across the three elevation views. Do not crop tips or roots. Separate rows generously.
Column headings exactly "FRONT", "SIDE", "BACK". Elevation views orthographic, not perspective. Front looking -Y, side looking -X, rear looking +Y. For carpets the front/side/back show their true thin low silhouette; the TOP column reveals flower/leaf layout. For rocks a 3/4 column reveals the mass.
Render language: extremely economical flat broad polygon surfaces and exactly three clean illumination values, bright yellow-green highlights, medium green, cool readable shadows; matte surfaces, no textured detail, no rounded bevels, no microfacets. PLANTS HAVE NO CONTOUR INK, NO INTERNAL LINEART, NO BLACK VEINS. Folded color planes can express a leaf rib. ROCKS gray-lavender with very fine blue-grey contour and at most 2-3 intentional principal creases, NOT triangulation wireframe. Keep restrained cream/gold/red/purple/pink/blue flower accents from reference.
Low geometry is essential. Grass blades are single thin polygon ribbons without thickness, each only 2-4 triangles worth of bends. Short grass has exactly 6 or 12 blades; do not add hidden filler. Flowers have flat angular petal polygons, a flat center, triangular-section simple stem, and flat leaves; no thick cups, rolled petals, high-segment cylinders, spheres or tiny florets. Fern leaflets are flat, angular, with few coarse segments. Wheat is 5 stems with simple angular whole seed-head shapes rather than many separate kernels.
Specific rows in this EXACT order:
N08 "GrassFanSparse" | total target size X width 10m, Y depth 6m, Z height 3m | label "H 3 m / LOD0 <= 12 tris" | shape instruction: 沿横向展开，纵深较浅。 主叶6片，每片最多2个三角面。 EXACTLY 6 main blades; same blades in every view.
N09 "GrassFanDense" | total target size X width 12m, Y depth 7m, Z height 3.5m | label "H 3.5 m / LOD0 <= 24 tris" | shape instruction: 同族扇形叶，前后两层错位。 主叶12片，每片最多2个三角面。 EXACTLY 12 main blades; same blades in every view.
N10 "GrassRadialSparse" | total target size X width 8m, Y depth 8m, Z height 2.5m | label "H 2.5 m / LOD0 <= 12 tris" | shape instruction: 低矮叶向四周放射，中部通透。 主叶6片，每片最多2个三角面。 EXACTLY 6 main blades; same blades in every view.
N11 "GrassRadialDense" | total target size X width 10m, Y depth 10m, Z height 3m | label "H 3 m / LOD0 <= 24 tris" | shape instruction: 同族圆形放射簇，中心更丰满。 主叶12片，每片最多2个三角面。 EXACTLY 12 main blades; same blades in every view.

Topology budgets are design targets, not claims of measured meshes. Use them to keep SHAPES very simple, don't draw wireframes. View consistency matters more than adding detail. No additional assets, platforms, pots, ground dirt, humans, decorative scene, watermark or logos.
```

## 04_Leaf_Fern

```text
Use case: stylized-concept. Production orthographic reference sheet derived from the provided GuLiStrike NATURE PACK Design A v2 overview.
Input image role: approved production INTENT and current CANDIDATE silhouettes/palette; this is not a user-approved final model. Develop ONLY the specified assets from that image into clear modeling reference views, preserve the simplified low-geometry style. This output is 2D concept art for review, not a Blender or Unreal screenshot.
Canvas: large high-resolution portrait sheet, pale warm grey background, understated charcoal sans-serif labels. Header exactly "GuLiStrike | LEAF PLANTS + FERNS | DESIGN A v2". Small subtitle "10x SCALE / LOW GEOMETRY / CONCEPT ONLY".
Layout: exactly 7 rows by 3 view columns. Each ROW is one SINGLE asset, same size and same design across all views, correctly rotated rather than different variations. Row label to left contains asset ID, simple English key, height in meters and LOD0 triangle-budget target. A thin common ground baseline across the three elevation views. Do not crop tips or roots. Separate rows generously.
Column headings exactly "FRONT", "SIDE", "BACK". Elevation views orthographic, not perspective. Front looking -Y, side looking -X, rear looking +Y. For carpets the front/side/back show their true thin low silhouette; the TOP column reveals flower/leaf layout. For rocks a 3/4 column reveals the mass.
Render language: extremely economical flat broad polygon surfaces and exactly three clean illumination values, bright yellow-green highlights, medium green, cool readable shadows; matte surfaces, no textured detail, no rounded bevels, no microfacets. PLANTS HAVE NO CONTOUR INK, NO INTERNAL LINEART, NO BLACK VEINS. Folded color planes can express a leaf rib. ROCKS gray-lavender with very fine blue-grey contour and at most 2-3 intentional principal creases, NOT triangulation wireframe. Keep restrained cream/gold/red/purple/pink/blue flower accents from reference.
Low geometry is essential. Grass blades are single thin polygon ribbons without thickness, each only 2-4 triangles worth of bends. Short grass has exactly 6 or 12 blades; do not add hidden filler. Flowers have flat angular petal polygons, a flat center, triangular-section simple stem, and flat leaves; no thick cups, rolled petals, high-segment cylinders, spheres or tiny florets. Fern leaflets are flat, angular, with few coarse segments. Wheat is 5 stems with simple angular whole seed-head shapes rather than many separate kernels.
Specific rows in this EXACT order:
N14 "LeafStemSingle" | total target size X width 5m, Y depth 3m, Z height 12m | label "H 12 m / LOD0 <= 120 tris" | shape instruction: 单直茎与交替披针形叶，保留疏朗空隙。
N15 "LeafStemBranched" | total target size X width 8m, Y depth 6m, Z height 15m | label "H 15 m / LOD0 <= 240 tris" | shape instruction: 三枝分叉，交替细叶；高低有主次。
N16 "LeafClumpThree" | total target size X width 8m, Y depth 6m, Z height 8m | label "H 8 m / LOD0 <= 36 tris" | shape instruction: 三片上展大折叶；一高两低。
N17 "FernSmall" | total target size X width 8m, Y depth 7m, Z height 4m | label "H 4 m / LOD0 <= 120 tris" | shape instruction: 五片开放羽状叶，成对简化小叶。
N18 "FernLarge" | total target size X width 14m, Y depth 12m, Z height 7m | label "H 7 m / LOD0 <= 240 tris" | shape instruction: 九片羽状叶，中高外低，叶片宽窄有序。
N19 "LeafClumpBroad" | total target size X width 18m, Y depth 14m, Z height 10m | label "H 10 m / LOD0 <= 84 tris" | shape instruction: 七片宽大弯折叶，沿地面向外伸展。
N20 "RosetteSmall" | total target size X width 6m, Y depth 6m, Z height 4m | label "H 4 m / LOD0 <= 42 tris" | shape instruction: 七片尖阔叶围绕中心，低矮无高茎。

Topology budgets are design targets, not claims of measured meshes. Use them to keep SHAPES very simple, don't draw wireframes. View consistency matters more than adding detail. No additional assets, platforms, pots, ground dirt, humans, decorative scene, watermark or logos.
```

## 05_Flowers

```text
Use case: stylized-concept. Production orthographic reference sheet derived from the provided GuLiStrike NATURE PACK Design A v2 overview.
Input image role: approved production INTENT and current CANDIDATE silhouettes/palette; this is not a user-approved final model. Develop ONLY the specified assets from that image into clear modeling reference views, preserve the simplified low-geometry style. This output is 2D concept art for review, not a Blender or Unreal screenshot.
Canvas: large high-resolution portrait sheet, pale warm grey background, understated charcoal sans-serif labels. Header exactly "GuLiStrike | FLOWERS / FLAT LOW GEOMETRY | DESIGN A v2". Small subtitle "10x SCALE / LOW GEOMETRY / CONCEPT ONLY".
Layout: exactly 6 rows by 4 view columns. Each ROW is one SINGLE asset, same size and same design across all views, correctly rotated rather than different variations. Row label to left contains asset ID, simple English key, height in meters and LOD0 triangle-budget target. A thin common ground baseline across the three elevation views. Do not crop tips or roots. Separate rows generously.
Column headings exactly "FRONT", "SIDE", "BACK", "TOP". Elevation views orthographic, not perspective. Front looking -Y, side looking -X, rear looking +Y. For carpets the front/side/back show their true thin low silhouette; the TOP column reveals flower/leaf layout. For rocks a 3/4 column reveals the mass.
Render language: extremely economical flat broad polygon surfaces and exactly three clean illumination values, bright yellow-green highlights, medium green, cool readable shadows; matte surfaces, no textured detail, no rounded bevels, no microfacets. PLANTS HAVE NO CONTOUR INK, NO INTERNAL LINEART, NO BLACK VEINS. Folded color planes can express a leaf rib. ROCKS gray-lavender with very fine blue-grey contour and at most 2-3 intentional principal creases, NOT triangulation wireframe. Keep restrained cream/gold/red/purple/pink/blue flower accents from reference.
Low geometry is essential. Grass blades are single thin polygon ribbons without thickness, each only 2-4 triangles worth of bends. Short grass has exactly 6 or 12 blades; do not add hidden filler. Flowers have flat angular petal polygons, a flat center, triangular-section simple stem, and flat leaves; no thick cups, rolled petals, high-segment cylinders, spheres or tiny florets. Fern leaflets are flat, angular, with few coarse segments. Wheat is 5 stems with simple angular whole seed-head shapes rather than many separate kernels.
Specific rows in this EXACT order:
N23 "FlowerWhiteDaisy" | total target size X width 3m, Y depth 2m, Z height 6m | label "H 6 m / LOD0 <= 32 tris" | shape instruction: 8片平面白花瓣、平花心、三棱细茎和2片平叶，禁止花瓣厚度。
N24 "FlowerYellow" | total target size X width 3m, Y depth 2m, Z height 5m | label "H 5 m / LOD0 <= 28 tris" | shape instruction: 6片平面金花瓣、平花心、三棱细茎和2片平叶。
N25 "FlowerRed" | total target size X width 3m, Y depth 2m, Z height 7m | label "H 7 m / LOD0 <= 40 tris" | shape instruction: 10片平面红花瓣、平花心、三棱细茎和2片平叶。
N26 "FlowerPurpleBare" | total target size X width 4m, Y depth 3m, Z height 9m | label "H 9 m / LOD0 <= 96 tris" | shape instruction: 3支裸茎紫花穗，每头3个宽大几何花瓣组，不细分小花。
N27 "FlowerPurpleLeafy" | total target size X width 6m, Y depth 5m, Z height 8m | label "H 8 m / LOD0 <= 128 tris" | shape instruction: 5支简化紫花穗和6片平面基叶。
N28 "FlowerPink" | total target size X width 3m, Y depth 2m, Z height 5.5m | label "H 5.5 m / LOD0 <= 28 tris" | shape instruction: 5片平面粉花瓣、平花心、三棱细茎和2片平叶，不做厚花杯。

Topology budgets are design targets, not claims of measured meshes. Use them to keep SHAPES very simple, don't draw wireframes. View consistency matters more than adding detail. No additional assets, platforms, pots, ground dirt, humans, decorative scene, watermark or logos.
```

## 06_Seed_Groundcover

```text
Use case: stylized-concept. Production orthographic reference sheet derived from the provided GuLiStrike NATURE PACK Design A v2 overview.
Input image role: approved production INTENT and current CANDIDATE silhouettes/palette; this is not a user-approved final model. Develop ONLY the specified assets from that image into clear modeling reference views, preserve the simplified low-geometry style. This output is 2D concept art for review, not a Blender or Unreal screenshot.
Canvas: large high-resolution portrait sheet, pale warm grey background, understated charcoal sans-serif labels. Header exactly "GuLiStrike | SEED HEADS + GROUNDCOVER | DESIGN A v2". Small subtitle "10x SCALE / LOW GEOMETRY / CONCEPT ONLY".
Layout: exactly 6 rows by 4 view columns. Each ROW is one SINGLE asset, same size and same design across all views, correctly rotated rather than different variations. Row label to left contains asset ID, simple English key, height in meters and LOD0 triangle-budget target. A thin common ground baseline across the three elevation views. Do not crop tips or roots. Separate rows generously.
Column headings exactly "FRONT", "SIDE", "BACK", "TOP". Elevation views orthographic, not perspective. Front looking -Y, side looking -X, rear looking +Y. For carpets the front/side/back show their true thin low silhouette; the TOP column reveals flower/leaf layout. For rocks a 3/4 column reveals the mass.
Render language: extremely economical flat broad polygon surfaces and exactly three clean illumination values, bright yellow-green highlights, medium green, cool readable shadows; matte surfaces, no textured detail, no rounded bevels, no microfacets. PLANTS HAVE NO CONTOUR INK, NO INTERNAL LINEART, NO BLACK VEINS. Folded color planes can express a leaf rib. ROCKS gray-lavender with very fine blue-grey contour and at most 2-3 intentional principal creases, NOT triangulation wireframe. Keep restrained cream/gold/red/purple/pink/blue flower accents from reference.
Low geometry is essential. Grass blades are single thin polygon ribbons without thickness, each only 2-4 triangles worth of bends. Short grass has exactly 6 or 12 blades; do not add hidden filler. Flowers have flat angular petal polygons, a flat center, triangular-section simple stem, and flat leaves; no thick cups, rolled petals, high-segment cylinders, spheres or tiny florets. Fern leaflets are flat, angular, with few coarse segments. Wheat is 5 stems with simple angular whole seed-head shapes rather than many separate kernels.
Specific rows in this EXACT order:
N21 "WheatGolden" | total target size X width 4m, Y depth 3m, Z height 10m | label "H 10 m / LOD0 <= 96 tris" | shape instruction: 五根细秆配分节穗头，穗头方向略错开。
N22 "SeedSprayGreen" | total target size X width 4m, Y depth 3m, Z height 10m | label "H 10 m / LOD0 <= 72 tris" | shape instruction: 直立绿秆，交替细长穗叶，通透轮廓。
N29 "GroundcoverLeaf" | total target size X width 20m, Y depth 20m, Z height 2m | label "H 2 m / LOD0 <= 128 tris" | shape instruction: 最多20片宽大平叶组成低地被，保持外缘缺口。
N30 "GroundcoverClover" | total target size X width 18m, Y depth 18m, Z height 1.5m | label "H 1.5 m / LOD0 <= 144 tris" | shape instruction: 18枚简化三瓣平面三叶草叶片，无叶片厚度。
N31 "GroundcoverFlowersWhite" | total target size X width 22m, Y depth 18m, Z height 2m | label "H 2 m / LOD0 <= 256 tris" | shape instruction: 12朵简化平面白花与少量基叶，整块不超过256三角面。
N32 "GroundcoverFlowersBlue" | total target size X width 22m, Y depth 18m, Z height 2m | label "H 2 m / LOD0 <= 256 tris" | shape instruction: 完全复用N31的12朵花、位置、网格和UV，仅花瓣浅蓝。
CRITICAL N31 and N32 are the identical mesh: exactly 12 small flat flowers, identical flower centers, leaf layout, stem positions, silhouette and rotations in EVERY corresponding column. N32 is a strict recoloring of N31 cream petals to pale blue, NOT a different arrangement. Make their outlines and shapes congruent.
Topology budgets are design targets, not claims of measured meshes. Use them to keep SHAPES very simple, don't draw wireframes. View consistency matters more than adding detail. No additional assets, platforms, pots, ground dirt, humans, decorative scene, watermark or logos.
```

## 07_Rocks

```text
Use case: stylized-concept. Production orthographic reference sheet derived from the provided GuLiStrike NATURE PACK Design A v2 overview.
Input image role: approved production INTENT and current CANDIDATE silhouettes/palette; this is not a user-approved final model. Develop ONLY the specified assets from that image into clear modeling reference views, preserve the simplified low-geometry style. This output is 2D concept art for review, not a Blender or Unreal screenshot.
Canvas: large high-resolution landscape sheet, pale warm grey background, understated charcoal sans-serif labels. Header exactly "GuLiStrike | ROCKS / PRINCIPAL PLANES | DESIGN A v2". Small subtitle "10x SCALE / LOW GEOMETRY / CONCEPT ONLY".
Layout: exactly 2 rows by 4 view columns. Each ROW is one SINGLE asset, same size and same design across all views, correctly rotated rather than different variations. Row label to left contains asset ID, simple English key, height in meters and LOD0 triangle-budget target. A thin common ground baseline across the three elevation views. Do not crop tips or roots. Separate rows generously.
Column headings exactly "FRONT", "SIDE", "BACK", "3/4". Elevation views orthographic, not perspective. Front looking -Y, side looking -X, rear looking +Y. For carpets the front/side/back show their true thin low silhouette; the TOP column reveals flower/leaf layout. For rocks a 3/4 column reveals the mass.
Render language: extremely economical flat broad polygon surfaces and exactly three clean illumination values, bright yellow-green highlights, medium green, cool readable shadows; matte surfaces, no textured detail, no rounded bevels, no microfacets. PLANTS HAVE NO CONTOUR INK, NO INTERNAL LINEART, NO BLACK VEINS. Folded color planes can express a leaf rib. ROCKS gray-lavender with very fine blue-grey contour and at most 2-3 intentional principal creases, NOT triangulation wireframe. Keep restrained cream/gold/red/purple/pink/blue flower accents from reference.
Low geometry is essential. Grass blades are single thin polygon ribbons without thickness, each only 2-4 triangles worth of bends. Short grass has exactly 6 or 12 blades; do not add hidden filler. Flowers have flat angular petal polygons, a flat center, triangular-section simple stem, and flat leaves; no thick cups, rolled petals, high-segment cylinders, spheres or tiny florets. Fern leaflets are flat, angular, with few coarse segments. Wheat is 5 stems with simple angular whole seed-head shapes rather than many separate kernels.
Specific rows in this EXACT order:
N33 "RockUpright" | total target size X width 20m, Y depth 18m, Z height 30m | label "H 30 m / LOD0 <= 160 tris" | shape instruction: 不对称高立轮廓，少量大切面与2–3条主折线。
N34 "RockLow" | total target size X width 12m, Y depth 10m, Z height 6m | label "H 6 m / LOD0 <= 96 tris" | shape instruction: 低矮不对称宽岩，大切面，轻外轮廓。

Topology budgets are design targets, not claims of measured meshes. Use them to keep SHAPES very simple, don't draw wireframes. View consistency matters more than adding detail. No additional assets, platforms, pots, ground dirt, humans, decorative scene, watermark or logos.
```

