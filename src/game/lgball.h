// MP 4.13.13: the real Rocket League ball (assets/MP/ball_-_rocket_league.glb, built into libMP by
// tools/make_league_ball.py) in place of GTA object 1946 (baskt_ball_hi) while you are in the League arena.
#pragma once

namespace RlBall
{
    void Init(void* gameHandle);
    // main thread, every frame until true: requests the model, then swaps its mesh + texture for the RL ball
    // (7.4 m wide, centred), a 7.4 m bounding box and a long draw distance. true = create the object now.
    bool Ready(int model);
    // after the ball object is gone: the GTA basketball comes back (model reloaded from gta3.img)
    void Restore(int model);
    bool Failed();          // an engine check failed (or the game closed during the swap once): use the drawn ball
}
