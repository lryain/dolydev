# Commit message 模板（Conventional Commits）

<type>(<scope>): <简短描述>

可选正文（换行分段，提供更详细的动机与实现要点）

可选尾注（如 BREAKING CHANGE 或 关联 issue）
BREAKING CHANGE: <描述>

常用 type：
- feat: 新功能
- fix: 修复 bug
- docs: 文档变更
- style: 代码格式（不影响行为）
- refactor: 重构（非功能/非修复）
- perf: 性能优化
- test: 测试相关
- chore: 构建/脚本/依赖等杂项
- ci: CI 配置
- build: 构建输出改动
- revert: 回退提交

示例：
feat(lcd): 增加双屏渲染支持

测试/检查清单（提交前）：
- [ ] 运行单元测试
- [ ] 通过 lint
- [ ] 更新相关文档/CHANGELOG（如需）
- [ ] 在真机验证（若有硬件变更）

请保持首行不超过72字符，正文换行宽度 72。最后保留单个换行符。