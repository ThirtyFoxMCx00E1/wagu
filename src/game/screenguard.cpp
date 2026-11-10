// 4.12.2: ScreenGuard (from research/ScreenGuard_source, Paltriz) built into MP.
// Without a valid src_Access.txt every game window gets FLAG_SECURE: screen recorders, screenshots,
// casting and the recent-apps preview see a black screen. With a valid key nothing is changed.
// The key file is checked against SHA-256 hashes only (src/game/screenguard_keys.h), so the keys
// can not be read out of the source or the .so.
#include "screenguard.h"
#include "screenguard_keys.h"
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <jni.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "mbedtls/sha256.h"

#define SG_FLAG_SECURE 0x00002000   // WindowManager.LayoutParams.FLAG_SECURE

namespace
{
    bool s_bLocked = true;
    int  s_nKey = 0;

    int CheckKeyText(const char* txt)
    {   // any token in the file that matches one of the 1000 hashes
        char tok[96]; int n = 0;
        for(const char* p = txt; ; ++p)
        {
            const bool part = *p && (isalnum((unsigned char)*p) || *p == '-');
            if(part) { if(n < (int)sizeof(tok) - 1) tok[n++] = (char)toupper((unsigned char)*p); continue; }
            if(n >= 10)
            {
                tok[n] = 0;
                unsigned char buf[128]; const char* salt = "MP-ScreenGuard-1|";
                const size_t sl = strlen(salt); memcpy(buf, salt, sl); memcpy(buf + sl, tok, n);
                uint8_t h[32]; mbedtls_sha256(buf, sl + n, h, 0);
                for(int i = 0; i < 1000; ++i) if(!memcmp(h, kSgKeyHash[i], 32)) return i + 1;
            }
            n = 0;
            if(!*p) break;
        }
        return 0;
    }
    int CheckKeyFile(const char* path)
    {
        FILE* f = fopen(path, "rb"); if(!f) return 0;
        char txt[2048]; size_t r = fread(txt, 1, sizeof(txt) - 1, f); fclose(f); txt[r] = 0;
        return CheckKeyText(txt);
    }

    bool ClearEx(JNIEnv* e) { if(e->ExceptionCheck()) { e->ExceptionClear(); return true; } return false; }

    // FLAG_SECURE on one activity's window (skips it when it is already set: no layout update every round)
    int SecureActivity(JNIEnv* env, jobject act)
    {
        if(!act) return 0;
        jclass actCls = env->FindClass("android/app/Activity");
        jclass winCls = env->FindClass("android/view/Window");
        jclass lpCls  = env->FindClass("android/view/WindowManager$LayoutParams");
        if(ClearEx(env) || !actCls || !winCls || !lpCls) return 0;
        jmethodID mWin = env->GetMethodID(actCls, "getWindow", "()Landroid/view/Window;");
        jmethodID mAdd = env->GetMethodID(winCls, "addFlags", "(I)V");
        jmethodID mAtt = env->GetMethodID(winCls, "getAttributes", "()Landroid/view/WindowManager$LayoutParams;");
        jfieldID  fFl  = env->GetFieldID(lpCls, "flags", "I");
        int done = 0;
        if(!ClearEx(env) && mWin && mAdd && mAtt && fFl)
        {
            jobject win = env->CallObjectMethod(act, mWin);
            if(!ClearEx(env) && win)
            {
                jobject lp = env->CallObjectMethod(win, mAtt);
                const bool has = !ClearEx(env) && lp && (env->GetIntField(lp, fFl) & SG_FLAG_SECURE);
                if(lp) env->DeleteLocalRef(lp);
                if(has) done = 1;
                else { env->CallVoidMethod(win, mAdd, (jint)SG_FLAG_SECURE); done = ClearEx(env) ? 0 : 1; }
                env->DeleteLocalRef(win);
            }
        }
        env->DeleteLocalRef(actCls); env->DeleteLocalRef(winCls); env->DeleteLocalRef(lpCls);
        return done;
    }

    // every activity of the game (ActivityThread.mActivities), like the original ScreenGuard
    int SecureAll(JNIEnv* env)
    {
        int n = SecureActivity(env, (jobject)aml->GetCurrentActivity());
        jclass atCls = env->FindClass("android/app/ActivityThread");
        if(ClearEx(env) || !atCls) return n;
        jmethodID cur = env->GetStaticMethodID(atCls, "currentActivityThread", "()Landroid/app/ActivityThread;");
        jfieldID fActs = cur ? env->GetFieldID(atCls, "mActivities", "Landroid/util/ArrayMap;") : nullptr;
        if(ClearEx(env) || !cur || !fActs) { env->DeleteLocalRef(atCls); return n; }
        jobject at = env->CallStaticObjectMethod(atCls, cur);
        jobject map = (!ClearEx(env) && at) ? env->GetObjectField(at, fActs) : nullptr;
        jclass mapCls = env->FindClass("android/util/ArrayMap");
        jclass recCls = env->FindClass("android/app/ActivityThread$ActivityClientRecord");
        if(!ClearEx(env) && map && mapCls && recCls)
        {
            jmethodID mSize = env->GetMethodID(mapCls, "size", "()I");
            jmethodID mVal  = env->GetMethodID(mapCls, "valueAt", "(I)Ljava/lang/Object;");
            jfieldID  fAct  = env->GetFieldID(recCls, "activity", "Landroid/app/Activity;");
            if(!ClearEx(env) && mSize && mVal && fAct)
            {
                const jint cnt = env->CallIntMethod(map, mSize);
                for(jint i = 0; !ClearEx(env) && i < cnt; ++i)
                {
                    jobject rec = env->CallObjectMethod(map, mVal, i);
                    if(ClearEx(env) || !rec) continue;
                    jobject act = env->GetObjectField(rec, fAct);
                    if(act) { n += SecureActivity(env, act); env->DeleteLocalRef(act); }
                    env->DeleteLocalRef(rec);
                }
            }
        }
        ClearEx(env);
        if(map) env->DeleteLocalRef(map);
        if(at) env->DeleteLocalRef(at);
        if(mapCls) env->DeleteLocalRef(mapCls);
        if(recCls) env->DeleteLocalRef(recCls);
        env->DeleteLocalRef(atCls);
        return n;
    }

    void* GuardThread(void*)
    {
        JavaVM* vm = aml->GetJavaVM();
        JNIEnv* env = nullptr;
        if(!vm || vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) { logger->Error("ScreenGuard: no JavaVM - inactive"); return nullptr; }
        bool logged = false;
        for(;;)
        {   // the game may re-create its window (resume, rotation): keep the flag on
            env->PushLocalFrame(64);
            const int n = SecureAll(env);
            env->PopLocalFrame(nullptr);
            if(n > 0 && !logged) { logger->Info("ScreenGuard: screen recording blocked (%d window(s)) - no valid src_Access.txt", n); logged = true; }
            usleep(1500 * 1000);
        }
        return nullptr;
    }
}

void ScreenGuard::Init(const char* mpDir)
{
    char p[512];
    snprintf(p, sizeof(p), "%s/src_Access.txt", mpDir);
    s_nKey = CheckKeyFile(p);
    if(!s_nKey) { snprintf(p, sizeof(p), "%s/../src_Access.txt", mpDir); s_nKey = CheckKeyFile(p); }   // files/src_Access.txt too
    s_bLocked = s_nKey == 0;
    if(!s_bLocked) { logger->Info("ScreenGuard: src_Access.txt key #%04d - screen recording allowed", s_nKey); return; }
    pthread_t t;
    if(pthread_create(&t, nullptr, GuardThread, nullptr) == 0) pthread_detach(t);
}
bool ScreenGuard::Locked() { return s_bLocked; }
int  ScreenGuard::KeyNumber() { return s_nKey; }
