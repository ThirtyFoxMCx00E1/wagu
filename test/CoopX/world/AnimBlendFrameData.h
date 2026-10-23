#pragma once

#include "RpHAnimBlendInterpFrame.h"

struct AnimBlendFrameData
{
    int8_t Flags;
    char undefined[3];

    union
    {
        RwV3d posn;
        RwV3d bonePosition;
    };

    union
    {
        RwFrame *pFrame;
        RpHAnimBlendInterpFrame *pStdKeyFrame;
    };

    int32_t boneTag;
};