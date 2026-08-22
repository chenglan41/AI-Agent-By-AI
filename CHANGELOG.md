# 更新日志

## [1.2.6] - 2026-08-21

### 修复

- 修复文件系统工具中文转码错误：`folder_list` 等工具使用 ANSI(GBK) 版 API（`FindFirstFileA` 等）读取中文文件名，GBK 字节直接混入返回结果，恰好构成非法 UTF-8 序列时导致 API 报 400 `invalid unicode code point`。所有文件系统操作统一改用宽字符(W) API：
  - 输入路径：UTF-8 → UTF-16（`UTF8ToWide`），解决中文路径找不到的问题
  - 输出名称：UTF-16 → UTF-8（`WideToUTF8`），中文文件名不再乱码
  - 涉及 `createFile` / `createFolder` / `deleteFile` / `deleteFolder` / `getFileInfo` / `getFolderInfo` / `listFolder` / `readFile` / `writeFile` 共 9 个工具
- 顺带修复 `deleteFolder` 双 null 结尾 bug（旧代码 `path + "\0\0"` 实际未追加 null 字符，改为 wstring 显式补两个 `L'\0'`）
- `json.h` `escape()` 增加 UTF-8 完整性校验兜底：非法/截断的字节序列替换为 U+FFFD，任何来源的坏字节都不会再让 JSON 非法
- 清空 content.txt 中已混入坏字节的历史消息

## [1.2.5] - 2026-08-21

### 变更

- 调大终端输出缓冲：
  - 伪控制台尺寸 80×25 → 200×100，子进程认为自己有更大的屏幕，长输出（如 dir、编译日志）不易被窗口尺寸截断
  - Agent 可读输出历史上限 10KB → 100KB（新增 `MAX_OUTPUT_BUFFER` 常量），`terminal_output` 能读到的内容更多

### 其他

- 更新 README.md：新增「🧾 终端输出缓冲」章节

## [1.2.4] - 2026-08-21

### 修复

- 修复 Agent 自杀问题：模型曾自主调用 `key_combo` 发出 ctrl+c，向控制台发送中断信号（^C），杀死 Agent 自身进程。三层防护：
  - `KeyboardController::combo`：代码层硬拦截 ctrl+c / ctrl+break / ctrl+pause 组合键，拒绝执行并返回提示（改用 terminal_remove 或向终端发送 exit）
  - `main.cpp`：注册 `SetConsoleCtrlHandler`，忽略 CTRL_C_EVENT / CTRL_BREAK_EVENT 中断信号，进程不再被杀（关窗口退出不受影响）
  - `buildToolList`：新增"工具使用规范"注释，key_combo / key_press / terminal_input 描述写入安全红线与规范用法（严禁自杀组合键、中断程序用 terminal_remove 或 exit）
- 清空 content.txt，移除记录模型发出 ctrl+c 自杀行为的历史消息，防止模型模仿

### 其他

- 更新 README.md：功能特性新增「防自杀保护」，并新增「🛡️ 防自杀保护」章节说明三层防护原理与退出方式

## [1.2.3] - 2026-08-21

### 修复

- 修复工具调用参数"格式中毒"连环错误（模型把 `{"tool":...,"params":...}` 包装格式塞进原生 tool_calls 的 arguments，导致参数层层嵌套、`Missing terminal_id` / `Unknown tool` / `Invalid JSON` 连环报错）：
  - `Cache::extractToolCallFromText`：回传 API 的 arguments 现在只放参数对象本身，不再包含 tool/params 包装；对历史嵌套数据自动解包（最多 3 层）
  - `Agent::parseToolCall`：解析出的 params 若嵌套包装格式，自动逐层解包取出真实参数
  - `parseMultiFormatResponse`：tool_calls 的 arguments 解析失败时，降级为空参数工具调用（让工具报缺参引导模型重试），不再一路落空导致 Empty response
  - 所有工具描述统一追加格式提醒："调用时 arguments 直接填写参数对象本身，不要包含 tool 名称或任何包装格式"
  - 清空 content.txt 中被污染的旧消息（含占位符模板残留）

### 变更

- 终端工具描述强化：terminal_create 强调"请务必记住返回的 ID"；terminal_input/output 说明 terminal_id 来源
- `terminal_output` 未传 terminal_id 时自动使用唯一活动终端（无终端时返回提示引导先创建）

## [1.2.2] - 2026-08-21

### 修复

- 修复 DeepSeek 思考模式报 400 `The reasoning_content in the thinking mode must be passed back to the API`：
  - `Message` 结构新增 `reasoning` 字段，assistant 消息的思考链随缓存一起保存/加载
  - `content.txt` 中思考链以 `[REASONING]...[/REASONING]` 块存储（位于正文之前）
  - `Cache::toJSON(bool includeReasoning)`：思考模式开启时，assistant 消息（含 tool_calls 格式）自动回传 `reasoning_content` 字段
  - `buildRequestJSON()` 按 `config_.enableThinking` 决定是否回传思考链
  - 思考链计入 token 估算（防止缓存超限）

## [1.2.1] - 2026-08-21

### 修复

- 修复 DeepSeek 报 400 `missing field 'tool_call_id'` 的问题：
  - `Cache::toJSON()` 现在会把后面紧邻 tool 消息、且文本中包含工具调用 JSON 的 assistant 消息转换为 OpenAI `tool_calls` 格式（id 形如 `call_0`、`call_1`…）
  - 与 assistant 配对的 tool 消息输出 `tool_call_id` 字段，与前面的 tool_calls 对应
  - 孤立（无配对）的 tool 消息自动降级为 user 文本，避免 API 校验报错

## [1.2.0] - 2026-08-21

### 新增

- 新增输出思考链功能，可在控制台查看模型的思考过程
- 新增配置项 `outputThinking`：是否输出思考链，默认 `true`
- 支持多种思考内容格式：
  - OpenAI 兼容：`choices[0].message.reasoning_content`（DeepSeek / Qwen 等）
  - `choices[0].message.reasoning`
  - DashScope 原生：`output.thought` / `output.reasoning`
  - 顶层 `reasoning_content` / `reasoning`
- 打印时以 `[Thinking]:` 标记，并做 GBK/UTF-8 编码转换，避免 Windows 控制台乱码
- 启动时打印 `Thinking output: ON/OFF` 状态

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
