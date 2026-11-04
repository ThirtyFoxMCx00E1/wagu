// 4.04: Android clipboard for the on-screen keyboard (Copy / Paste). The ClipboardManager is used on
// the Java UI thread (it needs a Looper), so paste is asynchronous: RequestPaste() then PollPaste().
#pragma once
#include <string>
namespace Clipboard
{
    void Copy(const char* text);
    void RequestPaste();
    bool PollPaste(std::string& out);   // true once when the text arrived (may be empty)
}
