















#pragma once
#include <stdint.h>

typedef void (*MPMenuSectionFn)(void* userData);
typedef void (*MPMenuWindowFn)(void* userData);

class IMPMenu
{
public:
    virtual int   GetVersion() = 0;
    virtual void* GetImGuiContext() = 0;

    virtual void  AddSection(const char* title, MPMenuSectionFn fn, void* userData = nullptr) = 0;

    virtual void  AddFloatingWindow(MPMenuWindowFn fn, void* userData = nullptr) = 0;

    virtual void  AddBubble(const char* label, float x, float y, MPMenuSectionFn onTap, void* userData = nullptr) = 0;
    virtual void  OpenKeyboard(const char* title, const char* initial, int maxLen, void (*onSubmit)(const char*, void*), void* userData = nullptr) = 0;
    virtual void  AddChatMessage(uint32_t rgba, const char* text) = 0;
    virtual bool  IsConnected() = 0;
    virtual float Scale(float px1080) = 0;


    virtual bool  AddResource(const char* name) = 0;
};
