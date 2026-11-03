// ================================================================================================
// 3.50: in-game MP Textdraw Editor (/tde). Works like samp-textdraw-editor (Nexor,
// https://github.com/ThirtyFoxMCx00E1/samp-textdraw-editor): a toolbar with the same 35 tools
// (assets/MP/tde_icons.png, 7 x 5 cells of 64 px), tap a textdraw to select it, drag to move /
// resize, palettes, the sprite browser, the deleted textdraws depot, undo / redo, global or player
// textdraws, preview models. Projects are one file each: files/MP/tde/projects/<name>.mptd (JSON,
// net/mptd.h), the same file the MP Textdraw Editor app saves. Export = Pawn (SA-MP / open.mp,
// files/MP/tde/export/<name>.pwn); Import = .mptd or a Pawn / SA-MP editor export (sscanf-style
// argument parsing in MptdImportPawn).
// Use it on your custom server: Add server > Plugins > Textdraw > Import file.
// ================================================================================================

enum eTdeTool
{
    TDT_PROJECTS = 0, TDT_SETTINGS, TDT_LIST, TDT_DEPO, TDT_NEW, TDT_COPY, TDT_DELETE,
    TDT_POSITION, TDT_TEXTSIZE, TDT_LETTERSIZE, TDT_TEXT, TDT_FONT0, TDT_FONT1, TDT_FONT2,
    TDT_FONT3, TDT_FONT4, TDT_FONT5, TDT_ALIGN1, TDT_ALIGN2, TDT_ALIGN3, TDT_PROP_OFF,
    TDT_PROP_ON, TDT_OUTLINE, TDT_SHADOW, TDT_COLOR, TDT_BGCOLOR, TDT_BOXCOLOR, TDT_BOX_OFF,
    TDT_BOX_ON, TDT_SEL_OFF, TDT_SEL_ON, TDT_MODELS, TDT_GLOBAL, TDT_PLAYER, TDT_UNDO, TDT_COUNT
};
enum { TDE_DRAG_POS = 0, TDE_DRAG_TEXTSIZE, TDE_DRAG_LETTER };
enum { TDE_PAL_NONE = 0, TDE_PAL_COLOR, TDE_PAL_BG, TDE_PAL_BOX };

struct TdeState
{
    bool open = false;
    MptdProject pr;
    std::string file;              // project name (no extension)
    int  sel = -1;
    int  drag = TDE_DRAG_POS;      // what a drag on the canvas changes
    int  palette = TDE_PAL_NONE;
    bool dirty = false, grid = false, hideTools = false, fine = false;
    std::vector<std::vector<MptdItem>> undo, redo;
    // touch
    bool down = false, moved = false; ImVec2 downAt, last; uint64_t downMs = 0;
    uint64_t savedAt = 0;
    char text[256] = "";
};
static TdeState s_Tde;

static std::string TdeDir(const char* sub) { return std::string(s_szDataDir) + "/tde/" + sub; }
static std::string TdeProjectPath(const std::string& n) { return TdeDir("projects") + "/" + n + ".mptd"; }
static MptdItem* TdeCur() { return s_Tde.sel >= 0 && s_Tde.sel < (int)s_Tde.pr.items.size() ? &s_Tde.pr.items[s_Tde.sel] : nullptr; }
static void TdeMsg(uint32_t col, const char* fmt, ...)
{
    char b[256]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
    SysMsg(col, "{FFD84A}[TDE]{FFFFFF} %s", b);
}
static void TdePush()
{   // one undo step (the whole list: projects are small)
    s_Tde.undo.push_back(s_Tde.pr.items);
    if(s_Tde.undo.size() > 60) s_Tde.undo.erase(s_Tde.undo.begin());
    s_Tde.redo.clear();
    s_Tde.dirty = true;
}
static void TdeUndo(bool redo)
{
    auto& from = redo ? s_Tde.redo : s_Tde.undo; auto& to = redo ? s_Tde.undo : s_Tde.redo;
    if(from.empty()) { TdeMsg(COL_YELLOW, redo ? "Nothing to redo." : "Nothing to undo."); return; }
    to.push_back(s_Tde.pr.items); s_Tde.pr.items = from.back(); from.pop_back();
    if(s_Tde.sel >= (int)s_Tde.pr.items.size()) s_Tde.sel = (int)s_Tde.pr.items.size() - 1;
    s_Tde.dirty = true;
}
static bool TdeSave(bool quiet = false)
{
    if(s_Tde.file.empty()) s_Tde.file = "project";
    TdeMkDirs(TdeDir("projects"));
    if(s_Tde.pr.name.empty()) s_Tde.pr.name = s_Tde.file;
    if(s_Tde.pr.author.empty()) s_Tde.pr.author = settings.name;
    if(s_Tde.pr.created.empty()) s_Tde.pr.created = MptdNow();
    s_Tde.pr.updated = MptdNow();
    if(!TdeWriteFile(TdeProjectPath(s_Tde.file), MptdWrite(s_Tde.pr))) { TdeMsg(COL_RED, "Could not save files/MP/tde/projects/%s.mptd", s_Tde.file.c_str()); return false; }
    s_Tde.dirty = false; s_Tde.savedAt = MP_NowMs();
    if(!quiet) TdeMsg(COL_GREEN, "Saved files/MP/tde/projects/%s.mptd (%d textdraws). Add it to your server: Add server > Plugins > Textdraw.", s_Tde.file.c_str(), (int)s_Tde.pr.items.size());
    return true;
}
static bool TdeLoad(const std::string& name)
{
    MptdProject pr; std::string err;
    std::string txt = TdeReadFile(TdeProjectPath(name));
    if(txt.empty() || !MptdParse(txt, pr, &err)) return false;
    s_Tde.pr = pr; s_Tde.file = name; s_Tde.sel = pr.items.empty() ? -1 : 0;
    s_Tde.undo.clear(); s_Tde.redo.clear(); s_Tde.dirty = false;
    return true;
}
static std::string TdeCleanName(const char* in)
{
    std::string o;
    for(const char* p = in; *p && o.size() < 32; ++p)
    {
        char c = *p;
        if(isalnum((unsigned char)c) || c == '_' || c == '-') o += c;
        else if(c == ' ') o += '_';
    }
    return o;
}
static void TdeNewProject(const std::string& name)
{
    s_Tde.pr = MptdProject(); s_Tde.file = name; s_Tde.pr.name = name; s_Tde.pr.author = settings.name;
    s_Tde.pr.created = MptdNow(); s_Tde.sel = -1; s_Tde.undo.clear(); s_Tde.redo.clear(); s_Tde.dirty = true;
}
static void TdeOpen(const char* name)
{
    if(SampDialogOpen()) CloseDialog(0);
    std::string n = name && *name ? TdeCleanName(name) : std::string();
    if(!n.empty()) { if(!TdeLoad(n)) { TdeNewProject(n); TdeMsg(COL_SYSTEM, "New project %s.", n.c_str()); } }
    else if(s_Tde.file.empty())
    {   // the last project, or a new one
        std::vector<std::string> f; TdeListDir(TdeDir("projects"), ".mptd", f);
        if(!f.empty()) { std::sort(f.begin(), f.end()); TdeLoad(f.back().substr(0, f.back().size() - 5)); }
        else TdeNewProject("project");
    }
    s_Tde.open = true; s_Tde.palette = TDE_PAL_NONE;
    TdeMsg(COL_SYSTEM, "Editing %s. Tap a textdraw to select it, drag to move. /tde close (saves) - /tde help.", s_Tde.file.c_str());
}
static void TdeClose()
{
    if(s_Tde.dirty) TdeSave(true);
    s_Tde.open = false; s_Tde.down = false;
    if(VKeyboard::IsOpen()) VKeyboard::Close();
    TdeMsg(COL_SYSTEM, "Editor closed (saved %s.mptd).", s_Tde.file.c_str());
}

// ---- tool actions -----------------------------------------------------------------------------
static void TdeAdd(const MptdItem* copyOf)
{
    if(s_Tde.pr.items.size() >= 1024) { TdeMsg(COL_RED, "1024 textdraws is the limit."); return; }
    TdePush();
    MptdItem it; if(copyOf) { it = *copyOf; it.x += 6; it.y += 6; it.var.clear(); }
    s_Tde.pr.items.push_back(it); s_Tde.sel = (int)s_Tde.pr.items.size() - 1;
}
static void TdeDelete()
{
    MptdItem* c = TdeCur(); if(!c) return;
    TdePush();
    MptdItem d = *c; d.deletedAt = MptdNow();
    s_Tde.pr.deleted.push_back(d); if(s_Tde.pr.deleted.size() > 200) s_Tde.pr.deleted.erase(s_Tde.pr.deleted.begin());
    s_Tde.pr.items.erase(s_Tde.pr.items.begin() + s_Tde.sel);
    if(s_Tde.sel >= (int)s_Tde.pr.items.size()) s_Tde.sel = (int)s_Tde.pr.items.size() - 1;
    TdeMsg(COL_SYSTEM, "Moved to the depot (Depo restores it).");
}
static std::string TdeItemLabel(const MptdItem& t, int i)
{
    char b[160];
    const char* kind = t.font == 4 ? "Sprite" : t.font == 5 ? "Model" : "Text";
    std::string tx = t.font == 5 ? std::to_string(t.model) : t.text; if(tx.size() > 40) tx = tx.substr(0, 40) + "...";
    snprintf(b, sizeof(b), "%d. %s %s\t%s\t%.0f, %.0f", i, t.player ? "{FFB84A}Player{FFFFFF}" : "{8CC8FF}Global{FFFFFF}", kind, tx.c_str(), t.x, t.y);
    return b;
}
static void TdeListDlg(int resp, int item, const char*, int)
{
    if(resp && item >= 0 && item < (int)s_Tde.pr.items.size()) s_Tde.sel = item;
}
static void TdeShowList()
{
    if(s_Tde.pr.items.empty()) { TdeMsg(COL_YELLOW, "No textdraws yet: tap New."); return; }
    std::string b = "Textdraw\tText\tPosition\n";
    for(int i = 0; i < (int)s_Tde.pr.items.size(); ++i) b += TdeItemLabel(s_Tde.pr.items[i], i) + "\n";
    b.pop_back();
    ShowDialog(0, DIALOG_STYLE_TABLIST_HEADERS, "Textdraws", b.c_str(), "Select", "Close", TdeListDlg);
}
static void TdeDepoDlg(int resp, int item, const char*, int)
{
    if(!resp || item < 0 || item >= (int)s_Tde.pr.deleted.size()) return;
    TdePush();
    MptdItem it = s_Tde.pr.deleted[item]; it.deletedAt.clear();
    s_Tde.pr.deleted.erase(s_Tde.pr.deleted.begin() + item);
    s_Tde.pr.items.push_back(it); s_Tde.sel = (int)s_Tde.pr.items.size() - 1;
    TdeMsg(COL_GREEN, "Textdraw restored.");
}
static void TdeShowDepo()
{
    if(s_Tde.pr.deleted.empty()) { TdeMsg(COL_YELLOW, "The depot is empty (deleted textdraws go here)."); return; }
    std::string b = "Deleted textdraw\tText\tDeleted\n";
    for(int i = 0; i < (int)s_Tde.pr.deleted.size(); ++i)
    {
        const MptdItem& t = s_Tde.pr.deleted[i];
        std::string tx = t.font == 5 ? std::to_string(t.model) : t.text; if(tx.size() > 36) tx = tx.substr(0, 36) + "...";
        b += std::to_string(i) + ". " + (t.font == 4 ? "Sprite" : t.font == 5 ? "Model" : "Text") + "\t" + tx + "\t" + t.deletedAt + "\n";
    }
    b.pop_back();
    ShowDialog(0, DIALOG_STYLE_TABLIST_HEADERS, "Depo - tap to restore", b.c_str(), "Restore", "Close", TdeDepoDlg);
}
// text: SA-MP style (~n~ new line, ~r~ colours, _ = space)
static void TdeTextKb(const char* text, void*)
{
    MptdItem* c = TdeCur(); if(!c || !text) return;
    TdePush(); c->text = text[0] ? text : "_";
}
static void TdeSpriteDlg(int resp, int item, const char*, int)
{
    MptdItem* c = TdeCur(); if(!c || !resp || item < 0 || item >= k_nTdeSprites) return;
    TdePush(); c->font = 4; c->text = k_TdeSprites[item];
    if(c->textX <= 0 || c->textY <= 0) { c->textX = 64; c->textY = 64; }
    if((c->color & 0xFF) == 0) c->color = 0xFFFFFFFF;
}
static void TdeShowSprites()
{
    std::string b;
    for(int i = 0; i < k_nTdeSprites; ++i) { b += k_TdeSprites[i]; b += '\n'; }
    if(!b.empty()) b.pop_back();
    ShowDialog(0, DIALOG_STYLE_LIST, "Sprite browser (font 4)", b.c_str(), "Use", "Close", TdeSpriteDlg);
}
static void TdeSetFont(int f)
{
    MptdItem* c = TdeCur(); if(!c) return;
    if(f == 4) { TdeShowSprites(); return; }
    TdePush();
    const int old = c->font; c->font = f;
    if(f == 5)
    {
        if(!c->model) c->model = 411;
        if(c->textX <= 0 || c->textY <= 0) { c->textX = 90; c->textY = 90; }
        c->useBox = 1;   // preview models need a box / textsize
    }
    else if(old == 4 || old == 5) { if(c->text.find(':') != std::string::npos) c->text = "New textdraw"; c->textX = 0; c->textY = 0; }
}
static void TdeModelDlg(int resp, int, const char* input, int what)
{
    MptdItem* c = TdeCur(); if(!c || !resp || !input) return;
    TdePush();
    if(what == 0) { int m = atoi(input); if(m >= 0 && m < 20000) { c->model = m; c->font = 5; } }
    else if(what == 1)
    {   // rx ry rz zoom (sscanf "ffff")
        float r[4] = { c->rot[0], c->rot[1], c->rot[2], c->rot[3] };
        if(sscanf(input, "%f %f %f %f", &r[0], &r[1], &r[2], &r[3]) >= 3) memcpy(c->rot, r, sizeof(r));
    }
    else
    {
        int a = -1, b = -1; if(sscanf(input, "%d %d", &a, &b) >= 1) { c->vehCol[0] = a; c->vehCol[1] = b; }
    }
}
static void TdeModelsMenu(int resp, int item, const char*, int)
{
    MptdItem* c = TdeCur(); if(!c || !resp) return;
    char cur[96];
    if(item == 0) { snprintf(cur, sizeof(cur), "Model id (skins 0-311, objects, vehicles 400-611). Now: %d", c->model); ShowDialog(0, DIALOG_STYLE_INPUT, "Preview model", cur, "Set", "Back", TdeModelDlg, 0); }
    else if(item == 1) { snprintf(cur, sizeof(cur), "Rotation X Y Z and zoom. Now: %.0f %.0f %.0f %.2f", c->rot[0], c->rot[1], c->rot[2], c->rot[3]); ShowDialog(0, DIALOG_STYLE_INPUT, "Preview rotation", cur, "Set", "Back", TdeModelDlg, 1); }
    else if(item == 2) { snprintf(cur, sizeof(cur), "Vehicle colours: colour1 colour2 (-1 = random). Now: %d %d", c->vehCol[0], c->vehCol[1]); ShowDialog(0, DIALOG_STYLE_INPUT, "Preview vehicle colours", cur, "Set", "Back", TdeModelDlg, 2); }
}
static void TdeShowModels()
{
    MptdItem* c = TdeCur(); if(!c) return;
    if(c->font != 5) TdeSetFont(5);
    ShowDialog(0, DIALOG_STYLE_LIST, "Preview model (font 5)", "Model id\nRotation and zoom\nVehicle colours", "Edit", "Close", TdeModelsMenu);
}
static void TdeColorKb(const char* text, void* who)
{
    MptdItem* c = TdeCur(); if(!c || !text) return;
    const char* s = text; while(*s == '#' || *s == ' ') ++s;
    if(s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    size_t n = strlen(s); if(!n) return;
    uint32_t v = (uint32_t)strtoul(s, nullptr, 16); if(n <= 6) v = (v << 8) | 0xFF;
    TdePush();
    const int w = (int)(intptr_t)who;
    (w == TDE_PAL_COLOR ? c->color : w == TDE_PAL_BG ? c->bgColor : c->boxColor) = v;
}
// projects menu: new, open, save, save as, export Pawn (SA-MP / open.mp), import, delete
static void TdeProjNameKb(const char* text, void* what)
{
    std::string n = TdeCleanName(text ? text : ""); if(n.empty()) return;
    const int w = (int)(intptr_t)what;
    if(w == 0) { if(s_Tde.dirty) TdeSave(true); TdeNewProject(n); TdeMsg(COL_SYSTEM, "New project %s.", n.c_str()); }
    else { s_Tde.file = n; s_Tde.pr.name = n; TdeSave(); }
}
static std::vector<std::string> s_TdeFiles;
static void TdeOpenDlg(int resp, int item, const char*, int del)
{
    if(!resp || item < 0 || item >= (int)s_TdeFiles.size()) return;
    std::string n = s_TdeFiles[item].substr(0, s_TdeFiles[item].size() - 5);
    if(del)
    {
        if(n == s_Tde.file) { TdeMsg(COL_YELLOW, "That project is open: open another one first."); return; }
        remove(TdeProjectPath(n).c_str()); TdeMsg(COL_SYSTEM, "Deleted %s.mptd.", n.c_str()); return;
    }
    if(s_Tde.dirty) TdeSave(true);
    if(TdeLoad(n)) TdeMsg(COL_GREEN, "Opened %s (%d textdraws).", n.c_str(), (int)s_Tde.pr.items.size());
    else TdeMsg(COL_RED, "%s.mptd is not an MP textdraw file.", n.c_str());
}
static void TdeImportDlg(int resp, int item, const char*, int)
{
    if(!resp || item < 0 || item >= (int)s_TdeFiles.size()) return;
    std::string txt = TdeReadFile(s_TdeFiles[item]);
    MptdProject pr; std::string err;
    int n = 0;
    if(MptdParse(txt, pr, &err)) n = (int)pr.items.size(); else n = MptdImportPawn(txt, pr);
    if(n <= 0) { TdeMsg(COL_RED, "No textdraws found in that file."); return; }
    TdePush();
    for(auto& t : pr.items) if(s_Tde.pr.items.size() < 1024) s_Tde.pr.items.push_back(t);
    s_Tde.sel = (int)s_Tde.pr.items.size() - 1;
    TdeMsg(COL_GREEN, "Imported %d textdraws.", n);
}
static void TdeProjectsDlg(int resp, int item, const char*, int)
{
    if(!resp) return;
    switch(item)
    {
        case 0: VKeyboard::Open("New project name", "", 32, TdeProjNameKb, (void*)(intptr_t)0); break;
        case 1: case 6:
        {
            s_TdeFiles.clear(); TdeListDir(TdeDir("projects"), ".mptd", s_TdeFiles); std::sort(s_TdeFiles.begin(), s_TdeFiles.end());
            if(s_TdeFiles.empty()) { TdeMsg(COL_YELLOW, "No saved projects yet."); return; }
            std::string b; for(auto& f : s_TdeFiles) b += f + "\n"; b.pop_back();
            ShowDialog(0, DIALOG_STYLE_LIST, item == 1 ? "Open project" : "Delete project", b.c_str(), item == 1 ? "Open" : "Delete", "Back", TdeOpenDlg, item == 6);
            break;
        }
        case 2: TdeSave(); break;
        case 3: VKeyboard::Open("Save as", s_Tde.file.c_str(), 32, TdeProjNameKb, (void*)(intptr_t)1); break;
        case 4: case 5:
        {
            TdeMkDirs(TdeDir("export"));
            const std::string p = TdeDir("export") + "/" + s_Tde.file + (item == 5 ? "_openmp" : "") + ".pwn";
            if(TdeWriteFile(p, MptdExportPawn(s_Tde.pr, item == 5))) TdeMsg(COL_GREEN, "Exported files/MP/tde/export/%s%s.pwn", s_Tde.file.c_str(), item == 5 ? "_openmp" : "");
            else TdeMsg(COL_RED, "Could not write %s", p.c_str());
            break;
        }
        case 7:
        {
            PluginCandidates(s_TdeFiles);
            std::vector<std::string> pw; TdeListDir(TdeDir("import"), ".pwn", pw);
            for(auto& f : pw) s_TdeFiles.push_back(TdeDir("import") + "/" + f);
            if(s_TdeFiles.empty()) { TdeMsg(COL_YELLOW, "Put a .mptd, .pwn or .txt export in files/MP/tde/import/ first."); return; }
            std::string b; for(auto& f : s_TdeFiles) b += f.substr(f.find_last_of('/') + 1) + "\n"; b.pop_back();
            ShowDialog(0, DIALOG_STYLE_LIST, "Import textdraws", b.c_str(), "Import", "Back", TdeImportDlg);
            break;
        }
        case 8: TdeClose(); break;
    }
}
static void TdeShowProjects()
{
    char cap[96]; snprintf(cap, sizeof(cap), "Projects - %s%s", s_Tde.file.c_str(), s_Tde.dirty ? " *" : "");
    ShowDialog(0, DIALOG_STYLE_LIST, cap, "New project\nOpen project\nSave\nSave as\nExport Pawn (SA-MP)\nExport Pawn (open.mp)\nDelete project\nImport (.mptd / .pwn / .txt)\nClose the editor", "Select", "Back", TdeProjectsDlg);
}
static void TdeVarKb(const char* text, void* which)
{
    std::string n = TdeCleanName(text ? text : ""); if(n.empty() || isdigit((unsigned char)n[0])) return;
    const int w = (int)(intptr_t)which;
    if(w == 0) s_Tde.pr.globalVar = n; else if(w == 1) s_Tde.pr.playerVar = n;
    else if(MptdItem* c = TdeCur()) { TdePush(); c->var = n; }
    s_Tde.dirty = true;
}
static void TdeSettingsDlg(int resp, int item, const char*, int)
{
    if(!resp) return;
    switch(item)
    {
        case 0: VKeyboard::Open("Global textdraw array", s_Tde.pr.globalVar.c_str(), 32, TdeVarKb, (void*)(intptr_t)0); break;
        case 1: VKeyboard::Open("Player textdraw array", s_Tde.pr.playerVar.c_str(), 32, TdeVarKb, (void*)(intptr_t)1); break;
        case 2: if(TdeCur()) VKeyboard::Open("Own variable (empty = array)", TdeCur()->var.c_str(), 32, TdeVarKb, (void*)(intptr_t)2); break;
        case 3: s_Tde.grid = !s_Tde.grid; TdeMsg(COL_SYSTEM, "Grid snap %s.", s_Tde.grid ? "on (5 px)" : "off"); break;
        case 4: s_Tde.fine = !s_Tde.fine; TdeMsg(COL_SYSTEM, "Fine drag %s.", s_Tde.fine ? "on (1/4 speed)" : "off"); break;
        case 5: s_Tde.hideTools = !s_Tde.hideTools; break;
    }
}
static void TdeShowSettings()
{
    char b[400];
    snprintf(b, sizeof(b), "Global array name\t%s\nPlayer array name\t%s\nThis textdraw's own variable\t%s\nGrid snap\t%s\nFine drag\t%s\nHide the tool bar (tap the corner to show)\t%s",
             s_Tde.pr.globalVar.c_str(), s_Tde.pr.playerVar.c_str(), TdeCur() && !TdeCur()->var.empty() ? TdeCur()->var.c_str() : "-",
             s_Tde.grid ? "On" : "Off", s_Tde.fine ? "On" : "Off", s_Tde.hideTools ? "On" : "Off");
    ShowDialog(0, DIALOG_STYLE_TABLIST, "Settings", b, "Change", "Close", TdeSettingsDlg);
}

static void TdeTool(int t)
{
    MpUiClick();
    MptdItem* c = TdeCur();
    const bool needSel = t >= TDT_COPY && t != TDT_UNDO;
    if(needSel && !c) { TdeMsg(COL_YELLOW, "Select a textdraw first (tap it, or List)."); return; }
    s_Tde.palette = TDE_PAL_NONE;
    switch(t)
    {
        case TDT_PROJECTS: TdeShowProjects(); break;
        case TDT_SETTINGS: TdeShowSettings(); break;
        case TDT_LIST:     TdeShowList(); break;
        case TDT_DEPO:     TdeShowDepo(); break;
        case TDT_NEW:      TdeAdd(nullptr); break;
        case TDT_COPY:     TdeAdd(c); break;
        case TDT_DELETE:   TdeDelete(); break;
        case TDT_POSITION: s_Tde.drag = TDE_DRAG_POS; break;
        case TDT_TEXTSIZE: s_Tde.drag = TDE_DRAG_TEXTSIZE; if(c->font < 4 && !c->useBox) { TdePush(); c->useBox = 1; } break;
        case TDT_LETTERSIZE: s_Tde.drag = TDE_DRAG_LETTER; break;
        case TDT_TEXT:     if(c->font == 4) TdeShowSprites(); else if(c->font == 5) TdeShowModels(); else VKeyboard::Open("Textdraw text (~n~ new line, ~r~ red, _ = space)", c->text.c_str(), 255, TdeTextKb); break;
        case TDT_FONT0: case TDT_FONT1: case TDT_FONT2: case TDT_FONT3: case TDT_FONT4: case TDT_FONT5: TdeSetFont(t - TDT_FONT0); break;
        case TDT_ALIGN1: case TDT_ALIGN2: case TDT_ALIGN3: TdePush(); c->align = t - TDT_ALIGN1 + 1; break;
        case TDT_PROP_OFF: case TDT_PROP_ON: TdePush(); c->proportional = !c->proportional; break;
        case TDT_OUTLINE:  TdePush(); c->outline = (c->outline + 1) % 5; if(c->outline) c->shadow = 0; break;
        case TDT_SHADOW:   TdePush(); c->shadow = (c->shadow + 1) % 5; if(c->shadow) c->outline = 0; break;
        case TDT_COLOR:    s_Tde.palette = TDE_PAL_COLOR; break;
        case TDT_BGCOLOR:  s_Tde.palette = TDE_PAL_BG; break;
        case TDT_BOXCOLOR: s_Tde.palette = TDE_PAL_BOX; if(!c->useBox) { TdePush(); c->useBox = 1; } break;
        case TDT_BOX_OFF: case TDT_BOX_ON: TdePush(); c->useBox = !c->useBox; break;
        case TDT_SEL_OFF: case TDT_SEL_ON: TdePush(); c->selectable = !c->selectable; break;
        case TDT_MODELS:   TdeShowModels(); break;
        case TDT_GLOBAL: case TDT_PLAYER: TdePush(); c->player = !c->player; break;
        case TDT_UNDO: break;   // the icon has two halves (TdeRender)
    }
}

// ---- drawing ----------------------------------------------------------------------------------
static void TdeIcon(ImDrawList* dl, void* tex, ImVec2 uv, int idx, ImVec2 a, float sz, ImU32 tint)
{
    const int col = idx % 7, row = idx / 7;
    ImVec2 u0(uv.x * col / 7.0f, uv.y * row / 5.0f), u1(uv.x * (col + 1) / 7.0f, uv.y * (row + 1) / 5.0f);
    dl->AddImage((ImTextureID)tex, a, ImVec2(a.x + sz, a.y + sz), u0, u1, tint);
}
static bool TdeToolActive(int t, const MptdItem* c)
{
    if(!c) return false;
    switch(t)
    {
        case TDT_POSITION: return s_Tde.drag == TDE_DRAG_POS;
        case TDT_TEXTSIZE: return s_Tde.drag == TDE_DRAG_TEXTSIZE;
        case TDT_LETTERSIZE: return s_Tde.drag == TDE_DRAG_LETTER;
        case TDT_FONT0: case TDT_FONT1: case TDT_FONT2: case TDT_FONT3: case TDT_FONT4: case TDT_FONT5: return c->font == t - TDT_FONT0;
        case TDT_ALIGN1: case TDT_ALIGN2: case TDT_ALIGN3: return c->align == t - TDT_ALIGN1 + 1;
        case TDT_OUTLINE: return c->outline > 0;
        case TDT_SHADOW: return c->shadow > 0;
        case TDT_COLOR: return s_Tde.palette == TDE_PAL_COLOR;
        case TDT_BGCOLOR: return s_Tde.palette == TDE_PAL_BG;
        case TDT_BOXCOLOR: return s_Tde.palette == TDE_PAL_BOX;
        default: return false;
    }
}
// which icons the bar shows (toggles show their current state: Proportion on/off, Box on/off...)
static void TdeBarTools(const MptdItem* c, std::vector<int>& out)
{
    out = { TDT_PROJECTS, TDT_SETTINGS, TDT_LIST, TDT_DEPO, TDT_NEW, TDT_UNDO };
    if(!c) return;
    const int more[] = { TDT_COPY, TDT_DELETE, TDT_POSITION, TDT_TEXTSIZE, TDT_LETTERSIZE, TDT_TEXT, TDT_FONT0, TDT_FONT1, TDT_FONT2, TDT_FONT3, TDT_FONT4, TDT_FONT5,
                         TDT_ALIGN1, TDT_ALIGN2, TDT_ALIGN3 };
    for(int t : more) out.push_back(t);
    out.push_back(c->proportional ? TDT_PROP_ON : TDT_PROP_OFF);
    out.push_back(TDT_OUTLINE); out.push_back(TDT_SHADOW); out.push_back(TDT_COLOR); out.push_back(TDT_BGCOLOR); out.push_back(TDT_BOXCOLOR);
    out.push_back(c->useBox ? TDT_BOX_ON : TDT_BOX_OFF);
    out.push_back(c->selectable ? TDT_SEL_ON : TDT_SEL_OFF);
    out.push_back(TDT_MODELS);
    out.push_back(c->player ? TDT_PLAYER : TDT_GLOBAL);
}
static void TdeApplyDrag(MptdItem& c, float dx, float dy)
{   // dx / dy in 640x448 canvas units
    if(s_Tde.fine) { dx *= 0.25f; dy *= 0.25f; }
    switch(s_Tde.drag)
    {
        case TDE_DRAG_POS: c.x += dx; c.y += dy; break;
        case TDE_DRAG_TEXTSIZE:
            if(c.font == 4 || c.font == 5) { c.textX += dx; c.textY += dy; }
            else if(c.align == 2) { if(c.textY <= 0) c.textY = 60; c.textY += dx * 2.0f; if(c.textX <= 0) c.textX = c.letterY * 9.0f; c.textX += dy; }
            else { if(c.textX <= 0) c.textX = c.x + 60; c.textX += dx; if(c.textY <= 0) c.textY = c.letterY * 9.0f; c.textY += dy; }
            break;
        case TDE_DRAG_LETTER:
            c.letterX = fmaxf(0.01f, c.letterX + dx * 0.004f); c.letterY = fmaxf(0.05f, c.letterY + dy * 0.02f); break;
    }
}
static void TdeSnap(MptdItem& c)
{
    if(!s_Tde.grid) return;
    auto sn = [](float v) { return roundf(v / 5.0f) * 5.0f; };
    if(s_Tde.drag == TDE_DRAG_POS) { c.x = sn(c.x); c.y = sn(c.y); }
    else if(s_Tde.drag == TDE_DRAG_TEXTSIZE) { c.textX = sn(c.textX); c.textY = sn(c.textY); }
}

static void TdeRender()
{
    if(!s_Tde.open) return;
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 ds = io.DisplaySize;
    const float kx = ds.x / 640.0f, ky = ds.y / 448.0f;
    const bool modal = SampDialogOpen() || VKeyboard::IsOpen();

    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ds);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##Tde", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus | (modal ? ImGuiWindowFlags_NoInputs : 0));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, 40));
    if(s_Tde.grid)
        for(int gx = 0; gx <= 640; gx += 40) dl->AddLine(ImVec2(gx * kx, 0), ImVec2(gx * kx, ds.y), IM_COL32(255, 255, 255, 22));
    if(s_Tde.grid)
        for(int gy = 0; gy <= 448; gy += 40) dl->AddLine(ImVec2(0, gy * ky), ImVec2(ds.x, gy * ky), IM_COL32(255, 255, 255, 22));

    // the textdraws, with the box each one covers (for tapping)
    std::vector<ImVec4> rects(s_Tde.pr.items.size());
    for(size_t i = 0; i < s_Tde.pr.items.size(); ++i) rects[i] = TdDrawOne(dl, MptdToPkt(s_Tde.pr.items[i]), 0, 0, kx, ky);
    MptdItem* c = TdeCur();
    if(c)
    {
        const ImVec4& r = rects[s_Tde.sel];
        dl->AddRect(ImVec2(r.x - 3, r.y - 3), ImVec2(r.z + 3, r.w + 3), IM_COL32(255, 216, 74, 255), 0, 0, UI::S(2.5f));
        dl->AddCircleFilled(ImVec2(c->x * kx, c->y * ky), UI::S(5), IM_COL32(255, 216, 74, 255));
    }

    // ---- tool bar (bottom, two rows) + info line (top) ----
    ImVec2 uv(1, 1); void* tex = HUD::Texture("tde_icons.png", &uv);
    const float isz = fminf(UI::S(104), ds.x / 12.5f), gap = UI::S(6);   // 3.58: bigger tools (was 64 px / 19.5 per row)
    std::vector<int> tools; TdeBarTools(c, tools);
    const int perRow = (int)((ds.x - gap) / (isz + gap));
    const int rows = s_Tde.hideTools ? 0 : ((int)tools.size() + perRow - 1) / perRow;
    const float barH = rows * (isz + gap) + gap;
    const float barY = ds.y - barH;
    struct Hit { ImVec2 a, b; int tool; };
    std::vector<Hit> hits;
    if(rows)
    {
        dl->AddRectFilled(ImVec2(0, barY), ds, IM_COL32(16, 16, 20, 225));
        for(size_t i = 0; i < tools.size(); ++i)
        {
            const int r = (int)i / perRow, k = (int)i % perRow;
            const int inRow = std::min(perRow, (int)tools.size() - r * perRow);
            const float x0 = (ds.x - inRow * (isz + gap) + gap) * 0.5f;
            ImVec2 a(x0 + k * (isz + gap), barY + gap + r * (isz + gap)), b(a.x + isz, a.y + isz);
            if(TdeToolActive(tools[i], c)) dl->AddRectFilled(a, b, IM_COL32(255, 216, 74, 60), UI::S(8));
            if(tex) TdeIcon(dl, tex, uv, tools[i], a, isz, IM_COL32_WHITE);
            else dl->AddRect(a, b, IM_COL32(255, 216, 74, 200), UI::S(6));
            hits.push_back({ a, b, tools[i] });
        }
    }
    else
    {   // hidden: one Projects button in the corner shows the bar again
        ImVec2 a(ds.x - isz - gap, ds.y - isz - gap), b(ds.x - gap, ds.y - gap);
        dl->AddRectFilled(a, b, IM_COL32(16, 16, 20, 200), UI::S(8));
        if(tex) TdeIcon(dl, tex, uv, TDT_SETTINGS, a, isz, IM_COL32_WHITE);
        hits.push_back({ a, b, -2 });
    }
    {   // info line
        char info[300];
        if(c)
        {
            const char* mode = s_Tde.drag == TDE_DRAG_POS ? "Position" : s_Tde.drag == TDE_DRAG_TEXTSIZE ? "Textsize" : "Lettersize";
            snprintf(info, sizeof(info), "%s%s  |  #%d/%d %s  |  %s: pos %.1f, %.1f  letter %.3f, %.3f  size %.1f, %.1f  font %d  colour %08X",
                     s_Tde.file.c_str(), s_Tde.dirty ? "*" : "", s_Tde.sel, (int)s_Tde.pr.items.size(), c->player ? "player" : "global", mode,
                     c->x, c->y, c->letterX, c->letterY, c->textX, c->textY, c->font, c->color);
        }
        else snprintf(info, sizeof(info), "%s%s  |  %d textdraws  |  tap New to add one, tap a textdraw to select it",
                      s_Tde.file.c_str(), s_Tde.dirty ? "*" : "", (int)s_Tde.pr.items.size());
        const float fs = UI::S(32);   // 3.58: bigger
        ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, info);
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(ds.x, ts.y + UI::S(12)), IM_COL32(16, 16, 20, 200));
        dl->AddText(UI::Font(), fs, ImVec2(UI::S(12), UI::S(6)), IM_COL32(255, 255, 255, 235), info);
        // close X (saves)
        ImVec2 xa(ds.x - ts.y - UI::S(14), 0), xb(ds.x, ts.y + UI::S(12));
        dl->AddText(UI::Font(), fs, ImVec2(xa.x + UI::S(6), UI::S(6)), IM_COL32(255, 120, 120, 255), "X");
        hits.push_back({ xa, xb, -1 });
    }
    // nudge pad (top right, under the info line): 0.5 / 1 / 5 px steps of the drag mode
    if(c && !s_Tde.hideTools)
    {
        const float bs = UI::S(92), y0 = UI::S(60), x0 = ds.x - bs * 3 - UI::S(14);
        const char* L[4] = { "<", ">", "^", "v" };
        const ImVec2 P[4] = { ImVec2(x0, y0 + bs), ImVec2(x0 + bs * 2, y0 + bs), ImVec2(x0 + bs, y0), ImVec2(x0 + bs, y0 + bs * 2) };
        for(int i = 0; i < 4; ++i)
        {
            ImVec2 a = P[i], b(a.x + bs - UI::S(4), a.y + bs - UI::S(4));
            dl->AddRectFilled(a, b, IM_COL32(16, 16, 20, 200), UI::S(8));
            ImVec2 ts = UI::Font()->CalcTextSizeA(UI::S(44), FLT_MAX, 0, L[i]);
            dl->AddText(UI::Font(), UI::S(44), ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), IM_COL32(255, 216, 74, 255), L[i]);
            hits.push_back({ a, b, 100 + i });
        }
    }
    // palette (premade colours of the original editor + Hex)
    if(c && s_Tde.palette)
    {
        const float cs = UI::S(70), pg = UI::S(8);
        const int cols = 10, prow = (k_nTdePalette + 1 + cols - 1) / cols;
        const float pw = cols * (cs + pg) + pg, ph = prow * (cs + pg) + pg + UI::S(44);
        ImVec2 pa((ds.x - pw) * 0.5f, barY - ph - UI::S(10)), pb(pa.x + pw, pa.y + ph);
        dl->AddRectFilled(pa, pb, IM_COL32(16, 16, 20, 235), UI::S(10));
        const char* title = s_Tde.palette == TDE_PAL_COLOR ? "Color" : s_Tde.palette == TDE_PAL_BG ? "Background / outline colour" : "Box colour";
        dl->AddText(UI::Font(), UI::S(32), ImVec2(pa.x + pg * 2, pa.y + pg), IM_COL32_WHITE, title);
        for(int i = 0; i <= k_nTdePalette; ++i)
        {
            const int r = i / cols, k = i % cols;
            ImVec2 a(pa.x + pg + k * (cs + pg), pa.y + UI::S(44) + pg + r * (cs + pg)), b(a.x + cs, a.y + cs);
            if(i < k_nTdePalette) { dl->AddRectFilled(a, b, TdCol(k_TdePalette[i] | 0xFF), UI::S(6)); dl->AddRect(a, b, IM_COL32(255, 255, 255, 60), UI::S(6)); }
            else { dl->AddRect(a, b, IM_COL32(255, 216, 74, 255), UI::S(6), 0, 2); dl->AddText(UI::Font(), UI::S(28), ImVec2(a.x + UI::S(8), a.y + UI::S(20)), IM_COL32_WHITE, "HEX"); }
            hits.push_back({ a, b, 200 + i });
        }
        hits.push_back({ pa, pb, 199 });   // the panel itself (no canvas tap through it)
    }
    // alpha slider row for the palette: tap left / right half of the title line
    ImGui::End(); ImGui::PopStyleVar(2); ImGui::PopStyleColor();
    if(modal) { s_Tde.down = false; return; }

    // ---- touch ----
    const ImVec2 m = io.MousePos;
    auto inside = [](ImVec2 p, ImVec2 a, ImVec2 b) { return p.x >= a.x && p.x <= b.x && p.y >= a.y && p.y <= b.y; };
    if(ImGui::IsMouseClicked(0))
    {
        s_Tde.down = true; s_Tde.moved = false; s_Tde.downAt = m; s_Tde.last = m; s_Tde.downMs = MP_NowMs();
        int hit = -100;
        for(auto it = hits.rbegin(); it != hits.rend(); ++it) if(inside(m, it->a, it->b)) { hit = it->tool; if(hit == TDT_UNDO) hit = m.x < (it->a.x + it->b.x) * 0.5f ? 300 : 301; break; }
        if(hit != -100)
        {
            s_Tde.down = false;
            if(hit == -1) TdeClose();
            else if(hit == -2) s_Tde.hideTools = false;
            else if(hit >= 300) { MpUiClick(); TdeUndo(hit == 301); }
            else if(hit >= 200 && c)
            {
                const int i = hit - 200;
                if(i >= k_nTdePalette)
                {
                    uint32_t cur = s_Tde.palette == TDE_PAL_COLOR ? c->color : s_Tde.palette == TDE_PAL_BG ? c->bgColor : c->boxColor;
                    char hx[12]; snprintf(hx, sizeof(hx), "%08X", cur);
                    VKeyboard::Open("Colour RRGGBBAA (hex)", hx, 10, TdeColorKb, (void*)(intptr_t)s_Tde.palette);
                }
                else
                {
                    TdePush();
                    uint32_t& dst = s_Tde.palette == TDE_PAL_COLOR ? c->color : s_Tde.palette == TDE_PAL_BG ? c->bgColor : c->boxColor;
                    uint32_t a = dst & 0xFF; if(!a) a = s_Tde.palette == TDE_PAL_COLOR ? 0xFF : 0x96;
                    dst = (k_TdePalette[i] & 0xFFFFFF00) | a;
                }
            }
            else if(hit == 199) {}
            else if(hit >= 100 && c)
            {
                const float st = s_Tde.fine ? 0.5f : 1.0f;
                TdePush();
                const float dx = hit == 100 ? -st : hit == 101 ? st : 0, dy = hit == 102 ? -st : hit == 103 ? st : 0;
                const bool f = s_Tde.fine; s_Tde.fine = false;
                if(s_Tde.drag == TDE_DRAG_LETTER) TdeApplyDrag(*c, dx * 2.5f, dy * 0.5f); else TdeApplyDrag(*c, dx, dy);
                s_Tde.fine = f;
            }
            else if(hit >= 0) TdeTool(hit);
            return;
        }
        // canvas: select the top one under the finger (keep the current one when it is under it too)
        int pick = -1;
        if(c) { const ImVec4& r = rects[s_Tde.sel]; if(m.x >= r.x - 8 && m.x <= r.z + 8 && m.y >= r.y - 8 && m.y <= r.w + 8) pick = s_Tde.sel; }
        if(pick < 0) for(int i = (int)rects.size() - 1; i >= 0; --i) { const ImVec4& r = rects[i]; if(m.x >= r.x && m.x <= r.z && m.y >= r.y && m.y <= r.w) { pick = i; break; } }
        if(pick >= 0 && pick != s_Tde.sel) { s_Tde.sel = pick; s_Tde.down = false; return; }
        if(pick < 0) { s_Tde.palette = TDE_PAL_NONE; if(!c) s_Tde.down = false; }
        if(s_Tde.down && c) TdePush();
    }
    if(s_Tde.down && ImGui::IsMouseDown(0) && (c = TdeCur()))
    {
        const float dx = (m.x - s_Tde.last.x) / kx, dy = (m.y - s_Tde.last.y) / ky;
        if(fabsf(m.x - s_Tde.downAt.x) + fabsf(m.y - s_Tde.downAt.y) > UI::S(6)) s_Tde.moved = true;
        if(s_Tde.moved && (dx || dy)) TdeApplyDrag(*c, dx, dy);
        s_Tde.last = m;
    }
    if(s_Tde.down && !ImGui::IsMouseDown(0))
    {
        s_Tde.down = false;
        if(MptdItem* cc = TdeCur()) TdeSnap(*cc);
        if(!s_Tde.moved && !s_Tde.undo.empty()) s_Tde.undo.pop_back();   // a plain tap changed nothing
    }
    if(s_Tde.dirty && MP_NowMs() - s_Tde.savedAt > 60000) TdeSave(true);   // auto-save every minute
}

static bool TdeCommand(const char* cmd, const char* arg)
{
    if(strcmp(cmd, "tde") && strcmp(cmd, "textdraw") && strcmp(cmd, "tdeditor")) return false;
    char sub[24] = ""; int n = 0; const char* p = arg;
    while(*p && *p != ' ' && n < 22) sub[n++] = (char)tolower((unsigned char)*p++);
    sub[n] = 0; while(*p == ' ') ++p;
    if(!strcmp(sub, "help"))
    {
        TdeMsg(COL_SYSTEM, "/tde [project] opens the editor, /tde close, /tde save, /tde new <name>, /tde open <name>, /tde export [openmp].");
        TdeMsg(COL_SYSTEM, "Files: files/MP/tde/projects/*.mptd - put it on your server with Add server > Plugins > Textdraw > Import file.");
        return true;
    }
    if(!strcmp(sub, "close") || !strcmp(sub, "exit")) { if(s_Tde.open) TdeClose(); return true; }
    if(!strcmp(sub, "save")) { if(s_Tde.file.empty()) TdeMsg(COL_YELLOW, "Open the editor first (/tde)."); else TdeSave(); return true; }
    if(!strcmp(sub, "new")) { std::string nm = TdeCleanName(*p ? p : "project"); if(s_Tde.dirty) TdeSave(true); TdeNewProject(nm); if(!s_Tde.open) TdeOpen(nullptr); return true; }
    if(!strcmp(sub, "open")) { if(s_Tde.dirty) TdeSave(true); s_Tde.file.clear(); TdeOpen(p); return true; }
    if(!strcmp(sub, "export"))
    {
        if(s_Tde.file.empty()) { TdeMsg(COL_YELLOW, "Open the editor first (/tde)."); return true; }
        TdeProjectsDlg(1, !strncasecmp(p, "open", 4) ? 5 : 4, nullptr, 0);
        return true;
    }
    if(s_Tde.open && !sub[0]) { TdeClose(); return true; }
    if(sub[0] && strcmp(s_Tde.file.c_str(), sub)) { if(s_Tde.dirty) TdeSave(true); s_Tde.file.clear(); }
    TdeOpen(sub[0] ? arg : nullptr);
    return true;
}
