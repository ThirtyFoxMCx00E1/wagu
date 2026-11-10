// 4.11: Files app picker for the mod (see filepick.h, tools/filepick/src/com/thirtyfox/mp/FilePick.java)
#include "filepick.h"
#include "filepick_dex.h"
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <jni.h>
#include <mutex>
#include <atomic>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/system_properties.h>
#include <sys/stat.h>

namespace
{
    std::mutex s_Mx;
    jclass s_Cls = nullptr;            // global ref to com.thirtyfox.mp.FilePick
    std::atomic<int> s_nLocal{ 0 };    // 6 = could not load the class
    std::string s_Dir, s_Mimes, s_LoadErr;

    JNIEnv* Env()
    {
        JavaVM* vm = aml->GetJavaVM();
        if(!vm) return nullptr;
        JNIEnv* env = nullptr;
        if(vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK && env) return env;
        if(vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
        return env;
    }
    bool ClearEx(JNIEnv* e)
    {
        if(!e->ExceptionCheck()) return false;
        jthrowable t = e->ExceptionOccurred(); e->ExceptionClear();
        if(t)
        {
            jclass oc = e->FindClass("java/lang/Object"); jmethodID ts = oc ? e->GetMethodID(oc, "toString", "()Ljava/lang/String;") : nullptr;
            jstring s = ts ? (jstring)e->CallObjectMethod(t, ts) : nullptr;
            if(e->ExceptionCheck()) e->ExceptionClear();
            if(s) { const char* c = e->GetStringUTFChars(s, nullptr); if(c) { s_LoadErr = c; logger->Error("FilePick: %s", c); e->ReleaseStringUTFChars(s, c); } }
        }
        return true;
    }
    int SdkInt() { char v[PROP_VALUE_MAX] = ""; __system_property_get("ro.build.version.sdk", v); return atoi(v); }

    // InMemoryDexClassLoader (Android 8+) or DexClassLoader from a read-only file in the app's code cache
    bool LoadClass(JNIEnv* e, jobject act)
    {
        if(s_Cls) return true;
        jclass ac = e->GetObjectClass(act);
        jmethodID gcl = e->GetMethodID(ac, "getClassLoader", "()Ljava/lang/ClassLoader;");
        if(ClearEx(e) || !gcl) return false;
        jobject parent = e->CallObjectMethod(act, gcl);
        if(ClearEx(e) || !parent) return false;
        jobject loader = nullptr;
        if(SdkInt() >= 26)
        {
            jclass lc = e->FindClass("dalvik/system/InMemoryDexClassLoader");
            jmethodID init = lc ? e->GetMethodID(lc, "<init>", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V") : nullptr;
            if(ClearEx(e) || !init) return false;
            jobject bb = e->NewDirectByteBuffer((void*)k_FilePickDex, (jlong)sizeof(k_FilePickDex));
            if(ClearEx(e) || !bb) return false;
            loader = e->NewObject(lc, init, bb, parent);
            if(ClearEx(e) || !loader) return false;
        }
        else
        {
            jmethodID gcd = e->GetMethodID(ac, "getCodeCacheDir", "()Ljava/io/File;");
            if(ClearEx(e) || !gcd) return false;
            jobject dirF = e->CallObjectMethod(act, gcd);
            jclass fc = e->FindClass("java/io/File");
            jmethodID gap = e->GetMethodID(fc, "getAbsolutePath", "()Ljava/lang/String;");
            if(ClearEx(e) || !dirF || !gap) return false;
            jstring dj = (jstring)e->CallObjectMethod(dirF, gap);
            const char* dc = e->GetStringUTFChars(dj, nullptr);
            std::string dir = dc ? dc : ""; if(dc) e->ReleaseStringUTFChars(dj, dc);
            std::string path = dir + "/mp_filepick.dex";
            remove(path.c_str());
            FILE* f = fopen(path.c_str(), "wb"); if(!f) return false;
            fwrite(k_FilePickDex, 1, sizeof(k_FilePickDex), f); fclose(f);
            chmod(path.c_str(), 0444);
            jclass lc = e->FindClass("dalvik/system/DexClassLoader");
            jmethodID init = lc ? e->GetMethodID(lc, "<init>", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V") : nullptr;
            if(ClearEx(e) || !init) return false;
            loader = e->NewObject(lc, init, e->NewStringUTF(path.c_str()), e->NewStringUTF(dir.c_str()), (jstring)nullptr, parent);
            if(ClearEx(e) || !loader) return false;
        }
        jclass clc = e->FindClass("java/lang/ClassLoader");
        jmethodID lcm = e->GetMethodID(clc, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        if(ClearEx(e) || !lcm) return false;
        jobject c = e->CallObjectMethod(loader, lcm, e->NewStringUTF("com.thirtyfox.mp.FilePick"));
        if(ClearEx(e) || !c) return false;
        s_Cls = (jclass)e->NewGlobalRef(c);
        return true;
    }

    void OpenOnUi(void*)
    {
        JNIEnv* e = Env();
        jobject act = aml->GetCurrentActivity();
        if(!e || !act) { s_LoadErr = "no activity"; s_nLocal = 6; return; }
        std::string dir, mimes; { std::lock_guard<std::mutex> l(s_Mx); dir = s_Dir; mimes = s_Mimes; }
        if(!LoadClass(e, act)) { if(s_LoadErr.empty()) s_LoadErr = "could not load the picker"; s_nLocal = 6; return; }
        jmethodID open = e->GetStaticMethodID(s_Cls, "open", "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;)V");
        if(ClearEx(e) || !open) { s_nLocal = 6; return; }
        e->CallStaticVoidMethod(s_Cls, open, act, e->NewStringUTF(dir.c_str()), e->NewStringUTF(mimes.c_str()));
        if(ClearEx(e)) s_nLocal = 6;
    }

    // 4.12.3: phone keyboard dialog
    std::string s_TTitle, s_TInit; int s_nTMax = 0; bool s_bTNum = false;
    std::atomic<int> s_nTLocal{ 0 };   // 1 = asked, waiting for the UI thread; 6 = could not load
    void AskOnUi(void*)
    {
        JNIEnv* e = Env();
        jobject act = aml->GetCurrentActivity();
        if(!e || !act || !LoadClass(e, act)) { s_nTLocal = 6; return; }
        std::string t, i; int mx; bool num; { std::lock_guard<std::mutex> l(s_Mx); t = s_TTitle; i = s_TInit; mx = s_nTMax; num = s_bTNum; }
        jmethodID m = e->GetStaticMethodID(s_Cls, "askText", "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;IZ)V");
        if(ClearEx(e) || !m) { s_nTLocal = 6; return; }
        e->CallStaticVoidMethod(s_Cls, m, act, e->NewStringUTF(t.c_str()), e->NewStringUTF(i.c_str()), (jint)mx, (jboolean)num);
        if(ClearEx(e)) { s_nTLocal = 6; return; }
        s_nTLocal = 0;
    }
    jfieldID Field(JNIEnv* e, const char* n, const char* sig) { jfieldID f = e->GetStaticFieldID(s_Cls, n, sig); if(ClearEx(e)) return nullptr; return f; }
    std::string StrField(const char* n)
    {
        JNIEnv* e = Env(); if(!e || !s_Cls) return "";
        jfieldID f = Field(e, n, "Ljava/lang/String;"); if(!f) return "";
        jstring s = (jstring)e->GetStaticObjectField(s_Cls, f); if(ClearEx(e) || !s) return "";
        const char* c = e->GetStringUTFChars(s, nullptr); std::string r = c ? c : ""; if(c) e->ReleaseStringUTFChars(s, c);
        e->DeleteLocalRef(s);
        return r;
    }
}

bool FilePick::Open(const char* toDir, const char* mimes)
{
    int st = State(); if(st == 1 || st == 2) return false;
    { std::lock_guard<std::mutex> l(s_Mx); s_Dir = toDir ? toDir : ""; s_Mimes = mimes ? mimes : ""; s_LoadErr.clear(); }
    s_nLocal = 1;   // until the Java side says so
    if(!aml->PushToJavaUIThread(OpenOnUi, nullptr)) OpenOnUi(nullptr);
    return true;
}
int FilePick::State()
{
    if(s_nLocal == 6) return 6;
    if(!s_Cls) return s_nLocal.load();
    JNIEnv* e = Env(); if(!e) return s_nLocal.load();
    jfieldID f = Field(e, "state", "I"); if(!f) return s_nLocal.load();
    int st = e->GetStaticIntField(s_Cls, f);
    if(st == 0 && s_nLocal == 1) return 1;   // OpenOnUi hasn't run yet
    return st;
}
long long FilePick::Copied()
{
    JNIEnv* e = Env(); if(!e || !s_Cls) return 0;
    jfieldID f = Field(e, "copied", "J"); return f ? (long long)e->GetStaticLongField(s_Cls, f) : 0;
}
std::string FilePick::Name() { return StrField("name"); }
std::string FilePick::Error() { return s_nLocal == 6 ? s_LoadErr : StrField("error"); }
void FilePick::Reset()
{
    s_nLocal = 0;
    JNIEnv* e = Env(); if(!e || !s_Cls) return;
    jfieldID f = Field(e, "state", "I"); if(f) e->SetStaticIntField(s_Cls, f, 0);
}

// 4.12.3: the phone's own keyboard (Settings > Keyboard)
bool FilePick::AskText(const char* title, const char* init, int maxLen, bool numeric)
{
    if(!aml->GetCurrentActivity()) return false;
    { std::lock_guard<std::mutex> l(s_Mx); s_TTitle = title ? title : ""; s_TInit = init ? init : ""; s_nTMax = maxLen; s_bTNum = numeric; }
    if(s_Cls) { JNIEnv* e = Env(); if(e) { jfieldID f = Field(e, "tstate", "I"); if(f) e->SetStaticIntField(s_Cls, f, 1); } }
    s_nTLocal = 1;
    if(!aml->PushToJavaUIThread(AskOnUi, nullptr)) AskOnUi(nullptr);
    return true;
}
int FilePick::TextState()
{
    if(s_nTLocal == 6) return 6;
    if(s_nTLocal == 1 || !s_Cls) return s_nTLocal == 1 ? 1 : 0;
    JNIEnv* e = Env(); if(!e) return 1;
    jfieldID f = Field(e, "tstate", "I"); if(!f) return 6;
    return e->GetStaticIntField(s_Cls, f);
}
std::string FilePick::Text() { return StrField("text"); }
void FilePick::TextReset()
{
    s_nTLocal = 0;
    JNIEnv* e = Env(); if(!e || !s_Cls) return;
    jfieldID f = Field(e, "tstate", "I"); if(f) e->SetStaticIntField(s_Cls, f, 0);
}
