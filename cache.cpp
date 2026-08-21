// cache.cpp - Message cache management implementation
// 使用纯文本格式保存，便于人类阅读和编辑
#include "cache.h"
#include <fstream>
#include <sstream>
#include <iostream>

// 分隔符定义
static const std::string MSG_SEPARATOR = "\n---MESSAGE---\n";
static const std::string ROLE_PREFIX = "[ROLE:";
static const std::string ROLE_SUFFIX = "]\n";
static const std::string IMAGE_PREFIX = "[IMAGE:";
static const std::string IMAGE_SUFFIX = "]\n";
static const std::string REASONING_BEGIN = "[REASONING]";
static const std::string REASONING_END = "[/REASONING]";

// 去掉 data URL 前缀（"data:image/jpeg;base64," 等），只保留纯 base64。
// 用最后一个逗号定位，可兼容双重前缀的旧坏数据。
static std::string stripDataUrlPrefix(const std::string& data) {
    if (data.compare(0, 5, "data:") == 0) {
        size_t commaPos = data.rfind(',');
        if (commaPos != std::string::npos) {
            return data.substr(commaPos + 1);
        }
    }
    return data;
}

// 去掉字符串前后的空白字符（空格、制表符、换行、回车）
static std::string trimWhitespace(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

// 从 assistant 消息文本中提取工具调用信息
// 支持格式: ```json\n{"tool":"xxx","params":{...}}\n``` 或裸 {...}
// 成功返回 true，并输出 toolName、arguments（JSON 字符串）和去掉工具调用块后的剩余文本
static bool extractToolCallFromText(const std::string& text, std::string& toolName,
                                    std::string& arguments, std::string& restText) {
    size_t fenceStart = text.find("```json");
    size_t jsonStart;
    size_t jsonEnd;
    size_t fenceEnd = std::string::npos;
    
    if (fenceStart != std::string::npos) {
        // 有代码块标记：找 { 和闭合的 ```
        jsonStart = text.find("{", fenceStart + 7);
        if (jsonStart == std::string::npos) return false;
        jsonEnd = text.find("```", jsonStart);
        if (jsonEnd == std::string::npos) return false;
        fenceEnd = jsonEnd + 3;
        restText = text.substr(0, fenceStart) + text.substr(fenceEnd);
    } else {
        // 无代码块标记：找第一个 { 到最后一个 }
        jsonStart = text.find("{");
        if (jsonStart == std::string::npos) return false;
        jsonEnd = text.rfind("}");
        if (jsonEnd == std::string::npos || jsonEnd <= jsonStart) return false;
        jsonEnd++;
        restText = text.substr(0, jsonStart) + text.substr(jsonEnd);
    }
    
    std::string jsonStr = text.substr(jsonStart, jsonEnd - jsonStart);
    size_t s = jsonStr.find("{");
    size_t e = jsonStr.rfind("}");
    if (s == std::string::npos || e == std::string::npos || e <= s) return false;
    jsonStr = jsonStr.substr(s, e - s + 1);
    
    try {
        json::Value root = json::parse(jsonStr);
        if (!root.has("tool")) return false;
        toolName = root["tool"].asString();
        
        // arguments 只放纯参数对象（OpenAI function calling 要求 arguments 就是参数本身）。
        // 之前把 {"tool":...,"params":...} 整体当 arguments 会污染上下文，
        // 导致模型在原生 tool_calls 里照抄包装格式。
        json::Value argsObj;
        if (root.has("params")) {
            argsObj = root["params"];
            // 若参数里又被塞了包装格式，解包嵌套的 params（最多 3 层）
            int depth = 0;
            while (argsObj.isObject() && argsObj.has("params") &&
                   argsObj["params"].isObject() && depth < 3) {
                json::Value inner = argsObj["params"];
                argsObj = inner;
                depth++;
            }
        }
        arguments = json::serialize(argsObj);
        restText = trimWhitespace(restText);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

Cache::Cache() {
}

Cache::~Cache() {
}

bool Cache::load(const std::string& filename) {
    std::ifstream file(filename.c_str(), std::ios::binary);
    if (!file.is_open()) {
        messages_.clear();
        return true;
    }
    
    std::stringstream ss;
    ss << file.rdbuf();
    file.close();
    
    std::string content = ss.str();
    if (content.empty()) {
        messages_.clear();
        return true;
    }
    
    messages_.clear();
    
    // 尝试解析纯文本格式
    size_t pos = 0;
    while (pos < content.size()) {
        // 查找 [ROLE:xxx]
        size_t roleStart = content.find(ROLE_PREFIX, pos);
        if (roleStart == std::string::npos) break;
        roleStart += ROLE_PREFIX.size();
        
        size_t roleEnd = content.find(ROLE_SUFFIX, roleStart);
        if (roleEnd == std::string::npos) break;
        
        std::string role = content.substr(roleStart, roleEnd - roleStart);
        pos = roleEnd + ROLE_SUFFIX.size();
        
        // 查找下一个分隔符或文件结尾
        size_t msgEnd = content.find(MSG_SEPARATOR, pos);
        std::string msgContent;
        if (msgEnd == std::string::npos) {
            msgContent = content.substr(pos);
            pos = content.size();
        } else {
            msgContent = content.substr(pos, msgEnd - pos);
            pos = msgEnd + MSG_SEPARATOR.size();
        }
        
        // 解析消息内容
        Message m;
        // tool 角色格式: "tool:函数名"
        if (role.compare(0, 5, "tool:") == 0) {
            m.role = "tool";
            m.toolName = role.substr(5);
        } else {
            m.role = role;
        }
        
        // 解析 assistant 的思考链块 [REASONING]...[/REASONING]（思考模式回传必需）
        if (msgContent.compare(0, REASONING_BEGIN.size(), REASONING_BEGIN) == 0) {
            size_t reasonBodyStart = REASONING_BEGIN.size();
            if (reasonBodyStart < msgContent.size() && msgContent[reasonBodyStart] == '\n') {
                reasonBodyStart++;
            }
            size_t reasonEndMark = msgContent.find("\n" + REASONING_END, reasonBodyStart);
            if (reasonEndMark != std::string::npos) {
                m.reasoning = msgContent.substr(reasonBodyStart, reasonEndMark - reasonBodyStart);
                msgContent = msgContent.substr(reasonEndMark + REASONING_END.size() + 1);
                // 去掉开头的换行
                if (!msgContent.empty() && msgContent[0] == '\n') {
                    msgContent = msgContent.substr(1);
                }
            }
        }
        
        // 检查是否包含图片
        size_t imgPos = msgContent.find(IMAGE_PREFIX);
        if (imgPos != std::string::npos) {
            // 提取文本部分
            std::string textPart = msgContent.substr(0, imgPos);
            // 去掉末尾换行
            while (!textPart.empty() && (textPart.back() == '\n' || textPart.back() == '\r')) {
                textPart.pop_back();
            }
            m.content = textPart;
            
            // 提取图片部分（去掉可能的 data URL 前缀，只存纯 base64）
            imgPos += IMAGE_PREFIX.size();
            size_t imgEnd = msgContent.find(IMAGE_SUFFIX, imgPos);
            if (imgEnd != std::string::npos) {
                std::string base64Data = stripDataUrlPrefix(msgContent.substr(imgPos, imgEnd - imgPos));
                m.contentItems.push_back(ContentItem::createText(textPart));
                m.contentItems.push_back(ContentItem::createImage(base64Data));
            }
        } else {
            // 纯文本消息
            // 去掉末尾换行
            while (!msgContent.empty() && (msgContent.back() == '\n' || msgContent.back() == '\r')) {
                msgContent.pop_back();
            }
            m.content = msgContent;
        }
        
        messages_.push_back(m);
    }
    
    // 如果纯文本格式解析失败，尝试 JSON 格式（向后兼容）
    if (messages_.empty() && content[0] == '[') {
        try {
            json::Value root = json::parse(content);
            if (root.isArray()) {
                for (size_t i = 0; i < root.size(); i++) {
                    const json::Value& msg = root[i];
                    if (!msg.has("role")) continue;
                    
                    Message m;
                    m.role = msg["role"].asString();
                    if (m.role == "tool" && msg.has("name")) {
                        m.toolName = msg["name"].asString();
                    }
                    if (m.role == "assistant" && msg.has("reasoning_content")
                        && msg["reasoning_content"].isString()) {
                        m.reasoning = msg["reasoning_content"].asString();
                    }
                    
                    if (msg.has("content")) {
                        if (msg["content"].isString()) {
                            m.content = msg["content"].asString();
                        } else if (msg["content"].isArray()) {
                            const json::Array& contentArr = msg["content"].asArray();
                            for (size_t j = 0; j < contentArr.size(); j++) {
                                const json::Value& item = contentArr[j];
                                if (!item.has("type")) continue;
                                
                                std::string type = item["type"].asString();
                                if (type == "text" && item.has("text")) {
                                    ContentItem ci = ContentItem::createText(item["text"].asString());
                                    m.contentItems.push_back(ci);
                                    if (m.content.empty()) {
                                        m.content = item["text"].asString();
                                    } else {
                                        m.content += " " + item["text"].asString();
                                    }
                                } else if (type == "image_url" && item.has("image_url")) {
                                    const json::Value& imgUrl = item["image_url"];
                                    if (imgUrl.has("url")) {
                                        std::string url = imgUrl["url"].asString();
                                        // 取最后一个逗号之后的内容，兼容双重前缀的旧坏数据
                                        std::string base64Data = stripDataUrlPrefix(url);
                                        ContentItem ci = ContentItem::createImage(base64Data);
                                        m.contentItems.push_back(ci);
                                    }
                                }
                            }
                        }
                    }
                    
                    messages_.push_back(m);
                }
            }
        } catch (...) {
            // JSON 解析也失败，保持空
        }
    }
    
    return true;
}

bool Cache::save(const std::string& filename) {
    std::ofstream file(filename.c_str(), std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // 使用纯文本格式保存，中文直接显示
    for (size_t i = 0; i < messages_.size(); i++) {
        const Message& msg = messages_[i];
        
        // 写入角色（tool 角色带上函数名: tool:函数名）
        std::string roleLine = msg.role;
        if (msg.role == "tool" && !msg.toolName.empty()) {
            roleLine = "tool:" + msg.toolName;
        }
        file << ROLE_PREFIX << roleLine << ROLE_SUFFIX;
        
        // 写入内容
        if (msg.hasMultiContent()) {
            // 写入文本部分
            for (const auto& item : msg.contentItems) {
                if (item.type == ContentType::Text) {
                    file << item.text << "\n";
                }
            }
            // 写入图片部分（只存纯 base64，不带 data URL 前缀，避免加载时双重前缀）
            for (const auto& item : msg.contentItems) {
                if (item.type == ContentType::Image) {
                    file << IMAGE_PREFIX << stripDataUrlPrefix(item.imageUrl) << IMAGE_SUFFIX;
                }
            }
        } else {
            // assistant 消息带思考链时，先写 reasoning 块再写正文
            if (msg.role == "assistant" && !msg.reasoning.empty()) {
                file << REASONING_BEGIN << "\n"
                     << msg.reasoning << "\n"
                     << REASONING_END << "\n";
            }
            file << msg.content << "\n";
        }
        
        // 写入分隔符（最后一条消息后也写入，便于追加）
        file << MSG_SEPARATOR;
    }
    
    file.close();
    return true;
}

void Cache::addMessage(const std::string& role, const std::string& content,
                       const std::string& reasoning) {
    // 用户消息：如果有待发送的截图，合并成一条多模态消息
    if (role == "user" && !pendingScreenshots_.empty()) {
        Message msg;
        msg.role = "user";
        
        // 文本部分：用户输入 + 截图标注
        std::string text = content + "\n\n当前屏幕：";
        msg.content = text;
        msg.contentItems.push_back(ContentItem::createText(text));
        
        // 图片部分：附加待发送的截图
        for (size_t i = 0; i < pendingScreenshots_.size(); i++) {
            msg.contentItems.push_back(ContentItem::createImage(pendingScreenshots_[i]));
        }
        pendingScreenshots_.clear();
        
        messages_.push_back(msg);
        return;
    }
    
    Message msg(role, content);
    if (role == "assistant") {
        msg.reasoning = reasoning;
    }
    messages_.push_back(msg);
}

void Cache::addToolResult(const std::string& toolName, const std::string& content) {
    Message msg;
    msg.role = "tool";
    msg.toolName = toolName;
    msg.content = content;
    messages_.push_back(msg);
}

void Cache::addScreenshot(const std::string& base64Image) {
    // 只保留最新一张截图，避免堆积过时图片
    pendingScreenshots_.clear();
    pendingScreenshots_.push_back(base64Image);
}

void Cache::addSystemPrompt(const std::string& prompt) {
    Message sysMsg;
    sysMsg.role = "system";
    sysMsg.content = prompt;
    messages_.insert(messages_.begin(), sysMsg);
}

void Cache::replaceSystemPrompt(const std::string& prompt) {
    // 第一条消息是 system（系统提示词）就直接替换内容，避免旧提示词残留
    if (!messages_.empty() && messages_[0].role == "system") {
        messages_[0].content = prompt;
        messages_[0].contentItems.clear();
    } else {
        addSystemPrompt(prompt);
    }
}

void Cache::clear() {
    messages_.clear();
    pendingScreenshots_.clear();
}

json::Value Cache::toJSON(bool includeReasoning) const {
    json::Array arr;
    int callSeq = 0;
    
    for (size_t i = 0; i < messages_.size(); i++) {
        const Message& msg = messages_[i];
        
        // assistant 消息：如果后面紧邻 tool 消息且文本中能提取出工具调用，
        // 输出 OpenAI tool_calls 格式，并给紧邻的 tool 消息生成配对的 tool_call_id
        if (msg.role == "assistant") {
            // 思考模式下 DeepSeek 要求每条 assistant 消息都回传 reasoning_content，
            // 缺思考链的旧消息（如关闭思考模式时期产生的）直接跳过，避免 400。
            // 其后紧跟的 tool 消息会作为孤立 tool 降级为 user 文本，上下文不丢。
            if (includeReasoning && msg.reasoning.empty()) {
                continue;
            }
            
            bool nextIsTool = (i + 1 < messages_.size() && messages_[i + 1].role == "tool");
            std::string toolName, arguments, restText;
            bool extracted = nextIsTool && extractToolCallFromText(msg.content, toolName, arguments, restText);
            
            if (extracted) {
                std::string callId = "call_" + std::to_string(callSeq++);
                
                json::Object aObj;
                aObj["role"] = json::Value(std::string("assistant"));
                // DeepSeek 对带 tool_calls 的 assistant 消息更认 content: null（无正文时）
                if (restText.empty()) {
                    aObj["content"] = json::Value(nullptr);
                } else {
                    aObj["content"] = json::Value(restText);
                }
                // 思考模式：必须回传 reasoning_content，否则 DeepSeek 400
                if (includeReasoning && !msg.reasoning.empty()) {
                    aObj["reasoning_content"] = json::Value(msg.reasoning);
                }
                
                json::Object fnObj;
                fnObj["name"] = json::Value(toolName);
                fnObj["arguments"] = json::Value(arguments);
                
                json::Object tcObj;
                tcObj["id"] = json::Value(callId);
                tcObj["type"] = json::Value(std::string("function"));
                tcObj["function"] = json::Value(fnObj);
                
                json::Array tcs;
                tcs.push_back(json::Value(tcObj));
                aObj["tool_calls"] = json::Value(tcs);
                arr.push_back(json::Value(aObj));
                
                // 紧邻的 tool 消息：输出 role=tool + tool_call_id（DeepSeek 必需）
                const Message& toolMsg = messages_[i + 1];
                json::Object tObj;
                tObj["role"] = json::Value(std::string("tool"));
                tObj["tool_call_id"] = json::Value(callId);
                tObj["name"] = json::Value(toolMsg.toolName);
                tObj["content"] = json::Value(toolMsg.content);
                arr.push_back(json::Value(tObj));
                
                i++; // 跳过已消费的 tool 消息
                continue;
            }
            
            // 无法配对：assistant 按纯文本输出
            json::Object obj;
            obj["role"] = json::Value(std::string("assistant"));
            obj["content"] = json::Value(msg.content);
            if (includeReasoning && !msg.reasoning.empty()) {
                obj["reasoning_content"] = json::Value(msg.reasoning);
            }
            arr.push_back(json::Value(obj));
            continue;
        }
        
        // 孤立 tool 消息（前面没有紧邻的可配对 assistant 工具调用）：
        // 降级为 user 文本，避免 DeepSeek 报 missing field 'tool_call_id'
        if (msg.role == "tool") {
            json::Object obj;
            obj["role"] = json::Value(std::string("user"));
            std::string text = "[工具结果 " + msg.toolName + "]\n" + msg.content;
            obj["content"] = json::Value(text);
            arr.push_back(json::Value(obj));
            continue;
        }
        
        // 其他消息（system / user）照旧输出
        json::Object obj;
        obj["role"] = json::Value(msg.role);
        
        if (msg.hasMultiContent()) {
            json::Array contentArr;
            for (const auto& item : msg.contentItems) {
                json::Object contentObj;
                if (item.type == ContentType::Text) {
                    contentObj["type"] = json::Value(std::string("text"));
                    contentObj["text"] = json::Value(item.text);
                } else if (item.type == ContentType::Image) {
                    contentObj["type"] = json::Value(std::string("image_url"));
                    json::Object imgUrl;
                    imgUrl["url"] = json::Value(item.imageUrl);
                    contentObj["image_url"] = json::Value(imgUrl);
                }
                contentArr.push_back(json::Value(contentObj));
            }
            obj["content"] = json::Value(contentArr);
        } else {
            obj["content"] = json::Value(msg.content);
        }
        
        arr.push_back(json::Value(obj));
    }
    
    return json::Value(arr);
}

int Cache::estimateTokens() const {
    int total = 0;
    for (const auto& msg : messages_) {
        total += estimateMessageTokens(msg);
    }
    return total;
}

void Cache::trimToTokenLimit(int maxTokens) {
    while (estimateTokens() > maxTokens && messages_.size() > 1) {
        // 保留 system 消息
        if (messages_[0].role == "system" && messages_.size() > 2) {
            messages_.erase(messages_.begin() + 1);
        } else {
            messages_.erase(messages_.begin());
        }
    }
}

int Cache::estimateMessageTokens(const Message& msg) const {
    int tokens = 0;
    if (!msg.content.empty()) {
        tokens += msg.content.size() / 4;
    }
    if (!msg.reasoning.empty()) {
        tokens += msg.reasoning.size() / 4;
    }
    for (const auto& item : msg.contentItems) {
        if (item.type == ContentType::Text) {
            tokens += item.text.size() / 4;
        } else if (item.type == ContentType::Image) {
            tokens += estimateImageTokens();
        }
    }
    return tokens;
}

std::vector<Message> Cache::extractOldMessages(size_t count) {
    std::vector<Message> extracted;
    if (count == 0) return extracted;
    
    std::vector<Message> remaining;
    size_t extractedCount = 0;
    
    for (size_t i = 0; i < messages_.size(); i++) {
        // system 消息（系统提示词 + 长期记忆）永远保留
        if (messages_[i].role != "system" && extractedCount < count) {
            extracted.push_back(messages_[i]);
            extractedCount++;
        } else {
            remaining.push_back(messages_[i]);
        }
    }
    
    messages_ = remaining;
    return extracted;
}

bool Cache::hasMemory() const {
    // 记忆消息是第二条及以后的 system 消息（第一条是系统提示词）
    int systemCount = 0;
    for (size_t i = 0; i < messages_.size(); i++) {
        if (messages_[i].role == "system") {
            systemCount++;
            if (systemCount >= 2) {
                return true;
            }
        }
    }
    return false;
}

std::string Cache::getMemory() const {
    int systemCount = 0;
    for (size_t i = 0; i < messages_.size(); i++) {
        if (messages_[i].role == "system") {
            systemCount++;
            if (systemCount >= 2) {
                return messages_[i].content;
            }
        }
    }
    return "";
}

void Cache::setMemory(const std::string& content) {
    // 已有记忆消息则替换内容
    int systemCount = 0;
    for (size_t i = 0; i < messages_.size(); i++) {
        if (messages_[i].role == "system") {
            systemCount++;
            if (systemCount >= 2) {
                messages_[i].content = content;
                return;
            }
        }
    }
    
    // 没有记忆消息：插入到第一条 system 消息之后
    Message memMsg("system", content);
    for (size_t i = 0; i < messages_.size(); i++) {
        if (messages_[i].role == "system") {
            messages_.insert(messages_.begin() + i + 1, memMsg);
            return;
        }
    }
    // 没有任何 system 消息：插到最前面
    messages_.insert(messages_.begin(), memMsg);
}
