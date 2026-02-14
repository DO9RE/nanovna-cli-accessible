#include "navigation_stack.h"

// Minimum stack depth (root level = main menu)
static const int ROOT_DEPTH = 1;

NavigationStack::NavigationStack() {
    // Initialize with main menu state (depth ROOT_DEPTH)
    stateStack.push("MainMenu");
}

void NavigationStack::push(const std::string& stateName) {
    stateStack.push(stateName.empty() ? "UnnamedState" : stateName);
}

bool NavigationStack::pop() {
    // Never pop below the main menu (depth ROOT_DEPTH)
    if (stateStack.size() <= ROOT_DEPTH) {
        return false;
    }
    stateStack.pop();
    return true;
}

int NavigationStack::getDepth() const {
    return static_cast<int>(stateStack.size());
}

std::string NavigationStack::getPromptSuffix() const {
    std::string suffix;
    int depth = getDepth();
    for (int i = 0; i < depth; ++i) {
        suffix += ">";
    }
    return suffix;
}

void NavigationStack::reset() {
    // Clear all states and reinitialize with main menu
    while (stateStack.size() > ROOT_DEPTH) {
        stateStack.pop();
    }
}

bool NavigationStack::isAtRoot() const {
    return stateStack.size() == ROOT_DEPTH;
}
