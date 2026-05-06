#pragma once
#include <readline/readline.h>
#include <readline/history.h>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include "Brezel/Console/Console.hpp"

namespace Brezel {

namespace CLI {

    inline Console* g_activeConsole = nullptr;
    inline std::vector<std::string> g_currentMatches;
    inline size_t g_currentMatchIdx = 0;

    inline char* completionGenerator(const char* text, int state) {
        if (state == 0) {
            g_currentMatchIdx = 0;
        }
        if (g_currentMatchIdx < g_currentMatches.size()) {
            return strdup(g_currentMatches[g_currentMatchIdx++].c_str());
        }
        return nullptr;
    }

    inline char** attemptCompletion(const char* text, int start, int end) {
        rl_attempted_completion_over = 1;
        if (!g_activeConsole) return nullptr;

        std::string prefix(text);
        if (start == 0) {
            g_currentMatches = g_activeConsole->completeVerb(prefix);
            rl_completion_append_character = ' ';
        } else {
            std::string line(rl_line_buffer);
            std::string verb;
            size_t spacePos = line.find(' ');
            if (spacePos != std::string::npos) {
                verb = line.substr(0, spacePos);
            }
            
            g_currentMatches = g_activeConsole->completePath(prefix, verb);
            rl_completion_append_character = '\0';
        }
        
        return rl_completion_matches(text, completionGenerator);
    }

    inline void runInteractive(Console& console) {
        g_activeConsole = &console;
        rl_attempted_completion_function = attemptCompletion;
        rl_completer_word_break_characters = (char*)" \t\n\"";

        char* buf = nullptr;
        while ((buf = readline("> ")) != nullptr) {
            std::string line(buf);
            if (line == "exit" || line == "quit") {
                free(buf);
                break;
            }
            if (!line.empty()) {
                add_history(buf);
                console.exec(line);
            }
            free(buf);
        }

        g_activeConsole = nullptr;
    }

} // namespace CLI

} // namespace Brezel
