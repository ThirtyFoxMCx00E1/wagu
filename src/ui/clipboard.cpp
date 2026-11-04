#include "clipboard.h"
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <jni.h>
#include <mutex>
#include <string.h>

namespace
{
    std::mutex s_Mx;
    std::string s_Copy, s_Paste;
    bool s_bPasteReady = false;

    JNIEnv* Env()
    {
        JavaVM* vm = aml->GetJavaVM();
        if(!vm) return nullptr;
        JNIEnv* env = nullptr;
        if(vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK && env) return env;
        if(vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
        return env;
    }
    bool ClearEx(JNIEnv* e) { if(e->ExceptionCheck()) { e->ExceptionClear(); return true; } return false; }

    jobject Manager(JNIEnv* e)
    {
        jobject ctx = aml->GetCurrentActivity();
        if(!ctx) ctx = aml->GetAppContextObject();
        if(!ctx) return nullptr;
        jclass cc = e->FindClass("android/content/Context");
        if(ClearEx(e) || !cc) return nullptr;
        jmethodID gs = e->GetMethodID(cc, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
        if(ClearEx(e) || !gs) return nullptr;
        jstring name = e->NewStringUTF("clipboard");
        jobject cm = e->CallObjectMethod(ctx, gs, name);
        e->DeleteLocalRef(name);
        if(ClearEx(e)) return nullptr;
        return cm;
    }

    void DoCopy(void*)
    {
        JNIEnv* e = Env(); if(!e) return;
        std::string t; { std::lock_guard<std::mutex> lk(s_Mx); t = s_Copy; }
        e->PushLocalFrame(16);
        if(jobject cm = Manager(e))
        {
            jclass cdc = e->FindClass("android/content/ClipData");
            jmethodID nt = cdc ? e->GetStaticMethodID(cdc, "newPlainText", "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Landroid/content/ClipData;") : nullptr;
            if(!ClearEx(e) && nt)
            {
                jobject clip = e->CallStaticObjectMethod(cdc, nt, e->NewStringUTF("MP chat"), e->NewStringUTF(t.c_str()));
                jclass cmc = e->GetObjectClass(cm);
                jmethodID set = e->GetMethodID(cmc, "setPrimaryClip", "(Landroid/content/ClipData;)V");
                if(!ClearEx(e) && clip && set) e->CallVoidMethod(cm, set, clip);
            }
            ClearEx(e);
        }
        e->PopLocalFrame(nullptr);
    }

    void DoPaste(void*)
    {
        std::string out;
        JNIEnv* e = Env();
        if(e)
        {
            e->PushLocalFrame(16);
            if(jobject cm = Manager(e))
            {
                jclass cmc = e->GetObjectClass(cm);
                jmethodID get = e->GetMethodID(cmc, "getPrimaryClip", "()Landroid/content/ClipData;");
                jobject clip = (!ClearEx(e) && get) ? e->CallObjectMethod(cm, get) : nullptr;
                if(!ClearEx(e) && clip)
                {
                    jclass cdc = e->GetObjectClass(clip);
                    jmethodID cnt = e->GetMethodID(cdc, "getItemCount", "()I");
                    jmethodID item = e->GetMethodID(cdc, "getItemAt", "(I)Landroid/content/ClipData$Item;");
                    if(!ClearEx(e) && cnt && item && e->CallIntMethod(clip, cnt) > 0)
                    {
                        jobject it = e->CallObjectMethod(clip, item, 0);
                        jclass ic = it ? e->GetObjectClass(it) : nullptr;
                        jmethodID gt = ic ? e->GetMethodID(ic, "getText", "()Ljava/lang/CharSequence;") : nullptr;
                        jobject cs = (!ClearEx(e) && gt) ? e->CallObjectMethod(it, gt) : nullptr;
                        if(!ClearEx(e) && cs)
                        {
                            jclass oc = e->FindClass("java/lang/Object");
                            jmethodID ts = e->GetMethodID(oc, "toString", "()Ljava/lang/String;");
                            jstring js = (jstring)e->CallObjectMethod(cs, ts);
                            if(!ClearEx(e) && js)
                            {
                                const char* u = e->GetStringUTFChars(js, nullptr);
                                if(u) { out = u; e->ReleaseStringUTFChars(js, u); }
                            }
                        }
                    }
                }
                ClearEx(e);
            }
            e->PopLocalFrame(nullptr);
        }
        for(char& c : out) if(c == '\n' || c == '\r' || c == '\t') c = ' ';
        std::lock_guard<std::mutex> lk(s_Mx);
        s_Paste = out; s_bPasteReady = true;
    }
}

void Clipboard::Copy(const char* text)
{
    { std::lock_guard<std::mutex> lk(s_Mx); s_Copy = text ? text : ""; }
    if(!aml->PushToJavaUIThread(DoCopy, nullptr)) DoCopy(nullptr);
}
void Clipboard::RequestPaste()
{
    { std::lock_guard<std::mutex> lk(s_Mx); s_bPasteReady = false; }
    if(!aml->PushToJavaUIThread(DoPaste, nullptr)) DoPaste(nullptr);
}
bool Clipboard::PollPaste(std::string& out)
{
    std::lock_guard<std::mutex> lk(s_Mx);
    if(!s_bPasteReady) return false;
    s_bPasteReady = false; out.swap(s_Paste); s_Paste.clear();
    return true;
}
