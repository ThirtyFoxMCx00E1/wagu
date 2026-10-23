#pragma once
// 2.37: pure helpers for ModScan (no game / AML code, tested in test/modscan_test.cpp).
#include <stddef.h>
#include <string.h>
#include <ctype.h>

namespace ModScanCore
{
    // "Cheat Menu##x" / "CheatMenu" / "cheat_menu" -> "cheatmenu" (lower case, no spaces / _ / -)
    inline void Norm(const char* s, char* out, size_t cap)
    {
        size_t n = 0;
        for(; s && *s && n + 1 < cap; ++s)
        {
            unsigned char c = (unsigned char)*s;
            if(c == ' ' || c == '_' || c == '-' || c == '.') continue;
            out[n++] = (char)tolower(c);
        }
        out[n] = 0;
    }
    inline bool IsCheatLabel(const char* label)   // the menu's open button
    {
        if(!label) return false;
        char n[160]; Norm(label, n, sizeof(n));
        return strstr(n, "cheatmenu") != nullptr;
    }
    inline bool IsCheatWindow(const char* name)   // its window (only hooked inside the cheat lib)
    {
        if(!name) return false;
        char n[160]; Norm(name, n, sizeof(n));
        return strstr(n, "cheat") != nullptr;
    }
    inline bool IsModPath(const char* path)
    {
        if(!path) return false;
        size_t l = strlen(path);
        return l > 3 && strcasecmp(path + l - 3, ".so") == 0 && strstr(path, "/mods/") != nullptr;
    }

    // Streaming search over a library file. A cheat menu = a Dear ImGui build that has the C string
    // "CheatMenu" / "Cheat Menu" (any case, ending in \0 or "##"). MP itself has those words in its
    // detection list, so a file with MP's GUID is never a hit.
    struct Matcher
    {
        bool label = false, imgui = false, self = false;
        unsigned char tail[40]; size_t tailN = 0;

        static bool CiEq(const unsigned char* p, const char* w, size_t n)
        { for(size_t i = 0; i < n; ++i) if(tolower(p[i]) != w[i]) return false; return true; }

        void Scan(const unsigned char* b, size_t n)
        {
            for(size_t i = 0; i < n; ++i)
            {
                if(!imgui && i + 5 <= n && (memcmp(b + i, "ImGui", 5) == 0 || memcmp(b + i, "imgui", 5) == 0)) imgui = true;
                if(!self && i + 16 <= n && memcmp(b + i, "net.thirtyfox.mp", 16) == 0) self = true;
                if(!label && b[i] == 0)
                {
                    static const char* const w[] = { "cheatmenu", "cheat menu" };
                    for(const char* word : w)
                    {
                        size_t l = strlen(word);
                        if(i + 1 + l < n && CiEq(b + i + 1, word, l) && (b[i + 1 + l] == 0 || b[i + 1 + l] == '#')) label = true;
                    }
                }
            }
        }
        void Feed(const unsigned char* b, size_t n)
        {
            // join the end of the previous chunk with the start of this one so no match is cut in half
            unsigned char join[80];
            size_t k = n < 40 ? n : 40;
            memcpy(join, tail, tailN); memcpy(join + tailN, b, k);
            Scan(join, tailN + k);
            Scan(b, n);
            // carry the last 40 bytes of (old tail + this chunk) - also right for tiny chunks
            size_t total = tailN + n, keep = total < sizeof(tail) ? total : sizeof(tail);
            unsigned char nt[40];
            for(size_t i = 0; i < keep; ++i) { size_t idx = total - keep + i; nt[i] = idx < tailN ? tail[idx] : b[idx - tailN]; }
            memcpy(tail, nt, keep); tailN = keep;
        }
        bool IsCheatMenu() const { return label && imgui && !self; }
    };
}
