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
#include <time.h>
#include <stdint.h>
#include <vector>
#include "mbedtls/sha256.h"

#define SG_FLAG_SECURE 0x00002000   // WindowManager.LayoutParams.FLAG_SECURE

namespace
{
    bool s_bLocked = true;
    int  s_nKey = 0;

    int CheckKeyText(const char* txt)
    {   // any token in the file that matches one of the key hashes
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
                for(int i = 0; i < (int)(sizeof(kSgKeyHash) / 32); ++i) if(!memcmp(h, kSgKeyHash[i], 32)) return i + 1;
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

    // 4.12.5: why 4.12.2 - 4.12.4 recorded fine: Window.addFlags() from this background thread sets the
    // flag in the window's own LayoutParams, then WindowManagerGlobal.updateViewLayout() -> View.requestLayout()
    // -> ViewRootImpl.checkThread() throws CalledFromWrongThreadException. ClearEx() hid it, the flag never
    // reached the window manager (the surface stayed recordable), and the next round saw the flag already in
    // the LayoutParams and counted the window as done. Now:
    //   1. the flag goes into the LayoutParams field directly (no dispatch, nothing throws), and
    //   2. it is pushed to the window manager through the window's ViewRootImpl.setLayoutParams(lp, false),
    //      which only schedules a traversal (thread-safe) - the UI thread then relayouts the window as secure;
    //   3. ScreenGuard::OnUiThread() (called from the game's touch hook) does the normal Window.setAttributes()
    //      when that hook really runs on the main thread, and marks SurfaceViews secure (older Androids).
    // A window counts as done only after one of these really went through, per ViewRootImpl (a re-created
    // window gets a new one and is secured again).
    std::vector<jweak> s_Done;
    bool IsDone(JNIEnv* env, jobject root)
    {
        for(size_t i = 0; i < s_Done.size(); )
        {
            if(env->IsSameObject(s_Done[i], nullptr)) { env->DeleteWeakGlobalRef(s_Done[i]); s_Done.erase(s_Done.begin() + i); continue; }
            if(env->IsSameObject(s_Done[i], root)) return true;
            ++i;
        }
        return false;
    }
    void MarkDone(JNIEnv* env, jobject root) { if(root && !IsDone(env, root)) s_Done.push_back(env->NewWeakGlobalRef(root)); }
    int s_nPushVia = 0;   // 1 = ViewRootImpl.setLayoutParams, 2 = Window.setAttributes on the UI thread

    // SurfaceView.setSecure(true) on every SurfaceView of the window (UI thread only)
    int SecureSurfaces(JNIEnv* env, jobject view, int depth)
    {
        if(!view || depth > 12) return 0;
        int n = 0;
        jclass svCls = env->FindClass("android/view/SurfaceView");
        jclass vgCls = env->FindClass("android/view/ViewGroup");
        if(ClearEx(env) || !svCls || !vgCls) return 0;
        if(env->IsInstanceOf(view, svCls))
        {
            jmethodID mSec = env->GetMethodID(svCls, "setSecure", "(Z)V");
            if(!ClearEx(env) && mSec) { env->CallVoidMethod(view, mSec, (jboolean)JNI_TRUE); if(!ClearEx(env)) ++n; }
        }
        else if(env->IsInstanceOf(view, vgCls))
        {
            jmethodID mCnt = env->GetMethodID(vgCls, "getChildCount", "()I");
            jmethodID mAt  = env->GetMethodID(vgCls, "getChildAt", "(I)Landroid/view/View;");
            if(!ClearEx(env) && mCnt && mAt)
            {
                const jint c = env->CallIntMethod(view, mCnt);
                for(jint i = 0; !ClearEx(env) && i < c && i < 64; ++i)
                {
                    jobject ch = env->CallObjectMethod(view, mAt, i);
                    if(ClearEx(env) || !ch) continue;
                    n += SecureSurfaces(env, ch, depth + 1);
                    env->DeleteLocalRef(ch);
                }
            }
        }
        env->DeleteLocalRef(svCls); env->DeleteLocalRef(vgCls);
        return n;
    }

    // FLAG_SECURE on one activity's window. Returns 1 when the window manager got it.
    int SecureActivity(JNIEnv* env, jobject act, bool uiThread)
    {
        if(!act) return 0;
        jclass actCls = env->FindClass("android/app/Activity");
        jclass winCls = env->FindClass("android/view/Window");
        jclass lpCls  = env->FindClass("android/view/WindowManager$LayoutParams");
        jclass viewCls = env->FindClass("android/view/View");
        if(ClearEx(env) || !actCls || !winCls || !lpCls || !viewCls) { ClearEx(env); return 0; }
        jmethodID mWin  = env->GetMethodID(actCls, "getWindow", "()Landroid/view/Window;");
        jmethodID mAtt  = env->GetMethodID(winCls, "getAttributes", "()Landroid/view/WindowManager$LayoutParams;");
        jmethodID mSetA = env->GetMethodID(winCls, "setAttributes", "(Landroid/view/WindowManager$LayoutParams;)V");
        jmethodID mPeek = env->GetMethodID(winCls, "peekDecorView", "()Landroid/view/View;");
        jmethodID mPar  = env->GetMethodID(viewCls, "getParent", "()Landroid/view/ViewParent;");
        jfieldID  fFl   = env->GetFieldID(lpCls, "flags", "I");
        int done = 0;
        if(!ClearEx(env) && mWin && mAtt && mSetA && mPeek && mPar && fFl)
        {
            jobject win = env->CallObjectMethod(act, mWin);
            jobject lp  = (!ClearEx(env) && win) ? env->CallObjectMethod(win, mAtt) : nullptr;
            jobject decor = (!ClearEx(env) && win) ? env->CallObjectMethod(win, mPeek) : nullptr;
            jobject root  = (!ClearEx(env) && decor) ? env->CallObjectMethod(decor, mPar) : nullptr;   // the ViewRootImpl
            ClearEx(env);
            if(lp && root && !IsDone(env, root))
            {
                // 1. the flag in the LayoutParams (a plain field write - no dispatch, no thread check)
                const jint fl = env->GetIntField(lp, fFl);
                if(!(fl & SG_FLAG_SECURE)) env->SetIntField(lp, fFl, fl | SG_FLAG_SECURE);
                ClearEx(env);
                if(uiThread)
                {   // 3. the normal way, on the main thread
                    env->CallVoidMethod(win, mSetA, lp);
                    if(!ClearEx(env)) { done = 1; s_nPushVia = 2; }
                    SecureSurfaces(env, decor, 0);
                }
                else
                {   // 2. ViewRootImpl.setLayoutParams(lp, false): copies the params, schedules a traversal
                    jclass rootCls = env->GetObjectClass(root);
                    jmethodID mSLP = rootCls ? env->GetMethodID(rootCls, "setLayoutParams", "(Landroid/view/WindowManager$LayoutParams;Z)V") : nullptr;
                    if(!ClearEx(env) && mSLP)
                    {
                        env->CallVoidMethod(root, mSLP, lp, (jboolean)JNI_FALSE);
                        if(!ClearEx(env)) { done = 1; s_nPushVia = 1; }
                    }
                    if(rootCls) env->DeleteLocalRef(rootCls);
                }
                if(done) MarkDone(env, root);
            }
            else if(root && IsDone(env, root))
            {
                done = 1;
                static int s_nSurfRuns = 0;   // the guard thread pushed it: still mark the SurfaceViews once from the UI thread
                if(uiThread && decor && s_nSurfRuns < 5) { ++s_nSurfRuns; SecureSurfaces(env, decor, 0); }
            }
            if(root) env->DeleteLocalRef(root);
            if(decor) env->DeleteLocalRef(decor);
            if(lp) env->DeleteLocalRef(lp);
            if(win) env->DeleteLocalRef(win);
        }
        ClearEx(env);
        env->DeleteLocalRef(actCls); env->DeleteLocalRef(winCls); env->DeleteLocalRef(lpCls); env->DeleteLocalRef(viewCls);
        return done;
    }

    // every activity of the game (ActivityThread.mActivities), like the original ScreenGuard
    int SecureAll(JNIEnv* env, bool uiThread)
    {
        int n = SecureActivity(env, (jobject)aml->GetCurrentActivity(), uiThread);
        jclass atCls = env->FindClass("android/app/ActivityThread");
        if(ClearEx(env) || !atCls) return n;
        jmethodID cur = env->GetStaticMethodID(atCls, "currentActivityThread", "()Landroid/app/ActivityThread;");
        jfieldID fActs = (!ClearEx(env) && cur) ? env->GetFieldID(atCls, "mActivities", "Landroid/util/ArrayMap;") : nullptr;
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
                    if(act) { n += SecureActivity(env, act, uiThread); env->DeleteLocalRef(act); }
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

    uint64_t s_nUiAfter = 0;   // 4.12.9: the UI thread starts securing the window after this (ms, monotonic)
    pthread_mutex_t s_Mx = PTHREAD_MUTEX_INITIALIZER;   // s_Done is used by the guard thread and the UI thread
    bool s_bLoggedOk = false;
    void LogOnce(int n)
    {
        if(n > 0 && !s_bLoggedOk) { s_bLoggedOk = true; logger->Info("ScreenGuard: screen recording blocked (%d window(s), via %s) - no valid src_Access.txt", n, s_nPushVia == 2 ? "UI thread" : "ViewRootImpl"); }
    }

    void* GuardThread(void*)
    {
        JavaVM* vm = aml->GetJavaVM();
        JNIEnv* env = nullptr;
        if(!vm || vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) { logger->Error("ScreenGuard: no JavaVM - inactive"); return nullptr; }
        int fails = 0;
        for(;;)
        {   // the game may re-create its window (resume, rotation): keep it secure
            env->PushLocalFrame(64);
            pthread_mutex_lock(&s_Mx);
            const int n = SecureAll(env, false);
            pthread_mutex_unlock(&s_Mx);
            env->PopLocalFrame(nullptr);
            LogOnce(n);
            if(n == 0 && ++fails == 20) logger->Error("ScreenGuard: the window could not be secured from the guard thread yet (waiting for the UI thread)");
            usleep(1000 * 1000);
        }
        return nullptr;
    }
}

void ScreenGuard::OnUiThread()
{
    if(!s_bLocked) return;
    if(gettid() != getpid()) return;   // only Android's main thread may touch the window
    static uint64_t s_nNext = 0;
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    const uint64_t now = (uint64_t)ts.tv_sec * 1000ull + ts.tv_nsec / 1000000;
    if(now < s_nNext || now < s_nUiAfter) return;
    s_nNext = now + 1000;
    JavaVM* vm = aml->GetJavaVM(); JNIEnv* env = nullptr;
    if(!vm || vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK || !env) return;
    if(env->PushLocalFrame(64) != 0) { ClearEx(env); return; }
    if(pthread_mutex_trylock(&s_Mx) == 0)
    {
        const int n = SecureAll(env, true);
        pthread_mutex_unlock(&s_Mx);
        LogOnce(n);
    }
    env->PopLocalFrame(nullptr);
    ClearEx(env);
}

void ScreenGuard::Init(const char* mpDir)
{
    char p[512];
    snprintf(p, sizeof(p), "%s/src_Access.txt", mpDir);
    s_nKey = CheckKeyFile(p);
    if(!s_nKey) { snprintf(p, sizeof(p), "%s/../src_Access.txt", mpDir); s_nKey = CheckKeyFile(p); }   // files/src_Access.txt too
    s_bLocked = s_nKey == 0;
    if(!s_bLocked) { logger->Info("ScreenGuard: src_Access.txt key #%04d - screen recording allowed", s_nKey); return; }
    // 4.12.9: no background guard thread any more. It called ViewRootImpl.setLayoutParams() on the game's
    // window from a non-UI thread every second from the first frame on (Android 14: a window relayout while the
    // game / PCMenu / TouchFixes set up their touch + menu state at the start - "the game closes by itself").
    // Only the UI thread secures the window now (Window.setAttributes, the normal Android way), and only once
    // the game has been running for a few seconds.
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    s_nUiAfter = (uint64_t)ts.tv_sec * 1000ull + ts.tv_nsec / 1000000 + 8000;
    (void)&GuardThread;
}
bool ScreenGuard::Locked() { return s_bLocked; }
int  ScreenGuard::KeyNumber() { return s_nKey; }
