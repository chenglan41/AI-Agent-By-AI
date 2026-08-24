// agent.cpp - AI Agent core logic implementation
#include "agent.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <regex>
#include <string>
#include <future>

#ifdef _WIN32
#include <windows.h>

std::string GBKToUTF8(const std::string& gbkStr) {
    if (gbkStr.empty()) return gbkStr;
    
    int wlen = MultiByteToWideChar(CP_ACP, 0, gbkStr.c_str(), -1, NULL, 0);
    if (wlen <= 0) return gbkStr;
    
    wchar_t* wstr = new wchar_t[wlen];
    MultiByteToWideChar(CP_ACP, 0, gbkStr.c_str(), -1, wstr, wlen);
    
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (ulen <= 0) { delete[] wstr; return gbkStr; }
    
    char* utf8Str = new char[ulen];
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, utf8Str, ulen, NULL, NULL);
    
    std::string result(utf8Str);
    delete[] wstr;
    delete[] utf8Str;
    return result;
}

std::string UTF8ToGBK(const std::string& utf8Str) {
    if (utf8Str.empty()) return utf8Str;
    
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, NULL, 0);
    if (wlen <= 0) return utf8Str;
    
    wchar_t* wstr = new wchar_t[wlen];
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, wstr, wlen);
    
    int glen = WideCharToMultiByte(CP_ACP, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (glen <= 0) { delete[] wstr; return utf8Str; }
    
    char* gbkStr = new char[glen];
    WideCharToMultiByte(CP_ACP, 0, wstr, -1, gbkStr, glen, NULL, NULL);
    
    std::string result(gbkStr);
    delete[] wstr;
    delete[] gbkStr;
    return result;
}

bool isUTF8(const std::string& str) {
    size_t i = 0;
    while (i < str.size()) {
        unsigned char c = str[i];
        int bytes = 0;
        
        if (c < 0x80) {
            bytes = 1;
        } else if ((c & 0xE0) == 0xC0) {
            bytes = 2;
        } else if ((c & 0xF0) == 0xE0) {
            bytes = 3;
        } else if ((c & 0xF8) == 0xF0) {
            bytes = 4;
        } else {
            return false;
        }
        
        if (i + bytes > str.size()) return false;
        
        for (int j = 1; j < bytes; j++) {
            if ((str[i + j] & 0xC0) != 0x80) return false;
        }
        
        i += bytes;
    }
    return true;
}
#endif

// 按字节截断字符串，并回退被切断的多字节 UTF-8 字符的连续字节，
// 避免截断点落在中文等多字节字符中间产生乱码
static std::string truncateUTF8(const std::string& str, size_t maxLen) {
    if (str.size() <= maxLen) return str;
    std::string cut = str.substr(0, maxLen);
    while (!cut.empty() && ((unsigned char)cut.back() & 0xC0) == 0x80) {
        cut.pop_back();
    }
    return cut;
}

Agent::Agent() : lastScreenshotTime_(std::chrono::steady_clock::now()) {
}

Agent::~Agent() {
}

bool Agent::init(const AgentConfig& config) {
    config_ = config;
    http_.setHeader("Authorization", "Bearer " + config_.apiKey);
    http_.setHeader("Content-Type", "application/json");
    http_.setDebug(config_.debug);
    // 可配置 HTTP 超时（秒），超时判定网络卡死；非法值在 HttpClient 内部回退 120 秒
    http_.setTimeout(config_.httpTimeoutSeconds);
    return true;
}

bool Agent::loadSystemPrompt(const std::string& filename) {
    std::ifstream file(filename.c_str());
    if (!file.is_open()) {
        if (config_.debug) {
            std::cerr << "Failed to open system prompt file: " << filename << std::endl;
        }
        return false;
    }
    
    std::stringstream ss;
    ss << file.rdbuf();
    file.close();
    
    // 替换第一条 system 消息，避免缓存里的旧提示词残留
    cache_.replaceSystemPrompt(ss.str());
    return true;
}

void Agent::setSystemPrompt(const std::string& prompt) {
    // 替换第一条 system 消息，避免缓存里的旧提示词残留
    cache_.replaceSystemPrompt(prompt);
}

bool Agent::loadCache(const std::string& filename) {
    return cache_.load(filename);
}

bool Agent::saveCache(const std::string& filename) {
    return cache_.save(filename);
}

void Agent::run() {
    std::cout << "=== AI Agent Started ===" << std::endl;
    std::cout << "Type 'exit' or 'quit' to stop" << std::endl;
    std::cout << "========================" << std::endl;
    
    while (true) {
        std::cout << "\n[You]: ";
        std::string input;
        std::getline(std::cin, input);
        
        if (!std::cin) {
            break;  // EOF 或读取失败，退出循环，防止死循环刷屏
        }
        
#ifdef _WIN32
        if (!input.empty() && !isUTF8(input)) {
            input = GBKToUTF8(input);
        }
#endif
        
        if (input == "exit" || input == "quit") {
            std::cout << "Exiting..." << std::endl;
            break;
        }
        
        if (input.empty()) continue;
        
        processInput(input);
    }
}

void Agent::processInput(const std::string& userInput) {
    // Capture screenshot if interval has passed
    captureAndAddScreenshot();
    
    // Add user message to cache
    cache_.addMessage("user", userInput);
    
    // Compress old messages into long-term memory if over token limit
    compressToMemory();
    
    // Send to AI and handle tool calls
    int iterations = 0;
    // 防卡死：记录上一次工具调用的签名，用于检测连续重复调用
    std::string lastToolSignature;
    int lastToolRepeatCount = 0;
    while (iterations < config_.maxToolIterations) {
        // 防卡死：每轮循环先检查缓存，超限及时压缩，
        // 避免长工具循环中请求体持续膨胀拖慢/卡死请求
        compressToMemory();
        
        if (config_.debug) {
            std::cout << "[Thinking...]" << std::endl;
        }
        
        std::string response = sendToAI();
        if (response.empty()) {
            if (config_.debug) {
                std::cout << "[Error: Empty response from AI]" << std::endl;
            }
            break;
        }
        
        // Parse response
        std::string thinking, finalResponse;
        extractResponse(response, thinking, finalResponse);
        
        // Display thinking chain (controlled by config outputThinking)
        if (config_.outputThinking) {
            // 优先输出 API 返回的思考链（reasoning_content），
            // 没有的话退回 <thinking> 标签提取的内容
            std::string displayThinking = !lastThinking_.empty() ? lastThinking_ : thinking;
            if (!displayThinking.empty()) {
#ifdef _WIN32
                if (isUTF8(displayThinking)) {
                    displayThinking = UTF8ToGBK(displayThinking);
                }
                std::cout << "\n[Thinking]: " << displayThinking << std::endl;
#else
                std::cout << "\n[Thinking]: " << displayThinking << std::endl;
#endif
            }
        }
        
        // Check for tool call
        std::string toolName;
        json::Value params;
        
        if (parseToolCall(response, toolName, params)) {
            if (config_.debug) {
                std::cout << "[Tool Call]: " << toolName << std::endl;
                // 防刷屏：参数超 500 字符截断打印（UTF-8 安全截断，避免中文被切断乱码）
                std::string paramsStr = json::serialize(params);
                size_t paramsTotal = paramsStr.size();
                if (paramsTotal > 500) {
                    paramsStr = truncateUTF8(paramsStr, 500) + "... (truncated, total " + std::to_string(paramsTotal) + " chars)";
                }
#ifdef _WIN32
                if (isUTF8(paramsStr)) {
                    paramsStr = UTF8ToGBK(paramsStr);
                }
                std::cout << "[Tool Params]: " << paramsStr << std::endl;
#else
                std::cout << "[Tool Params]: " << paramsStr << std::endl;
#endif
            }
            
            // 防卡死拦截 1：模型主动请求退出本轮任务，记录并立即结束循环
            if (toolName == "agent_exit") {
                std::string exitReason = params.has("reason") ? params["reason"].asString() : "(未说明原因)";
                
                json::Value storeObj;
                storeObj["tool"] = json::Value(toolName);
                storeObj["params"] = params;
                cache_.addMessage("assistant", "```json\n" + json::serialize(storeObj) + "\n```", lastThinking_);
                cache_.addToolResult(toolName, "Exit request accepted");
                
                if (config_.debug) {
                    std::cout << "[Agent Exit Request]: " << exitReason << std::endl;
                }
#ifdef _WIN32
                std::string displayReason = exitReason;
                if (isUTF8(displayReason)) {
                    displayReason = UTF8ToGBK(displayReason);
                }
                std::cout << "\n[Agent]: " << displayReason << std::endl;
#else
                std::cout << "\n[Agent]: " << exitReason << std::endl;
#endif
                break;
            }
            
            // 防卡死拦截 2：同一工具+参数连续重复 3 次判定为卡死，强制结束本轮
            std::string toolSignature = toolName + ":" + json::serialize(params);
            if (toolSignature == lastToolSignature) {
                lastToolRepeatCount++;
            } else {
                lastToolSignature = toolSignature;
                lastToolRepeatCount = 1;
            }
            if (lastToolRepeatCount >= 3) {
                json::Value storeObj;
                storeObj["tool"] = json::Value(toolName);
                storeObj["params"] = params;
                cache_.addMessage("assistant", "```json\n" + json::serialize(storeObj) + "\n```", lastThinking_);
                cache_.addToolResult(toolName, "Blocked: identical tool call repeated 3 times, judged as stuck, loop terminated");
                
#ifdef _WIN32
                std::string stuckMsg = UTF8ToGBK("检测到同一工具调用连续重复 3 次，判定为卡死，本轮任务已结束");
                std::cout << "\n[Agent]: " << stuckMsg << std::endl;
#else
                std::cout << "\n[Agent]: 检测到同一工具调用连续重复 3 次，判定为卡死，本轮任务已结束" << std::endl;
#endif
                break;
            }
            
            // Execute tool with timeout protection（工具执行超时保护）
            // 工具内部可能阻塞（写大文件、网络盘、管道写满等），同步调用会卡死整轮，
            // 之前的防卡死拦截都来不及生效。超过 toolTimeoutSeconds 未返回则放弃等待，
            // 把超时结果作为 tool result 回传，让模型换方式或调用 agent_exit。
            std::string result;
            {
                std::shared_ptr<std::promise<std::string>> execPromise =
                    std::make_shared<std::promise<std::string>>();
                std::future<std::string> execFuture = execPromise->get_future();
                std::string execToolName = toolName;
                json::Value execParams = params;
                std::thread worker([this, execToolName, execParams, execPromise]() {
                    try {
                        execPromise->set_value(tools_.execute(execToolName, execParams));
                    } catch (...) {
                        try { execPromise->set_value(std::string("Tool execution threw an exception")); }
                        catch (...) {}
                    }
                });
                worker.detach();

                std::future_status status = execFuture.wait_for(
                    std::chrono::seconds(config_.toolTimeoutSeconds));

                if (status == std::future_status::timeout) {
                    result = "Tool execution timeout after " + std::to_string(config_.toolTimeoutSeconds)
                           + "s, judged as stuck. The tool may still be running in background with uncertain side effects. "
                           + "Do NOT retry the same tool with identical params. Use a different approach, "
                           + "or call agent_exit if the task cannot proceed.";
                    // 保留 future：后台线程可能仍阻塞，future 留存避免被提前析构；
                    // 共享状态随 Agent 销毁释放，detach 线程自行结束
                    orphanedFutures_.push_back(std::move(execFuture));

#ifdef _WIN32
                    std::string timeoutMsg = UTF8ToGBK("工具执行超时（" + std::to_string(config_.toolTimeoutSeconds) + "秒），判定为卡死，已放弃等待该工具结果");
                    std::cout << "\n[Agent]: " << timeoutMsg << std::endl;
#else
                    std::cout << "\n[Agent]: 工具执行超时（" << config_.toolTimeoutSeconds << "秒），判定为卡死，已放弃等待该工具结果" << std::endl;
#endif
                } else {
                    try {
                        result = execFuture.get();
                    } catch (...) {
                        result = "Tool execution threw an exception";
                    }
                }
            }
            if (config_.debug) {
                // 防刷屏：结果超 1000 字符截断打印（UTF-8 安全截断，避免中文被切断乱码）
                std::string displayResult = result;
                if (displayResult.size() > 1000) {
                    displayResult = truncateUTF8(displayResult, 1000) + "... (truncated, total " + std::to_string(result.size()) + " chars)";
                }
#ifdef _WIN32
                if (isUTF8(displayResult)) {
                    displayResult = UTF8ToGBK(displayResult);
                }
                std::cout << "[Tool Result]: " << displayResult << std::endl;
#else
                std::cout << "[Tool Result]: " << displayResult << std::endl;
#endif
            }
            
            // Add assistant message and tool result (role="tool") to cache
            // 思考链（reasoning_content）一并存入，思考模式下 DeepSeek 要求回传
            // 存储前用解包后的干净参数重新打包，避免 content.txt 累积嵌套的包装格式（格式中毒源头）
            json::Value storeObj;
            storeObj["tool"] = json::Value(toolName);
            storeObj["params"] = params;
            cache_.addMessage("assistant", "```json\n" + json::serialize(storeObj) + "\n```", lastThinking_);
            cache_.addToolResult(toolName, result);
            
            iterations++;
            continue;
        }
        
        // No tool call, display final response
        if (!finalResponse.empty()) {
            cache_.addMessage("assistant", finalResponse, lastThinking_);
            
#ifdef _WIN32
            std::string displayResponse = finalResponse;
            if (isUTF8(displayResponse)) {
                displayResponse = UTF8ToGBK(displayResponse);
            }
            std::cout << "\n[Agent]: " << displayResponse << std::endl;
#else
            std::cout << "\n[Agent]: " << finalResponse << std::endl;
#endif
        }
        
        break;
    }
    
    // Save cache after each interaction
    saveCache("content.txt");
}

void Agent::captureAndAddScreenshot() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - lastScreenshotTime_).count();
    
    // Check if screenshot interval has passed
    if (elapsed >= config_.screenshotInterval) {
        if (config_.debug) {
            std::cout << "[Capturing screenshot...]" << std::endl;
        }
        
        std::string base64Image = Screenshot::captureAsBase64(85);
        
        if (!base64Image.empty()) {
            // Add screenshot to cache
            cache_.addScreenshot(base64Image);
            
            if (config_.debug) {
                std::cout << "[Screenshot captured and added to cache]" << std::endl;
            }
        } else {
            if (config_.debug) {
                std::cerr << "[Failed to capture screenshot]" << std::endl;
            }
        }
        
        lastScreenshotTime_ = now;
    }
}

// Multi-format response parser - supports OpenAI, Qwen/DashScope, and other formats
static std::string parseMultiFormatResponse(const std::string& response, bool debug) {
    try {
        json::Value root = json::parse(response);
        
        // Format 1: OpenAI standard (choices[0].message.content / tool_calls)
        if (root.has("choices") && root["choices"].isArray() && root["choices"].size() > 0) {
            const json::Value& choice = root["choices"][0];
            if (choice.has("message")) {
                const json::Value& message = choice["message"];
                
                // Check tool_calls (OpenAI format)
                if (message.has("tool_calls") && message["tool_calls"].isArray() && message["tool_calls"].size() > 0) {
                    const json::Value& toolCall = message["tool_calls"][0];
                    if (toolCall.has("function")) {
                        const json::Value& function = toolCall["function"];
                        std::string toolName = function.has("name") ? function["name"].asString() : "";
                        std::string arguments = function.has("arguments") ? function["arguments"].asString() : "{}";
                        
                        try {
                            json::Value result;
                            result["tool"] = toolName;
                            result["params"] = json::parse(arguments);
                            
                            std::string toolCallJson = "```json\n" + json::serialize(result) + "\n```";
                            if (debug) {
                                std::cout << "[DEBUG] Detected OpenAI tool_call: " << toolName << std::endl;
                            }
                            return toolCallJson;
                        } catch (const std::exception& e) {
                            if (debug) {
                                std::cerr << "[DEBUG] Failed to parse tool call arguments (tool=" << toolName << "): " << e.what() << std::endl;
                            }
                            // 降级：arguments 解析失败时用空参数发起工具调用，
                            // 让工具返回缺参错误引导模型重试；不再落到 content 分支导致 Empty response
                            if (!toolName.empty()) {
                                json::Value result;
                                result["tool"] = toolName;
                                result["params"] = json::parse("{}");
                                return "```json\n" + json::serialize(result) + "\n```";
                            }
                        }
                    }
                }
                
                // Return content
                if (message.has("content") && message["content"].isString()) {
                    return message["content"].asString();
                }
            }
            
            // Qwen/alternative format (choices[0].text or choices[0].content)
            if (choice.has("text") && choice["text"].isString()) {
                if (debug) {
                    std::cout << "[DEBUG] Using text field (Qwen/alternative format)" << std::endl;
                }
                return choice["text"].asString();
            }
            
            if (choice.has("content") && choice["content"].isString()) {
                if (debug) {
                    std::cout << "[DEBUG] Using content field (alternative format)" << std::endl;
                }
                return choice["content"].asString();
            }
        }
        
        // Format 2: DashScope (output.text / output.tool_calls)
        if (root.has("output")) {
            const json::Value& output = root["output"];
            if (output.has("text") && output["text"].isString()) {
                if (debug) {
                    std::cout << "[DEBUG] Detected DashScope format" << std::endl;
                }
                return output["text"].asString();
            }
            if (output.has("tool_calls") && output["tool_calls"].isArray() && output["tool_calls"].size() > 0) {
                if (debug) {
                    std::cout << "[DEBUG] Detected DashScope tool_calls" << std::endl;
                }
                const json::Value& toolCall = output["tool_calls"][0];
                if (toolCall.has("function")) {
                    const json::Value& function = toolCall["function"];
                    std::string toolName = function.has("name") ? function["name"].asString() : "";
                    std::string arguments = function.has("arguments") ? function["arguments"].asString() : "{}";
                    
                    try {
                        json::Value result;
                        result["tool"] = toolName;
                        result["params"] = json::parse(arguments);
                        return "```json\n" + json::serialize(result) + "\n```";
                    } catch (const std::exception& e) {
                        if (debug) {
                            std::cerr << "[DEBUG] Failed to parse DashScope tool call arguments (tool=" << toolName << "): " << e.what() << std::endl;
                        }
                        // 降级：同 OpenAI 分支，避免解析失败导致 Empty response
                        if (!toolName.empty()) {
                            json::Value result;
                            result["tool"] = toolName;
                            result["params"] = json::parse("{}");
                            return "```json\n" + json::serialize(result) + "\n```";
                        }
                    }
                }
            }
        }
        
        // Format 3: Simple formats (result / data / content / response)
        if (root.has("result") && root["result"].isString()) {
            if (debug) std::cout << "[DEBUG] Using result field" << std::endl;
            return root["result"].asString();
        }
        
        if (root.has("output") && root["output"].isString()) {
            if (debug) std::cout << "[DEBUG] Using output field" << std::endl;
            return root["output"].asString();
        }
        
        if (root.has("data") && root["data"].isString()) {
            if (debug) std::cout << "[DEBUG] Using data field" << std::endl;
            return root["data"].asString();
        }
        
        if (root.has("content") && root["content"].isString()) {
            if (debug) std::cout << "[DEBUG] Using content field" << std::endl;
            return root["content"].asString();
        }
        
        if (root.has("response") && root["response"].isString()) {
            if (debug) std::cout << "[DEBUG] Using response field" << std::endl;
            return root["response"].asString();
        }
        
        // Format 4: Nested data object
        if (root.has("data") && root["data"].isObject() && root["data"].has("text")) {
            if (debug) std::cout << "[DEBUG] Using data.text field" << std::endl;
            return root["data"]["text"].asString();
        }
        
        // Unrecognized format
        if (debug) {
            std::cerr << "\n========== UNRECOGNIZED FORMAT ==========" << std::endl;
            std::cerr << "Response: " << response.substr(0, 1000) << std::endl;
            std::cerr << "==========================================\n" << std::endl;
        }
        
        return "";
        
    } catch (const std::exception& e) {
        if (debug) {
            std::cerr << "[DEBUG] JSON parse error: " << e.what() << std::endl;
            std::cerr << "[DEBUG] Raw response: " << response.substr(0, 500) << std::endl;
        }
        return "";
    }
}

// Extract reasoning/thinking chain from raw API response
// Supports: choices[0].message.reasoning_content (DeepSeek/Qwen OpenAI-compatible),
//           choices[0].message.reasoning, top-level reasoning_content/reasoning,
//           DashScope native output.thought / output.reasoning
static std::string parseReasoningContent(const std::string& response, bool debug) {
    try {
        json::Value root = json::parse(response);
        
        // OpenAI compatible format
        if (root.has("choices") && root["choices"].isArray() && root["choices"].size() > 0) {
            const json::Value& choice = root["choices"][0];
            if (choice.has("message")) {
                const json::Value& message = choice["message"];
                
                if (message.has("reasoning_content") && message["reasoning_content"].isString()) {
                    std::string r = message["reasoning_content"].asString();
                    if (!r.empty()) {
                        if (debug) {
                            std::cout << "[DEBUG] Detected reasoning_content (OpenAI-compatible)" << std::endl;
                        }
                        return r;
                    }
                }
                if (message.has("reasoning") && message["reasoning"].isString()) {
                    std::string r = message["reasoning"].asString();
                    if (!r.empty()) return r;
                }
            }
        }
        
        // DashScope native format
        if (root.has("output") && root["output"].isObject()) {
            const json::Value& output = root["output"];
            if (output.has("thought") && output["thought"].isString()) {
                std::string r = output["thought"].asString();
                if (!r.empty()) return r;
            }
            if (output.has("reasoning") && output["reasoning"].isString()) {
                std::string r = output["reasoning"].asString();
                if (!r.empty()) return r;
            }
        }
        
        // Top-level fields
        if (root.has("reasoning_content") && root["reasoning_content"].isString()) {
            return root["reasoning_content"].asString();
        }
        if (root.has("reasoning") && root["reasoning"].isString()) {
            return root["reasoning"].asString();
        }
        
    } catch (const std::exception& e) {
        if (debug) {
            std::cerr << "[DEBUG] Reasoning parse error: " << e.what() << std::endl;
        }
    }
    
    return "";
}

std::string Agent::sendToAI() {
    lastThinking_.clear();
    
    std::string requestBody = buildRequestJSON();
    std::string response = http_.post(config_.baseURL, requestBody);
    
    if (response.empty()) {
        if (config_.debug) {
            std::cerr << "\n========== HTTP ERROR ==========" << std::endl;
            std::cerr << "Error: " << http_.getLastError() << std::endl;
            std::cerr << "URL: " << config_.baseURL << std::endl;
            std::cerr << "================================\n" << std::endl;
        }
        return "";
    }
    
    long statusCode = http_.getLastStatusCode();
    if (statusCode != 200) {
        if (config_.debug) {
            std::cerr << "\n========== HTTP ERROR ==========" << std::endl;
            std::cerr << "HTTP Status Code: " << statusCode << std::endl;
            std::cerr << "URL: " << config_.baseURL << std::endl;
            std::cerr << "Response: " << response << std::endl;
            std::cerr << "================================\n" << std::endl;
        }
    }
    
    // Check for API error first
    try {
        json::Value root = json::parse(response);
        
        if (root.has("error")) {
            if (config_.debug) {
                std::cerr << "\n========== API ERROR ==========" << std::endl;
                std::cerr << "HTTP Status Code: " << statusCode << std::endl;
                std::cerr << "Error Details: " << std::endl;
                
                if (root["error"].isString()) {
                    std::cerr << "  Message: " << root["error"].asString() << std::endl;
                } else if (root["error"].isObject()) {
                    const json::Value& errorObj = root["error"];
                    if (errorObj.has("message")) {
                        std::cerr << "  Message: " << errorObj["message"].asString() << std::endl;
                    }
                    if (errorObj.has("type")) {
                        std::cerr << "  Type: " << errorObj["type"].asString() << std::endl;
                    }
                    if (errorObj.has("code")) {
                        std::cerr << "  Code: " << errorObj["code"].asString() << std::endl;
                    }
                }
                
                std::cerr << "Full Response: " << response << std::endl;
                std::cerr << "================================\n" << std::endl;
            }
            return "";
        }
    } catch (const std::exception& e) {
        if (config_.debug) {
            std::cerr << "[DEBUG] Error check parse failed: " << e.what() << std::endl;
        }
    }
    
    // Extract reasoning/thinking chain from raw response (if any)
    lastThinking_ = parseReasoningContent(response, config_.debug);
    
    // Use multi-format parser
    return parseMultiFormatResponse(response, config_.debug);
}

bool Agent::parseToolCall(const std::string& response, 
                          std::string& toolName, 
                          json::Value& params) {
    size_t jsonStart = response.find("```json");
    if (jsonStart == std::string::npos) {
        jsonStart = response.find("{");
        if (jsonStart == std::string::npos) return false;
    } else {
        jsonStart += 7;
    }
    
    size_t jsonEnd = response.find("```", jsonStart);
    if (jsonEnd == std::string::npos) {
        jsonEnd = response.find("}", jsonStart);
        if (jsonEnd == std::string::npos) return false;
        jsonEnd++;
    }
    
    std::string jsonStr = response.substr(jsonStart, jsonEnd - jsonStart);
    
    size_t start = jsonStr.find("{");
    size_t end = jsonStr.rfind("}");
    if (start == std::string::npos || end == std::string::npos) return false;
    
    jsonStr = jsonStr.substr(start, end - start + 1);
    
    try {
        json::Value root = json::parse(jsonStr);
        if (!root.has("tool")) return false;
        
        toolName = root["tool"].asString();
        if (root.has("params")) {
            params = root["params"];
        }
        
        // 兼容"格式中毒"：模型若把 {"tool":..., "params":{...}} 包装格式整个塞进参数，
        // 这里自动解包取出内层真实参数（最多解 3 层，防极端嵌套）
        for (int depth = 0; depth < 3; depth++) {
            if (params.isObject() && params.has("params") && params["params"].isObject()) {
                params = params["params"];
            } else {
                break;
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        if (config_.debug) {
            std::cerr << "Failed to parse tool call JSON: " << e.what() << std::endl;
        }
        return false;
    }
}

void Agent::extractResponse(const std::string& response, 
                            std::string& thinking, 
                            std::string& finalResponse) {
    size_t thinkStart = response.find("<thinking>");
    size_t thinkEnd = response.find("</thinking>");
    
    if (thinkStart != std::string::npos && thinkEnd != std::string::npos) {
        thinkStart += 10;
        thinking = response.substr(thinkStart, thinkEnd - thinkStart);
        finalResponse = response.substr(thinkEnd + 11);
        
        size_t start = finalResponse.find_first_not_of(" \t\n\r");
        if (start != std::string::npos) {
            finalResponse = finalResponse.substr(start);
        }
    } else {
        finalResponse = response;
    }
}

// ---- OpenAI function calling tool schema builders ----

// Build a JSON-schema property definition
static json::Value makeProperty(const std::string& type, const std::string& desc) {
    json::Value p;
    p["type"] = json::Value(type);
    p["description"] = json::Value(desc);
    return p;
}

// Build a single tool definition
static json::Value makeTool(const std::string& name,
                            const std::string& description,
                            const json::Object& properties,
                            const json::Array& required) {
    json::Value fn;
    fn["name"] = json::Value(name);
    fn["description"] = json::Value(description);

    json::Value params;
    params["type"] = json::Value(std::string("object"));
    params["properties"] = json::Value(properties);
    if (!required.empty()) {
        params["required"] = json::Value(required);
    }
    fn["parameters"] = params;

    json::Value tool;
    tool["type"] = json::Value(std::string("function"));
    tool["function"] = fn;
    return tool;
}

// ========== 工具调用格式强制规定（每次请求都以 system 消息发送，见 buildRequestJSON） ==========
// 之前的规范只写在 C++ 注释里，注释不会发送给模型，实际未生效；现改为随请求发送。
static const std::string kToolCallFormatRules = std::string(
    "【工具调用格式强制规定】（必须严格遵守，违反会导致工具调用失败）\n"
    "1. 需要调用工具时，必须且只能输出一个 JSON 代码块，格式如下，前后不要附加任何其他文字：\n"
    "```json\n"
    "{\"tool\":\"工具名\",\"params\":{参数对象}}\n"
    "```\n"
    "2. params 必须是纯参数对象本身（如 {\"terminal_id\":1}），禁止套 {\"tool\":...} 外层包装，禁止把 {\"tool\":...,\"params\":...} 包装格式嵌套进 params。\n"
    "3. params 必须包含工具定义中要求的全部必需参数，参数名必须与工具定义完全一致，禁止编造参数名或省略参数。\n"
    "4. terminal_id 必须使用 terminal_create 返回的真实 ID，禁止编造 ID。\n"
    "5. 终端工具必须先创建再使用：先调用 terminal_create 创建终端并记住返回 ID，再用 terminal_input 输入命令、terminal_output 读取输出。\n"
    "6. 【安全红线】禁止 key_combo 组合 ctrl+c、ctrl+break、ctrl+pause（会杀死 Agent 自身）；中断终端程序用 terminal_remove 或向终端发送 exit。\n"
    "7. 一次只调用一个工具，等工具结果返回后再决定下一步。\n"
    "8. 【防卡死】同一操作连续失败 2 次以上、终端长时间无输出、工具返回 timeout（执行超时）或任务无法推进时，必须停止重试，调用 agent_exit 工具（params 示例：{\"reason\":\"卡死原因说明\"}）请求退出本轮任务，禁止无限重复调用同一工具。");

// Build the full tool list for the AI
static json::Array buildToolList() {
    json::Array tools;

    // --- Terminal tools ---
    {
        json::Object p;
        json::Array r;
        tools.push_back(makeTool("terminal_create", "创建一个新的终端会话并返回终端ID。任何终端操作前都要先创建终端：必须先调用本工具创建终端，并务必记住返回的ID，后续 terminal_input、terminal_output、terminal_remove 都需要传入该ID。注意：arguments 里直接填参数本身（如 {}），不要套 {\"tool\":...} 外层包装", p, r));
    }
    {
        json::Object p;
        p["terminal_id"] = makeProperty("integer", "终端会话ID");
        json::Array r;
        r.push_back(json::Value(std::string("terminal_id")));
        tools.push_back(makeTool("terminal_remove", "关闭指定终端会话。arguments 只填参数本身，如 {\"terminal_id\":1}", p, r));
    }
    {
        json::Object p;
        p["terminal_id"] = makeProperty("integer", "终端会话ID");
        p["command"] = makeProperty("string", "要执行的命令");
        json::Array r;
        r.push_back(json::Value(std::string("terminal_id")));
        r.push_back(json::Value(std::string("command")));
        tools.push_back(makeTool("terminal_input", "向指定终端发送一条命令并执行。终端要先创建再输入：必须先调用 terminal_create 创建终端并获取 terminal_id，然后才能发送命令，禁止未创建终端就输入。arguments 只填参数本身，如 {\"terminal_id\":1,\"command\":\"dir\"}。需要中断终端中正在运行的程序时，请发送 exit 或用 terminal_remove，禁止发送 ctrl+c", p, r));
    }
    {
        json::Object p;
        p["terminal_id"] = makeProperty("integer", "终端会话ID");
        json::Array r;
        r.push_back(json::Value(std::string("terminal_id")));
        tools.push_back(makeTool("terminal_output", "读取指定终端的输出缓冲区。终端要先创建再读取：若还没有终端，必须先调用 terminal_create 创建并获取终端ID。arguments 只填参数本身，如 {\"terminal_id\":1}", p, r));
    }

    // --- Mouse tools ---
    {
        json::Object p;
        p["x"] = makeProperty("integer", "屏幕X坐标");
        p["y"] = makeProperty("integer", "屏幕Y坐标");
        p["duration_ms"] = makeProperty("integer", "按下时长(毫秒)，默认100");
        json::Array r;
        r.push_back(json::Value(std::string("x")));
        r.push_back(json::Value(std::string("y")));
        tools.push_back(makeTool("mouse_click", "鼠标左键点击屏幕指定坐标", p, r));
    }
    {
        json::Object p;
        p["x1"] = makeProperty("integer", "起始X坐标");
        p["y1"] = makeProperty("integer", "起始Y坐标");
        p["x2"] = makeProperty("integer", "结束X坐标");
        p["y2"] = makeProperty("integer", "结束Y坐标");
        json::Array r;
        r.push_back(json::Value(std::string("x1")));
        r.push_back(json::Value(std::string("y1")));
        r.push_back(json::Value(std::string("x2")));
        r.push_back(json::Value(std::string("y2")));
        tools.push_back(makeTool("mouse_drag", "鼠标按住拖动(带真实感移动轨迹)", p, r));
    }

    // --- Keyboard tools ---
    {
        json::Object p;
        p["key"] = makeProperty("string", "按键名，如 'a', 'enter', 'esc', 'ctrl'");
        p["duration_ms"] = makeProperty("integer", "按下时长(毫秒)，默认50");
        json::Array r;
        r.push_back(json::Value(std::string("key")));
        tools.push_back(makeTool("key_press", "按下并释放一个按键（单键）。禁止用它配合其他按键模拟 ctrl+c 自杀组合键", p, r));
    }
    {
        json::Object p;
        p["key1"] = makeProperty("string", "第一个按键，如 'ctrl'");
        p["key2"] = makeProperty("string", "第二个按键，如 'v'");
        p["duration_ms"] = makeProperty("integer", "按下时长(毫秒)，默认50");
        json::Array r;
        r.push_back(json::Value(std::string("key1")));
        r.push_back(json::Value(std::string("key2")));
        tools.push_back(makeTool("key_combo", "同时按下两个按键(组合键)。【安全红线】严禁组合出 ctrl+c、ctrl+break、ctrl+pause：会向控制台发送中断信号杀死 Agent 自身进程。需要中断终端里的程序时改用 terminal_remove 关闭终端，或向终端发送 exit 命令。arguments 只填参数本身，如 {\"key1\":\"ctrl\",\"key2\":\"v\"}", p, r));
    }

    // --- File system tools ---
    {
        json::Object p;
        p["path"] = makeProperty("string", "文件路径");
        json::Array r;
        r.push_back(json::Value(std::string("path")));
        tools.push_back(makeTool("file_create", "创建新文件", p, r));
    }
    {
        json::Object p;
        p["path"] = makeProperty("string", "文件夹路径");
        json::Array r;
        r.push_back(json::Value(std::string("path")));
        tools.push_back(makeTool("folder_create", "创建新文件夹", p, r));
    }
    {
        json::Object p;
        p["path"] = makeProperty("string", "文件路径");
        json::Array r;
        r.push_back(json::Value(std::string("path")));
        tools.push_back(makeTool("file_delete", "删除文件", p, r));
    }
    {
        json::Object p;
        p["path"] = makeProperty("string", "文件夹路径");
        json::Array r;
        r.push_back(json::Value(std::string("path")));
        tools.push_back(makeTool("folder_delete", "删除文件夹", p, r));
    }
    {
        json::Object p;
        p["path"] = makeProperty("string", "文件路径");
        json::Array r;
        r.push_back(json::Value(std::string("path")));
        tools.push_back(makeTool("file_info", "获取文件信息(大小、修改时间等)", p, r));
    }
    {
        json::Object p;
        p["path"] = makeProperty("string", "文件夹路径");
        json::Array r;
        r.push_back(json::Value(std::string("path")));
        tools.push_back(makeTool("folder_info", "获取文件夹信息", p, r));
    }
    {
        json::Object p;
        p["path"] = makeProperty("string", "文件夹路径");
        json::Array r;
        r.push_back(json::Value(std::string("path")));
        tools.push_back(makeTool("folder_list", "列出文件夹内容", p, r));
    }
    {
        json::Object p;
        p["path"] = makeProperty("string", "文件路径");
        json::Array r;
        r.push_back(json::Value(std::string("path")));
        tools.push_back(makeTool("file_read", "读取文件内容", p, r));
    }
    {
        json::Object p;
        p["path"] = makeProperty("string", "文件路径");
        p["content"] = makeProperty("string", "要写入的内容");
        json::Array r;
        r.push_back(json::Value(std::string("path")));
        r.push_back(json::Value(std::string("content")));
        tools.push_back(makeTool("file_write", "写入内容到文件(覆盖)", p, r));
    }

    // --- Agent exit request (防卡死退出请求) ---
    {
        json::Object p;
        p["reason"] = makeProperty("string", "退出原因说明（可省略）");
        json::Array r;
        tools.push_back(makeTool("agent_exit", "请求退出本轮任务。当任务已卡死、无法继续推进、终端长时间无输出、工具执行超时（工具结果返回 timeout 信息）、同一操作连续失败 2 次以上时，必须调用本工具请求退出，禁止无限重试。arguments 只填参数本身，如 {\"reason\":\"任务无法完成\"}", p, r));
    }

    return tools;
}

std::string Agent::buildRequestJSON() {
    json::Value root;
    
    // 工具调用格式强制规定：作为 system 消息插到 messages 最前面，每次请求都可见，
    // 强制模型按规范输出工具调用（避免包装格式/缺参/编造参数导致调用失败）
    json::Array msgs;
    json::Object formatObj;
    formatObj["role"] = json::Value(std::string("system"));
    formatObj["content"] = json::Value(kToolCallFormatRules);
    msgs.push_back(json::Value(formatObj));
    
    json::Value cached = cache_.toJSON(config_.enableThinking);
    const json::Array& cachedArr = cached.asArray();
    for (size_t i = 0; i < cachedArr.size(); i++) {
        msgs.push_back(cachedArr[i]);
    }
    
    // 思考模式开启时回传历史 assistant 消息的 reasoning_content（DeepSeek 必需）
    root["messages"] = json::Value(msgs);
    root["model"] = config_.model;
    root["max_tokens"] = config_.maxTokens;
    root["temperature"] = config_.temperature;
    root["top_p"] = config_.topP;
    // 把工具列表发给AI（OpenAI function calling 格式）
    root["tools"] = buildToolList();
    
    // 思考模式配置（OpenAI 兼容格式）
    if (config_.enableThinking) {
        // 开启思考：发送 reasoning_effort 控制思考强度
        root["reasoning_effort"] = config_.thinkingEffort; // low / medium / high
    } else {
        // 关闭思考：发送 enable_thinking=false（OpenAI 兼容 API 常见字段）
        root["enable_thinking"] = false;
    }
    
    return json::serialize(root);
}

// ============ 记忆压缩功能 ============

// 加载记忆压缩提示词模板（memory_prompt.md）
bool Agent::loadMemoryPrompt(const std::string& filename) {
    std::ifstream file(filename.c_str());
    if (!file.is_open()) {
        if (config_.debug) {
            std::cerr << "Warning: memory_prompt.md not found, will use built-in template" << std::endl;
        }
        return false;
    }
    
    std::stringstream ss;
    ss << file.rdbuf();
    file.close();
    
    memoryPromptTemplate_ = ss.str();
    return true;
}

// 超过 token 限制时，把最旧的一半非 system 消息压缩成长期记忆
void Agent::compressToMemory() {
    // 未超限则不需要压缩
    if (cache_.estimateTokens() <= config_.maxTokens) {
        return;
    }
    
    // 最多 8 轮压缩，防止死循环
    for (int round = 0; round < 8; round++) {
        if (cache_.estimateTokens() <= config_.maxTokens) {
            return;
        }
        
        const std::vector<Message>& messages = cache_.getMessages();
        
        // 统计非 system 消息数量
        size_t nonSystemCount = 0;
        for (size_t i = 0; i < messages.size(); i++) {
            if (messages[i].role != "system") {
                nonSystemCount++;
            }
        }
        
        if (nonSystemCount == 0) {
            return;  // 没有可压缩的对话
        }
        
        // 取最旧一半（至少 1 条）
        size_t count = nonSystemCount / 2;
        if (count < 1) count = 1;
        
        // 取出最旧的非 system 消息
        std::vector<Message> oldMessages = cache_.extractOldMessages(count);
        if (oldMessages.empty()) {
            return;
        }
        
        if (config_.debug) {
            std::cout << "[Compressing " << oldMessages.size() << " old messages into memory...]" << std::endl;
        }
        
        // 拼接上下文
        std::string context;
        for (size_t i = 0; i < oldMessages.size(); i++) {
            std::string role = oldMessages[i].role;
            if (role == "tool" && !oldMessages[i].toolName.empty()) {
                role = "tool(" + oldMessages[i].toolName + ")";
            }
            
            std::string content = oldMessages[i].content;
            if (content.size() > 2000) {
                content = content.substr(0, 2000);
            }
            
            context += "[" + role + "]: " + content + "\n";
        }
        
        // 构建压缩请求的 system 提示词
        std::string sysPrompt;
        if (!memoryPromptTemplate_.empty()) {
            sysPrompt = memoryPromptTemplate_;
            const std::string placeholder = "${content}";
            size_t pos = sysPrompt.find(placeholder);
            if (pos != std::string::npos) {
                sysPrompt.replace(pos, placeholder.size(), context);
            } else {
                sysPrompt += "\n上下文:\n" + context;
            }
        } else {
            sysPrompt = std::string(
                "你是对话记忆压缩助手。请把下面的对话上下文压缩成简洁的长期记忆。\n"
                "要求：\n"
                "1. 保留重要事实、用户的要求、偏好、任务进展和已做决策；\n"
                "2. 省略闲聊、重复内容和已完成的临时细节；\n"
                "3. 时间越靠前的信息权重越小，越新的信息权重越大；\n"
                "4. 禁止包含涉黄、涉政、涉暴、涉恐等违法内容；\n"
                "5. 只输出记忆内容本身，不要任何解释或客套话。\n\n"
                "上下文:\n") + context;
        }
        
        // 构建压缩请求（不带 tools，让 AI 直接输出记忆文本）
        json::Value root;
        json::Array arr;
        
        json::Object sysObj;
        sysObj["role"] = json::Value(std::string("system"));
        sysObj["content"] = json::Value(sysPrompt);
        arr.push_back(json::Value(sysObj));
        
        std::string existingMemory = cache_.getMemory();
        json::Object userObj;
        userObj["role"] = json::Value(std::string("user"));
        if (existingMemory.empty()) {
            userObj["content"] = json::Value(std::string("请把上面的上下文压缩成长期记忆。"));
        } else {
            userObj["content"] = json::Value(std::string(
                "下面是已有的长期记忆，请结合上面的新上下文合并更新（输出合并后的完整记忆）：\n\n"
                "已有记忆：\n") + existingMemory);
        }
        arr.push_back(json::Value(userObj));
        
        root["messages"] = json::Value(arr);
        root["model"] = json::Value(config_.model);
        root["max_tokens"] = json::Value(6000);
        root["temperature"] = json::Value(0.3);
        
        std::string requestBody = json::serialize(root);
        std::string response = http_.post(config_.baseURL, requestBody);
        
        if (response.empty()) {
            if (config_.debug) {
                std::cerr << "[Memory compression request failed, falling back to trimming]" << std::endl;
            }
            break;
        }
        
        std::string memory = parseMultiFormatResponse(response, config_.debug);
        if (memory.empty()) {
            if (config_.debug) {
                std::cerr << "[Memory compression returned empty result, falling back to trimming]" << std::endl;
            }
            break;
        }
        
        // 保存记忆
        cache_.setMemory(memory);
        if (config_.debug) {
            std::cout << "[Memory updated (" << memory.size() << " chars)]" << std::endl;
        }
    }
    
    // 兜底：压缩失败或仍超限时，简单裁剪最旧消息保命
    cache_.trimToTokenLimit(config_.maxTokens);
}
