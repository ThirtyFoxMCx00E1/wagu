#include "../src/net/mptd.h"
#include <fstream>
#include <sstream>
#include <cassert>
static std::string rd(const char* p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
int main(int argc, char** argv)
{
    for(int i = 1; i < argc; ++i)
    {
        MptdProject pr; int n = MptdImportPawn(rd(argv[i]), pr);
        int g = 0, p = 0, nm = 0, sp = 0, pm = 0, bx = 0; for(auto& t : pr.items) { (t.player ? p : g)++; if(!t.var.empty()) nm++; if(t.font == 4) sp++; if(t.font == 5) pm++; if(t.useBox) bx++; }
        printf("%s: %d imported (global %d player %d named %d sprite %d preview %d box %d)\n", argv[i], n, g, p, nm, sp, pm, bx);
        std::string js = MptdWrite(pr); MptdProject b; std::string err;
        bool ok = MptdParse(js, b, &err); assert(ok); assert(b.items.size() == pr.items.size());
        for(size_t k = 0; k < b.items.size(); ++k) { assert(b.items[k].text == pr.items[k].text); assert(b.items[k].color == pr.items[k].color); assert(fabsf(b.items[k].x - pr.items[k].x) < 0.001f); assert(b.items[k].font == pr.items[k].font); }
        std::string pw = MptdExportPawn(pr, false), om = MptdExportPawn(pr, true);
        MptdProject c; int n2 = MptdImportPawn(pw, c); MptdProject d; int n3 = MptdImportPawn(om, d);
        printf("  roundtrip json ok, pawn re-import %d, open.mp re-import %d\n", n2, n3);
        assert(n2 == n && n3 == n);
        for(size_t k = 0; k < c.items.size(); ++k) { assert(c.items[k].align == d.items[k].align); assert(c.items[k].font == d.items[k].font); assert(c.items[k].color == d.items[k].color); assert(c.items[k].proportional == d.items[k].proportional); }
        if(i == 1) { FILE* f = fopen("/tmp/mptd_sample.mptd", "w"); fputs(js.c_str(), f); fclose(f); f = fopen("/tmp/mptd_sample.pwn", "w"); fputs(om.c_str(), f); fclose(f); }
    }
    MptdProject e; assert(!MptdParse("{\"format\":\"x\"}", e)); assert(!MptdParse("garbage", e));
    puts("all ok");
}
