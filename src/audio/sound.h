#pragma once
#include <math.h>
namespace Sound
{
    bool PlayFile(const char* absPath, float volume = 1.0f);
    bool PlayUi(const char* absPath, float volume = 1.0f);   // 3.29: UI clicks (own voices, never cut the hit / fx)
    bool PreloadHit(const char* absPath);                    // 3.00: hit sound, loaded once
    bool PlayHit(const char* absPath, float volume = 1.0f);  // 3.00: restarts a ready voice (cheap, no stutter)
    // 3.04: one looping music voice (decoded on a thread; call MusicUpdate() every game tick)
    bool PlayMusic(const char* absPath, float volume = 1.0f, bool loop = true);
    void MusicUpdate();
    void SetMusicVolume(float volume);
    void StopMusic();
    bool MusicPlaying();
    // 3.63: long tracks (1 h Day Z ambient): decoded on a thread a second at a time and streamed
    // into a buffer-queue voice (no full decode in RAM, no media framework / URI player)
    bool  StreamPlay(const char* absPath, float volume = 1.0f);   // false = file missing / not an mp3
    void  StreamUpdate();                // call every game tick (starts the queue when data is ready)
    void  StreamSetVolume(float volume);
    void  StreamStop();
    bool  StreamActive();                // started and not finished yet
    bool  StreamEnded();                 // the file played to its end
    float StreamPosition();              // seconds played
    float StreamDuration();              // seconds (estimated from the bitrate), 0 = unknown
    const char* StreamInfo();            // 4.00.1: what the file is / which decoder plays it / why it failed
    void* EngineItf();   // 3.11: the shared OpenSL engine (SLEngineItf) for voice chat
    void* OutputMix();   // 3.11: SLObjectItf output mix
}
