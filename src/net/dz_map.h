#pragma once
// 4.10: Project Day Z Legacy - dynamic mapping. Shared by the server (net/server_dzmap.inc) and the
// phone (mp_dzmap.inc).
//
// The server keeps a list of map objects - any GTA San Andreas model id (dff + txd from the phone's
// own gta3.img / gta_int.img), plus SA-MP objects that are in the Day Z model pack - and sends it to
// everybody on Project Day Z Legacy (PKT_DZ_MAP). The phones stream them in and out around the player
// like SA-MP's streamer, so a server can carry thousands of objects.
//
// Where the objects come from (server folder, next to the survivors file):
//   dayz_mapping.txt   the objects placed in game with /mapping (O id model x y z rx ry rz interior)
//   dayz_maps/         any number of map files, read at start and on /mapping reload:
//                        *.pwn *.inc *.txt  SA-MP:  CreateObject(model, x, y, z, rx, ry, rz[, dd]);
//                                                   CreateDynamicObject(model, x, y, z, rx, ry, rz[, world, interior ...]);
//                                                   CreateDynamicObjectEx(...) / AddStaticObject? -> same order
//                        *.map              MTA:    <object model="" posX="" posY="" posZ="" rotX="" rotY="" rotZ="" interior="" />
//
// The model catalog (Host/DayZ/mapping/catalog.txt, tools/make_dz_mapping_catalog.py) has all 15 694
// ids with dff / txd names and tags, from github.com/Fernando-A-Rocha/mta-ide-search (IDE files) and
// github.com/Dryxio/gta-scout (tags). The phone downloads it once for /mapping find and names.
#include <stdint.h>

#define DZM_MAX_ALL   20000    // objects on a server
#define DZM_PAGE      32
#define DZM_STREAM_IN   260.0f // the phone makes an object within this distance
#define DZM_STREAM_OUT  300.0f // and removes it past this one
#define DZM_MAX_SHOWN   300    // objects made at the same time (the game's object pool is shared)

enum eDzMapOp : uint8_t { DZMO_LIST = 1, DZMO_MORE, DZMO_SET, DZMO_DEL };
enum eDzMapFlag : uint8_t { DZMF_FILE = 1 /* from dayz_maps/ (edit the file to change it) */, DZMF_NOCOL = 2 };

#pragma pack(push, 1)
struct DzMapRow { uint32_t id; uint16_t model; uint8_t interior, flags; float x, y, z; int16_t rx10, ry10, rz10; uint16_t pad; };   // 24 bytes
struct PktDzMap { uint8_t type, op, n, pad; uint16_t total, pad2; DzMapRow r[DZM_PAGE]; };
#pragma pack(pop)

inline int16_t DzMapAng10(float a)
{
    while(a > 180.0f) a -= 360.0f; while(a < -180.0f) a += 360.0f;
    return (int16_t)(a * 10.0f + (a < 0 ? -0.5f : 0.5f));
}
