# 更新日志

## [1.3.2] - 2026-08-22

### 新增

- 新增**文件工具慢路径预检查**：UNC 路径（`\\server\...`）与网络映射盘（`GetDriveTypeW` 返回 `DRIVE_REMOTE`）在 `file_create` / `file_read` / `file_write` 执行前秒拒并返回错误，避免 `fopen` 在慢盘上长时间阻塞、每次都要等满 `toolTimeoutSeconds` 才被超时救场；相对路径按当前工作目录所在盘判断，本地盘放行
- 新增**HTTP 请求超时保护**（防卡死第五层）：单次 HTTP 请求超过 `httpTimeoutSeconds`（默认 120 秒）未返回视为网络卡死，curl 立即返回错误，不再无限等待；POST 与 GET 共用该超时
- 新增**工具循环内缓存压缩检查**：`processInput` 的工具 while 循环每次迭代调用 `compressToMemory()`，防止长工具循环中请求体持续膨胀拖慢请求
- 新增**调试打印截断**：`[Tool Params]` 超过 500 字符、`[Tool Result]` 超过 1000 字符时截断打印并标注原始长度（UTF-8 安全截断，中文不会被切出乱码），防止大内容刷屏

### 变更

- `HttpClient` 新增 `setTimeout`：POST/GET 超时从硬编码（120s/60s）改为可配置 `timeout_`，非法值回退 120 秒
- `AgentConfig` 新增 `httpTimeoutSeconds` 字段（默认 120）；main.cpp 读取配置并在启动时打印 `HTTP timeout: Xs`

### 修复

- 修复 exe 拷到未安装 MinGW 的机器上运行报 `libpthread-1.dll` 缺失：build.bat 链接改为 `-static` 全静态链接，把 pthread 等运行时全部静态进 exe（此前只静态了 libgcc/libstdc++，`std::thread` 等线程库仍依赖 pthread DLL；先尝试 `-static-libwinpthread`，老版 g++ 报 `unrecognized command-line option` 后改用 `-static`）
- 全静态后 libcurl/libssl/libcrypto 依赖的系统库报 undefined reference，链接行末尾追加系统库补齐：`-lcrypt32 -lwldap32 -lbcrypt -lws2_32 -lsecur32 -liphlpapi`

### 其他

- README.md 更新：功能特性与「🛑 防卡死保护」章节改为五层防护（新增请求超时层）、配置表新增 `httpTimeoutSeconds`

## [1.3.1] - 2026-08-22

### 新增

- 新增**工具执行超时保护**（防卡死第四层）：工具执行本身可能阻塞（如 `file_write` 写大文件、`terminal_input` 管道写满等），此前所有防卡死拦截都在 `execute` 返回后才生效，一旦工具卡住整轮直接死锁：
  - `processInput` 的工具执行改为 promise/future + 独立线程 + `wait_for` 超时等待，超时时长 `toolTimeoutSeconds`（默认 30 秒，可在 config.json 配置）
  - 超时后放弃等待，返回明确超时结果给模型（禁止重试相同操作、建议换方式或调用 `agent_exit`）；仍阻塞的后台线程 detach 继续运行，future 保留在 `orphanedFutures_` 防止析构阻塞
  - 工具执行抛异常时捕获并转为错误字符串回传，不再让异常直接崩掉进程
- 新增配置项 `toolTimeoutSeconds`（秒，默认 30），main.cpp 启动时打印 `Tool timeout: 30s` 状态

### 变更

- `kToolCallFormatRules` 第 8 条、`agent_exit` 工具描述补充「工具返回 timeout（执行超时）」场景，引导模型超时后停止重试并请求退出
- README.md 更新：「🛑 防卡死保护」章节改为四层防护（新增执行超时层）、配置表新增 `toolTimeoutSeconds`

## [1.3.0] - 2026-08-22

### 新增

- 新增**防卡死退出请求**功能，三层防卡死防护：
  - **退出请求层**：新增 `agent_exit` 工具（参数 `reason` 可选，说明退出原因），模型判断任务卡死、无法推进时可主动请求退出本轮任务；`processInput` 在执行工具前拦截 `agent_exit`，记录原因并立即结束本轮工具循环
  - **重复检测层**：同一工具+相同参数连续重复 3 次自动判定为卡死，强制结束本轮任务并输出提示（缓存中记录 blocked 结果，保持 tool 消息配对）
  - **提示词层**：`kToolCallFormatRules` 新增第 8 条——同一操作连续失败 2 次以上、终端长时间无输出或任务无法推进时必须停止重试并调用 `agent_exit` 请求退出，禁止无限重复调用同一工具
- 工具数量 17 → 18 个（新增 `agent_exit`；`ToolDispatcher::execute` 与 `getToolList` 同步补充兜底分支与列表项）

### 其他

- README.md 更新：功能特性新增「防卡死退出」、工具列表新增 `agent_exit`、格式规范新增第 8 条、新增「🛑 防卡死保护」章节

## [1.2.9] - 2026-08-21

### 变更

- 新增**工具调用格式强制规定**：`buildRequestJSON` 每次请求都在 messages 最前面插入一条 system 消息（`kToolCallFormatRules`），强制模型按规范输出工具调用：
  - 调用工具时只能输出 ```` ```json {"tool":"工具名","params":{...}}``` ```` 代码块，前后不附加其他文字
  - `params` 必须是纯参数对象，禁止套 `{"tool":...}` 外层包装或嵌套 `{"tool":...,"params":...}` 包装格式
  - `params` 必须包含全部必需参数，参数名与工具定义完全一致，禁止编造
  - `terminal_id` 必须使用 `terminal_create` 返回的真实 ID
  - 终端工具先创建再输入；【安全红线】禁止 ctrl+c 等自杀组合键；一次只调用一个工具
- 修复规范"写在注释里"的问题：此前格式规范只写在 `buildToolList` 的 C++ 注释里，注释不会发送给模型，实际从未生效；现改为随每次请求发送
- README.md 新增「📐 工具调用格式规范」章节

## [1.2.8] - 2026-08-21

### 修复

- 修复关闭思考模式（enableThinking=false）后仍报 400 `The reasoning_content in the thinking mode must be passed back to the API`：
  - 原因：本地关闭思考后服务端仍可能返回 reasoning_content（thinking mode 未被真正关闭），而 `toJSON` 按本地配置选择不回传，导致下一轮请求被 API 拒绝
  - 兼容写法（两点）：
    - reasoning_content 回传改为**按消息粒度判断**——某条 assistant 消息只要有思考链就回传，不再看本地配置
    - 跳过条件改为**按思考模式判断**——本地开启思考或缓存里出现过思考链（说明服务端实际处于 thinking mode）时，缺思考链的旧 assistant 消息直接跳过（防 400）

## [1.2.7] - 2026-08-21

### 变更

- 工具描述强化"终端要先创建再输入"规则：
  - `terminal_create`：明确任何终端操作前都要先创建终端并记住返回的 ID
  - `terminal_input`：明确必须先创建终端获取 terminal_id 再输入命令，禁止未创建就输入
  - `terminal_output`：明确终端要先创建再读取
- 核验确认：各板块工具（终端 4 个、文件系统 9 个、鼠标 2 个、键盘 2 个）执行后结果均统一回传 API（assistant tool_calls + tool 消息 tool_call_id 配对），无漏回传

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
  - 聊天记录缓存与持久化

### 修复

- 修复自定义 systemPrompt 不生效的问题
- 修复 API 请求 404（baseURL 需包含完整路径）
- 修复图片前缀重复问题

### 其他

- 新增 `README.md`、`.gitignore`、`config.example.json`
- debug 日志改为英文输出，避免 GBK 乱码
