#pragma once

#include "framework/console/Console.hpp"
#include <readline/readline.h>
#include <readline/history.h>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>

namespace CLI {

    // Global state required for readline C-callbacks
    inline Console* g_activeConsole = nullptr;
    inline std::vector<std::string> g_currentMatches;
    inline size_t g_currentMatchIdx = 0;

    /**
     * @brief Generator function called by readline for each match.
     * Returns strings one by one. Readline frees them automatically.
     */
    inline char* completionGenerator(const char* text, int state) {
        if (state == 0) {
            g_currentMatchIdx = 0;
        }
        if (g_currentMatchIdx < g_currentMatches.size()) {
            return strdup(g_currentMatches[g_currentMatchIdx++].c_str());
        }
        return nullptr;
    }

    /**
     * @brief Main completion callback hooked into readline.
     * Evaluates if we are completing a verb or a path based on cursor position.
     */
    inline char** attemptCompletion(const char* text, int start, int end) {
        rl_attempted_completion_over = 1; // Prevent fallback to default filename completion
        if (!g_activeConsole) return nullptr;

        std::string prefix(text);
        if (start == 0) {
            // First word: complete command verbs
            g_currentMatches = g_activeConsole->completeVerb(prefix);
            rl_completion_append_character = ' '; // Space after verbs
        } else {
            // Subsequent words: complete ECS paths
            std::string line(rl_line_buffer);
            std::string verb;
            size_t spacePos = line.find(' ');
            if (spacePos != std::string::npos) {
                verb = line.substr(0, spacePos);
            }
            
            g_currentMatches = g_activeConsole->completePath(prefix, verb);
            rl_completion_append_character = '\0'; // No space after paths to allow continuous typing
        }
        
        return rl_completion_matches(text, completionGenerator);
    }

    /**
     * @brief Starts the interactive REPL (Read-Eval-Print Loop) for the console.
     * Blocks the thread until the user types 'exit' or 'quit'.
     */
    inline void runInteractive(Console& console) {
        g_activeConsole = &console;
        
        // Hook our custom completion function
        rl_attempted_completion_function = attemptCompletion;
        
        // Prevent readline from breaking words at dots and slashes so our ECS paths stay intact
        rl_completer_word_break_characters = (char*)" \t\n\"";

        char* buf = nullptr;
        // readline automatically handles history navigation via Up/Down arrows
        while ((buf = readline("> ")) != nullptr) {
            std::string line(buf);
            if (line == "exit" || line == "quit") {
                free(buf);
                break;
            }
            if (!line.empty()) {
                add_history(buf);
                console.exec(line); // Execute the command through the Console
            }
            free(buf);
        }

        g_activeConsole = nullptr;
    }

} // namespace CLI
