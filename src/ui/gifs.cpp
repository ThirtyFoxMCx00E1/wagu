#include "gifs.h"
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <atomic>
#include <deque>
#include <condition_variable>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include "../mp.h"
#include "../net/online.h"

namespace
{
    const char* kBase = "https://mpsamp-open.github.io/gifs/";   // 4.12.7: the gifs branch is published on the website
    std::mutex s_Mx;
    std::map<int, Gifs::Info> s_Info;
    std::vector<int> s_Order;
    std::vector<std::string> s_Cats;
    std::atomic<bool> s_bReady{false}, s_bStarted{false};
    std::deque<int> s_Queue; std::set<int> s_Queued, s_Failed;
    std::condition_variable s_Cv;
    bool s_bWorker = false;

    std::string Dir() { return std::string(MP::DataDir()) + "/gifs"; }

    void ParseIndex(const std::string& txt)
    {
        std::map<int, Gifs::Info> m; std::vector<int> order; std::set<std::string> cats;
        size_t p = 0;
        while(p < txt.size())
        {
            size_t e = txt.find('\n', p); if(e == std::string::npos) e = txt.size();
            std::string l = txt.substr(p, e - p); p = e + 1;
            if(!l.empty() && l.back() == '\r') l.pop_back();
            std::vector<std::string> f; size_t a = 0;
            for(;;) { size_t b = l.find('|', a); f.push_back(l.substr(a, b == std::string::npos ? std::string::npos : b - a)); if(b == std::string::npos) break; a = b + 1; }
            if(f.size() < 8) continue;
            Gifs::Info i; i.id = atoi(f[0].c_str()); i.cat = f[1]; i.name = f[2];
            i.frames = atoi(f[3].c_str()); i.cols = atoi(f[4].c_str()); i.fw = atoi(f[5].c_str()); i.fh = atoi(f[6].c_str()); i.delay = atoi(f[7].c_str());
            if(i.id <= 0 || i.frames <= 0 || i.cols <= 0 || i.fw <= 0 || i.fh <= 0) continue;
            for(char& c : i.name) if(c == '-' || c == '_') c = ' ';
            if(i.delay < 40) i.delay = 40;
            cats.insert(i.cat); order.push_back(i.id); m[i.id] = i;
        }
        std::lock_guard<std::mutex> lk(s_Mx);
        s_Info.swap(m); s_Order.swap(order);
        s_Cats.clear(); s_Cats.push_back("All");
        for(auto& c : cats) s_Cats.push_back(c);
        s_bReady = !s_Info.empty();
    }

    void Worker()
    {
        for(;;)
        {
            int id;
            {
                std::unique_lock<std::mutex> lk(s_Mx);
                s_Cv.wait(lk, [] { return !s_Queue.empty(); });
                id = s_Queue.back(); s_Queue.pop_back();   // newest wish first (what is on screen now)
            }
            char rel[32]; snprintf(rel, sizeof(rel), "g/%d.jpg", id);
            std::string body, why;
            bool ok = Online::HttpsFetch(std::string(kBase) + rel, body, 30000, why, 512 * 1024) && body.size() > 4 && (uint8_t)body[0] == 0xFF && (uint8_t)body[1] == 0xD8;
            if(ok)
            {
                char fn[512]; snprintf(fn, sizeof(fn), "%s/%d.jpg", Dir().c_str(), id);
                std::string tmp = std::string(fn) + ".part";
                if(FILE* f = fopen(tmp.c_str(), "wb")) { fwrite(body.data(), 1, body.size(), f); fclose(f); rename(tmp.c_str(), fn); }
                else ok = false;
            }
            std::lock_guard<std::mutex> lk(s_Mx);
            s_Queued.erase(id);
            if(!ok) s_Failed.insert(id);
        }
    }
}

void Gifs::Init()
{
    if(s_bStarted.exchange(true)) return;
    mkdir(Dir().c_str(), 0777);
    std::thread([] {
        std::string path = Dir() + "/index.txt", txt;
        struct stat st{}; bool have = stat(path.c_str(), &st) == 0 && st.st_size > 100;
        if(have) { if(FILE* f = fopen(path.c_str(), "rb")) { txt.resize((size_t)st.st_size); txt.resize(fread(&txt[0], 1, txt.size(), f)); fclose(f); } ParseIndex(txt); }
        if(!have || time(nullptr) - st.st_mtime > 86400)   // refresh once a day
        {
            std::string body, why;
            if(Online::HttpsFetch(std::string(kBase) + "index.txt", body, 30000, why, 4 * 1024 * 1024) && body.find('|') != std::string::npos)
            {
                if(FILE* f = fopen(path.c_str(), "wb")) { fwrite(body.data(), 1, body.size(), f); fclose(f); }
                ParseIndex(body);
            }
        }
    }).detach();
}
bool Gifs::Ready() { return s_bReady; }
const Gifs::Info* Gifs::Get(int id)
{
    Init();   // lazy: the first GIF in chat / the GIF page loads the index
    std::lock_guard<std::mutex> lk(s_Mx);
    auto it = s_Info.find(id); return it == s_Info.end() ? nullptr : &it->second;   // entries are never removed after load
}
const std::vector<std::string>& Gifs::Categories() { return s_Cats; }
void Gifs::List(const char* cat, const char* search, std::vector<int>& out)
{
    out.clear();
    Init();
    std::lock_guard<std::mutex> lk(s_Mx);
    bool all = !cat || !*cat || !strcmp(cat, "All");
    for(int id : s_Order)
    {
        const Info& i = s_Info[id];
        if(!all && i.cat != cat) continue;
        if(search && *search && !strcasestr(i.name.c_str(), search) && !strcasestr(i.cat.c_str(), search)) continue;
        out.push_back(id);
    }
}
bool Gifs::SheetPath(int id, std::string& path)
{
    char fn[512]; snprintf(fn, sizeof(fn), "%s/%d.jpg", Dir().c_str(), id);
    struct stat st{};
    if(stat(fn, &st) == 0 && st.st_size > 0) { path = fn; return true; }
    std::lock_guard<std::mutex> lk(s_Mx);
    if(s_Failed.count(id) || s_Queued.count(id)) return false;
    s_Queued.insert(id); s_Queue.push_back(id);
    if(s_Queue.size() > 64) { s_Queued.erase(s_Queue.front()); s_Queue.pop_front(); }
    if(!s_bWorker) { s_bWorker = true; std::thread(Worker).detach(); std::thread(Worker).detach(); }
    s_Cv.notify_one();
    return false;
}
int Gifs::ParseToken(const char* t)
{
    if(!t) return 0;
    while(*t == ' ') ++t;
    if(strncasecmp(t, "[gif:", 5)) return 0;
    t += 5; int id = 0, n = 0;
    while(isdigit((unsigned char)*t) && n < 6) { id = id * 10 + (*t - '0'); ++t; ++n; }
    if(*t != ']' || !n) return 0;
    ++t; while(*t == ' ') ++t;
    return *t ? 0 : id;
}
