#pragma once
#include <stack>
#include <string>

// Central navigation stack for managing UI state depth
// According to doc/interaktionsmodell.md:
// - Enter pushes a new state (depth +1)
// - Escape pops the current state (depth -1)
// - Number of '>' chars = current depth
class NavigationStack {
public:
    NavigationStack();
    
    // Push a new state onto the stack
    void push(const std::string& stateName = "");
    
    // Pop the current state from the stack
    // Returns false if at root (depth 1), true otherwise
    bool pop();
    
    // Get current depth (1-indexed, minimum 1)
    int getDepth() const;
    
    // Get prompt suffix with correct number of '>' chars
    std::string getPromptSuffix() const;
    
    // Reset to depth 1 (main menu)
    void reset();
    
    // Check if at main menu (depth 1)
    bool isAtRoot() const;
    
private:
    std::stack<std::string> stateStack;
};
