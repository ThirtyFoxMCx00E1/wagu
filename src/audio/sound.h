#pragma once
#include <math.h>
namespace Sound
{
    bool PlayFile(const char* absPath, float volume = 1.0f);
    bool PreloadHit(const char* absPath);                    // 3.00: hit sound, loaded once
    bool PlayHit(const char* absPath, float volume = 1.0f);  // 3.00: restarts a ready voice (cheap, no stutter)
    // 3.04: one looping music voice (decoded on a thread; call MusicUpdate() every game tick)
    bool PlayMusic(const char* absPath, float volume = 1.0f, bool loop = true);
    void MusicUpdate();
    void SetMusicVolume(float volume);
    void StopMusic();
    bool MusicPlaying();
    void* EngineItf();   // 3.11: the shared OpenSL engine (SLEngineItf) for voice chat
    void* OutputMix();   // 3.11: SLObjectItf output mix
}
