// cache.h - Message cache management
#ifndef CACHE_H
#define CACHE_H

#include <string>
#include <vector>
#include "json.h"

// Message content types
enum class ContentType {
    Text,
    Image
};

// Single content item
struct ContentItem {
    ContentType type;
    std::string text;        // for text content
    std::string imageUrl;    // for image content (base64 data URL)
    
    ContentItem() : type(ContentType::Text) {}
    
    static ContentItem createText(const std::string& t) {
        ContentItem item;
        item.type = ContentType::Text;
        item.text = t;
        return item;
    }
    
    static ContentItem createImage(const std::string& base64Data) {
        ContentItem item;
        item.type = ContentType::Image;
        item.imageUrl = "data:image/jpeg;base64," + base64Data;
        return item;
    }
};

struct Message {
    std::string role; // "system", "user", "assistant", "tool"
    std::string content;  // simple text content (for backward compatibility)
    std::string toolName; // for "tool" role messages: the tool function name
    std::string reasoning; // assistant 消息的思考链（思考模式下服务端要求回传）
    std::vector<ContentItem> contentItems;  // multi-modal content
    
    Message() {}
    Message(const std::string& r, const std::string& c) : role(r), content(c) {}
    
    // Check if message has multi-modal content
    bool hasMultiContent() const { return !contentItems.empty(); }
};

class Cache {
public:
    Cache();
    ~Cache();
    
    // Load cache from file
    bool load(const std::string& filename);
    
    // Save cache to file
    bool save(const std::string& filename);
    
    // Add message to cache (text only)
    // reasoning: assistant 消息的思考链（可空，思考模式下必须回传）
    void addMessage(const std::string& role, const std::string& content, const std::string& reasoning = "");
    
    // Add tool result message (role="tool", with tool name)
    void addToolResult(const std::string& toolName, const std::string& content);
    
    // Add screenshot to cache (will be sent with next user message)
    void addScreenshot(const std::string& base64Data);
    
    // Add system prompt
    void addSystemPrompt(const std::string& prompt);
    
    // Replace the first system message (system prompt) content, or insert if none
    void replaceSystemPrompt(const std::string& prompt);
    
    // Clear cache
    void clear();
    
    // Get all messages as JSON array (OpenAI vision API format)
    // includeReasoning: 本地配置是否开启思考
    // 兼容：①reasoning_content 按消息粒度回传——某条 assistant 消息只要有思考链
    //      就回传，不再看本地配置；②跳过条件按"思考模式"判断——本地开启思考或
    //      缓存里出现过思考链（说明服务端实际处于 thinking mode）时，缺思考链的
    //      旧 assistant 消息直接跳过（防 400）
    json::Value toJSON(bool includeReasoning = false) const;
    
    // Estimate token count (rough estimate)
    int estimateTokens() const;
    
    // Trim oldest messages if over token limit
    void trimToTokenLimit(int maxTokens);
    
    // Extract oldest non-system messages (used for memory compression)
    std::vector<Message> extractOldMessages(size_t count);
    
    // Long-term memory (stored as a system message after the system prompt)
    bool hasMemory() const;
    std::string getMemory() const;
    void setMemory(const std::string& content);
    
    // Get messages
    const std::vector<Message>& getMessages() const { return messages_; }

private:
    std::vector<Message> messages_;
    std::vector<std::string> pendingScreenshots_;  // screenshots waiting to be sent
    
    // Rough token estimation (1 token ≈ 4 chars)
    int estimateMessageTokens(const Message& msg) const;
    
    // Estimate tokens for an image (rough estimate based on resolution)
    int estimateImageTokens() const { return 1000; }  // ~1000 tokens per image
};

#endif // CACHE_H
