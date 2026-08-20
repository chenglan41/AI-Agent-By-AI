# 更新日志

## [1.1.0] - 2026-08-21

### 新增

- 新增思考模式配置项，兼容 OpenAI 格式：
  - `enableThinking`：是否开启思考模式，默认 `true`
  - `thinkingEffort`：思考强度，可选 `low` / `medium` / `high`，默认 `medium`
- 开启思考模式时，请求自动携带 `reasoning_effort` 字段；关闭时携带 `enable_thinking: false`
- 启动时打印当前思考模式状态，方便调试
- `thinkingEffort` 填写非法值时会自动回退为 `medium` 并给出警告

### 变更

- 更新 `README.md`：补充配置表说明与思考模式提示
- 更新 `config.example.json`：加入新增配置字段

## [1.0.0] - 2026-08-21

### 新增

- Windows AI Agent 初版，支持：
  - 17 个工具：终端 4 个、鼠标 2 个、键盘 2 个、文件 5 个、文件夹 4 个
  - 自动截屏（多模态识别）
  - Token 压缩与记忆消息（超长自动压缩，最长保留 8 轮）
  - 多格式响应解析（OpenAI / DashScope 等）
  - 聊天记录缓存与持久化

### 修复

- 修复自定义 systemPrompt 不生效的问题
- 修复 API 请求 404（baseURL 需包含完整路径）
- 修复图片前缀重复问题

### 其他

- 新增 `README.md`、`.gitignore`、`config.example.json`
- debug 日志改为英文输出，避免 GBK 乱码
