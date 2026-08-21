// main.cpp - AI Agent entry point
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include "json.h"
#include "agent.h"

#ifdef _WIN32
#include <windows.h>

// ========== 防自杀第二层：忽略 Ctrl+C / Ctrl+Break 控制台事件 ==========
// 即使键盘注入意外发出了 Ctrl+C，也拦截中断信号，不让 Agent 进程被杀。
// 用户仍可正常输入 exit / quit 退出；CTRL_CLOSE_EVENT（关窗口）不拦截。
static BOOL WINAPI ConsoleCtrlHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
        return TRUE;  // 拦截中断信号，不终止进程
    }
    return FALSE;
}
#endif

// Load configuration from JSON file
bool loadConfig(const std::string& filename, AgentConfig& config) {
    std::ifstream file(filename.c_str());
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << filename << std::endl;
        return false;
    }
    
    std::stringstream ss;
    ss << file.rdbuf();
    file.close();
    
    try {
        json::Value root = json::parse(ss.str());
        
        // ========== 支持 providers 新结构 ==========
        if (root.has("providers") && root.has("activeProvider")) {
            std::string activeProvider = root["activeProvider"].asString();
            
            if (root["providers"].has(activeProvider)) {
                const json::Value& provider = root["providers"][activeProvider];
                
                if (provider.has("baseURL")) config.baseURL = provider["baseURL"].asString();
                if (provider.has("apiKey")) config.apiKey = provider["apiKey"].asString();
                if (provider.has("model")) config.model = provider["model"].asString();
                
                std::cout << "[INFO] Using provider: " << activeProvider << std::endl;
                std::cout << "[INFO] baseURL: " << config.baseURL << std::endl;
            } else {
                std::cerr << "Error: Provider '" << activeProvider << "' not found!" << std::endl;
                return false;
            }
        }
        // ========== 兼容旧结构 ==========
        else {
            if (root.has("baseURL")) config.baseURL = root["baseURL"].asString();
            if (root.has("apiKey")) config.apiKey = root["apiKey"].asString();
            if (root.has("model")) config.model = root["model"].asString();
        }
        
        // 系统提示词：配置里非空则直接使用
        if (root.has("systemPrompt")) config.systemPrompt = root["systemPrompt"].asString();
        
        if (root.has("screenshotInterval")) config.screenshotInterval = (int)root["screenshotInterval"].asNumber();
        if (root.has("maxTokens")) config.maxTokens = (int)root["maxTokens"].asNumber();
        if (root.has("temperature")) config.temperature = root["temperature"].asNumber();
        if (root.has("topP")) config.topP = root["topP"].asNumber();
        if (root.has("maxToolIterations")) config.maxToolIterations = (int)root["maxToolIterations"].asNumber();
        if (root.has("debug")) config.debug = root["debug"].asBool();
        
        // 思考模式配置（OpenAI 兼容格式）
        if (root.has("enableThinking")) config.enableThinking = root["enableThinking"].asBool();
        if (root.has("thinkingEffort")) {
            config.thinkingEffort = root["thinkingEffort"].asString();
            if (config.thinkingEffort != "low" &&
                config.thinkingEffort != "medium" &&
                config.thinkingEffort != "high") {
                std::cerr << "[WARNING] Invalid thinkingEffort '" << config.thinkingEffort
                          << "', using 'medium'" << std::endl;
                config.thinkingEffort = "medium";
            }
        }
        
        // 思考链输出配置
        if (root.has("outputThinking")) config.outputThinking = root["outputThinking"].asBool();
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error parsing config: " << e.what() << std::endl;
        return false;
    }
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // 防自杀第二层：注册控制台事件处理器，拦截 Ctrl+C / Ctrl+Break 中断信号
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#endif

    std::cout << "AI Agent - Starting up..." << std::endl;
    
    // Load configuration - 使用当前目录下的 config.json
    AgentConfig config;
    if (!loadConfig("config.json", config)) {
        std::cerr << "Failed to load configuration. Using defaults." << std::endl;
        config.baseURL = "https://api.openai.com/v1/chat/completions";
        config.apiKey = "your-api-key-here";
        config.model = "gpt-4";
    }
    
    // Validate configuration
    if (config.apiKey == "your-api-key-here") {
        std::cerr << "\n[WARNING] API Key not configured!" << std::endl;
        std::cerr << "Please edit config.json and set your API key." << std::endl;
        std::cerr << "Press Enter to continue anyway..." << std::endl;
        std::cin.get();
    }
    
    // Print debug status
    std::cout << "Debug mode: " << (config.debug ? "ON" : "OFF") << std::endl;
    std::cout << "Thinking mode: " << (config.enableThinking ? "ON" : "OFF") << std::endl;
    if (config.enableThinking) {
        std::cout << "Thinking effort: " << config.thinkingEffort << std::endl;
    }
    std::cout << "Thinking output: " << (config.outputThinking ? "ON" : "OFF") << std::endl;
    
    // Create and run agent
    Agent agent;
    if (!agent.init(config)) {
        std::cerr << "Failed to initialize agent." << std::endl;
        return 1;
    }
    
    // 先加载缓存（content.txt 里可能存有旧的系统提示词，之后会被新提示词替换）
    if (!agent.loadCache("content.txt")) {
        std::cout << "Starting with empty cache." << std::endl;
    } else {
        std::cout << "Loaded cache from content.txt" << std::endl;
    }
    
    // 系统提示词来自 config.json（在 loadCache 之后设置，确保新提示词覆盖缓存里的旧提示词）
    if (!config.systemPrompt.empty()) {
        agent.setSystemPrompt(config.systemPrompt);
        if (config.debug) {
            std::cout << "[INFO] Using system prompt from config.json" << std::endl;
        }
    }
    
    // Load memory compression prompt template (uses built-in template if file missing)
    agent.loadMemoryPrompt("memory_prompt.md");
    
    // Run the agent
    agent.run();
    
    // Save cache before exit
    agent.saveCache("content.txt");
    std::cout << "Cache saved. Goodbye!" << std::endl;
    
    return 0;
}
