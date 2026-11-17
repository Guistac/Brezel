// SPDX-License-Identifier: MIT
// Brezel tests. Copyright (c) 2026 Leo Becker. See LICENSE.md.
//
// The header check (docs/Framework_Guide.md section 13): the build of this program is the test. tests/CMakeLists.txt
// generates one translation unit per public header (InteractiveConsole.hpp aside: it needs GNU readline): a header
// that compiles alone is included twice on its own, a header with a "First" entry after the header(s) it names. They
// all link into this program, so a non-inline definition in a header shows as a duplicate symbol.
#include <cstdio>

int main() {
    std::printf("Brezel headers: every public header but InteractiveConsole.hpp compiled and linked\n");
    return 0;
}
