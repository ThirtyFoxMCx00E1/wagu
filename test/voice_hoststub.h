// 3.11: stand-ins for AML + logger so src/audio/voice.cpp builds on a PC (voice_test.cpp)
#pragma once
#include <stdio.h>
#include <jni.h>
struct VoiceTestAml
{
    JavaVM* GetJavaVM() { return nullptr; }
    jobject GetCurrentActivity() { return nullptr; }
    jobject GetAppContextObject() { return nullptr; }
    bool PushToJavaUIThread(void (*)(void*), void* = nullptr) { return true; }
};
struct VoiceTestLog
{
    template<typename... A> void Info(const char* f, A... a) { printf("[voice] "); printf(f, a...); printf("\n"); }
    template<typename... A> void Error(const char* f, A... a) { printf("[voice ERR] "); printf(f, a...); printf("\n"); }
};
extern VoiceTestAml* aml;
extern VoiceTestLog* logger;
