// 4.04: chat GIFs (github.com/ThirtyFoxMCx00E1/animated-gifs, converted by tools/make_gifs.py into small
// sprite sheets on the wagu "gifs" branch). Sent in chat as "[gif:<id>]"; every phone downloads the
// sheet it needs into files/MP/gifs/ and the chat shows it animated on a taller row.
#pragma once
#include <string>
#include <vector>
namespace Gifs
{
    struct Info { int id = 0; std::string cat, name; int frames = 1, cols = 1, fw = 0, fh = 0, delay = 100; };
    void Init();                                   // loads / downloads index.txt (once, in the background)
    bool Ready();                                  // the index is loaded
    const Info* Get(int id);
    const std::vector<std::string>& Categories();  // "All" first
    void List(const char* cat, const char* search, std::vector<int>& out);
    bool SheetPath(int id, std::string& path);     // true = files/MP/gifs/<id>.jpg is here, false = queued
    int  ParseToken(const char* text);             // "[gif:123]" (whole message, spaces ok) -> 123, else 0
}
