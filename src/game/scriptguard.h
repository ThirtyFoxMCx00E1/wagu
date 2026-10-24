// 2.16.2: crash guard for script opcodes that use a car / object which no longer exists.
#pragma once

namespace ScriptGuard
{
    // Hooks CRunningScript::ProcessCommands1000To1099. Does nothing (and logs why) when the game's
    // code doesn't look like GTA SA 2.00 (32-bit) / 2.10 (64-bit); MP itself keeps working either way.
    bool Init(void* gameHandle);
    // 3.21.3: every opcode range runs inside a fault guard (a crashing CLEO opcode stops that script,
    // not the game). Call after Init, so it wraps the guards above. Tick: once per game frame.
    void InitFaultGuard(void* gameHandle);
    void Tick();
}
