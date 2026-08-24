// agent.h - AI Agent core logic
#ifndef AGENT_H
#define AGENT_H

#include <string>
#include <vector>
#include <future>
#include <chrono>
#include "json.h"
#include "cache.h"
#include "tools.h"
#include "http_client.h"
#include "screenshot.h"

struct AgentConfig {
    std::string baseURL;
    std::string apiKey;
    std::string model;
    std::string systemPrompt;   // 系统提示词（配置里非空则优先使用，空则用 agent.md）
    int screenshotInterval;
    int maxTokens;
    double temperature;
    double topP;
    int maxToolIterations;
    int toolTimeoutSeconds;     // 单个工具执行超时（秒），超时判定卡死并放弃等待
    int httpTimeoutSeconds;     // 单次 HTTP 请求超时（秒），超时判定网络卡死
    bool debug;
    bool enableThinking;        // 是否开启思考模式（true 时发送 reasoning_effort）
    std::string thinkingEffort; // 思考强度：low / medium / high（OpenAI 兼容格式）
    bool outputThinking;        // 是否输出思考链（模型返回的 reasoning_content）
    
    AgentConfig() : screenshotInterval(5), maxTokens(8000), 
                    temperature(0.7), topP(0.9), maxToolIterations(10),
                    toolTimeoutSeconds(30), httpTimeoutSeconds(120),
                    debug(false), enableThinking(true), thinkingEffort("medium"),
                    outputThinking(true) {}
};

class Agent {
public:
    Agent();
    ~Agent();
    
    // Initialize agent with config
    bool init(const AgentConfig& config);
    
    // Load system prompt from file
    bool loadSystemPrompt(const std::string& filename);
    
    // Set system prompt directly from config string
    void setSystemPrompt(const std::string& prompt);
    
    // Load memory compression prompt template
    bool loadMemoryPrompt(const std::string& filename);
    
    // Load cache from file
    bool loadCache(const std::string& filename);
    
    // Save cache to file
    bool saveCache(const std::string& filename);
    
    // Main interaction loop
    void run();
    
    // Process single user input
    void processInput(const std::string& userInput);

private:
    AgentConfig config_;
    Cache cache_;
    ToolDispatcher tools_;
    HttpClient http_;
    
    // Screenshot timing
    std::chrono::steady_clock::time_point lastScreenshotTime_;
    
    // Memory compression prompt template
    std::string memoryPromptTemplate_;
    
    // Last reasoning/thinking chain extracted from raw API response
    std::string lastThinking_;
    
    // 超时放弃等待的工具执行结果（保留 future，避免析构时阻塞主线程）
    // 后台线程可能仍在运行，其副作用不确定；promise/future 手动创建的共享状态析构不会 join
    std::vector<std::future<std::string>> orphanedFutures_;
    
    // Send messages to AI and get response
    std::string sendToAI();
    
    // Parse AI response for tool calls
    bool parseToolCall(const std::string& response, 
                       std::string& toolName, 
                       json::Value& params);
    
    // Extract thinking and final response
    void extractResponse(const std::string& response,
                        std::string& thinking,
                        std::string& finalResponse);
    
    // Capture screenshot and add to cache
    void captureAndAddScreenshot();
    
    // Compress oldest messages into long-term memory when over token limit
    void compressToMemory();
    
    // Build request JSON
    std::string buildRequestJSON();
};

#endif // AGENT_H
