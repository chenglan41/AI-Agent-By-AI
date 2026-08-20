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
            file << msg.content << "\n";
        }
        
        // 写入分隔符（最后一条消息后也写入，便于追加）
        file << MSG_SEPARATOR;
    }
    
    file.close();
    return true;
}

void Cache::addMessage(const std::string& role, const std::string& content) {
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
    
    messages_.push_back(Message(role, content));
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

json::Value Cache::toJSON() const {
    json::Array arr;
    for (const auto& msg : messages_) {
        json::Object obj;
        obj["role"] = json::Value(msg.role);
        
        // tool 角色消息附带函数名
        if (msg.role == "tool" && !msg.toolName.empty()) {
            obj["name"] = json::Value(msg.toolName);
        }
        
        if (msg.hasMultiContent()) {
            json::Array contentArr;
            for (const auto& item : msg.contentItems) {
                json::Object contentObj;
                if (item.type == ContentType::Text) {
                    contentObj["type"] = json::Value("text");
                    contentObj["text"] = json::Value(item.text);
                } else if (item.type == ContentType::Image) {
                    contentObj["type"] = json::Value("image_url");
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
