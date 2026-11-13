#pragma once
// 4.13.15: Host map editor - the host owner of a CUSTOM server (the phone that made it / its device id)
// adds, moves, turns and deletes map objects (any GTA SA model id) in game, like SA-MP's object editor.
// Featured servers (default, Brief Case War, MW3, Mega Vil, Day Z, League) never run it.
// The server keeps the objects in hostmap_<port>.txt and sends them to everybody (PKT_HOST_MAP);
// phones stream them in and out like the Day Z mapping (net/dz_map.h rows, 24 bytes each).
#include <stdint.h>
#include "dz_map.h"

#define HMAP_MAX 3000      // objects on one server

enum eHostMapOp : uint8_t
{
    HMO_LIST = 1,   // S->C first page (clears the old list)
    HMO_MORE,       // S->C next pages
    HMO_SET,        // S->C one object added / changed (pad = 1 + slot that added it: that phone selects it)
    HMO_DEL,        // S->C objects removed
    HMO_PERM,       // S->C pad = 1: you are the host owner (you may edit)
    HMO_ADD,        // C->S r[0] = the new object (id ignored)
    HMO_EDIT,       // C->S r[0] = changed object (id = which)
    HMO_REMOVE,     // C->S r[0].id = which
    HMO_CLEAR,      // C->S every object goes
};

#pragma pack(push, 1)
struct PktHostMap { uint8_t type, op, n, pad; uint16_t total, pad2; DzMapRow r[DZM_PAGE]; };
#pragma pack(pop)
