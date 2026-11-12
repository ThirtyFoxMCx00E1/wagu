// 4.00: objects that follow a ped bone every frame (no lag, moves with the torso). See boneattach.cpp.
#pragma once

namespace BoneAttach
{
    void Init(void* gameHandle);
    bool Available();
    // obj is already attached to ped by the script attach (for the start pose). boneId: 3 = spine1
    // key (4.05): bag model / level - a calibration made once is used again at once (saved in SetCalFile)
    void Follow(int obj, int ped, int boneId = 3, int key = 0);
    void SetCalFile(const char* path);
    void Calibrate(int obj);     // call while the ped stands still on foot: stores the bag relative to the bone
    bool Calibrated(int obj);
    void Stop(int obj);
    // 4.13.10: draw a map object bigger / smaller (sx, sy, sz along the model's own x, y, z). Only the
    // picture is scaled (the collision stays the model's). Call again from time to time: the game rebuilds
    // the frame when the object streams in again or moves.
    bool ScaleObject(int obj, float sx, float sy, float sz);
}
