// 3.11: voice chat - see voice.h
#ifndef MP_VOICE_HOSTTEST
#include <mod/amlmod.h>
#include <mod/logger.h>
#else
#include "voice_hoststub.h"   // test/voice_test.cpp: no AML / logcat on the PC
#endif
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <SLES/OpenSLES_AndroidConfiguration.h>
#include <jni.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <mutex>
#include <atomic>
#include "opus.h"
#include "voice.h"
#include "sound.h"
#include "net/protocol.h"
#include "net/server.h"   // MP_NowMs

#define V_RATE       48000
#define V_FRAME      960                  // 20 ms at 48 kHz
#define V_RING       16                   // jitter buffer slots per speaker (320 ms)
#define V_START      2                    // start playing once 2 frames (40 ms) are buffered...
#define V_START_MS   40                   // ...or the first one is 40 ms old
#define V_MAXQ       5                    // more than 100 ms queued: skip ahead (delay never grows)
#define V_HANG       15                   // keep sending 300 ms after speech
#define V_SPEAK_MS   300

namespace
{
    Voice::SendFn s_Send = nullptr;
    bool s_bInit = false, s_bOk = false;
    char s_szErr[128] = "";

    SLEngineItf s_Eng = nullptr;
    SLObjectItf s_Mix = nullptr;

    // ---- playback ----
    SLObjectItf s_PlayObj = nullptr;
    SLPlayItf   s_Play = nullptr;
    SLAndroidSimpleBufferQueueItf s_PlayQ = nullptr;
    int16_t     s_PlayBuf[3][V_FRAME];
    int         s_nPlayBuf = 0;
    std::atomic<float> s_fVolume{1.0f};

    struct Frame { uint16_t seq; bool has; uint16_t len; uint8_t data[MP_VOICE_MAX_BYTES]; };
    struct Stream
    {
        OpusDecoder* dec = nullptr;
        Frame    ring[V_RING];
        bool     playing = false, any = false;
        uint16_t playSeq = 0, newest = 0;
        uint64_t firstAt = 0, lastRx = 0;
        int      misses = 0;
    };
    Stream* s_Streams[MP_MAX_PLAYERS] = {};
    std::atomic<float> s_fGain[MP_MAX_PLAYERS];
    std::atomic<uint64_t> s_nLastVoice[MP_MAX_PLAYERS];
    std::mutex s_StreamM;

    // ---- capture ----
    SLObjectItf s_RecObj = nullptr;
    SLRecordItf s_Rec = nullptr;
    SLAndroidSimpleBufferQueueItf s_RecQ = nullptr;
    int16_t     s_RecBuf[2][V_FRAME];
    int         s_nRecBuf = 0, s_nRecRate = V_RATE, s_nRecFrame = V_FRAME;
    OpusEncoder* s_Enc = nullptr;
    std::atomic<bool> s_bMic{false}, s_bTalking{false};
    std::atomic<float> s_fLevel{0}, s_fMicGain{1.0f};
    uint8_t     s_nMyId = 0;
    uint16_t    s_nSeq = 0;
    int         s_nHang = 0;
    float       s_fNoise = 200.0f;
    uint8_t     s_Prev[sizeof(PktVoiceHdr) + MP_VOICE_MAX_BYTES]; int s_nPrev = 0;   // pre-roll (1 frame)
    std::mutex  s_RecM;

    // 3.11: voice pitch (what the others hear). Two-tap delay-line pitch shifter with cross-fade:
    // works sample by sample, adds at most 30 ms, no FFT. ratio 1 = off.
    std::atomic<float> s_fPitch{1.0f};
    std::atomic<bool>  s_bMonitor{false};
    struct PitchShifter
    {
        float buf[4096] = {0}; int w = 0; float phase = 0;
        float Read(float d) const
        {
            float pos = (float)w - d; while(pos < 0) pos += 4096.0f;
            int i0 = (int)pos; float fr = pos - i0; int i1 = (i0 + 1) & 4095;
            return buf[i0 & 4095] * (1 - fr) + buf[i1] * fr;
        }
        void Process(int16_t* x, int n, float ratio, int rate)
        {
            const float W = rate * 0.030f;   // 30 ms window
            for(int i = 0; i < n; ++i)
            {
                buf[w] = x[i];
                phase += 1.0f - ratio; if(phase < 0) phase += W; if(phase >= W) phase -= W;
                float d1 = phase + 1.0f, d2 = phase + W * 0.5f; if(d2 >= W) d2 -= W; d2 += 1.0f;
                float g1 = 1.0f - fabsf(2.0f * phase / W - 1.0f), g2 = 1.0f - g1;
                float y = Read(d1) * g1 + Read(d2) * g2;
                w = (w + 1) & 4095;
                x[i] = y > 32767.f ? 32767 : (y < -32768.f ? -32768 : (int16_t)y);
            }
        }
    } s_Pitch;
}
void Voice::SetMonitor(bool on) { s_bMonitor = on; }
bool Voice::Monitor() { return s_bMonitor; }
void Voice::SetPitch(float r) { s_fPitch = r < 0.6f ? 0.6f : (r > 1.6f ? 1.6f : r); }
float Voice::Pitch() { return s_fPitch.load(); }

const char* Voice::LastError() { return s_szErr; }
bool Voice::Available() { return s_bOk; }

static inline int16_t Clip(float v) { return v > 32767.f ? 32767 : (v < -32768.f ? -32768 : (int16_t)v); }

// ---- playback (audio thread) ----------------------------------------------------------------
static void MixFrame(int16_t* out)
{
    static float acc[V_FRAME];
    static int16_t pcm[V_FRAME];
    memset(acc, 0, sizeof(acc));
    uint64_t now = MP_NowMs();
    float master = s_fVolume.load();
    std::lock_guard<std::mutex> lk(s_StreamM);
    for(int id = 0; id < MP_MAX_PLAYERS; ++id)
    {
        Stream* st = s_Streams[id];
        if(!st || !st->any) continue;
        if(!st->playing)
        {
            int ahead = (int16_t)(st->newest - st->playSeq);
            if(ahead + 1 < V_START && now - st->firstAt < V_START_MS) continue;   // still buffering
            st->playing = true; st->misses = 0;
        }
        // never let the delay grow: too much queued (network burst) -> skip the oldest frames
        while((int16_t)(st->newest - st->playSeq) >= V_MAXQ) { st->ring[st->playSeq % V_RING].has = false; ++st->playSeq; }
        Frame& f = st->ring[st->playSeq % V_RING];
        int n = 0;
        if(f.has && f.seq == st->playSeq) { n = opus_decode(st->dec, f.data, f.len, pcm, V_FRAME, 0); f.has = false; st->misses = 0; }
        else
        {
            Frame& nx = st->ring[(uint16_t)(st->playSeq + 1) % V_RING];
            if(nx.has && nx.seq == (uint16_t)(st->playSeq + 1)) n = opus_decode(st->dec, nx.data, nx.len, pcm, V_FRAME, 1);   // FEC from the next frame
            else n = opus_decode(st->dec, nullptr, 0, pcm, V_FRAME, 0);                                                  // concealment
            ++st->misses;
        }
        ++st->playSeq;
        if(st->misses >= 5 && (int16_t)(st->newest - st->playSeq) < 0)   // 100 ms nothing: the talk spurt ended
        { st->playing = false; st->any = false; opus_decoder_ctl(st->dec, OPUS_RESET_STATE); continue; }
        float g = s_fGain[id].load() * master;
        if(n <= 0 || g <= 0.001f) continue;
        for(int i = 0; i < n && i < V_FRAME; ++i) acc[i] += pcm[i] * g;
    }
    for(int i = 0; i < V_FRAME; ++i) out[i] = Clip(acc[i]);
}
static void PlayCb(SLAndroidSimpleBufferQueueItf q, void*)
{
    int16_t* b = s_PlayBuf[s_nPlayBuf]; s_nPlayBuf = (s_nPlayBuf + 1) % 3;
    MixFrame(b);
    (*q)->Enqueue(q, b, sizeof(s_PlayBuf[0]));
}

static bool CreatePlayer()
{
    if(s_PlayObj) return true;
    SLDataLocator_AndroidSimpleBufferQueue loc = { SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 3 };
    SLDataFormat_PCM fmt = { SL_DATAFORMAT_PCM, 1, SL_SAMPLINGRATE_48, SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16, SL_SPEAKER_FRONT_CENTER, SL_BYTEORDER_LITTLEENDIAN };
    SLDataSource src = { &loc, &fmt };
    SLDataLocator_OutputMix om = { SL_DATALOCATOR_OUTPUTMIX, s_Mix };
    SLDataSink snk = { &om, nullptr };
    const SLInterfaceID ids[1] = { SL_IID_ANDROIDSIMPLEBUFFERQUEUE };
    const SLboolean req[1] = { SL_BOOLEAN_TRUE };
    if((*s_Eng)->CreateAudioPlayer(s_Eng, &s_PlayObj, &src, &snk, 1, ids, req) != SL_RESULT_SUCCESS) { s_PlayObj = nullptr; return false; }
    if((*s_PlayObj)->Realize(s_PlayObj, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS ||
       (*s_PlayObj)->GetInterface(s_PlayObj, SL_IID_PLAY, &s_Play) != SL_RESULT_SUCCESS ||
       (*s_PlayObj)->GetInterface(s_PlayObj, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &s_PlayQ) != SL_RESULT_SUCCESS)
    { (*s_PlayObj)->Destroy(s_PlayObj); s_PlayObj = nullptr; return false; }
    (*s_PlayQ)->RegisterCallback(s_PlayQ, PlayCb, nullptr);
    return true;
}

void Voice::SetOutput(bool on)
{
    if(!s_bOk) return;
    static bool running = false;
    if(on == running) return;
    if(on)
    {
        if(!CreatePlayer()) { snprintf(s_szErr, sizeof(s_szErr), "voice player could not be created"); return; }
        memset(s_PlayBuf, 0, sizeof(s_PlayBuf));
        (*s_PlayQ)->Clear(s_PlayQ);
        (*s_PlayQ)->Enqueue(s_PlayQ, s_PlayBuf[0], sizeof(s_PlayBuf[0]));   // 2 x 20 ms in flight = 40 ms output latency
        (*s_PlayQ)->Enqueue(s_PlayQ, s_PlayBuf[1], sizeof(s_PlayBuf[1]));
        s_nPlayBuf = 2;
        (*s_Play)->SetPlayState(s_Play, SL_PLAYSTATE_PLAYING);
    }
    else
    {
        (*s_Play)->SetPlayState(s_Play, SL_PLAYSTATE_STOPPED);
        (*s_PlayQ)->Clear(s_PlayQ);
        ResetAll();
    }
    running = on;
}

void Voice::SetVolume(float v) { s_fVolume = v < 0 ? 0 : (v > 2 ? 2 : v); }
void Voice::SetMicGain(float g) { s_fMicGain = g < 0.5f ? 0.5f : (g > 3 ? 3 : g); }
void Voice::SetGain(int id, float g) { if(id >= 0 && id < MP_MAX_PLAYERS) s_fGain[id] = g < 0 ? 0 : g; }
bool Voice::IsSpeaking(int id) { return id >= 0 && id < MP_MAX_PLAYERS && MP_NowMs() - s_nLastVoice[id].load() < V_SPEAK_MS; }

void Voice::OnPacket(int id, uint16_t seq, uint8_t flags, const uint8_t* data, int len)
{
    if(!s_bOk || id < 0 || id >= MP_MAX_PLAYERS || len <= 0 || len > MP_VOICE_MAX_BYTES) return;
    std::lock_guard<std::mutex> lk(s_StreamM);
    Stream*& st = s_Streams[id];
    if(!st)
    {
        st = new Stream();
        int err = 0; st->dec = opus_decoder_create(V_RATE, 1, &err);
        if(!st->dec) { delete st; st = nullptr; return; }
        memset(st->ring, 0, sizeof(st->ring));
    }
    uint64_t now = MP_NowMs();
    if(!st->any || (int16_t)(seq - st->newest) > V_RING || (int16_t)(seq - st->playSeq) < -V_RING)   // new talk spurt / resync
    {
        for(auto& f : st->ring) f.has = false;
        st->any = true; st->playing = false; st->playSeq = seq; st->newest = seq; st->firstAt = now; st->misses = 0;
    }
    if((int16_t)(seq - st->playSeq) < 0) return;   // too late, already played (or concealed)
    Frame& f = st->ring[seq % V_RING];
    f.seq = seq; f.has = true; f.len = (uint16_t)len; memcpy(f.data, data, len);
    if((int16_t)(seq - st->newest) > 0) st->newest = seq;
    st->lastRx = now;
    if(flags & VOICE_END) s_nLastVoice[id] = 0;   // stopped talking: the icon goes away right now
    else if(len > 2) s_nLastVoice[id] = now;      // talking: icon on (gone 300 ms after the last frame if the end packet is lost)
}

void Voice::ResetSpeaker(int id)
{
    if(id < 0 || id >= MP_MAX_PLAYERS) return;
    std::lock_guard<std::mutex> lk(s_StreamM);
    if(Stream* st = s_Streams[id]) { st->any = false; st->playing = false; for(auto& f : st->ring) f.has = false; if(st->dec) opus_decoder_ctl(st->dec, OPUS_RESET_STATE); }
    s_nLastVoice[id] = 0;
}
void Voice::ResetAll() { for(int i = 0; i < MP_MAX_PLAYERS; ++i) ResetSpeaker(i); }

// ---- capture (audio thread) -----------------------------------------------------------------
static void SendFrame(const int16_t* pcm, int n)
{
    // energy gate with a slowly adapting noise floor + hang time
    double sum = 0; for(int i = 0; i < n; ++i) sum += (double)pcm[i] * pcm[i];
    float rms = sqrtf((float)(sum / (n ? n : 1)));
    s_fLevel = rms / 6000.0f > 1.0f ? 1.0f : rms / 6000.0f;
    if(rms < s_fNoise) s_fNoise = s_fNoise * 0.9f + rms * 0.1f; else s_fNoise = s_fNoise * 0.998f + rms * 0.002f;
    if(s_fNoise < 60.0f) s_fNoise = 60.0f;
    bool speech = rms > s_fNoise * 2.8f && rms > 220.0f;
    bool wasActive = s_nHang > 0;
    if(speech) s_nHang = V_HANG; else if(s_nHang > 0) --s_nHang;

    uint8_t pkt[sizeof(PktVoiceHdr) + MP_VOICE_MAX_BYTES];
    PktVoiceHdr* h = (PktVoiceHdr*)pkt;
    int nb = opus_encode(s_Enc, pcm, n, pkt + sizeof(PktVoiceHdr), MP_VOICE_MAX_BYTES);   // always encode: the encoder keeps its state
    uint16_t seq = s_nSeq++;
    if(nb <= 0) return;
    h->type = PKT_VOICE; h->id = s_nMyId; h->seq = seq; h->flags = (s_nHang == 0 && wasActive) ? VOICE_END : 0; h->pad = 0; h->len = (uint16_t)nb;
    int total = (int)sizeof(PktVoiceHdr) + nb;
    bool send = s_nHang > 0 || wasActive;
    s_bTalking = s_nHang > 0;
    if(send && s_Send)
    {
        if(!wasActive && s_nPrev) s_Send(s_Prev, s_nPrev);   // the frame just before speech started (first syllable)
        s_Send(pkt, total);
    }
    if(send && s_bMonitor) Voice::OnPacket(s_nMyId, seq, h->flags, pkt + sizeof(PktVoiceHdr), nb);   // "hear myself" test (pitch preview)
    memcpy(s_Prev, pkt, total); s_nPrev = total;
}
static void RecCb(SLAndroidSimpleBufferQueueItf q, void*)
{
    int16_t* b = s_RecBuf[s_nRecBuf];
    if(s_bMic)
    {
        float g = s_fMicGain.load();
        if(g != 1.0f) for(int i = 0; i < s_nRecFrame; ++i) b[i] = Clip(b[i] * g);
        float pr = s_fPitch.load();
        if(fabsf(pr - 1.0f) > 0.01f) s_Pitch.Process(b, s_nRecFrame, pr, s_nRecRate);   // 3.11: voice pitch
        std::lock_guard<std::mutex> lk(s_RecM);
        if(s_Enc) SendFrame(b, s_nRecFrame);
    }
    (*q)->Enqueue(q, b, s_nRecFrame * sizeof(int16_t));
    s_nRecBuf ^= 1;
}

static bool CreateRecorder(int rate)
{
    SLDataLocator_IODevice dev = { SL_DATALOCATOR_IODEVICE, SL_IODEVICE_AUDIOINPUT, SL_DEFAULTDEVICEID_AUDIOINPUT, nullptr };
    SLDataSource src = { &dev, nullptr };
    SLDataLocator_AndroidSimpleBufferQueue loc = { SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2 };
    SLDataFormat_PCM fmt = { SL_DATAFORMAT_PCM, 1, (SLuint32)rate * 1000, SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16, SL_SPEAKER_FRONT_CENTER, SL_BYTEORDER_LITTLEENDIAN };
    SLDataSink snk = { &loc, &fmt };
    const SLInterfaceID ids[2] = { SL_IID_ANDROIDSIMPLEBUFFERQUEUE, SL_IID_ANDROIDCONFIGURATION };
    const SLboolean req[2] = { SL_BOOLEAN_TRUE, SL_BOOLEAN_FALSE };
    if((*s_Eng)->CreateAudioRecorder(s_Eng, &s_RecObj, &src, &snk, 2, ids, req) != SL_RESULT_SUCCESS) { s_RecObj = nullptr; return false; }
    SLAndroidConfigurationItf cfg = nullptr;
    if((*s_RecObj)->GetInterface(s_RecObj, SL_IID_ANDROIDCONFIGURATION, &cfg) == SL_RESULT_SUCCESS && cfg)
    {
        SLuint32 preset = SL_ANDROID_RECORDING_PRESET_VOICE_COMMUNICATION;   // echo cancel + noise suppression (game sound is not sent back)
        (*cfg)->SetConfiguration(cfg, SL_ANDROID_KEY_RECORDING_PRESET, &preset, sizeof(preset));
    }
    if((*s_RecObj)->Realize(s_RecObj, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS ||
       (*s_RecObj)->GetInterface(s_RecObj, SL_IID_RECORD, &s_Rec) != SL_RESULT_SUCCESS ||
       (*s_RecObj)->GetInterface(s_RecObj, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &s_RecQ) != SL_RESULT_SUCCESS)
    { (*s_RecObj)->Destroy(s_RecObj); s_RecObj = nullptr; return false; }
    s_nRecRate = rate; s_nRecFrame = rate / 50;
    (*s_RecQ)->RegisterCallback(s_RecQ, RecCb, nullptr);
    return true;
}

bool Voice::StartMic(uint8_t myId)
{
    if(!s_bOk) return false;
    if(s_bMic) return true;
    if(MicPermission() != PERM_GRANTED) { snprintf(s_szErr, sizeof(s_szErr), "no microphone permission"); return false; }
    if(!s_RecObj && !CreateRecorder(48000) && !CreateRecorder(16000))
    { snprintf(s_szErr, sizeof(s_szErr), "the microphone could not be opened"); return false; }
    {
        std::lock_guard<std::mutex> lk(s_RecM);
        if(s_Enc) { opus_encoder_destroy(s_Enc); s_Enc = nullptr; }
        int err = 0;
        s_Enc = opus_encoder_create(s_nRecRate, 1, OPUS_APPLICATION_VOIP, &err);
        if(!s_Enc) { snprintf(s_szErr, sizeof(s_szErr), "Opus encoder error %d", err); return false; }
        opus_encoder_ctl(s_Enc, OPUS_SET_BITRATE(24000));
        opus_encoder_ctl(s_Enc, OPUS_SET_COMPLEXITY(5));
        opus_encoder_ctl(s_Enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
        opus_encoder_ctl(s_Enc, OPUS_SET_INBAND_FEC(1));         // a lost frame is rebuilt from the next one
        opus_encoder_ctl(s_Enc, OPUS_SET_PACKET_LOSS_PERC(10));
        s_nMyId = myId; s_nHang = 0; s_nPrev = 0; s_fNoise = 200.0f;
    }
    s_nRecBuf = 0;
    (*s_RecQ)->Clear(s_RecQ);
    (*s_RecQ)->Enqueue(s_RecQ, s_RecBuf[0], s_nRecFrame * sizeof(int16_t));
    (*s_RecQ)->Enqueue(s_RecQ, s_RecBuf[1], s_nRecFrame * sizeof(int16_t));
    s_bMic = true;
    if((*s_Rec)->SetRecordState(s_Rec, SL_RECORDSTATE_RECORDING) != SL_RESULT_SUCCESS)
    { s_bMic = false; snprintf(s_szErr, sizeof(s_szErr), "the microphone is busy"); return false; }
    logger->Info("Voice: mic on (%d Hz)", s_nRecRate);
    return true;
}
void Voice::StopMic()
{
    if(!s_bMic) return;
    s_bMic = false; s_bTalking = false; s_fLevel = 0;
    if(s_Rec) (*s_Rec)->SetRecordState(s_Rec, SL_RECORDSTATE_STOPPED);
    if(s_RecQ) (*s_RecQ)->Clear(s_RecQ);
    // the recorder is destroyed so Android's "microphone in use" dot goes away
    std::lock_guard<std::mutex> lk(s_RecM);
    if(s_RecObj) { (*s_RecObj)->Destroy(s_RecObj); s_RecObj = nullptr; s_Rec = nullptr; s_RecQ = nullptr; }
    logger->Info("Voice: mic off");
}
bool  Voice::MicOn() { return s_bMic; }
bool  Voice::MicTalking() { return s_bMic && s_bTalking; }
float Voice::MicLevel() { return s_bMic ? s_fLevel.load() : 0.0f; }

// ---- RECORD_AUDIO permission (JNI) --------------------------------------------------------
static JNIEnv* Env()
{
    JavaVM* vm = aml->GetJavaVM();
    if(!vm) return nullptr;
    JNIEnv* env = nullptr;
    if(vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK && env) return env;
    if(vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
    return env;
}
static bool ClearEx(JNIEnv* e) { if(e->ExceptionCheck()) { e->ExceptionClear(); return true; } return false; }

int Voice::MicPermission()
{
    JNIEnv* e = Env();
    jobject ctx = aml->GetCurrentActivity();
    if(!ctx) ctx = aml->GetAppContextObject();
    if(!e || !ctx) return PERM_UNKNOWN;
    jstring perm = e->NewStringUTF("android.permission.RECORD_AUDIO");
    jclass cc = e->GetObjectClass(ctx);
    int result = PERM_UNKNOWN;
    jmethodID chk = e->GetMethodID(cc, "checkSelfPermission", "(Ljava/lang/String;)I");   // API 23+
    if(ClearEx(e) || !chk)
    {
        // Android 5 / 6.0 without runtime permissions: granted at install if the APK asks for it
        jmethodID cp = e->GetMethodID(cc, "checkCallingOrSelfPermission", "(Ljava/lang/String;)I");
        if(!ClearEx(e) && cp) { jint r = e->CallIntMethod(ctx, cp, perm); if(!ClearEx(e)) result = r == 0 ? PERM_GRANTED : PERM_NOT_IN_APK; }
    }
    else
    {
        jint r = e->CallIntMethod(ctx, chk, perm);
        if(!ClearEx(e)) result = r == 0 ? PERM_GRANTED : PERM_ASK;
    }
    if(result == PERM_ASK)
    {
        // is RECORD_AUDIO in the APK's AndroidManifest.xml at all? (otherwise Android never asks)
        bool declared = false;
        jmethodID gpm = e->GetMethodID(cc, "getPackageManager", "()Landroid/content/pm/PackageManager;");
        jmethodID gpn = e->GetMethodID(cc, "getPackageName", "()Ljava/lang/String;");
        if(!ClearEx(e) && gpm && gpn)
        {
            jobject pm = e->CallObjectMethod(ctx, gpm); jobject pn = e->CallObjectMethod(ctx, gpn);
            if(!ClearEx(e) && pm && pn)
            {
                jclass pmc = e->GetObjectClass(pm);
                jmethodID gpi = e->GetMethodID(pmc, "getPackageInfo", "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;");
                jobject pi = (!ClearEx(e) && gpi) ? e->CallObjectMethod(pm, gpi, pn, 0x1000 /* GET_PERMISSIONS */) : nullptr;
                if(!ClearEx(e) && pi)
                {
                    jclass pic = e->GetObjectClass(pi);
                    jfieldID rp = e->GetFieldID(pic, "requestedPermissions", "[Ljava/lang/String;");
                    jobjectArray arr = (!ClearEx(e) && rp) ? (jobjectArray)e->GetObjectField(pi, rp) : nullptr;
                    if(!ClearEx(e) && arr)
                    {
                        jsize n = e->GetArrayLength(arr);
                        for(jsize i = 0; i < n && !declared; ++i)
                        {
                            jstring s = (jstring)e->GetObjectArrayElement(arr, i);
                            if(!s) continue;
                            const char* c = e->GetStringUTFChars(s, nullptr);
                            if(c) { declared = !strcmp(c, "android.permission.RECORD_AUDIO"); e->ReleaseStringUTFChars(s, c); }
                            e->DeleteLocalRef(s);
                        }
                    }
                }
            }
        }
        if(!declared) result = PERM_NOT_IN_APK;
    }
    e->DeleteLocalRef(perm);
    return result;
}

static void RequestOnUi(void*)
{
    JNIEnv* e = Env();
    jobject act = aml->GetCurrentActivity();
    if(!e || !act) return;
    jclass ac = e->GetObjectClass(act);
    jmethodID rq = e->GetMethodID(ac, "requestPermissions", "([Ljava/lang/String;I)V");
    if(ClearEx(e) || !rq) return;
    jclass sc = e->FindClass("java/lang/String");
    jobjectArray arr = e->NewObjectArray(1, sc, e->NewStringUTF("android.permission.RECORD_AUDIO"));
    e->CallVoidMethod(act, rq, arr, 3110);
    ClearEx(e);
}
void Voice::RequestMicPermission()
{
    if(!aml->PushToJavaUIThread(RequestOnUi, nullptr)) RequestOnUi(nullptr);
}

bool Voice::Init(SendFn send)
{
    s_Send = send;
    if(s_bInit) return s_bOk;
    s_bInit = true;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) { s_fGain[i] = 1.0f; s_nLastVoice[i] = 0; }
    s_Eng = (SLEngineItf)Sound::EngineItf();
    s_Mix = (SLObjectItf)Sound::OutputMix();
    if(!s_Eng || !s_Mix) { snprintf(s_szErr, sizeof(s_szErr), "OpenSL ES is not available"); logger->Error("Voice: %s", s_szErr); return false; }
    s_bOk = true;
    logger->Info("Voice: ready (Opus %s)", opus_get_version_string());
    return true;
}

#ifdef MP_VOICE_HOSTTEST
namespace Voice
{
    void TestInit() { s_bInit = s_bOk = true; for(int i = 0; i < MP_MAX_PLAYERS; ++i) { s_fGain[i] = 1.0f; s_nLastVoice[i] = 0; } }
    void TestMix(int16_t* out) { MixFrame(out); }
    int  TestQueued(int id) { std::lock_guard<std::mutex> lk(s_StreamM); Stream* st = s_Streams[id]; return st && st->any ? (int16_t)(st->newest - st->playSeq) + 1 : 0; }
    void TestPitch(int16_t* x, int n, float ratio, int rate) { s_Pitch.Process(x, n, ratio, rate); }
    bool TestPlaying(int id) { std::lock_guard<std::mutex> lk(s_StreamM); Stream* st = s_Streams[id]; return st && st->playing; }
}
#endif
