import { createScene } from './scene.js'

// Add independent scene factories here. Each receives its own SDK instance.
export function createProject() {
  return {
    id: 'quadruped-mech-side-panels',
    name: '四足机甲 · 侧板贴合与悬空修复',
    defaultSceneId: 'main',
    scenes: [{ id: 'main', name: '修复版模型', createScene }],
  }
}
