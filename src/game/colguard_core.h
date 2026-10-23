#pragma once
// 3.10: crash guard for broken collision data (pure logic, tested on Linux by test/colguard_test.cpp).
//
// The crash (aml_crashlog, 3.04.6, 32-bit):
//   SIGSEGV, fault address 0x5CAC in CColTrianglePlane::Set(const CVector*, CColTriangle&)
//   <- CCollisionData::CalculateTrianglePlanes <- CCollision::CalculateTrianglePlanes(CCollisionData*)
//   <- CCollision::ProcessColModels <- CBike::ProcessEntityCollision <- CPhysical::ProcessCollision
//   (hooked by VehDeform32.so).
// 0x5CAC = 1977 * 12 = &vertices[1977] with vertices == NULL: a model's collision still says it has
// triangles, but its vertex array is gone (freed / never allocated - e.g. a vehicle whose collision
// was rebuilt by a deformation mod). The first bike that touches it computes the triangle planes
// and reads address 0x5CAC. The guard checks the collision data before the game uses it: a
// triangle list without vertices (or without triangles) is switched off (triangle count 0), the
// spheres / boxes / lines are kept if their arrays are fine. The model then only collides with
// its spheres and boxes, instead of crashing everyone near it.
#include <stdint.h>
#include <string.h>

namespace ColGuard
{
    // CCollisionData (32-bit / 64-bit, same order):
    //   u16 nSpheres, u16 nBoxes, u16 nTriangles, u8 nLines, u8 flags,
    //   then pointers: spheres, boxes, lines, vertices, triangles, trianglePlanes
    enum { CD_NSPH = 0, CD_NBOX = 2, CD_NTRI = 4, CD_NLINES = 6, CD_PTRS = 8 };
    enum { P_SPH = 0, P_BOX, P_LINES, P_VERTS, P_TRIS, P_PLANES };
    enum { FIX_TRIS = 1, FIX_SPH = 2, FIX_BOX = 4, FIX_LINES = 8 };

    inline bool BadPtr(uintptr_t p) { return p < 0x10000 || (p & 3) != 0; }   // NULL page / misaligned
    inline uintptr_t Ptr(const uint8_t* cd, int k, int ptrSize)
    {
        if(ptrSize == 8) { uint64_t v; memcpy(&v, cd + CD_PTRS + k * 8, 8); return (uintptr_t)v; }
        uint32_t v; memcpy(&v, cd + CD_PTRS + k * 4, 4); return (uintptr_t)v;
    }
    inline uint16_t U16(const uint8_t* cd, int o) { uint16_t v; memcpy(&v, cd + o, 2); return v; }
    inline void Zero16(uint8_t* cd, int o) { uint16_t z = 0; memcpy(cd + o, &z, 2); }

    // returns the FIX_* bits that were applied (0 = the data was fine)
    inline int Sanitize(uint8_t* cd, int ptrSize = (int)sizeof(void*))
    {
        if(!cd) return 0;
        int fix = 0;
        if(U16(cd, CD_NTRI) && (BadPtr(Ptr(cd, P_VERTS, ptrSize)) || BadPtr(Ptr(cd, P_TRIS, ptrSize)))) { Zero16(cd, CD_NTRI); fix |= FIX_TRIS; }
        if(U16(cd, CD_NSPH) && BadPtr(Ptr(cd, P_SPH, ptrSize))) { Zero16(cd, CD_NSPH); fix |= FIX_SPH; }
        if(U16(cd, CD_NBOX) && BadPtr(Ptr(cd, P_BOX, ptrSize))) { Zero16(cd, CD_NBOX); fix |= FIX_BOX; }
        if(cd[CD_NLINES] && BadPtr(Ptr(cd, P_LINES, ptrSize))) { cd[CD_NLINES] = 0; fix |= FIX_LINES; }
        return fix;
    }

    // CColModel: CBoundingBox (24) + CSphere (16) + u8 slot + u8 flags, then CCollisionData* (0x2C / 0x30)
    inline int ColDataOffset(int ptrSize = (int)sizeof(void*)) { return ptrSize == 8 ? 0x30 : 0x2C; }
}
