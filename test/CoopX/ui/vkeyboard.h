
#pragma once
namespace VKeyboard
{
    typedef void (*SubmitFn)(const char* text, void* data);

    void Open(const char* title, const char* initial, int maxLen, SubmitFn fn, void* data = nullptr, bool numeric = false);
    void Close();
    bool IsOpen();
    void Render();
}
