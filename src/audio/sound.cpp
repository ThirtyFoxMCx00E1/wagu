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
#include <mutex>
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_STDIO
#include "../../third_party/minimp3/minimp3_ex.h"
#include "sound.h"
#include <fcntl.h>
#include <unistd.h>
#include <SLES/OpenSLES_Android.h>

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
    Voice s_Ui[2];       // 3.29: click.mp3 / click2.mp3 / click3.mp3
    int   s_nNextUi = 0;
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


// ---- 3.63: streamed music (Day Z ambient, ~1 h mp3) --------------------------------------------
namespace
{
    enum { ST_SLOTS = 4 };
    struct Stream
    {
        Voice v; std::mutex mx;
        std::vector<int16_t> slot[ST_SLOTS]; int slotState[ST_SLOTS] = {0};   // 0 free, 1 ready, 2 queued
        int head = 0;                          // next slot to play
        std::atomic<int> gen{0};
        std::atomic<bool> eof{false}, active{false}, failed{false};
        std::atomic<long long> played{0};      // sample frames done
        int ch = 0, hz = 0; float vol = 1.0f, dur = 0.0f;
    };
    Stream s_St;
    bool StEnqueueNextLocked()
    {   // the slot at head (if ready) goes into the OpenSL queue
        for(int k = 0; k < ST_SLOTS; ++k)
        {
            int i = (s_St.head + k) % ST_SLOTS;
            if(s_St.slotState[i] == 2) continue;
            if(s_St.slotState[i] != 1) return false;
            if(!s_St.v.bq) return false;
            if((*s_St.v.bq)->Enqueue(s_St.v.bq, s_St.slot[i].data(), (SLuint32)(s_St.slot[i].size() * sizeof(int16_t))) != SL_RESULT_SUCCESS) return false;
            s_St.slotState[i] = 2;
            return true;
        }
        return false;
    }
    void StDone(SLAndroidSimpleBufferQueueItf, void*)
    {
        std::lock_guard<std::mutex> lk(s_St.mx);
        int i = s_St.head;
        if(s_St.slotState[i] == 2)
        {
            if(s_St.ch > 0) s_St.played += (long long)s_St.slot[i].size() / s_St.ch;
            s_St.slotState[i] = 0; s_St.head = (i + 1) % ST_SLOTS;
        }
        while(StEnqueueNextLocked()) {}
    }
    void StDecodeThread(std::string path, int myGen)
    {
        FILE* f = fopen(path.c_str(), "rb");
        if(!f) { s_St.failed = true; s_St.eof = true; return; }
        fseek(f, 0, SEEK_END); long fileLen = ftell(f); fseek(f, 0, SEEK_SET);
        long skip = 0;   // ID3v2 tag (cover art can be MBs)
        unsigned char h[10];
        if(fread(h, 1, 10, f) == 10 && h[0] == 'I' && h[1] == 'D' && h[2] == '3')
            skip = 10 + (((long)(h[6] & 0x7F) << 21) | ((long)(h[7] & 0x7F) << 14) | ((long)(h[8] & 0x7F) << 7) | (long)(h[9] & 0x7F));
        fseek(f, skip, SEEK_SET);
        mp3dec_t dec; mp3dec_init(&dec);   // per thread (an old thread may still be ending)
        std::vector<uint8_t> in(64 * 1024); size_t pos = 0, avail = 0; bool fileEof = false;
        mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
        std::vector<int16_t> block; bool first = true; int ch = 0, hz = 0;
        while(s_St.gen == myGen)
        {
            if(avail < 16 * 1024 && !fileEof)
            {
                if(pos) { memmove(in.data(), in.data() + pos, avail); pos = 0; }
                size_t got = fread(in.data() + avail, 1, in.size() - avail, f);
                if(got == 0) fileEof = true; avail += got;
            }
            if(avail == 0) break;
            mp3dec_frame_info_t info; memset(&info, 0, sizeof(info));
            int n = mp3dec_decode_frame(&dec, in.data() + pos, (int)avail, pcm, &info);
            if(info.frame_bytes <= 0) { if(fileEof) break; pos += avail > 1 ? 1 : avail; avail -= avail > 1 ? 1 : avail; continue; }
            pos += info.frame_bytes; avail -= info.frame_bytes;
            if(n <= 0) continue;
            if(first)
            {
                first = false; ch = info.channels; hz = info.hz;
                if(ch < 1 || ch > 2 || hz < 8000) { s_St.failed = true; break; }
                std::lock_guard<std::mutex> lk(s_St.mx);
                if(s_St.gen != myGen) break;
                s_St.ch = ch; s_St.hz = hz;
                s_St.dur = info.bitrate_kbps > 0 ? (float)(fileLen - skip) * 8.0f / (info.bitrate_kbps * 1000.0f) : 0.0f;
            }
            if(info.channels != ch || info.hz != hz) continue;   // odd frame: skip
            block.insert(block.end(), pcm, pcm + n * ch);
            if((int)block.size() >= hz * ch)   // ~1 s per slot
            {
                for(;;)
                {
                    if(s_St.gen != myGen) { fclose(f); return; }
                    {
                        std::lock_guard<std::mutex> lk(s_St.mx);
                        int freeSlot = -1;
                        for(int k = 0; k < ST_SLOTS; ++k) { int i = (s_St.head + k) % ST_SLOTS; if(s_St.slotState[i] == 0) { freeSlot = i; break; } }
                        if(freeSlot >= 0) { s_St.slot[freeSlot].swap(block); s_St.slotState[freeSlot] = 1; block.clear(); break; }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(40));
                }
            }
        }
        // the last part
        while(!block.empty() && s_St.gen == myGen)
        {
            {
                std::lock_guard<std::mutex> lk(s_St.mx);
                int freeSlot = -1;
                for(int k = 0; k < ST_SLOTS; ++k) { int i = (s_St.head + k) % ST_SLOTS; if(s_St.slotState[i] == 0) { freeSlot = i; break; } }
                if(freeSlot >= 0) { s_St.slot[freeSlot].swap(block); s_St.slotState[freeSlot] = 1; block.clear(); break; }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        fclose(f);
        if(s_St.gen == myGen) { if(first) s_St.failed = true; s_St.eof = true; }
    }
}

// 4.00.1: the Android media decoder (an OpenSL player on a file descriptor) for files minimp3 can't
// read: m4a / aac / ogg / opus / webm saved as ".mp3" (YouTube downloads), or odd mp3s. It streams
// from the file itself, any length.
namespace
{
    struct FdPlayer { SLObjectItf obj = nullptr; SLPlayItf play = nullptr; SLVolumeItf vol = nullptr; int fd = -1; };
    FdPlayer s_Fd;
    bool s_FdMode = false, s_FdTried = false;
    std::atomic<bool> s_FdEnd{false};
    std::string s_StPath;
    char s_StInfo[160] = "";
    void FdCb(SLPlayItf, void*, SLuint32 ev) { if(ev & SL_PLAYEVENT_HEADATEND) s_FdEnd = true; }
    void FdStop()
    {
        if(s_Fd.obj) (*s_Fd.obj)->Destroy(s_Fd.obj);
        if(s_Fd.fd >= 0) close(s_Fd.fd);
        s_Fd = FdPlayer(); s_FdMode = false; s_FdEnd = false;
    }
    void FdVolume(float volume)
    {
        if(s_Fd.vol) (*s_Fd.vol)->SetVolumeLevel(s_Fd.vol, (volume <= 0.01f) ? SL_MILLIBEL_MIN : (SLmillibel)(2000.0f * log10f(volume > 1.0f ? 1.0f : volume)));
    }
    bool FdStart(const std::string& path, float volume)
    {
        FdStop(); s_FdTried = true;
        int fd = open(path.c_str(), O_RDONLY);
        if(fd < 0) { snprintf(s_StInfo, sizeof(s_StInfo), "Android decoder: can't open the file"); return false; }
        SLDataLocator_AndroidFD loc = { SL_DATALOCATOR_ANDROIDFD, fd, 0, SL_DATALOCATOR_ANDROIDFD_USE_FILE_SIZE };
        SLDataFormat_MIME mime = { SL_DATAFORMAT_MIME, nullptr, SL_CONTAINERTYPE_UNSPECIFIED };
        SLDataSource src = { &loc, &mime };
        SLDataLocator_OutputMix outMix = { SL_DATALOCATOR_OUTPUTMIX, g_Mix };
        SLDataSink sink = { &outMix, nullptr };
        const SLInterfaceID ids[1] = { SL_IID_VOLUME };
        const SLboolean req[1] = { SL_BOOLEAN_FALSE };
        FdPlayer p; p.fd = fd;
        if((*g_EngineItf)->CreateAudioPlayer(g_EngineItf, &p.obj, &src, &sink, 1, ids, req) != SL_RESULT_SUCCESS)
        { close(fd); snprintf(s_StInfo, sizeof(s_StInfo), "Android decoder: format not supported"); return false; }
        if((*p.obj)->Realize(p.obj, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS || (*p.obj)->GetInterface(p.obj, SL_IID_PLAY, &p.play) != SL_RESULT_SUCCESS)
        { (*p.obj)->Destroy(p.obj); close(fd); snprintf(s_StInfo, sizeof(s_StInfo), "Android decoder: the file can't be decoded (not audio / damaged)"); return false; }
        if((*p.obj)->GetInterface(p.obj, SL_IID_VOLUME, &p.vol) != SL_RESULT_SUCCESS) p.vol = nullptr;
        s_Fd = p; s_FdMode = true; s_FdEnd = false;
        (*p.play)->RegisterCallback(p.play, FdCb, nullptr);
        (*p.play)->SetCallbackEventsMask(p.play, SL_PLAYEVENT_HEADATEND);
        FdVolume(volume);
        if((*p.play)->SetPlayState(p.play, SL_PLAYSTATE_PLAYING) != SL_RESULT_SUCCESS)
        { FdStop(); snprintf(s_StInfo, sizeof(s_StInfo), "Android decoder: could not start"); return false; }
        snprintf(s_StInfo, sizeof(s_StInfo), "playing with the Android decoder");
        return true;
    }
    // what is in the file? 1 = mp3, 2 = something else (m4a / ogg / webm / wav ...), 0 = unreadable / empty
    int Sniff(const char* path, char* what, size_t n)
    {
        FILE* f = fopen(path, "rb"); if(!f) { snprintf(what, n, "missing"); return 0; }
        unsigned char h[16] = {}; size_t got = fread(h, 1, sizeof(h), f); fclose(f);
        if(got < 12) { snprintf(what, n, "empty / too small"); return 0; }
        if(h[0] == 'I' && h[1] == 'D' && h[2] == '3') { snprintf(what, n, "mp3 (ID3)"); return 1; }
        if(h[0] == 0xFF && (h[1] & 0xE0) == 0xE0 && (h[1] & 0x06) != 0) { snprintf(what, n, (h[1] & 0x06) == 0x02 ? "mp3" : "aac / mp2 (adts)"); return (h[1] & 0x06) == 0x02 ? 1 : 2; }
        if(!memcmp(h + 4, "ftyp", 4)) { snprintf(what, n, "m4a / mp4 (AAC) renamed to .mp3"); return 2; }
        if(!memcmp(h, "OggS", 4)) { snprintf(what, n, "ogg / opus renamed to .mp3"); return 2; }
        if(h[0] == 0x1A && h[1] == 0x45 && h[2] == 0xDF && h[3] == 0xA3) { snprintf(what, n, "webm / opus renamed to .mp3"); return 2; }
        if(!memcmp(h, "RIFF", 4)) { snprintf(what, n, "wav"); return 2; }
        if(!memcmp(h, "fLaC", 4)) { snprintf(what, n, "flac"); return 2; }
        if(h[0] == '<' || (h[0] == 0xEF && h[1] == 0xBB)) { snprintf(what, n, "a web page (html), not audio - the download failed"); return 0; }
        snprintf(what, n, "unknown (%02X %02X %02X %02X)", h[0], h[1], h[2], h[3]); return 2;
    }
}

bool Sound::StreamPlay(const char* path, float volume)
{
    s_StInfo[0] = 0;
    if(!path || !*path || !EnsureEngine()) { snprintf(s_StInfo, sizeof(s_StInfo), "no audio engine"); return false; }
    char what[96]; int kind = Sniff(path, what, sizeof(what));
    StreamStop();
    s_StPath = path; s_FdTried = false;
    if(kind == 0) { snprintf(s_StInfo, sizeof(s_StInfo), "file: %s", what); return false; }
    if(kind == 2)
    {
        bool ok = FdStart(s_StPath, volume);
        char tmp[160]; snprintf(tmp, sizeof(tmp), "file: %s - %s", what, s_StInfo); snprintf(s_StInfo, sizeof(s_StInfo), "%s", tmp);
        return ok;
    }
    {
        std::lock_guard<std::mutex> lk(s_St.mx);
        int g = s_St.gen + 1; s_St.gen = g;
        for(int i = 0; i < ST_SLOTS; ++i) { s_St.slotState[i] = 0; s_St.slot[i].clear(); }
        s_St.head = 0; s_St.played = 0; s_St.eof = false; s_St.failed = false; s_St.active = true;
        s_St.ch = 0; s_St.hz = 0; s_St.dur = 0; s_St.vol = volume;
        std::thread(StDecodeThread, std::string(path), g).detach();
    }
    snprintf(s_StInfo, sizeof(s_StInfo), "file: %s - decoding", what);
    return true;
}

void Sound::StreamUpdate()
{
    if(s_FdMode) { if(s_FdEnd) { FdStop(); s_St.eof = true; s_St.failed = false; s_St.active = false; } return; }
    if(!s_St.active) return;
    std::unique_lock<std::mutex> lk(s_St.mx);
    if(s_St.failed)
    {
        s_St.active = false;
        if(s_St.played == 0 && !s_FdTried && !s_StPath.empty())
        {   // minimp3 couldn't read it: the Android decoder plays (almost) everything
            float vol = s_St.vol; lk.unlock();
            if(FdStart(s_StPath, vol)) { char tmp[160]; snprintf(tmp, sizeof(tmp), "mp3 decoder failed - %s", s_StInfo); snprintf(s_StInfo, sizeof(s_StInfo), "%s", tmp); }
        }
        return;
    }
    if(s_St.ch <= 0) return;
    Voice& v = s_St.v;
    if(!v.obj || v.ch != s_St.ch || v.hz != s_St.hz)
    {
        bool anyQueued = false; for(int i = 0; i < ST_SLOTS; ++i) if(s_St.slotState[i] == 2) anyQueued = true;
        if(anyQueued) return;
        if(v.obj) { (*v.obj)->Destroy(v.obj); v = Voice(); }   // nothing queued: safe
        if(!BuildVoice(v, s_St.ch, s_St.hz)) { s_St.failed = true; return; }   // (the Android decoder next tick)
        (*v.bq)->RegisterCallback(v.bq, StDone, nullptr);
        snprintf(s_StInfo, sizeof(s_StInfo), "playing mp3 (%d Hz, %d ch)", s_St.hz, s_St.ch);
    }
    SetVoiceVolume(v, s_St.vol);
    while(StEnqueueNextLocked()) {}
    if(s_St.eof) { bool any = false; for(int i = 0; i < ST_SLOTS; ++i) if(s_St.slotState[i]) any = true; if(!any) s_St.active = false; }
}

const char* Sound::StreamInfo() { return s_StInfo; }

void Sound::StreamSetVolume(float volume)
{
    if(s_FdMode) { FdVolume(volume); return; }
    std::lock_guard<std::mutex> lk(s_St.mx);
    s_St.vol = volume;
    if(s_St.v.obj) SetVoiceVolume(s_St.v, volume);
}

void Sound::StreamStop()
{
    FdStop();
    std::lock_guard<std::mutex> lk(s_St.mx);
    s_St.gen = s_St.gen + 1;   // the decoder thread ends itself
    if(s_St.v.obj && s_St.v.bq) (*s_St.v.bq)->Clear(s_St.v.bq);
    for(int i = 0; i < ST_SLOTS; ++i) s_St.slotState[i] = 0;
    s_St.head = 0; s_St.active = false;
}

bool  Sound::StreamActive() { return s_FdMode || s_St.active; }
bool  Sound::StreamEnded() { return !s_FdMode && !s_St.active && s_St.eof && !s_St.failed; }
float Sound::StreamPosition()
{
    if(s_FdMode) { SLmillisecond ms = 0; if(s_Fd.play && (*s_Fd.play)->GetPosition(s_Fd.play, &ms) == SL_RESULT_SUCCESS) return ms / 1000.0f; return 0.0f; }
    return s_St.hz > 0 ? (float)((double)s_St.played / s_St.hz) : 0.0f;
}
float Sound::StreamDuration()
{
    if(s_FdMode) { SLmillisecond ms = 0; if(s_Fd.play && (*s_Fd.play)->GetDuration(s_Fd.play, &ms) == SL_RESULT_SUCCESS && ms != SL_TIME_UNKNOWN) return ms / 1000.0f; return 0.0f; }
    return s_St.dur;
}

void* Sound::EngineItf() { return EnsureEngine() ? (void*)g_EngineItf : nullptr; }   // 3.11
void* Sound::OutputMix() { return EnsureEngine() ? (void*)g_Mix : nullptr; }

// 3.29: UI clicks - a missing file is decoded (tried) once and then just stays silent
bool Sound::PlayUi(const char* path, float volume)
{
    if(!path || !*path || !EnsureEngine()) return false;
    const Pcm* p = Decode(path);
    if(!p) return false;
    Voice& v = s_Ui[s_nNextUi]; s_nNextUi = (s_nNextUi + 1) % 2;
    return PlayOn(v, *p, volume);
}
