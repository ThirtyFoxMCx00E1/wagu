#pragma once
// 4.11: open the Android Files app (system document picker) and copy the chosen file into a folder.
// Works inside the GTA process: a Fragment (tools/filepick, dex in filepick_dex.h) gets the answer.
#include <string>

namespace FilePick
{
    bool Open(const char* toDir, const char* mimes = nullptr);   // mimes: "application/zip,application/x-rar-compressed" or null = any
    int  State();                  // 0 idle, 1 Files app open, 2 copying, 3 done, 4 failed, 5 cancelled, 6 can't load
    long long Copied();            // bytes copied so far (state 2)
    std::string Name();            // the saved file name (state 3)
    std::string Error();           // state 4 / 6
    void Reset();                  // back to 0 after you read a result
    // 4.12.3: a dialog with a text box -> the phone's own keyboard types (Settings > Keyboard)
    bool AskText(const char* title, const char* init, int maxLen, bool numeric);
    int  TextState();              // 0 idle, 1 open, 3 done, 4 failed, 5 cancelled, 6 can't load
    std::string Text();            // state 3
    void TextReset();
}
