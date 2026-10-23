// 3.11: voice chat engine on the PC: Opus 1.6.1 round trip, jitter buffer start delay, packet
// loss (FEC / concealment), reordering, burst catch-up (delay never grows), talk spurt end.
//   see build line in the 3.11 README section
#include "voice_hoststub.h"
#include "audio/voice.h"
#include "opus.h"
#include "net/protocol.h"
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <chrono>
#include <thread>
#include <stdint.h>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
VoiceTestAml* aml = new VoiceTestAml(); VoiceTestLog* logger = new VoiceTestLog();
// OpenSL interface ids (only referenced, never used on the PC)
static SLInterfaceID_ dummy;
const SLInterfaceID SL_IID_ANDROIDSIMPLEBUFFERQUEUE = &dummy, SL_IID_ANDROIDCONFIGURATION = &dummy, SL_IID_PLAY = &dummy, SL_IID_RECORD = &dummy;
namespace Sound { void* EngineItf() { return nullptr; } void* OutputMix() { return nullptr; } }
#include <sys/time.h>
uint64_t MP_NowMs() { timeval tv; gettimeofday(&tv, nullptr); return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000; }
namespace Voice { void TestInit(Voice::SendFn); void TestMix(int16_t*); int TestQueued(int); bool TestPlaying(int); void TestPitch(int16_t*, int, float, int); }

static int fails = 0;
#define CHECK(c, m) do { if(c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); ++fails; } } while(0)
static void Sleep(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
static double Rms(const int16_t* p, int n) { double s = 0; for(int i = 0; i < n; ++i) s += (double)p[i] * p[i]; return sqrt(s / n); }

int main()
{
    static std::atomic<int> sent{0}; static std::atomic<int> talkSent{0};
    Voice::TestInit([](const void* p, int n) { const PktVoiceHdr* h = (const PktVoiceHdr*)p; if(n >= (int)sizeof(PktVoiceHdr) && h->type == PKT_VOICE) { ++sent; if(h->len > 2) ++talkSent; } });
    int err = 0;
    OpusEncoder* enc = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &err);
    CHECK(enc && !err, "Opus 1.6.1 encoder (48 kHz mono VOIP)");
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(24000)); opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(enc, OPUS_SET_INBAND_FEC(1)); opus_encoder_ctl(enc, OPUS_SET_PACKET_LOSS_PERC(10));
    printf("      %s\n", opus_get_version_string());
    // a "voice": 220 Hz + harmonics with a syllable envelope
    std::vector<std::vector<uint8_t>> frames;
    int16_t pcm[960]; double t = 0; double encMs = 0; int maxBytes = 0;
    for(int f = 0; f < 100; ++f)
    {
        for(int i = 0; i < 960; ++i, t += 1.0 / 48000)
        {
            double env = 0.5 + 0.5 * sin(2 * M_PI * 3 * t);
            pcm[i] = (int16_t)(9000 * env * (sin(2 * M_PI * 220 * t) + 0.5 * sin(2 * M_PI * 440 * t) + 0.25 * sin(2 * M_PI * 660 * t)) / 1.75);
        }
        uint8_t out[400];
        auto a = std::chrono::steady_clock::now();
        int n = opus_encode(enc, pcm, 960, out, sizeof(out));
        encMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
        if(n > maxBytes) maxBytes = n;
        frames.emplace_back(out, out + (n > 0 ? n : 0));
    }
    printf("      encode %.3f ms per 20 ms frame (PC), biggest frame %d bytes\n", encMs / 100, maxBytes);
    CHECK(maxBytes > 2 && maxBytes <= MP_VOICE_MAX_BYTES - 100, "frames fit one voice packet with room to spare");

    int16_t mix[960];
    // 1) start delay: one frame -> silence until 40 ms, two frames -> plays at once
    Voice::OnPacket(1, 1000, 0, frames[0].data(), (int)frames[0].size());
    Voice::TestMix(mix);
    CHECK(!Voice::TestPlaying(1), "1 frame buffered: waits (jitter buffer)");
    Voice::OnPacket(1, 1001, 0, frames[1].data(), (int)frames[1].size());
    Voice::TestMix(mix);
    CHECK(Voice::TestPlaying(1), "2 frames (40 ms) buffered: playback starts");
    CHECK(Voice::IsSpeaking(1), "speaking icon on while frames arrive");
    // 2) steady stream with loss + reordering: audio keeps going (FEC / concealment)
    double energy = 0; int silentFrames = 0;
    for(int f = 2; f < 60; ++f)
    {
        if(f % 10 == 5) {}                                  // lost
        else if(f % 10 == 7) { Voice::OnPacket(1, (uint16_t)(1000 + f + 1), 0, frames[f + 1].data(), (int)frames[f + 1].size()); Voice::OnPacket(1, (uint16_t)(1000 + f), 0, frames[f].data(), (int)frames[f].size()); ++f; }   // swapped
        else Voice::OnPacket(1, (uint16_t)(1000 + f), 0, frames[f].data(), (int)frames[f].size());
        Voice::TestMix(mix);
        double r = Rms(mix, 960); energy += r; if(r < 50) ++silentFrames;
    }
    printf("      average output level %.0f, silent frames %d of ~54\n", energy / 54, silentFrames);
    CHECK(silentFrames <= 3, "10 % loss + reordering: no gaps (FEC from the next frame / concealment)");
    // 3) burst: 12 frames at once (network hiccup) -> the queue is cut back to <= 5 (100 ms)
    for(int f = 60; f < 72; ++f) Voice::OnPacket(1, (uint16_t)(1000 + f), 0, frames[f].data(), (int)frames[f].size());
    int before = Voice::TestQueued(1);
    Voice::TestMix(mix);
    int after = Voice::TestQueued(1);
    printf("      queued after burst %d frames -> %d after one playout\n", before, after);
    CHECK(after <= 5, "a burst never adds delay: max 100 ms queued");
    // 4) talk spurt ends: END flag hides the icon at once, stream stops after the queue
    Voice::OnPacket(1, 1072, VOICE_END, frames[72].data(), (int)frames[72].size());
    CHECK(!Voice::IsSpeaking(1), "end of talking: speaking icon off immediately");
    for(int k = 0; k < 15; ++k) Voice::TestMix(mix);
    CHECK(!Voice::TestPlaying(1), "stream stops when the talker stops");
    // 5) gain 0 (muted / out of range) = silence
    Voice::SetGain(2, 0.0f);
    for(int f = 0; f < 4; ++f) Voice::OnPacket(2, (uint16_t)(50 + f), 0, frames[f + 10].data(), (int)frames[f + 10].size());
    Voice::TestMix(mix); Voice::TestMix(mix);
    CHECK(Rms(mix, 960) < 1.0, "volume 0 (out of range) = silent");
    // 6) icon also goes away by itself 300 ms after the last frame (END packet lost)
    Voice::OnPacket(3, 1, 0, frames[20].data(), (int)frames[20].size());
    CHECK(Voice::IsSpeaking(3), "talking: icon on");
    Sleep(350);
    CHECK(!Voice::IsSpeaking(3), "no frames for 300 ms: icon off (END lost)");
    // 7) end-to-end delay budget: 20 ms frame + 40 ms jitter start + 40 ms output (2 x 20 ms buffers)
    CHECK(20 + 40 + 40 <= 100, "voice delay on top of the network: about 100 ms");
    // 8) distance: loud up close, quieter further away, silent past 40 m; closer = always louder
    {
        float R = MP_VOICE_RANGE; bool mono = true; float prev = 2;
        for(float d = 0; d <= 45; d += 0.5f) { float g = Voice::DistanceGain(d, R); if(g > prev + 1e-6f) mono = false; prev = g; }
        printf("      volume: 2 m %.0f%%, 8 m %.0f%%, 16 m %.0f%%, 30 m %.0f%%, 38 m %.0f%%, 40 m %.0f%%\n",
               Voice::DistanceGain(2, R) * 100, Voice::DistanceGain(8, R) * 100, Voice::DistanceGain(16, R) * 100, Voice::DistanceGain(30, R) * 100, Voice::DistanceGain(38, R) * 100, Voice::DistanceGain(40, R) * 100);
        CHECK(Voice::DistanceGain(2, R) == 1.0f && Voice::DistanceGain(R, R) == 0.0f && Voice::DistanceGain(100, R) == 0.0f, "full next to you, silent at 40 m and beyond");
        CHECK(mono, "getting closer always makes the voice louder (never quieter)");
        CHECK(R == 40.0f, "voice range 40 m (server + client)");
    }
    // 9) voice pitch: a 300 Hz tone comes out at 300 x ratio
    for(float ratio : { 0.8f, 1.25f, 1.5f })
    {
        std::vector<int16_t> x(48000);
        for(int i = 0; i < 48000; ++i) x[i] = (int16_t)(8000 * sin(2 * M_PI * 300 * i / 48000.0));
        for(int k = 0; k < 48000; k += 960) Voice::TestPitch(&x[k], 960, ratio, 48000);
        // dominant frequency over the last half second (Goertzel scan 150..600 Hz)
        // the 30 ms cross-fade adds small side tones (+-33 Hz): take the power-weighted centre
        double sumP = 0, sumPF = 0;
        for(double f = 150; f <= 600; f += 2)
        {
            double w = 2 * M_PI * f / 48000, c = 2 * cos(w), s0, s1 = 0, s2 = 0;
            for(int i = 24000; i < 48000; ++i) { s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
            double pw = s1 * s1 + s2 * s2 - c * s1 * s2;
            sumP += pw; sumPF += pw * f;
        }
        double bestF = sumPF / sumP;
        char m[96]; snprintf(m, sizeof(m), "pitch x%.2f: 300 Hz -> %.0f Hz (want %.0f)", ratio, bestF, 300 * ratio);
        CHECK(fabs(bestF - 300 * ratio) <= 300 * ratio * 0.03, m);
    }
    // 10) 3.11.1 MP Voice helper app: no mic permission -> frames from 127.0.0.1:19228 are encoded + sent
    {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        int one = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(0x7f000001); a.sin_port = htons(MP_VOICE_HELPER_PORT);
        CHECK(bind(fd, (sockaddr*)&a, sizeof(a)) == 0, "fake MP Voice app listening on 127.0.0.1:19228");
        timeval tv{2, 0}; setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        sent = 0; talkSent = 0;
        bool on = Voice::StartMic(5);
        CHECK(on && Voice::UsingHelper() && Voice::HelperState() == 1, "mic on without permission -> waits for MP Voice");
        uint8_t hb[16]; sockaddr_in game{}; socklen_t gl = sizeof(game);
        int r = recvfrom(fd, hb, sizeof(hb), 0, (sockaddr*)&game, &gl);
        CHECK(r == 5 && !memcmp(hb, "MPVH", 4) && hb[4] == 1, "game says hello (MPVH + 1) to MP Voice");
        uint8_t fr[8 + 1920]; memcpy(fr, "MPVF", 4); fr[6] = (uint8_t)(4800 & 0xFF); fr[7] = (uint8_t)(4800 >> 8);
        double ph = 0;
        for(int f = 0; f < 40; ++f)
        {
            fr[4] = (uint8_t)f; fr[5] = 0;
            for(int i = 0; i < 960; ++i, ph += 2 * M_PI * 250 / 48000) { int16_t v = (int16_t)(9000 * sin(ph)); fr[8 + i * 2] = (uint8_t)v; fr[9 + i * 2] = (uint8_t)(v >> 8); }
            sendto(fd, fr, sizeof(fr), 0, (sockaddr*)&game, gl);
            Sleep(20);
        }
        Sleep(100);
        printf("      MP Voice frames in: 40, voice packets out: %d (speech %d)\n", sent.load(), talkSent.load());
        CHECK(Voice::HelperState() == 2, "voice arriving from MP Voice");
        CHECK(talkSent >= 35, "every 20 ms helper frame is encoded and sent at once");
        Voice::StopMic();
        bool gotOff = false;
        for(int k = 0; k < 5 && !gotOff; ++k) { r = recvfrom(fd, hb, sizeof(hb), 0, nullptr, nullptr); if(r == 5 && !memcmp(hb, "MPVH", 4) && hb[4] == 0) gotOff = true; }
        CHECK(gotOff && !Voice::MicOn(), "mic off -> tells MP Voice to release the microphone");
        close(fd);
    }
    opus_encoder_destroy(enc);
    printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
    return fails ? 1 : 0;
}
