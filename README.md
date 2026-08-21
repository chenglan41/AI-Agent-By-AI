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
- 🪶 **轻量**：纯 C++ 实现，仅依赖 curl，编译产物小巧

## 🛠️ 工具列表

| 分类 | 工具 |
|------|------|
| 终端 | `terminal_create` / `terminal_remove` / `terminal_input` / `terminal_output` |
| 鼠标 | `mouse_click` / `mouse_drag` |
| 键盘 | `key_press` / `key_combo` |
| 文件 | `file_create` / `file_delete` / `file_read` / `file_write` / `file_info` |
| 文件夹 | `folder_create` / `folder_delete` / `folder_list` / `folder_info` |

## 🛡️ 防自杀保护

Agent 拥有模拟键盘的能力，为避免其误发 Ctrl+C 等中断信号杀死自身进程，内置三层防护：

1. **键盘层**：`key_combo` 硬拦截 `ctrl+c` / `ctrl+break` / `ctrl+pause` 组合键，拒绝执行并返回提示
2. **进程层**：程序注册 `SetConsoleCtrlHandler`，忽略 Ctrl+C / Ctrl+Break 控制台中断信号
3. **提示词层**：工具描述中写明安全红线，引导模型中断程序时使用 `terminal_remove` 或向终端发送 `exit`

> 💡 运行 Agent 的控制台窗口按 Ctrl+C 不会退出（会被拦截），请使用 `exit` / `quit` 命令正常退出；关闭窗口仍可随时退出。

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
