# Windows AI Agent

一个体量小、能力强的 Windows AI 助手，通过自然语言即可操控电脑，无需安装庞大的依赖环境。

## ⚠️ 安全警告

该 Agent 主打体量小；但是拥有较高权限，如读取文件、创建终端、截屏、模拟键鼠等，请谨慎使用或在沙箱内运行。

## ✨ 功能特性

- 🖥️ **终端管理**：创建、关闭终端，发送命令并读取输出
- 🖱️ **模拟键鼠**：鼠标点击/拖拽、按键/组合键
- 📁 **文件管理**：读写、创建、删除文件与文件夹，查看信息
- 📸 **自动截屏**：按间隔截取屏幕供 AI 观察
- 🧠 **长对话压缩**：上下文超限自动压缩历史消息，防止内存爆炸
- 💬 **记忆消息**：支持持久记忆，跨会话保持上下文
- 🖼️ **多模态**：支持图片输入（如截图），配合视觉模型使用
- 💭 **思考链输出**：可在控制台查看模型的思考过程（reasoning_content）
- 🛡️ **防自杀保护**：三层防护拦截 Ctrl+C / Ctrl+Break 中断信号与自杀组合键，防止 Agent 误杀自身进程
- 🛑 **防卡死保护**：退出请求 + 重复调用检测 + 慢路径秒拒 + 工具执行超时 + HTTP 请求超时，多层防护任务卡死
- 🪶 **轻量**：纯 C++ 实现，仅依赖 curl，编译产物小巧

## 🛠️ 工具列表

| 分类 | 工具 |
|------|------|
| 终端 | `terminal_create` / `terminal_remove` / `terminal_input` / `terminal_output` |
| 鼠标 | `mouse_click` / `mouse_drag` |
| 键盘 | `key_press` / `key_combo` |
| 文件 | `file_create` / `file_delete` / `file_read` / `file_write` / `file_info` |
| 文件夹 | `folder_create` / `folder_delete` / `folder_list` / `folder_info` |
| 控制 | `agent_exit` |

## 📐 工具调用格式规范

为避免格式错误导致工具调用失败，每次 API 请求都会附带一条**工具调用格式强制规定**（system 消息）：

1. 调用工具时只能输出一个 JSON 代码块：`` ```json {"tool":"工具名","params":{...}}``` ``
2. `params` 必须是纯参数对象本身（如 `{"terminal_id":1}`），禁止套 `{"tool":...}` 外层包装，禁止把 `{"tool":...,"params":...}` 包装格式嵌套进 `params`
3. `params` 必须包含工具定义要求的全部必需参数，参数名与工具定义完全一致，禁止编造参数名或省略参数
4. `terminal_id` 必须使用 `terminal_create` 返回的真实 ID，禁止编造
5. 终端工具必须先创建再使用：先 `terminal_create` 获取 ID，再 `terminal_input` / `terminal_output`
6. 【安全红线】禁止 `key_combo` 组合 `ctrl+c` / `ctrl+break` / `ctrl+pause`（会杀死 Agent 自身）；中断终端程序用 `terminal_remove` 或发送 `exit`
7. 一次只调用一个工具，等工具结果返回后再决定下一步
8. 【防卡死】同一操作连续失败 2 次以上、终端长时间无输出、工具返回 `timeout`（执行超时）或任务无法推进时，必须停止重试并调用 `agent_exit` 工具请求退出本轮任务，禁止无限重复调用同一工具

> 💡 **代码层兜底**：即使模型违反上述规范，解析器仍会尽力修复——嵌套包装格式自动解包（最多 3 层）；`arguments` 解析失败时降级为空参数调用，让工具返回缺参提示引导模型重试。

## 🛡️ 防自杀保护

Agent 拥有模拟键盘的能力，为避免其误发 Ctrl+C 等中断信号杀死自身进程，内置三层防护：

1. **键盘层**：`key_combo` 硬拦截 `ctrl+c` / `ctrl+break` / `ctrl+pause` 组合键，拒绝执行并返回提示
2. **进程层**：程序注册 `SetConsoleCtrlHandler`，忽略 Ctrl+C / Ctrl+Break 控制台中断信号
3. **提示词层**：工具描述与每次请求附带的格式强制规定中写明安全红线，引导模型中断程序时使用 `terminal_remove` 或向终端发送 `exit`

> 💡 运行 Agent 的控制台窗口按 Ctrl+C 不会退出（会被拦截），请使用 `exit` / `quit` 命令正常退出；关闭窗口仍可随时退出。

## 🛑 防卡死保护

Agent 调用工具时可能陷入死循环或阻塞（如终端阻塞命令长时间无输出、模型反复重试同一操作、文件写入卡住不返回、网络请求无响应），内置五层防卡死防护：

1. **退出请求层**：新增 `agent_exit` 工具——模型判断任务卡死/无法推进时主动调用（参数 `reason` 说明原因），Agent 收到后记录原因并立即结束本轮任务
2. **重复检测层**：代码层检测工具调用——同一工具+相同参数连续重复 3 次自动判定为卡死，强制结束本轮任务并提示
3. **执行超时层**：工具执行套超时保护——工具超过 `toolTimeoutSeconds`（默认 30 秒）未返回视为卡死，放弃等待并把超时结果回传模型（后台线程可能仍在运行，其副作用不确定，模型被告知禁止重试相同操作）
4. **请求超时层**：单次 HTTP 请求套超时保护——请求超过 `httpTimeoutSeconds`（默认 120 秒）未返回视为网络卡死，curl 立即返回错误，不会无限等待；POST 与 GET 共用该超时
5. **提示词层**：每次请求附带的格式强制规定第 8 条引导模型：同一操作连续失败 2 次以上、终端长时间无输出、工具返回 timeout 时停止重试并调用 `agent_exit` 请求退出

> 💡 文件工具（`file_create` / `file_read` / `file_write`）执行前会预检查路径：UNC 与网络映射盘秒拒并返回错误，避免在慢盘上阻塞到执行超时，引导模型换本地路径。

## 🧾 终端输出缓冲

- 伪控制台尺寸为 **200 列 × 100 行**，子进程会认为自己有更大的屏幕，长输出（如 `dir`、编译日志）不易被窗口尺寸截断
- Agent 通过 `terminal_output` 能读到的输出历史上限为 **100KB**（`MAX_OUTPUT_BUFFER`），超出后只保留最新部分

## 🔧 编译

1. 下载 curl 静态库，头文件放入 `include/`，库文件放入 `lib/`
2. 运行 `build.bat`

## ⚙️ 配置

复制 `config.example.json` 为 `config.json` 并填写：

| 字段 | 说明 |
|------|------|
| `baseURL` | API 完整地址（含 `/chat/completions`） |
| `apiKey` | API 密钥 |
| `model` | 模型名称 |
| `systemPrompt` | 系统提示词（人设） |
| `maxTokens` | 上下文上限，超限自动压缩 |
| `temperature` | 采样温度 |
| `enableThinking` | 是否开启思考模式（true/false） |
| `thinkingEffort` | 思考强度：low / medium / high（OpenAI 兼容格式） |
| `outputThinking` | 是否在控制台输出思考链（true/false） |
| `screenshotInterval` | 自动截图间隔（消息数） |
| `toolTimeoutSeconds` | 工具执行超时时间（秒），超时判定卡死并放弃等待 |
| `httpTimeoutSeconds` | 单次 HTTP 请求超时时间（秒），超时判定网络卡死 |
| `debug` | 调试输出开关 |

> 💡 **关于思考模式**：`enableThinking` 为 `true` 时，请求会携带 OpenAI 兼容的 `reasoning_effort` 字段（取值 `thinkingEffort`）控制思考强度；为 `false` 时会发送 `enable_thinking: false` 关闭思考。注意：模型本身是否支持思考由服务端决定。
>
> 💡 **关于思考链输出**：`outputThinking` 为 `true` 时，会把模型返回的思考过程打印到控制台（支持 OpenAI 兼容格式的 `reasoning_content`、DashScope 原生格式的 `thought` 等字段），并以 `[Thinking]:` 标记；为 `false` 时隐藏。需搭配支持返回思考内容的模型（如推理类模型）使用。
>
> 💡 **提示**：若使用的模型不支持多模态（不能接收图片），请将 `screenshotInterval` 设为较大的数（如 `9999999999999999`），即可防止向模型发送截图。

## 🚀 使用

配置完成后运行 `agent.exe`，直接用自然语言与 AI 对话即可。

## 📁 项目结构

```
agent.*       AI 核心逻辑
main.cpp      入口
http_client.* HTTP 请求
terminal.*    终端管理
mouse.*       鼠标控制
keyboard.*    键盘控制
filesystem.*  文件系统操作
screenshot.*  屏幕截图
cache.*       聊天记录缓存
json.h        JSON 解析
```

## ⚠️ 免责声明

本项目仅供学习研究使用。使用者需自行承担所有风险与责任，请勿用于任何违法违规用途，建议仅在虚拟机或沙箱中运行。
