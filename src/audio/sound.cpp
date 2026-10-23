// Plays MP sound effects (mp3) with OpenSL ES so we don't depend on the game's audio engine.
//
// 3.01 fix (crash in libstagefright_foundation.so, android::MediaBuffer::~MediaBuffer, SIGSEGV,
// Android 16): the mp3s used to be played by OpenSL "URI" players, which decode through Android's
// media framework (stagefright) on its own threads. Restarting a hit voice (STOPPED -> PLAYING) and
// destroying the "connected" player while it was still decoding freed media buffers that decoder
// thread was still using. Now every mp3 is decoded once by MP itself (minimp3, CC0) into PCM, and
// played by buffer-queue players: no media framework, no decoder threads, nothing is destroyed while
// playing. A replay is Clear() + Enqueue() of the same PCM, which is safe at any time.
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_STDIO
#include "../../third_party/minimp3/minimp3_ex.h"
#include "sound.h"

static SLObjectItf g_Engine = nullptr, g_Mix = nullptr;
static SLEngineItf g_EngineItf = nullptr;

static bool EnsureEngine()
{
    if(g_EngineItf) return true;
    if(slCreateEngine(&g_Engine, 0, nullptr, 0, nullptr, nullptr) != SL_RESULT_SUCCESS) { g_Engine = nullptr; return false; }
    if((*g_Engine)->Realize(g_Engine, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS) return false;
    if((*g_Engine)->GetInterface(g_Engine, SL_IID_ENGINE, &g_EngineItf) != SL_RESULT_SUCCESS) { g_EngineItf = nullptr; return false; }
    if((*g_EngineItf)->CreateOutputMix(g_EngineItf, &g_Mix, 0, nullptr, nullptr) != SL_RESULT_SUCCESS) { g_EngineItf = nullptr; return false; }
    if((*g_Mix)->Realize(g_Mix, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS) { g_EngineItf = nullptr; return false; }
    return true;
}

namespace
{
    // ---- decoded sounds (kept for the whole session: the players read straight from them) ----
    struct Pcm { std::string path; std::vector<int16_t> data; int ch = 0, hz = 0; bool ok = false; };
    std::vector<Pcm*> s_Pcm;

    bool DecodeInto(const char* path, Pcm* p);
    const Pcm* Decode(const char* path)
    {
        for(Pcm* p : s_Pcm) if(p->path == path) return p->ok ? p : nullptr;
        Pcm* p = new Pcm(); p->path = path; s_Pcm.push_back(p);
        return DecodeInto(path, p) ? p : nullptr;
    }
    bool DecodeInto(const char* path, Pcm* p)
    {
        FILE* f = fopen(path, "rb");
        if(!f) { logger->Error("Sound: %s not found", path); return false; }
        fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
        if(len <= 0 || len > 32 * 1024 * 1024) { fclose(f); logger->Error("Sound: %s has a bad size", path); return false; }
        std::vector<uint8_t> raw((size_t)len);
        size_t got = fread(raw.data(), 1, raw.size(), f); fclose(f);
        mp3dec_t dec; mp3dec_file_info_t info; memset(&info, 0, sizeof(info));
        if(mp3dec_load_buf(&dec, raw.data(), got, &info, nullptr, nullptr) || !info.buffer || !info.samples || info.channels < 1 || info.channels > 2 || info.hz < 8000)
        {
            if(info.buffer) free(info.buffer);
            logger->Error("Sound: could not decode %s", path);
            return false;
        }
        p->data.assign(info.buffer, info.buffer + info.samples);
        p->ch = info.channels; p->hz = info.hz; p->ok = true;
        free(info.buffer);
        logger->Info("Sound: %s decoded (%d Hz, %d ch, %.1f s)", path, p->hz, p->ch, (float)info.samples / p->ch / p->hz);
        return true;
    }

    // ---- buffer-queue voices -------------------------------------------------------------------
    struct Voice
    {
        SLObjectItf obj = nullptr; SLPlayItf play = nullptr; SLVolumeItf vol = nullptr;
        SLAndroidSimpleBufferQueueItf bq = nullptr; int ch = 0, hz = 0;
    };
    bool BuildVoice(Voice& v, int ch, int hz)
    {
        SLDataLocator_AndroidSimpleBufferQueue loc = { SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2 };
        SLDataFormat_PCM fmt = { SL_DATAFORMAT_PCM, (SLuint32)ch, (SLuint32)hz * 1000, SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16,
                                 ch == 2 ? (SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT) : SL_SPEAKER_FRONT_CENTER, SL_BYTEORDER_LITTLEENDIAN };
        SLDataSource src = { &loc, &fmt };
        SLDataLocator_OutputMix outMix = { SL_DATALOCATOR_OUTPUTMIX, g_Mix };
        SLDataSink sink = { &outMix, nullptr };
        const SLInterfaceID ids[2] = { SL_IID_ANDROIDSIMPLEBUFFERQUEUE, SL_IID_VOLUME };
        const SLboolean req[2] = { SL_BOOLEAN_TRUE, SL_BOOLEAN_FALSE };
        if((*g_EngineItf)->CreateAudioPlayer(g_EngineItf, &v.obj, &src, &sink, 2, ids, req) != SL_RESULT_SUCCESS) { v = Voice(); return false; }
        if((*v.obj)->Realize(v.obj, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS
        || (*v.obj)->GetInterface(v.obj, SL_IID_PLAY, &v.play) != SL_RESULT_SUCCESS
        || (*v.obj)->GetInterface(v.obj, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &v.bq) != SL_RESULT_SUCCESS)
        { (*v.obj)->Destroy(v.obj); v = Voice(); return false; }
        if((*v.obj)->GetInterface(v.obj, SL_IID_VOLUME, &v.vol) != SL_RESULT_SUCCESS) v.vol = nullptr;
        (*v.play)->SetPlayState(v.play, SL_PLAYSTATE_PLAYING);   // always "playing": an empty queue is silent
        v.ch = ch; v.hz = hz;
        return true;
    }
    bool PlayOn(Voice& v, const Pcm& p, float volume)
    {
        if(!v.obj || v.ch != p.ch || v.hz != p.hz)
        {
            if(v.obj) { (*v.obj)->Destroy(v.obj); v = Voice(); }   // never playing anything from a decoder: safe
            if(!BuildVoice(v, p.ch, p.hz)) return false;
        }
        if(v.vol) (*v.vol)->SetVolumeLevel(v.vol, (volume <= 0.01f) ? SL_MILLIBEL_MIN : (SLmillibel)(2000.0f * log10f(volume > 1.0f ? 1.0f : volume)));
        (*v.bq)->Clear(v.bq);
        return (*v.bq)->Enqueue(v.bq, p.data.data(), (SLuint32)(p.data.size() * sizeof(int16_t))) == SL_RESULT_SUCCESS;
    }

    Voice s_Fx;          // "connected" and other one-shot sounds
    Voice s_Hit[3];      // hit sound, round robin (a minigun's hits overlap)
    int   s_nNextHit = 0;
}

bool Sound::PlayFile(const char* path, float volume)
{
    if(!path || !*path) return false;
    if(!EnsureEngine()) { logger->Error("OpenSL ES init failed"); return false; }
    const Pcm* p = Decode(path);
    return p && PlayOn(s_Fx, *p, volume);
}

bool Sound::PreloadHit(const char* path)
{
    if(!path || !*path || !EnsureEngine()) return false;
    const Pcm* p = Decode(path);
    if(!p) return false;
    for(Voice& v : s_Hit) if(!v.obj || v.ch != p->ch || v.hz != p->hz) { if(v.obj) { (*v.obj)->Destroy(v.obj); v = Voice(); } BuildVoice(v, p->ch, p->hz); }
    return s_Hit[0].obj != nullptr;
}

bool Sound::PlayHit(const char* path, float volume)
{
    if(!PreloadHit(path)) return false;
    const Pcm* p = Decode(path);
    for(int tries = 0; tries < 3; ++tries)
    {
        Voice& v = s_Hit[s_nNextHit]; s_nNextHit = (s_nNextHit + 1) % 3;
        if(v.obj && PlayOn(v, *p, volume)) return true;
    }
    return false;
}

// ---- 3.04: looping music (Brief Case War: war_ambient.mp3, 45 %) ------------------------------
// The mp3 is decoded on its own thread (a 3-minute song takes a moment on a phone), then played by
// a buffer-queue voice whose "buffer done" callback queues the same PCM again = endless loop.
namespace
{
    struct Music
    {
        Voice v; Pcm* pcm = nullptr; std::string path;
        std::atomic<int> state{0};   // 0 idle, 1 decoding, 2 ready, 3 playing, -1 failed
        std::atomic<bool> loop{true}, want{false};
        float vol = 1.0f;
    };
    Music s_Music;
    void MusicDone(SLAndroidSimpleBufferQueueItf bq, void*)
    {
        if(s_Music.want && s_Music.loop && s_Music.pcm && s_Music.pcm->ok)
            (*bq)->Enqueue(bq, s_Music.pcm->data.data(), (SLuint32)(s_Music.pcm->data.size() * sizeof(int16_t)));
    }
    void SetVoiceVolume(Voice& v, float volume)
    {
        if(v.vol) (*v.vol)->SetVolumeLevel(v.vol, (volume <= 0.01f) ? SL_MILLIBEL_MIN : (SLmillibel)(2000.0f * log10f(volume > 1.0f ? 1.0f : volume)));
    }
}

bool Sound::PlayMusic(const char* path, float volume, bool loop)
{
    if(!path || !*path || !EnsureEngine()) return false;
    s_Music.vol = volume; s_Music.loop = loop; s_Music.want = true;
    if(s_Music.path == path && s_Music.state != -1)
    {
        if(s_Music.state == 3) SetVoiceVolume(s_Music.v, volume);
        return true;   // decoding / ready / already playing
    }
    if(s_Music.state == 1) return false;   // another song still decoding
    if(s_Music.v.obj) { (*s_Music.v.bq)->Clear(s_Music.v.bq); }
    s_Music.path = path;
    FILE* f = fopen(path, "rb");
    if(!f) { s_Music.state = -1; logger->Error("Music: %s not found (put war_ambient.mp3 in files/MP)", path); return false; }
    fclose(f);
    s_Music.state = 1;
    Pcm* old = s_Music.pcm; s_Music.pcm = nullptr;
    std::string p = path;
    std::thread([p, old]() {
        Pcm* n = new Pcm(); n->path = p;
        bool ok = DecodeInto(p.c_str(), n);
        s_Music.pcm = n;   // the old PCM is kept (a voice may still have read from it)
        (void)old;
        s_Music.state = ok ? 2 : -1;
    }).detach();
    return true;
}

void Sound::MusicUpdate()
{
    if(s_Music.state == 2 && s_Music.want)
    {
        Pcm* p = s_Music.pcm;
        if(!p || !p->ok) { s_Music.state = -1; return; }
        Voice& v = s_Music.v;
        if(!v.obj || v.ch != p->ch || v.hz != p->hz)
        {
            if(v.obj) { (*v.obj)->Destroy(v.obj); v = Voice(); }
            if(!BuildVoice(v, p->ch, p->hz)) { s_Music.state = -1; return; }
            (*v.bq)->RegisterCallback(v.bq, MusicDone, nullptr);
        }
        SetVoiceVolume(v, s_Music.vol);
        (*v.bq)->Clear(v.bq);
        (*v.bq)->Enqueue(v.bq, p->data.data(), (SLuint32)(p->data.size() * sizeof(int16_t)));
        s_Music.state = 3;
    }
}

void Sound::SetMusicVolume(float volume)
{
    s_Music.vol = volume;
    if(s_Music.state == 3) SetVoiceVolume(s_Music.v, volume);
}

void Sound::StopMusic()
{
    s_Music.want = false;
    if(s_Music.v.obj && s_Music.v.bq) (*s_Music.v.bq)->Clear(s_Music.v.bq);
    if(s_Music.state == 3) s_Music.state = 2;   // decoded PCM stays: the next PlayMusic of the same file is instant
}

bool Sound::MusicPlaying() { return s_Music.state == 3 && s_Music.want; }
