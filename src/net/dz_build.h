#pragma once
// 4.08: Project Day Z Legacy - base building (Rust style: foundation, wall, doorway, window frame,
// ceiling, door; wood or concrete). Shared by the server (net/server_dzbase.inc) and the phone
// (mp_dzbase.inc).
//
// The pieces are the SA-MP wall sections from samp.img (tools/make_dz_build.py puts them into the
// Day Z model pack on GTA pickup models that are never placed on the map):
//   wood:     19356 wall004 (wall / foundation / ceiling), 19386 wall034 (doorway), 19402 wall050 (window)
//   concrete: 19365 wall013 (wall / foundation / ceiling), 19394 wall042 (doorway), 19411 wall059 (window)
//   door:     19857 mihouse1door1
// A wall section is 0.178 thick (x), 3.212 wide (y) and 3.5 high (z), origin in the middle. A
// foundation / ceiling is the same wall laid flat (turned 90 degrees on y): 3.5 x 3.212 x 0.178.
#include <stdint.h>

enum eDzPiece : uint8_t { DZB_FOUNDATION = 0, DZB_WALL, DZB_DOORWAY, DZB_WINDOW, DZB_CEILING, DZB_DOOR, DZB_COUNT };
enum eDzTier  : uint8_t { DZBT_WOOD = 0, DZBT_CONCRETE, DZBT_COUNT };

#define DZB_CELL        3.212f   // grid = the width of a wall section
#define DZB_WALL_H      3.5f
#define DZB_HALF_T      0.089f   // half the thickness of a section
#define DZB_MAX_OWN     80       // pieces per survivor
#define DZB_MAX_ALL     2500     // pieces on the server
#define DZB_REACH       14.0f    // how far from the survivor a piece can go (server check)
#define DZB_PAGE        32

static const char* const kDzPieceName[DZB_COUNT] = { "Foundation", "Wall", "Doorway", "Window frame", "Ceiling", "Door" };
static const char* const kDzTierName[DZBT_COUNT] = { "Wood", "Concrete" };

// the GTA model each piece is drawn with (the SA-MP model swapped in by the Day Z model pack)
inline int DzPieceModel(int kind, int tier)
{
    const bool c = tier == DZBT_CONCRETE;
    switch(kind)
    {
        case DZB_DOORWAY: return c ? 1274 : 1254;
        case DZB_WINDOW:  return c ? 1275 : 1272;
        case DZB_DOOR:    return 1313;
        default:          return c ? 1273 : 1253;   // wall, foundation, ceiling
    }
}
inline int DzPieceSampId(int kind, int tier)
{
    const bool c = tier == DZBT_CONCRETE;
    switch(kind)
    {
        case DZB_DOORWAY: return c ? 19394 : 19386;
        case DZB_WINDOW:  return c ? 19411 : 19402;
        case DZB_DOOR:    return 19857;
        default:          return c ? 19365 : 19356;
    }
}
inline bool DzPieceFlat(int kind) { return kind == DZB_FOUNDATION || kind == DZB_CEILING; }

// collision boxes in model space (min xyz, max xyz) - the holes of the doorway / window stay open
struct DzBox { float v[6]; };
inline int DzModelBoxes(int model, DzBox* out)
{
    const float t = DZB_HALF_T, w = 1.606f, h = 1.75f;
    int n = 0;
    auto add = [&](float x0, float y0, float z0, float x1, float y1, float z1) { DzBox b = { { x0, y0, z0, x1, y1, z1 } }; out[n++] = b; };
    switch(model)
    {
        case 1253: case 1273: add(-t, -w, -h, t, w, h); break;                          // solid
        case 1254: case 1274:                                                           // doorway: y -0.732..0.77, z up to 0.743
            add(-t, -w, -h, t, -0.732f, h); add(-t, 0.77f, -h, t, w, h); add(-t, -0.732f, 0.743f, t, 0.77f, h); break;
        case 1272: case 1275:                                                           // window: y +-0.89, z -0.557..0.868
            add(-t, -w, -h, t, -0.89f, h); add(-t, 0.89f, -h, t, w, h);
            add(-t, -0.89f, -h, t, 0.89f, -0.557f); add(-t, -0.89f, 0.868f, t, 0.89f, h); break;
        case 1313: add(0.0f, -0.03f, -1.255f, 1.5f, 0.03f, 1.255f); break;            // door (hinge at x 0)
        default: break;
    }
    return n;
}

struct DzBaseRow { uint32_t id; uint8_t kind, tier, flags, pad; float x, y, z; int16_t head10; uint16_t hp; };   // flags: 1 door open, 2 yours / your party's
enum eDzBaseOp : uint8_t { DZBO_LIST = 1, DZBO_MORE, DZBO_SET, DZBO_DEL };
struct PktDzBase { uint8_t type, op, n, pad; uint16_t total, max; DzBaseRow r[DZB_PAGE]; };
