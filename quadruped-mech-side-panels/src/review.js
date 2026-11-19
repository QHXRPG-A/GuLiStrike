// Inspect actual content screenshots and task-specific behavior before passing this review.
// Content changes invalidate old evidence: return to pending and recheck affected states.
// Evidence identifies real screenshots/check results; it is not an aesthetic score from the SDK.
export const review = {
  status: 'passed',
  checks: [
    {
      name: '四块侧板独立节点与几何数量',
      status: 'passed',
      evidence: 'blender/modification-report.json：四个独立侧板；修改前后均为 63,131 个顶点。',
    },
    {
      name: '两个悬空连接件接触复核',
      status: 'passed',
      evidence: 'blender/component-contacts.json：未再检测到超过 15 mm 的孤立零件间隙；目标连接件间隙约 0.074 mm。',
    },
  ],
  limitations: ['按项目规则未进行助手截图或渲染目检；最终侧板贴合与整体视觉效果由用户验收。'],
}
