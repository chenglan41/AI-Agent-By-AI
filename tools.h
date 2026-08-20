// tools.h - Tool dispatcher
#ifndef TOOLS_H
#define TOOLS_H

#include <string>
#include <map>
#include "json.h"
#include "terminal.h"
#include "mouse.h"
#include "keyboard.h"
#include "filesystem.h"

class ToolDispatcher {
public:
    ToolDispatcher();
    ~ToolDispatcher();
    
    // Execute a tool call
    std::string execute(const std::string& toolName, const json::Value& params);
    
    // Get tool list description
    std::string getToolList();

private:
    TerminalManager terminalMgr_;
};

#endif // TOOLS_H
