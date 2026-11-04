#include <string.h>
// 4.05.5: tiny DXT1/DXT5 encoder for tools/texdb2txd.py (stb_dxt.h by Sean Barrett, public domain / MIT)
#define STB_DXT_IMPLEMENTATION
#include "stb_dxt.h"
// rgba (w*h*4) -> dxt1 (alpha=0) or dxt5 (alpha=1). returns bytes written
int encode(const unsigned char* rgba, int w, int h, int alpha, unsigned char* out)
{
    int bs = alpha ? 16 : 8, n = 0;
    for(int by = 0; by < (h + 3) / 4; ++by) for(int bx = 0; bx < (w + 3) / 4; ++bx)
    {
        unsigned char blk[64];
        for(int i = 0; i < 16; ++i)
        {
            int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
            if(x >= w) x = w - 1; if(y >= h) y = h - 1;
            memcpy(blk + i * 4, rgba + ((size_t)y * w + x) * 4, 4);
        }
        stb_compress_dxt_block(out + n, blk, alpha, STB_DXT_HIGHQUAL);
        n += bs;
    }
    return n;
}
