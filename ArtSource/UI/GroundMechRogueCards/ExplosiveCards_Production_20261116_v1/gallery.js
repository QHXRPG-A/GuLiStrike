"use strict";

Object.assign(window.cardCollection, {
  "FireRate": {
    "canvas": [
      1152,
      1728
    ],
    "window": [
      64,
      96,
      1088,
      1632
    ],
    "travel_px": [
      36,
      44
    ],
    "revision": 4,
    "draw_order": [
      6,
      "3-back",
      4,
      5,
      "3-front",
      2,
      1
    ],
    "reference_similarity": {
      "metric": "RGB SSIM",
      "window": [
        64,
        96,
        1088,
        1632
      ],
      "gaussian_sigma": 1.5,
      "gaussian_width": 11,
      "minimum": 0.95,
      "value": 0.9820958375930786,
      "source": "Six saved-equivalent RGBA planes at zero displacement and full opacity"
    },
    "name": "增加攻速",
    "source": "Library/FireRate/Master.png",
    "bundle": "Library/FireRate/FireRate_09.01_SixLayers_20261110.zip",
    "layers": [
      {
        "index": 1,
        "name": "近处尘埃 · 前",
        "depth": 1.0,
        "size": [
          1152,
          1728
        ],
        "file": "Library/FireRate/Layers/01_DustNear.png"
      },
      {
        "index": 2,
        "name": "近处尘埃 · 后",
        "depth": 0.7,
        "size": [
          1152,
          1728
        ],
        "file": "Library/FireRate/Layers/02_DustBehind.png"
      },
      {
        "index": 3,
        "name": "闪电",
        "depth": 0.44,
        "size": [
          1152,
          1728
        ],
        "file": "Library/FireRate/Layers/03_Lightning.png"
      },
      {
        "index": 4,
        "name": "枪管和机甲身体",
        "depth": 0.44,
        "size": [
          1152,
          1728
        ],
        "file": "Library/FireRate/Layers/04_GunAndMech.png"
      },
      {
        "index": 5,
        "name": "枪口火焰与弹道",
        "depth": 0.44,
        "size": [
          1152,
          1728
        ],
        "file": "Library/FireRate/Layers/05_MuzzleFlame.png"
      },
      {
        "index": 6,
        "name": "背景",
        "depth": 0.0,
        "size": [
          1152,
          1728
        ],
        "file": "Library/FireRate/Layers/06_Background.png"
      }
    ],
    "split_layer": {
      "index": 3,
      "mask": "Library/FireRate/Masks/LightningFront.png"
    }
  },
  "EnhancedBullet": {
    "canvas": [
      1152,
      1728
    ],
    "window": [
      64,
      96,
      1088,
      1632
    ],
    "travel_px": [
      40,
      48
    ],
    "revision": 1,
    "draw_order": [
      6,
      5,
      4,
      3,
      2,
      1
    ],
    "reference_similarity": {
      "metric": "RGB SSIM",
      "value": 0.999413788318634,
      "minimum": 0.95,
      "gaussian_width": 11,
      "sigma": 1.5
    },
    "name": "强化子弹",
    "source": "Library/EnhancedBullet/Master.png",
    "bundle": "Library/EnhancedBullet/EnhancedBullet_10.01_SixLayers_20261116.zip",
    "layers": [
      {
        "index": 1,
        "name": "近景碎玻璃",
        "depth": 1.0,
        "size": [
          1152,
          1728
        ],
        "file": "Library/EnhancedBullet/Layers/01_NearShards.png"
      },
      {
        "index": 2,
        "name": "弹体",
        "depth": 0.22,
        "size": [
          1152,
          1728
        ],
        "file": "Library/EnhancedBullet/Layers/02_Projectile.png"
      },
      {
        "index": 3,
        "name": "远景碎玻璃",
        "depth": 0.63,
        "size": [
          1152,
          1728
        ],
        "file": "Library/EnhancedBullet/Layers/03_FarShards.png"
      },
      {
        "index": 4,
        "name": "裂纹与破口光",
        "depth": 0.22,
        "size": [
          1152,
          1728
        ],
        "file": "Library/EnhancedBullet/Layers/04_CrackedPane.png"
      },
      {
        "index": 5,
        "name": "二进制数字雨",
        "depth": 0.08,
        "size": [
          1152,
          1728
        ],
        "file": "Library/EnhancedBullet/Layers/05_BinaryRain.png"
      },
      {
        "index": 6,
        "name": "墨绿背景",
        "depth": 0.0,
        "size": [
          1152,
          1728
        ],
        "file": "Library/EnhancedBullet/Layers/06_Background.png"
      }
    ],
    "background": "#001413"
  }
});

for (const id of ["FireRate", "EnhancedBullet", "ExplosivePackage", "NoSurvivors"]) {
  const link = document.createElement("a");
  link.href = "?card=" + id;
  link.dataset.card = id;
  link.textContent = window.cardCollection[id].name;
  document.querySelector("#card-switch").append(link);
}
