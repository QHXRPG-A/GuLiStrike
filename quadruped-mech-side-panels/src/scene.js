import * as THREE from 'three'
import { enableToneMapping } from './environment/tonemap.js'
import { excludeFromFraming, framingReport } from './kit/edit3d.js'

function createStudioEnvironment() {
  const width = 64
  const height = 32
  const data = new Uint8Array(width * height * 4)
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const vertical = y / (height - 1)
      const strip = Math.exp(-Math.pow((x / width - 0.24) * 12, 2))
        + 0.65 * Math.exp(-Math.pow((x / width - 0.72) * 16, 2))
      const value = Math.min(235, 58 + 82 * (1 - vertical) + 72 * strip)
      const offset = (y * width + x) * 4
      data[offset] = value * 0.94
      data[offset + 1] = value * 0.98
      data[offset + 2] = value
      data[offset + 3] = 255
    }
  }
  const texture = new THREE.DataTexture(data, width, height, THREE.RGBAFormat)
  texture.name = '摄影棚 · 柔和反射条'
  texture.mapping = THREE.EquirectangularReflectionMapping
  texture.colorSpace = THREE.SRGBColorSpace
  texture.magFilter = THREE.LinearFilter
  texture.minFilter = THREE.LinearFilter
  texture.generateMipmaps = false
  texture.needsUpdate = true
  return texture
}

export async function createScene({ asset }) {
  enableToneMapping({ curve: 'aces' })
  const scene = new THREE.Scene()
  scene.background = new THREE.Color(0x252a30)
  const environment = createStudioEnvironment()
  scene.environment = environment
  scene.environmentIntensity = 0.8

  const model = await asset.loadModel('/models/quadruped-mech-side-panels.glb')
  model.object.traverse((object) => {
    if (!object.isMesh) return
    object.castShadow = true
    object.receiveShadow = true
  })
  scene.add(model.object)

  const bounds = new THREE.Box3().setFromObject(model.object)
  const center = bounds.getCenter(new THREE.Vector3())
  const size = bounds.getSize(new THREE.Vector3())
  const camera = new THREE.PerspectiveCamera(42, 1, 0.03, 150)
  camera.name = '主相机 · 前侧三分之四视角'
  camera.position.set(
    center.x + size.x * 1.18,
    center.y + size.y * 0.72,
    center.z + size.z * 1.35,
  )
  camera.lookAt(center.x, center.y + size.y * 0.08, center.z)
  scene.add(camera)

  const ambient = new THREE.HemisphereLight(0xdde9f2, 0x25211f, 1.35)
  ambient.name = '环境光 · 冷暖平衡'
  const key = new THREE.DirectionalLight(0xfff1df, 3.1)
  key.name = '主光 · 左前柔光'
  key.position.set(center.x - 4.5, center.y + 7, center.z + 5)
  key.target.position.copy(center)
  key.castShadow = true
  key.shadow.mapSize.set(2048, 2048)
  key.shadow.camera.left = -5
  key.shadow.camera.right = 5
  key.shadow.camera.top = 5
  key.shadow.camera.bottom = -5
  key.shadow.camera.near = 0.5
  key.shadow.camera.far = 25
  key.shadow.bias = -0.00025
  const rim = new THREE.DirectionalLight(0xb9d8ef, 1.45)
  rim.name = '轮廓光 · 右后冷光'
  rim.position.set(center.x + 5, center.y + 4, center.z - 4)
  rim.target.position.copy(center)
  scene.add(ambient, key, key.target, rim, rim.target)

  const groundMaterial = new THREE.MeshStandardMaterial({
    color: 0x353b40,
    roughness: 0.96,
    metalness: 0.02,
  })
  groundMaterial.name = '展示台 · 深灰哑光'
  const ground = new THREE.Mesh(new THREE.PlaneGeometry(30, 30), groundMaterial)
  ground.name = '展示台'
  ground.rotation.x = -Math.PI / 2
  ground.position.set(center.x, bounds.min.y - 0.012, center.z)
  ground.receiveShadow = true
  scene.add(ground)
  excludeFromFraming(ground)

  asset.register('mech', model.object, { name: '四足机甲 · 侧板修复版' })
  const panelNames = {
    Side_Panel_Front_L: '前左侧板 · 内倾 32.5°',
    Side_Panel_Front_R: '前右侧板 · 内倾 32.5°',
    Side_Panel_Rear_L: '后左侧板 · 内倾 35°',
    Side_Panel_Rear_R: '后右侧板 · 内倾 35°',
  }
  for (const [nodeName, label] of Object.entries(panelNames)) {
    const panel = model.object.getObjectByName(nodeName)
    if (!panel) throw new Error(`缺少侧板节点：${nodeName}`)
    asset.register(nodeName, panel, { name: label, parent: 'mech' })
  }
  asset.register('camera_main', camera, { name: '主相机' })

  const framing = framingReport(scene, model.object)
  if (!framing.ok) console.warn('模型取景中心检查未通过', framing)

  return {
    scene,
    camera,
    dispose() {
      ground.geometry.dispose()
      groundMaterial.dispose()
      environment.dispose()
    },
  }
}
