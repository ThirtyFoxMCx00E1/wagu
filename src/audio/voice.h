#pragma once
// 3.11: voice chat (Opus 1.6.1 + OpenSL ES).
//
//  mic -> OpenSL recorder (48 kHz mono, VOICE_COMMUNICATION preset = the phone's echo
//         cancel / noise suppression) -> 20 ms frame -> Opus (VOIP, 24 kbit/s, in-band FEC)
//      -> sent from the audio thread the moment it is recorded (no batching, no game frame wait)
//  net -> per-speaker jitter buffer (starts after 40 ms, never holds more than ~100 ms: older
//         frames are skipped so the delay can't grow) -> Opus decode / FEC / loss concealment
//      -> mixed with each speaker's distance volume -> one OpenSL player (48 kHz mono)
//
// Only frames with speech are sent (energy gate + 300 ms hang time), so a quiet open mic
// costs nothing and the speaker icon is only shown while someone really talks.
#include <stdint.h>

namespace Voice
{
    typedef void (*SendFn)(const void* pkt, int len);

    bool  Init(SendFn send);                // once; false = no Opus / OpenSL (voice off)
    bool  Available();

    // microphone (Android RECORD_AUDIO permission)
    enum ePerm { PERM_GRANTED = 0, PERM_ASK = 1, PERM_NOT_IN_APK = 2, PERM_UNKNOWN = 3 };
    int   MicPermission();                  // ePerm
    void  RequestMicPermission();           // shows Android's "Allow microphone?" dialog
    bool  StartMic(uint8_t myId);           // false = recorder failed. No permission -> MP Voice helper app
    // 3.11.1: MP Voice helper app (com.thirtyfox.mpvoice) for GTA SA APKs without RECORD_AUDIO
    bool  UsingHelper();
    int   HelperState();                    // 0 not used, 1 waiting for MP Voice, 2 getting your voice
    void  OpenHelperApp();                  // opens MP Voice (it turns its mic on and comes back)
    int   OpenHelperResult();               // 0 pending, 1 opened, 2 not installed
    void  StopMic();
    bool  MicOn();
    bool  MicTalking();                     // mic on and you are talking right now
    float MicLevel();                       // 0..1 for the button ring

    // playback
    void  SetOutput(bool on);               // connected + voice enabled: player running
    void  SetVolume(float v);               // 0..2
    void  SetMicGain(float g);              // 0.5..3
    void  SetPitch(float ratio);            // 0.6..1.6, your voice for the others (1 = normal)
    float Pitch();
    void  SetMonitor(bool on);              // hear your own voice (test the pitch, use earphones)
    bool  Monitor();
    void  OnPacket(int id, uint16_t seq, uint8_t flags, const uint8_t* opus, int len);
    void  SetGain(int id, float gain);      // distance volume, 0 = inaudible
    bool  IsSpeaking(int id);               // got voice from this player in the last 300 ms
    void  ResetSpeaker(int id);
    void  ResetAll();
    const char* LastError();

    // like real life: 100 % up to 4 m, then inverse distance (8 m 50 %, 16 m 25 %), the last 30 %
    // of the range fades smoothly to silence. range = MP_VOICE_RANGE (40 m)
    inline float DistanceGain(float d, float range)
    {
        const float ref = 4.0f;
        if(d >= range) return 0.0f;
        float g = d <= ref ? 1.0f : ref / d;
        float from = range * 0.7f;
        if(d > from) { float t = (d - from) / (range - from); g *= 1.0f - t * t * (3.0f - 2.0f * t); }
        return g;
    }
}
