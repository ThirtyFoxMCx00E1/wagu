// 2.16.2: the decision logic of the script guard (see scriptguard.cpp), kept free of AML / Android
// so test/guard_test.cpp can run it on Linux with a fake game.
#pragma once
#include <stdint.h>

namespace ScriptGuard
{
    // GET_OFFSET_FROM_OBJECT_IN_WORLD_COORDS and GET_OFFSET_FROM_CAR_IN_WORLD_COORDS ask the game for
    // an object / car by handle. When that object / car doesn't exist (any more) - a CLEO script kept
    // the handle of a car that was deleted or despawned, a handle left over from before a save load -
    // the game still carries on with a NULL pointer and crashes at [NULL + matrix offset]
    // (SIGSEGV in CRunningScript::ProcessCommands1000To1099, GTA SA 2.00 and 2.10 alike).
    enum { OP_GET_OFFSET_FROM_OBJECT = 0x0400, OP_GET_OFFSET_FROM_CAR = 0x0407,
           OP_GET_CHAR_IN_CAR_PASSENGER_SEAT = 0x0432 };   // 2.28: car, seat, result -> 3 params

    enum { PASSENGER_SEAT_OPCODE_PARAMS = 3 };

    // handle + 3 offset values + 3 result variables
    enum { OFFSET_OPCODE_PARAMS = 7 };

    struct Env
    {
        int   (*peekParam)(void* script);                   // CRunningScript::CollectNextParameterWithoutIncreasingPC
        void  (*skipParams)(void* script, int16_t count);   // CRunningScript::CollectParameters (moves the script past them)
        void* (*getVehicle)(int handle);                    // CPools::GetVehicle (null when the handle is dead)
        void* (*getObject)(int handle);                     // CPools::GetObject  (null when the handle is dead)
    };

    // True when the opcode must NOT be handed to the game because its car / object is gone. The
    // script has then been moved past the opcode's parameters, so it simply carries on with its next
    // command (the result variables keep their old values). `handle` is the dead handle, for the log.
    inline bool ShouldSkip(const Env& e, void* script, int opcode, int& handle)
    {
        if(opcode != OP_GET_OFFSET_FROM_CAR && opcode != OP_GET_OFFSET_FROM_OBJECT && opcode != OP_GET_CHAR_IN_CAR_PASSENGER_SEAT) return false;
        handle = e.peekParam(script);
        void* entity = (opcode == OP_GET_OFFSET_FROM_OBJECT) ? e.getObject(handle) : e.getVehicle(handle);
        if(entity) return false;
        // 2.28: 0432 with a dead car (any script - MP or CLEO) is skipped too; the result variable is left as it was.
        e.skipParams(script, opcode == OP_GET_CHAR_IN_CAR_PASSENGER_SEAT ? (int16_t)PASSENGER_SEAT_OPCODE_PARAMS : (int16_t)OFFSET_OPCODE_PARAMS);
        return true;
    }
}

// 3.21: LOCATE_CHAR_ANY_MEANS / ON_FOOT / IN_CAR _OBJECT_2D (0471-0473, 5 params: char, object,
// radius x, radius y, sphere) and _3D (0474-0476, 6 params). Two crash logs (realme RE6054, Android
// 16, CLEO scripts): SIGSEGV in CRunningScript::LocateCharObjectCommand - the game looks the char and
// the object up and uses whatever comes back, also NULL. The guard reads both handles first (the
// object is the 2nd parameter: step over the 1st, read, step back) and skips the opcode with
// "condition false" when either is gone. The script position is only touched when it looks like a
// script position (inside the script, moved forward by 1..16 bytes); else only the char is checked.
namespace ScriptGuard
{
    enum { OP_LOCATE_OBJ_FIRST = 0x0471, OP_LOCATE_OBJ_LAST_2D = 0x0473, OP_LOCATE_OBJ_LAST = 0x0476 };
    inline bool IsLocateObject(int op) { return op >= OP_LOCATE_OBJ_FIRST && op <= OP_LOCATE_OBJ_LAST; }
    inline int16_t LocateParams(int op) { return op <= OP_LOCATE_OBJ_LAST_2D ? 5 : 6; }

    struct LocateEnv
    {
        Env       e;
        void*     (*getPed)(int handle);           // CPools::GetPed (null when the handle is dead)
        uintptr_t (*getIp)(void* script);          // CRunningScript::m_pCurrentIP
        void      (*setIp)(void* script, uintptr_t ip);
        uintptr_t (*getBase)(void* script);        // CRunningScript::m_pBaseIP
    };

    // true = skip (the caller then moves the script past the params and sets the condition false)
    inline bool LocateShouldSkip(const LocateEnv& l, void* script, int opcode, bool& ipOk, int& ch, int& obj)
    {
        ch = -1; obj = -1;
        if(!IsLocateObject(opcode) || !script) return false;
        ch = l.e.peekParam(script);
        if(ch <= 0 || !l.getPed(ch)) return true;
        if(!ipOk || !l.getIp) return false;
        uintptr_t save = l.getIp(script), base = l.getBase ? l.getBase(script) : 0;
        if(!save || save < base || save - base > 0x1000000) { ipOk = false; return false; }
        l.e.skipParams(script, 1);
        uintptr_t now = l.getIp(script);
        if(now <= save || now - save > 16) { l.setIp(script, save); ipOk = false; return false; }
        obj = l.e.peekParam(script);
        l.setIp(script, save);
        return obj <= 0 || !l.e.getObject(obj);
    }
}

// 2.37: world-add guard (see scriptguard.cpp, "CPhysical::Add").
// CPhysical::Add files a car / ped / object into the 50 x 50 m repeat sectors its bounding rect
// covers, taking one CPtrNodeDoubleLink + one CEntryInfoNode from the game's pools per sector. It
// trusts the rect: a position past the map edge gives a negative sector -> it indexes in front of
// CWorld::ms_aRepeatSectors (wild pointer, SIGSEGV in _ZN9CPhysical3AddEv), a NaN / huge rect loops
// over thousands of sectors and runs the node pools dry (null node -> SIGSEGV), same function.
namespace ScriptGuard
{
    enum AddVerdict { ADD_OK = 0, ADD_CLAMP_POS, ADD_REFUSE_BAD_POS, ADD_REFUSE_BAD_RECT, ADD_REFUSE_POOL };

    const float WORLD_LIMIT     = 2990.0f;   // sectors cover -3000..3000; keep the rect inside
    const float SECTOR_SIZE     = 50.0f;
    const float MAX_RECT_SPAN   = 1500.0f;   // the biggest stock vehicle is ~75 m; 1500 m = broken col model
    const int   POOL_MARGIN     = 16;        // never take the last nodes of a pool

    inline bool Fin(float f) { return f == f && f < 1e9f && f > -1e9f; }

    // pos = the entity's position, r = its bound rect {x0, y0, x1, y1} in any corner order.
    // freeLinks / freeInfo = free slots in the two node pools (-1 = unknown, not checked).
    inline AddVerdict CheckAdd(const float pos[3], const float r[4], int freeLinks, int freeInfo, int* needOut = nullptr)
    {
        if(!Fin(pos[0]) || !Fin(pos[1]) || !Fin(pos[2])) return ADD_REFUSE_BAD_POS;
        if(pos[0] > WORLD_LIMIT || pos[0] < -WORLD_LIMIT || pos[1] > WORLD_LIMIT || pos[1] < -WORLD_LIMIT) return ADD_CLAMP_POS;
        for(int i = 0; i < 4; ++i) if(!Fin(r[i])) return ADD_REFUSE_BAD_RECT;
        float x0 = r[0] < r[2] ? r[0] : r[2], x1 = r[0] < r[2] ? r[2] : r[0];
        float y0 = r[1] < r[3] ? r[1] : r[3], y1 = r[1] < r[3] ? r[3] : r[1];
        if(x1 - x0 > MAX_RECT_SPAN || y1 - y0 > MAX_RECT_SPAN) return ADD_REFUSE_BAD_RECT;
        if(x0 < -3000.0f || y0 < -3000.0f || x1 > 3000.0f || y1 > 3000.0f) return ADD_CLAMP_POS;   // sane size, pokes past the edge: nudge it in
        int nx = (int)((x1 + 3000.0f) / SECTOR_SIZE) - (int)((x0 + 3000.0f) / SECTOR_SIZE) + 1;
        int ny = (int)((y1 + 3000.0f) / SECTOR_SIZE) - (int)((y0 + 3000.0f) / SECTOR_SIZE) + 1;
        int need = nx * ny;
        if(needOut) *needOut = need;
        if((freeLinks >= 0 && freeLinks < need + POOL_MARGIN) || (freeInfo >= 0 && freeInfo < need + POOL_MARGIN)) return ADD_REFUSE_POOL;
        return ADD_OK;
    }

    inline float ClampWorld(float v) { return v > WORLD_LIMIT ? WORLD_LIMIT : v < -WORLD_LIMIT ? -WORLD_LIMIT : v; }

    // how far to move an entity so its rect is inside the map (0 when it already is)
    inline void ShiftInside(const float r[4], float& dx, float& dy)
    {
        float x0 = r[0] < r[2] ? r[0] : r[2], x1 = r[0] < r[2] ? r[2] : r[0];
        float y0 = r[1] < r[3] ? r[1] : r[3], y1 = r[1] < r[3] ? r[3] : r[1];
        dx = x0 < -2999.0f ? -2999.0f - x0 : x1 > 2999.0f ? 2999.0f - x1 : 0.0f;
        dy = y0 < -2999.0f ? -2999.0f - y0 : y1 > 2999.0f ? 2999.0f - y1 : 0.0f;
    }
}
