// On-screen ImGui keyboard used for chat and for editing menu text fields.
#pragma once
namespace VKeyboard
{
    typedef void (*SubmitFn)(const char* text, void* data);

    void Open(const char* title, const char* initial, int maxLen, SubmitFn fn, void* data = nullptr, bool numeric = false);
    void Close();
    bool IsOpen();
    void Render();   // call inside an ImGui frame
    // 2.19: live text (chat box + slash-command suggestions)
    const char* GetText();
    void SetText(const char* text);
    bool IsChat();         // opened by the chat box
    void SetChat(bool chat);
    float Height();        // on-screen height (0 = closed)
    // 2.37: a keyboard opened with this submit function checks the text live: when check() returns
    // a message the text turns red, the message shows and Enter does nothing (reserved names)
    typedef const char* (*CheckFn)(const char* text);
    void SetValidator(SubmitFn forSubmit, CheckFn check);
}
