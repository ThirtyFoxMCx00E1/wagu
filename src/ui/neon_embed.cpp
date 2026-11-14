// MP 4.13.33: neon title menu files built in (src/embed/neon_blob.S) - written to files/TYFMC/MP/neon/ when missing or different
#include <mod/logger.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <errno.h>

extern "C" {
extern const unsigned char mp_neon_0[], mp_neon_0_end[];
extern const unsigned char mp_neon_1[], mp_neon_1_end[];
extern const unsigned char mp_neon_2[], mp_neon_2_end[];
extern const unsigned char mp_neon_3[], mp_neon_3_end[];
extern const unsigned char mp_neon_4[], mp_neon_4_end[];
extern const unsigned char mp_neon_5[], mp_neon_5_end[];
extern const unsigned char mp_neon_6[], mp_neon_6_end[];
extern const unsigned char mp_neon_7[], mp_neon_7_end[];
extern const unsigned char mp_neon_8[], mp_neon_8_end[];
extern const unsigned char mp_neon_9[], mp_neon_9_end[];
extern const unsigned char mp_neon_10[], mp_neon_10_end[];
extern const unsigned char mp_neon_11[], mp_neon_11_end[];
extern const unsigned char mp_neon_12[], mp_neon_12_end[];
extern const unsigned char mp_neon_13[], mp_neon_13_end[];
extern const unsigned char mp_neon_14[], mp_neon_14_end[];
extern const unsigned char mp_neon_15[], mp_neon_15_end[];
extern const unsigned char mp_neon_16[], mp_neon_16_end[];
extern const unsigned char mp_neon_17[], mp_neon_17_end[];
extern const unsigned char mp_neon_18[], mp_neon_18_end[];
extern const unsigned char mp_neon_19[], mp_neon_19_end[];
extern const unsigned char mp_neon_20[], mp_neon_20_end[];
}

namespace NeonEmbed
{
struct File { const char* name; const unsigned char* a; const unsigned char* z; };
static const File kFiles[] = {
    { "art.png", mp_neon_0, mp_neon_0_end },
    { "logo.png", mp_neon_1, mp_neon_1_end },
    { "shade_left.png", mp_neon_2, mp_neon_2_end },
    { "shade_bottom.png", mp_neon_3, mp_neon_3_end },
    { "shade_top.png", mp_neon_4, mp_neon_4_end },
    { "card_identity.png", mp_neon_5, mp_neon_5_end },
    { "card_featured.png", mp_neon_6, mp_neon_6_end },
    { "dialog.png", mp_neon_7, mp_neon_7_end },
    { "button.png", mp_neon_8, mp_neon_8_end },
    { "pill.png", mp_neon_9, mp_neon_9_end },
    { "discord.png", mp_neon_10, mp_neon_10_end },
    { "arrow.png", mp_neon_11, mp_neon_11_end },
    { "arrow_down.png", mp_neon_12, mp_neon_12_end },
    { "dot.png", mp_neon_13, mp_neon_13_end },
    { "white.png", mp_neon_14, mp_neon_14_end },
    { "font_gothic.png", mp_neon_15, mp_neon_15_end },
    { "font_gothic.txt", mp_neon_16, mp_neon_16_end },
    { "font_caps.png", mp_neon_17, mp_neon_17_end },
    { "font_caps.txt", mp_neon_18, mp_neon_18_end },
    { "font_ui.png", mp_neon_19, mp_neon_19_end },
    { "font_ui.txt", mp_neon_20, mp_neon_20_end },
};

static bool Same(const std::string& p, const unsigned char* d, size_t n)
{
    struct stat st;
    if(stat(p.c_str(), &st) != 0 || (size_t)st.st_size != n) return false;
    FILE* f = fopen(p.c_str(), "rb"); if(!f) return false;
    std::vector<unsigned char> b(n ? n : 1);
    bool same = fread(b.data(), 1, n, f) == n && memcmp(b.data(), d, n) == 0;
    fclose(f);
    return same;
}

// returns how many files were (re)written; dir ends with '/'
int Ensure(const std::string& dir)
{
    mkdir(dir.c_str(), 0777);
    int wrote = 0;
    for(const File& f : kFiles)
    {
        const size_t n = (size_t)(f.z - f.a);
        const std::string p = dir + f.name;
        if(Same(p, f.a, n)) continue;
        const std::string tmp = p + ".tmp";
        FILE* o = fopen(tmp.c_str(), "wb");
        if(!o) { logger->Error("Neon menu: can't write %s (errno %d)", tmp.c_str(), errno); continue; }
        bool ok = fwrite(f.a, 1, n, o) == n;
        ok = (fclose(o) == 0) && ok;
        if(!ok || rename(tmp.c_str(), p.c_str()) != 0) { remove(tmp.c_str()); logger->Error("Neon menu: writing %s failed", p.c_str()); continue; }
        ++wrote;
    }
    if(wrote) logger->Info("Neon menu: %d built-in file(s) written to %s", wrote, dir.c_str());
    return wrote;
}
}
