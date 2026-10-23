#pragma once
#include <aml-psdk/renderware/RwVector.h>
struct RtQuat { RQVector imag; float real; };
struct RpHAnimBlendInterpFrame { RtQuat q; RwV3d t; };
