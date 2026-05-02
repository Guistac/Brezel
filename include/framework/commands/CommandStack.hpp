#pragma once

#include <vector>

#include "framework/commands/Command.hpp"

class CommandStack {
public:
    void pushAndExecute(std::unique_ptr<Command> cmd) {
        cmd->execute();
        m_undoStack.push_back(std::move(cmd));
        m_redoStack.clear(); // New actions break the redo chain
    }

    void undo() {
        if (m_undoStack.empty()) return;
        
        auto cmd = std::move(m_undoStack.back());
        m_undoStack.pop_back();
        
        cmd->undo();
        m_redoStack.push_back(std::move(cmd));
    }

    // Redo would look similar...

private:
    std::vector<std::unique_ptr<Command>> m_undoStack;
    std::vector<std::unique_ptr<Command>> m_redoStack;
};