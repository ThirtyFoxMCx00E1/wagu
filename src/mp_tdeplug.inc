// 3.50: MP Launcher > Add server > Plugins. Type "Textdraw" (the two others are not used yet):
// "Import file" takes a .mptd made by the in-game editor (/tde) or the MP Textdraw Editor app and
// copies it into files/MP/<your server name>/. When this phone hosts that server, every .mptd in
// the folder is shown to all players (MPServer::SetPluginTextdraws, ids 64..255).
#include "net/mptd.h"
#include "ui/tde_data.h"   // 3.50: sprite browser list + palette (samp-textdraw-editor)
#include <sys/stat.h>

static const char* const k_PluginTypes[] = { "Textdraw", "Unknown", "Unknown" };

static std::string TdeReadFile(const std::string& p)
{
    std::string s; FILE* f = fopen(p.c_str(), "rb"); if(!f) return s;
    char b[4096]; size_t n; while((n = fread(b, 1, sizeof(b), f)) > 0) s.append(b, n);
    fclose(f); return s;
}
static bool TdeWriteFile(const std::string& p, const std::string& s)
{
    FILE* f = fopen(p.c_str(), "wb"); if(!f) return false;
    bool ok = fwrite(s.data(), 1, s.size(), f) == s.size(); fclose(f); return ok;
}
static void TdeMkDirs(const std::string& p)
{
    for(size_t i = 1; i <= p.size(); ++i) if(i == p.size() || p[i] == '/') mkdir(p.substr(0, i).c_str(), 0775);
}
static bool TdeEndsWith(const std::string& s, const char* e) { size_t n = strlen(e); return s.size() >= n && !strcasecmp(s.c_str() + s.size() - n, e); }
static void TdeListDir(const std::string& d, const char* ext, std::vector<std::string>& out)
{   // file names (not paths), sorted
    DIR* dp = opendir(d.c_str()); if(!dp) return;
    while(dirent* de = readdir(dp)) { std::string n = de->d_name; if(n[0] != '.' && (!ext || TdeEndsWith(n, ext))) out.push_back(n); }
    closedir(dp);
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return strcasecmp(a.c_str(), b.c_str()) < 0; });
}
// "{FF0000}My Server: 24/7" -> "My Server_ 24_7" (colour codes out, no path characters)
static std::string PluginFolderName(const char* name)
{
    std::string o;
    for(const char* p = name ? name : ""; *p; ++p)
    {
        if(*p == '{' && strlen(p) >= 8 && p[7] == '}') { p += 7; continue; }
        unsigned char c = (unsigned char)*p;
        o += (c < 32 || strchr("/\\:*?\"<>|", c)) ? '_' : (char)c;
    }
    while(!o.empty() && (o.back() == ' ' || o.back() == '.')) o.pop_back();
    while(!o.empty() && (o[0] == ' ' || o[0] == '.')) o.erase(0, 1);
    if(o.empty()) o = "server";
    if(o.size() > 60) o.resize(60);
    return o;
}
static std::string PluginDir(const char* serverName) { return std::string(s_szDataDir) + "/" + PluginFolderName(serverName); }

static PktTextDraw MptdToPkt(const MptdItem& t)
{
    PktTextDraw d; memset(&d, 0, sizeof(d));
    d.type = PKT_TEXTDRAW; d.show = 1;
    d.x = t.x; d.y = t.y; d.font = (uint8_t)(t.font < 0 || t.font > 5 ? 1 : t.font);
    d.color = t.color; d.boxColor = t.boxColor; d.bgColor = t.bgColor;
    d.align = (uint8_t)(t.align < 1 || t.align > 3 ? 1 : t.align);
    d.outline = (uint8_t)std::max(0, std::min(255, t.outline)); d.shadow = (uint8_t)std::max(0, std::min(255, t.shadow));
    d.useBox = t.useBox ? 1 : 0; d.proportional = t.proportional ? 1 : 0;
    if(d.font == 4 || d.font == 5)
    {
        d.letterX = t.textX; d.letterY = t.textY;   // the size of the picture
        if(d.font == 5) snprintf(d.text, sizeof(d.text), "Model %d", t.model);
        else MP_CopyStr(d.text, t.text.c_str(), sizeof(d.text));
    }
    else
    {
        d.letterX = t.letterX; d.letterY = t.letterY;
        d.boxX = t.textX > 0 || t.textY > 0 ? (d.align == 2 ? t.x + t.textY : t.textX) : 0;
        MP_CopyStr(d.text, t.text.c_str(), sizeof(d.text));
    }
    return d;
}

// the .mptd files of a server (names)
static void PluginList(const char* serverName, std::vector<std::string>& out) { out.clear(); TdeListDir(PluginDir(serverName), ".mptd", out); }

// hosting this server now: send its textdraws to everybody
static int PluginApply(const char* serverName)
{
    if(!s_Server.IsRunning()) return 0;
    if(serverName && strcasecmp(PluginFolderName(serverName).c_str(), PluginFolderName(s_Server.GetName()).c_str())) return 0;
    std::vector<std::string> files; PluginList(s_Server.GetName(), files);
    std::vector<PktTextDraw> tds;
    for(auto& f : files)
    {
        MptdProject pr; if(!MptdParse(TdeReadFile(PluginDir(s_Server.GetName()) + "/" + f), pr)) continue;
        for(auto& t : pr.items) if(tds.size() < 192) tds.push_back(MptdToPkt(t));
    }
    s_Server.SetPluginTextdraws(tds);
    return (int)tds.size();
}

// copy a .mptd (or an SA-MP Textdraw Editor export .txt, converted) into the server folder
static bool PluginImport(const char* serverName, const std::string& src, std::string* msg)
{
    std::string txt = TdeReadFile(src);
    if(txt.empty()) { if(msg) *msg = "Could not read the file."; return false; }
    MptdProject pr; std::string err;
    if(!MptdParse(txt, pr, &err))
    {   // 3.50: a Pawn export works too
        if(MptdImportPawn(txt, pr) <= 0) { if(msg) *msg = "Not a textdraw file (" + err + ")."; return false; }
        txt = MptdWrite(pr);
    }
    std::string base = src.substr(src.find_last_of('/') + 1);
    size_t dot = base.find_last_of('.'); if(dot != std::string::npos) base.resize(dot);
    if(base.empty()) base = "textdraw";
    const std::string dir = PluginDir(serverName); TdeMkDirs(dir);
    if(!TdeWriteFile(dir + "/" + base + ".mptd", txt)) { if(msg) *msg = "Could not write to " + dir + "."; return false; }
    int n = PluginApply(serverName);
    if(msg) { char b[300]; snprintf(b, sizeof(b), "%s.mptd added to files/MP/%s/ (%d textdraws)%s", base.c_str(), PluginFolderName(serverName).c_str(), (int)pr.items.size(), n ? " - live on your server now" : ""); *msg = b; }
    return true;
}
static void PluginRemove(const char* serverName, const std::string& file)
{
    remove((PluginDir(serverName) + "/" + file).c_str());
    PluginApply(serverName);
}
// where a .mptd can come from: the in-game editor, the app (Downloads/MP Textdraws), Downloads, files/MP/import
static void PluginCandidates(std::vector<std::string>& paths)
{
    paths.clear();
    const std::string dd = s_szDataDir;
    const std::string dirs[] = { dd + "/tde/projects", dd + "/import", "/sdcard/Download/MP Textdraws", "/storage/emulated/0/Download/MP Textdraws", "/sdcard/Download", dd + "/tde/import" };
    std::vector<std::string> seen;
    for(auto& d : dirs)
    {
        std::vector<std::string> f; TdeListDir(d, nullptr, f);
        for(auto& n : f)
        {
            if(!TdeEndsWith(n, ".mptd") && !(TdeEndsWith(n, ".txt") && d.find("import") != std::string::npos)) continue;
            if(std::find(seen.begin(), seen.end(), n) != seen.end()) continue;
            seen.push_back(n); paths.push_back(d + "/" + n);
        }
    }
}

// ---- the Plugins section of MP Launcher > Add server (under Gamemodes / Rules) ------------------
static int s_nPluginType = 0;
static std::vector<std::string> s_PluginPick;
static std::string s_PluginServer;
static void PluginImportDlg(int resp, int item, const char*, int)
{
    if(!resp || item < 0 || item >= (int)s_PluginPick.size()) return;
    std::string msg;
    const bool ok = PluginImport(s_PluginServer.c_str(), s_PluginPick[item], &msg);
    SysMsg(ok ? COL_GREEN : COL_RED, "%s", msg.c_str());
}
static const char* PluginServerName(const char* typed)
{
    if(s_Server.IsRunning() && !MP::IsHostingDefault()) return s_Server.GetName();
    return typed && typed[0] ? typed : settings.serverName;
}
static void PluginsSection(const char* typedName)
{
    ImGui::PushID("plugins");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextUnformatted("Plugins");
    const char* srv = PluginServerName(typedName);
    const std::string folder = PluginFolderName(srv);
    ImGui::TextDisabled("Files for your custom server go to files/MP/%s/.", folder.c_str());
    ImGui::TextUnformatted("Type");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::BeginCombo("##ptype", k_PluginTypes[s_nPluginType]))
    {
        for(int i = 0; i < 3; ++i) if(ImGui::Selectable(k_PluginTypes[i], s_nPluginType == i)) s_nPluginType = i;
        ImGui::EndCombo();
    }
    if(s_nPluginType != 0) { ImGui::TextDisabled("This plugin type is not available yet."); ImGui::PopID(); return; }
    ImGui::TextDisabled("Textdraw: a .mptd saved by /tde (in game) or the MP Textdraw Editor app. Every player on your server sees them.");
    if(FloatMenu::BigButton("Import file"))
    {
        PluginCandidates(s_PluginPick); s_PluginServer = srv;
        if(s_PluginPick.empty()) SysMsg(COL_YELLOW, "No .mptd files found. Save one with /tde, or put it in Download/MP Textdraws or files/MP/import/.");
        else
        {
            std::string b; for(auto& f : s_PluginPick) { std::string d = f.substr(0, f.find_last_of('/')); b += f.substr(f.find_last_of('/') + 1) + "\t" + d.substr(d.find_last_of('/') + 1) + "\n"; }
            b.pop_back();
            ShowDialog(0, DIALOG_STYLE_TABLIST, "Import a textdraw file", b.c_str(), "Import", "Cancel", PluginImportDlg);
        }
    }
    std::vector<std::string> files; PluginList(srv, files);
    if(files.empty()) ImGui::TextDisabled("No textdraw files on this server yet.");
    for(auto& f : files)
    {
        ImGui::PushID(f.c_str());
        ImGui::TextUnformatted(f.c_str()); ImGui::SameLine(ImGui::GetContentRegionAvail().x - UI::S(150));
        if(ImGui::Button("Remove", ImVec2(UI::S(150), UI::S(56)))) { PluginRemove(srv, f); ImGui::PopID(); break; }
        ImGui::PopID();
    }
    ImGui::TextDisabled("Make one: /tde in game (files/MP/tde/projects/), or the MP Textdraw Editor app.");
    ImGui::PopID();
}
