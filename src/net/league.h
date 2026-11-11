// MP 4.13.4: San Andreas Online League - the 6th featured 24/7 server: Rocket League style car
// soccer ("Rocket Soccer" of San Andreas Online / MTA Neon, github.com/ThirtyFoxMCx00E1/san-andreas-online).
// SA:O runs RocketSim (Bullet) at 120 Hz in a WASM kernel; that is far too heavy for a phone server
// that also has to stay up 24/7, so MP has its own small ball model here, shared by the server
// (authoritative, server_league.inc) and the phone (prediction + free play, mp_league.inc).
//
// One packet type, PKT_LG, with a sub-op. Old phones / servers ignore it.
#pragma once
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <math.h>

#define MP_LG_SERVER_NAME   "San Andreas Online League"
#define MP_LG_SHORT_NAME    "SAO League"
#define MP_LG_GAMEMODE      "Rocket Soccer"
#define MP_LG_PORT          1934
#define MP_LG_MAX_PLAYERS   16
#define MP_LG_BUILD         41304   // phones older than 4.13.4 are refused (no league client)
#define MP_LG_TEAMS         2       // 0 = Blue, 1 = Orange

static inline bool MP_IsLgServerName(const char* n)
{
    if(!n || !n[0]) return false;
    if(!strcasecmp(n, MP_LG_SERVER_NAME) || !strcasecmp(n, MP_LG_SHORT_NAME)) return true;
    return strcasestr(n, "Andreas Online League") != nullptr || strcasestr(n, "Rocket League") != nullptr;
}

// ---- the arena: Las Venturas airport apron (flat, open, far from traffic) ----
// Field along X: Blue defends -X (attacks +X), Orange defends +X.
static const float kLgCenter[3]   = { 1477.0f, 1500.0f, 10.82f };
#define LG_HALF_L       52.0f    // goal line at +-52 m
#define LG_HALF_W       34.0f    // side walls at +-34 m
#define LG_GOAL_HALF_W   9.0f
#define LG_GOAL_H        6.6f
#define LG_GOAL_DEPTH    8.0f
#define LG_CEIL         28.0f
#define LG_BALL_R        1.85f   // drawn + collision radius (m)
#define LG_BALL_MAXV    60.0f
#define LG_GRAV         13.0f    // RL ball is floaty: 650 uu/s^2 = 6.5 m/s^2; a bit more on a phone screen
#define LG_MATCH_SEC    300      // 5:00
#define LG_KICKOFF_MS   3000
#define LG_GOAL_MS      5000
#define LG_END_MS      12000

enum eLgOp : uint8_t { LGO_STATE = 1, LGO_PLAYERS, LGO_EVENT, LGO_ME, LGO_CMD };
enum eLgPhase : uint8_t { LGP_WARMUP = 0, LGP_KICKOFF, LGP_PLAY, LGP_GOAL, LGP_END };
enum eLgEv : uint8_t { LGE_GOAL = 1, LGE_SPAWN, LGE_END, LGE_SAVE, LGE_SHOT, LGE_ASSIST, LGE_QCHAT, LGE_NOTICE, LGE_MVP, LGE_RANK };
enum eLgCmd : uint8_t { LGC_TOUCH = 1, LGC_CAR, LGC_TEAM, LGC_QCHAT, LGC_SPAWN, LGC_LEAVE_ARENA, LGC_STATS };

// S->C every 100 ms (20 Hz in play would be nicer; 10 Hz + phone prediction is enough and cheap)
struct PktLgState
{
    uint8_t  type, op, phase, overtime;
    uint8_t  score[2];
    uint16_t clock;          // seconds left (overtime: seconds played)
    uint16_t phaseMs;        // ms left in KICKOFF / GOAL / END
    uint8_t  lastTouch, lastTeam;   // MP_INVALID_ID = nobody
    float    ball[3], vel[3];       // relative to kLgCenter
    uint32_t seq;
    uint8_t  blueN, orangeN, mvp, pad;
};
struct LgRow { uint8_t id, team, car, flags; uint16_t score, goals, assists, saves, shots, mmr; };   // flags 1 = MVP
struct PktLgPlayers { uint8_t type, op, n, pad; LgRow r[MP_LG_MAX_PLAYERS]; };
struct PktLgEvent { uint8_t type, op, ev, a, b, c; uint16_t val; float pos[4]; char text[72]; };
struct PktLgMe
{
    uint8_t  type, op, team, car;
    uint16_t mmr, peak;
    uint32_t games, wins, goals, assists, saves, shots, mvps;
    uint8_t  tier, div, streak, pad;
};
struct PktLgCmd { uint8_t type, op, cmd, a; float pos[3], vel[3]; uint32_t seq; char text[48]; };

// ---- ranks (Rocket League competitive tiers, MMR) ----
static const char* const g_LgTierName[8] = { "Bronze", "Silver", "Gold", "Platinum", "Diamond", "Champion", "Grand Champion", "Supersonic Legend" };
static const uint32_t g_LgTierCol[8] = { 0xB0754AFF, 0xC9D1DCFF, 0xF2C94CFF, 0x6FD3E8FF, 0x4A7BF7FF, 0xA05CF2FF, 0xE8404AFF, 0xF4F4FFFF };
static inline int LgTierOf(int mmr) { static const int lo[8] = { 0, 400, 600, 800, 1000, 1200, 1400, 1700 }; int t = 0; for(int i = 0; i < 8; ++i) if(mmr >= lo[i]) t = i; return t; }
static inline int LgDivOf(int mmr)
{
    static const int lo[9] = { 0, 400, 600, 800, 1000, 1200, 1400, 1700, 2200 };
    int t = LgTierOf(mmr); if(t >= 7) return 0;
    int span = lo[t + 1] - lo[t]; int d = (mmr - lo[t]) * 4 / (span > 0 ? span : 1);
    return d < 0 ? 0 : (d > 3 ? 3 : d);   // Division I..IV
}

// ---- the ball (relative coordinates, z = height above the field) ----
struct LgBall { float p[3], v[3]; };
static inline void LgBallReset(LgBall& b) { b.p[0] = 0; b.p[1] = 0; b.p[2] = LG_BALL_R + 0.05f; b.v[0] = b.v[1] = b.v[2] = 0; }
// returns 0, or 1 = Blue scored (ball in the +X goal), 2 = Orange scored (-X goal)
static inline int LgBallStep(LgBall& b, float dt)
{
    if(!(dt > 0)) return 0;
    if(dt > 0.05f) dt = 0.05f;
    b.v[2] -= LG_GRAV * dt;
    const float drag = 1.0f - 0.03f * dt;   // RL: 0.03 / s air drag
    for(int k = 0; k < 3; ++k) { b.v[k] *= drag; b.p[k] += b.v[k] * dt; }
    const float R = LG_BALL_R;
    // ground
    if(b.p[2] < R)
    {
        b.p[2] = R;
        if(b.v[2] < 0) b.v[2] = -b.v[2] * 0.60f;
        if(b.v[2] < 1.2f) b.v[2] = 0;
        const float fr = 1.0f - 0.35f * dt;   // rolling friction
        b.v[0] *= fr; b.v[1] *= fr;
    }
    if(b.p[2] > LG_CEIL - R) { b.p[2] = LG_CEIL - R; if(b.v[2] > 0) b.v[2] = -b.v[2] * 0.6f; }
    // side walls
    if(b.p[1] > LG_HALF_W - R)  { b.p[1] = LG_HALF_W - R;  if(b.v[1] > 0) b.v[1] = -b.v[1] * 0.6f; }
    if(b.p[1] < -LG_HALF_W + R) { b.p[1] = -LG_HALF_W + R; if(b.v[1] < 0) b.v[1] = -b.v[1] * 0.6f; }
    // end walls with the goal mouths
    const bool mouth = fabsf(b.p[1]) < LG_GOAL_HALF_W - R * 0.3f && b.p[2] < LG_GOAL_H - R * 0.3f;
    if(b.p[0] > LG_HALF_L - R && !mouth) { b.p[0] = LG_HALF_L - R; if(b.v[0] > 0) b.v[0] = -b.v[0] * 0.6f; }
    if(b.p[0] < -LG_HALF_L + R && !mouth) { b.p[0] = -LG_HALF_L + R; if(b.v[0] < 0) b.v[0] = -b.v[0] * 0.6f; }
    if(b.p[0] > LG_HALF_L + R) return 1;    // fully over the line
    if(b.p[0] < -LG_HALF_L - R) return 2;
    // inside a goal: posts / roof / back keep it in
    if(fabsf(b.p[0]) > LG_HALF_L)
    {
        if(b.p[1] > LG_GOAL_HALF_W - R) { b.p[1] = LG_GOAL_HALF_W - R; b.v[1] = -fabsf(b.v[1]) * 0.5f; }
        if(b.p[1] < -LG_GOAL_HALF_W + R) { b.p[1] = -LG_GOAL_HALF_W + R; b.v[1] = fabsf(b.v[1]) * 0.5f; }
        if(b.p[2] > LG_GOAL_H - R) { b.p[2] = LG_GOAL_H - R; if(b.v[2] > 0) b.v[2] = -b.v[2] * 0.5f; }
    }
    const float sp = sqrtf(b.v[0] * b.v[0] + b.v[1] * b.v[1] + b.v[2] * b.v[2]);
    if(sp > LG_BALL_MAXV) { const float s = LG_BALL_MAXV / sp; for(int k = 0; k < 3; ++k) b.v[k] *= s; }
    return 0;
}
// a car (relative position + velocity in m/s) hits the ball: RL-like "push + extra impulse"
static inline void LgBallHit(LgBall& b, const float car[3], const float cv[3])
{
    float d[3] = { b.p[0] - car[0], b.p[1] - car[1], b.p[2] - (car[2] + 0.4f) };
    float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); if(l < 0.01f) { d[0] = 1; d[1] = 0; d[2] = 0; l = 1; }
    for(int k = 0; k < 3; ++k) d[k] /= l;
    d[2] += 0.08f;   // the RL "lift": hits go up a little
    float cs = sqrtf(cv[0] * cv[0] + cv[1] * cv[1] + cv[2] * cv[2]); if(cs > 50) cs = 50;
    float rel = (cv[0] - b.v[0]) * d[0] + (cv[1] - b.v[1]) * d[1] + (cv[2] - b.v[2]) * d[2]; if(rel < 0) rel = 0;
    const float push = 3.0f + rel * 1.15f + cs * 0.10f;
    float add[3] = { d[0] * push, d[1] * push, d[2] * push };
    // a ground car (no upward speed) gives a low shot; jumps / aerials (cv z > 0) can lift it
    const float hz = sqrtf(add[0] * add[0] + add[1] * add[1]);
    const float maxUp = hz * 0.22f + (cv[2] > 0 ? cv[2] * 1.2f : 0.0f) + 2.0f;
    if(add[2] > maxUp) add[2] = maxUp;
    for(int k = 0; k < 3; ++k) b.v[k] += add[k];
    const float bs = sqrtf(b.v[0] * b.v[0] + b.v[1] * b.v[1] + b.v[2] * b.v[2]); if(bs > 60.0f) for(int k = 0; k < 3; ++k) b.v[k] *= 60.0f / bs;   // RL max ball speed
    // out of the car
    const float need = LG_BALL_R + 1.7f;
    if(l < need) for(int k = 0; k < 3; ++k) b.p[k] = car[k] + (k == 2 ? 0.4f : 0.0f) + d[k] / sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) * need;
    if(b.p[2] < LG_BALL_R) b.p[2] = LG_BALL_R;
}
// will the ball (no more touches) cross the line of goal `team` (0 = Blue's goal at -X) within `sec`?
static inline bool LgHeadsIntoGoal(const LgBall& b0, int team, float sec)
{
    LgBall b = b0;
    for(float t = 0; t < sec; t += 0.05f)
    {
        int g = LgBallStep(b, 0.05f);
        if(g) return (team == 0 && g == 2) || (team == 1 && g == 1);
    }
    return false;
}

// the cars (RL names, the closest GTA:SA body)
struct LgCar { const char* name; int16_t model; uint8_t col1, col2; };
static const LgCar g_LgCars[6] = {
    { "OCTANE",   568, 0, 0 },    // Bandito
    { "FENNEC",   494, 0, 0 },    // Hotring Racer
    { "DOMINUS",  434, 0, 0 },    // Hotknife
    { "BREAKOUT", 424, 0, 0 },    // BF Injection
    { "MERC",     495, 0, 0 },    // Sandking
    { "BACKFIRE", 429, 0, 0 },    // Banshee
};
#define LG_CAR_COUNT 6
static const uint8_t g_LgTeamCarCol[2] = { 79, 6 };   // GTA colours: blue / orange
