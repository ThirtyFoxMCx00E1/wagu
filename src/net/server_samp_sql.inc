// 4.11: MySQL R41 (https://github.com/pawn-lang/pawn-mysql) natives on SQLite, and SA-MP's own
// db_* (SQLite) natives. A SA-MP gamemode that wants a MySQL server gets a file in its folder
// instead (mp_mysql_<database>.db). Tables and columns the script uses but never made are made
// when a query needs them, so a gamemode without its .sql schema still works.
#include "../../third_party/sqlite/sqlite3.h"
namespace {

struct SqlCache
{
    std::vector<std::string> cols; std::vector<std::vector<std::string>> rows; std::vector<std::vector<bool>> nulls;
    long long insertId = 0; int affected = 0; std::string query; bool saved = false; int errNo = 0; std::string err;
};
struct SqlState
{
    sqlite3* db = nullptr; std::string dbName; int lastErr = 0; std::string lastErrText;
    std::map<int, SqlCache> caches; int nextCache = 1; int active = 0;
    std::map<int, sqlite3*> dbs; int nextDb = 1;            // db_open
    std::map<int, SqlCache> results; int nextRes = 1;       // db_query results
    std::map<int, int> resRow;                              // db_next_row position
};
static SqlState* g_Sql = nullptr;
static SqlState& SQ() { if(!g_Sql) g_Sql = new SqlState(); return *g_Sql; }

// ---- MySQL -> SQLite --------------------------------------------------------------------------------
static void SqlFnNow(sqlite3_context* c, int, sqlite3_value**) { time_t t = time(nullptr); struct tm tm; localtime_r(&t, &tm); char b[32]; strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &tm); sqlite3_result_text(c, b, -1, SQLITE_TRANSIENT); }
static void SqlFnCurdate(sqlite3_context* c, int, sqlite3_value**) { time_t t = time(nullptr); struct tm tm; localtime_r(&t, &tm); char b[32]; strftime(b, sizeof(b), "%Y-%m-%d", &tm); sqlite3_result_text(c, b, -1, SQLITE_TRANSIENT); }
static void SqlFnUnix(sqlite3_context* c, int n, sqlite3_value** v)
{
    if(n >= 1 && sqlite3_value_type(v[0]) == SQLITE_TEXT)
    {
        struct tm tm; memset(&tm, 0, sizeof(tm)); const char* s = (const char*)sqlite3_value_text(v[0]);
        if(s && sscanf(s, "%d-%d-%d %d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) >= 3) { tm.tm_year -= 1900; tm.tm_mon -= 1; tm.tm_isdst = -1; sqlite3_result_int64(c, (sqlite3_int64)mktime(&tm)); return; }
    }
    sqlite3_result_int64(c, (sqlite3_int64)time(nullptr));
}
static void SqlFnFromUnix(sqlite3_context* c, int, sqlite3_value** v) { time_t t = (time_t)sqlite3_value_int64(v[0]); struct tm tm; localtime_r(&t, &tm); char b[32]; strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &tm); sqlite3_result_text(c, b, -1, SQLITE_TRANSIENT); }
static void SqlFnRand(sqlite3_context* c, int, sqlite3_value**) { sqlite3_result_double(c, (double)rand() / RAND_MAX); }
static void SqlFnConcat(sqlite3_context* c, int n, sqlite3_value** v) { std::string s; for(int i = 0; i < n; ++i) { const unsigned char* t = sqlite3_value_text(v[i]); if(t) s += (const char*)t; } sqlite3_result_text(c, s.c_str(), (int)s.size(), SQLITE_TRANSIENT); }
static void SqlFnLastId(sqlite3_context* c, int, sqlite3_value**) { sqlite3_result_int64(c, sqlite3_last_insert_rowid(sqlite3_context_db_handle(c))); }
static void SqlFns(sqlite3* db)
{
    sqlite3_create_function(db, "NOW", 0, SQLITE_UTF8, nullptr, SqlFnNow, nullptr, nullptr);
    sqlite3_create_function(db, "CURDATE", 0, SQLITE_UTF8, nullptr, SqlFnCurdate, nullptr, nullptr);
    sqlite3_create_function(db, "UNIX_TIMESTAMP", -1, SQLITE_UTF8, nullptr, SqlFnUnix, nullptr, nullptr);
    sqlite3_create_function(db, "FROM_UNIXTIME", 1, SQLITE_UTF8, nullptr, SqlFnFromUnix, nullptr, nullptr);
    sqlite3_create_function(db, "RAND", 0, SQLITE_UTF8, nullptr, SqlFnRand, nullptr, nullptr);
    sqlite3_create_function(db, "CONCAT", -1, SQLITE_UTF8, nullptr, SqlFnConcat, nullptr, nullptr);
    sqlite3_create_function(db, "LAST_INSERT_ID", 0, SQLITE_UTF8, nullptr, SqlFnLastId, nullptr, nullptr);
    sqlite3_busy_timeout(db, 2000);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
}
static std::string Up(std::string s) { for(auto& c : s) c = (char)toupper((unsigned char)c); return s; }
static void ReplaceAllCi(std::string& s, const std::string& from, const std::string& to)
{
    std::string us = Up(s), uf = Up(from); size_t at = 0;
    while((at = us.find(uf, at)) != std::string::npos) { s.replace(at, from.size(), to); us.replace(at, from.size(), Up(to)); at += to.size(); }
}
static std::vector<std::string> SplitTop(const std::string& s)
{   // split on commas outside (), '' and ``
    std::vector<std::string> out; std::string cur; int depth = 0; char q = 0;
    for(char c : s)
    {
        if(q) { cur += c; if(c == q) q = 0; continue; }
        if(c == '\'' || c == '`' || c == '"') { q = c; cur += c; continue; }
        if(c == '(') ++depth; else if(c == ')') --depth;
        if(c == ',' && depth == 0) { out.push_back(cur); cur.clear(); continue; }
        cur += c;
    }
    if(!cur.empty()) out.push_back(cur);
    return out;
}
static std::string Trim(std::string s) { while(!s.empty() && isspace((unsigned char)s.front())) s.erase(0, 1); while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); return s; }
static std::string SqlCreateFix(const std::string& q)
{   // MySQL CREATE TABLE -> SQLite
    size_t open = q.find('('), close = q.rfind(')');
    if(open == std::string::npos || close == std::string::npos || close < open) return q;
    std::string head = q.substr(0, open), body = q.substr(open + 1, close - open - 1);
    std::vector<std::string> parts = SplitTop(body), keep; std::string autoCol;
    for(auto& p0 : parts)
    {
        std::string p = Trim(p0), up = Up(p);
        if(up.rfind("KEY ", 0) == 0 || up.rfind("INDEX ", 0) == 0 || up.rfind("FULLTEXT", 0) == 0 || up.rfind("SPATIAL", 0) == 0 || up.rfind("CONSTRAINT", 0) == 0 || up.rfind("FOREIGN KEY", 0) == 0) continue;
        if(up.rfind("UNIQUE KEY", 0) == 0 || up.rfind("UNIQUE INDEX", 0) == 0) { size_t b = p.find('('); if(b != std::string::npos) keep.push_back("UNIQUE " + p.substr(b)); continue; }
        if(up.rfind("PRIMARY KEY", 0) == 0) { if(autoCol.empty()) keep.push_back(p); continue; }
        if(up.find("AUTO_INCREMENT") != std::string::npos)
        {
            size_t sp = p.find_first_of(" \t"); std::string col = p.substr(0, sp);
            autoCol = col; keep.push_back(col + " INTEGER PRIMARY KEY AUTOINCREMENT"); continue;
        }
        ReplaceAllCi(p, " UNSIGNED", ""); ReplaceAllCi(p, " ON UPDATE CURRENT_TIMESTAMP", ""); ReplaceAllCi(p, " ZEROFILL", "");
        { std::string u = Up(p); size_t cs = u.find(" CHARACTER SET "); if(cs != std::string::npos) { size_t e = u.find(' ', cs + 15); p.erase(cs, (e == std::string::npos ? p.size() : e) - cs); } }
        { std::string u = Up(p); size_t cs = u.find(" COLLATE "); if(cs != std::string::npos) { size_t e = u.find(' ', cs + 9); p.erase(cs, (e == std::string::npos ? p.size() : e) - cs); } }
        { std::string u = Up(p); size_t cs = u.find(" COMMENT '"); if(cs != std::string::npos) { size_t e = p.find('\'', cs + 10); p.erase(cs, (e == std::string::npos ? p.size() : e + 1) - cs); } }
        keep.push_back(p);
    }
    // a PRIMARY KEY (a, b) with the AUTO_INCREMENT column: SQLite has the autoincrement one only
    std::string out = head + "(";
    for(size_t i = 0; i < keep.size(); ++i) { if(i) out += ", "; out += keep[i]; }
    return out + ")";
}
static std::string SqlFix(const std::string& q0)
{
    std::string q = Trim(q0); while(!q.empty() && q.back() == ';') q.pop_back();
    std::string u = Up(q);
    if(u.rfind("CREATE TABLE", 0) == 0) return SqlCreateFix(q);
    if(u.rfind("SET NAMES", 0) == 0 || u.rfind("SET CHARACTER", 0) == 0 || u.rfind("SET SESSION", 0) == 0 || u.rfind("SET GLOBAL", 0) == 0 || u.rfind("SET SQL_MODE", 0) == 0 || u.rfind("LOCK TABLES", 0) == 0 || u.rfind("UNLOCK TABLES", 0) == 0 || u.rfind("OPTIMIZE TABLE", 0) == 0)
        return "SELECT 1 WHERE 0";
    if(u.rfind("INSERT IGNORE", 0) == 0) q = "INSERT OR IGNORE" + q.substr(13);
    if(u.rfind("TRUNCATE ", 0) == 0) { std::string t = Trim(q.substr(9)); if(Up(t).rfind("TABLE ", 0) == 0) t = Trim(t.substr(6)); q = "DELETE FROM " + t; }
    size_t odk = Up(q).find("ON DUPLICATE KEY UPDATE");
    if(odk != std::string::npos)
    {
        std::string tail = q.substr(odk + 23);
        for(;;) { std::string ut = Up(tail); size_t v = ut.find("VALUES("); if(v == std::string::npos) break; size_t e = tail.find(')', v); if(e == std::string::npos) break; tail = tail.substr(0, v) + "excluded." + tail.substr(v + 7, e - v - 7) + tail.substr(e + 1); }
        q = q.substr(0, odk) + "ON CONFLICT DO UPDATE SET" + tail;
    }
    return q;
}
static std::string SqlGuessTable(const std::string& q)
{
    std::string u = Up(q);
    const char* keys[] = { "INSERT OR IGNORE INTO ", "INSERT OR REPLACE INTO ", "INSERT INTO ", "REPLACE INTO ", "UPDATE ", "DELETE FROM ", " FROM " };
    for(const char* k : keys)
    {
        size_t at = u.find(k); if(at == std::string::npos) continue;
        at += strlen(k); while(at < q.size() && q[at] == ' ') ++at;
        std::string t; while(at < q.size() && !strchr(" (,;\t\r\n", q[at])) t += q[at++];
        t.erase(std::remove(t.begin(), t.end(), '`'), t.end());
        if(!t.empty()) return t;
    }
    return std::string();
}
static void SqlLogSchema(const std::string& what) { if(g_SampSrv) g_SampSrv->SampLog("[samp] database: %s", what.c_str()); }
// run (maybe several statements), fill the cache from the last one with columns
static bool SqlRun(sqlite3* db, const std::string& query, SqlCache& out, bool autoSchema)
{
    out = SqlCache(); out.query = query;
    if(!db) { out.errNo = 2006; out.err = "no database"; return false; }
    std::string q = SqlFix(query);
    for(int attempt = 0; attempt < 40; ++attempt)
    {
        const char* tail = q.c_str(); bool fail = false; std::string err;
        out.cols.clear(); out.rows.clear(); out.nulls.clear(); out.affected = 0;
        sqlite3_exec(db, "SAVEPOINT mpq", nullptr, nullptr, nullptr);
        while(tail && *tail)
        {
            sqlite3_stmt* st = nullptr; const char* next = nullptr;
            int rc = sqlite3_prepare_v2(db, tail, -1, &st, &next);
            if(rc != SQLITE_OK) { err = sqlite3_errmsg(db); fail = true; break; }
            if(!st) { tail = next; continue; }
            int nc = sqlite3_column_count(st);
            std::vector<std::string> cols; for(int i = 0; i < nc; ++i) { const char* n = sqlite3_column_name(st, i); cols.push_back(n ? n : ""); }
            std::vector<std::vector<std::string>> rows; std::vector<std::vector<bool>> nulls;
            while((rc = sqlite3_step(st)) == SQLITE_ROW)
            {
                std::vector<std::string> r; std::vector<bool> nl;
                for(int i = 0; i < nc; ++i) { const unsigned char* t = sqlite3_column_text(st, i); r.push_back(t ? (const char*)t : ""); nl.push_back(sqlite3_column_type(st, i) == SQLITE_NULL); }
                rows.push_back(std::move(r)); nulls.push_back(std::move(nl));
            }
            if(rc != SQLITE_DONE) { err = sqlite3_errmsg(db); sqlite3_finalize(st); fail = true; break; }
            if(nc) { out.cols = cols; out.rows = std::move(rows); out.nulls = std::move(nulls); }
            else { out.affected = sqlite3_changes(db); out.insertId = sqlite3_last_insert_rowid(db); }
            sqlite3_finalize(st); tail = next;
        }
        if(!fail) { sqlite3_exec(db, "RELEASE mpq", nullptr, nullptr, nullptr); return true; }
        sqlite3_exec(db, "ROLLBACK TO mpq; RELEASE mpq", nullptr, nullptr, nullptr);
        out.err = err; out.errNo = 1064;
        if(!autoSchema) return false;
        // the missing schema: make it and try again
        std::string fix;
        if(err.rfind("no such table: ", 0) == 0)
        {
            std::string t = err.substr(15); size_t dot = t.find('.'); if(dot != std::string::npos) t = t.substr(dot + 1);
            fix = "CREATE TABLE IF NOT EXISTS `" + t + "` (id INTEGER PRIMARY KEY AUTOINCREMENT)";
            out.errNo = 1146;
        }
        else if(err.find(" has no column named ") != std::string::npos)
        {
            size_t a = err.find("table "), b = err.find(" has no column named ");
            std::string t = a == std::string::npos ? SqlGuessTable(q) : err.substr(a + 6, b - a - 6), c = err.substr(b + 21);
            fix = "ALTER TABLE `" + t + "` ADD COLUMN `" + c + "` DEFAULT ''";
            out.errNo = 1054;
        }
        else if(err.rfind("no such column: ", 0) == 0)
        {
            std::string c = err.substr(16), t; size_t dot = c.find('.');
            if(dot != std::string::npos) { t = c.substr(0, dot); c = c.substr(dot + 1); }
            if(t.empty() || Up(q).find(Up(t) + " ") == std::string::npos) t = SqlGuessTable(q);
            if(!t.empty()) fix = "ALTER TABLE `" + t + "` ADD COLUMN `" + c + "` DEFAULT ''";
            out.errNo = 1054;
        }
        if(fix.empty()) return false;
        char* e2 = nullptr;
        if(sqlite3_exec(db, fix.c_str(), nullptr, nullptr, &e2) != SQLITE_OK) { if(e2) { out.err += std::string(" / ") + e2; sqlite3_free(e2); } return false; }
        SqlLogSchema(fix);
    }
    return false;
}
static SqlCache* ActiveCache() { auto it = SQ().caches.find(SQ().active); return it == SQ().caches.end() ? nullptr : &it->second; }
static int ColIdx(SqlCache* c, const std::string& name)
{
    if(!c) return -1; std::string n = Lower(name);
    for(size_t i = 0; i < c->cols.size(); ++i) if(Lower(c->cols[i]) == n) return (int)i;
    return -1;
}
static const std::string* CacheVal(SqlCache* c, cell row, int col) { if(!c || row < 0 || row >= (cell)c->rows.size() || col < 0 || col >= (int)c->cols.size()) return nullptr; return &c->rows[(size_t)row][(size_t)col]; }

// ---- MySQL natives -------------------------------------------------------------------------------------
N(mysql_connect)
{
    std::string db = Str(amx, params[4]); if(db.empty()) db = "samp";
    for(auto& ch : db) if(!isalnum((unsigned char)ch) && ch != '_' && ch != '-') ch = '_';
    SqlState& s = SQ();
    if(!s.db)
    {
        std::string path = g_Samp->root + "/mp_mysql_" + db + ".db";
        if(sqlite3_open(path.c_str(), &s.db) != SQLITE_OK) { s.lastErr = 2003; if(s.db) { sqlite3_close(s.db); s.db = nullptr; } return 0; }
        SqlFns(s.db); s.dbName = db;
        S_->SampLog("[samp] MySQL %s@%s -> SQLite %s (MP makes the tables the gamemode uses)", Str(amx, params[2]).c_str(), Str(amx, params[1]).c_str(), path.c_str());
    }
    s.lastErr = 0; return 1;
}
N(mysql_connect_file)
{
    cell p[6] = { 5 * (cell)sizeof(cell), 0, 0, 0, 0, 0 }; (void)p;
    SqlState& s = SQ();
    if(!s.db) { std::string path = g_Samp->root + "/mp_mysql_samp.db"; if(sqlite3_open(path.c_str(), &s.db) != SQLITE_OK) return 0; SqlFns(s.db); s.dbName = "samp"; }
    return 1;
}
N(mysql_close) { UNUSED_AMX; return 1; }
N(mysql_errno) { UNUSED_AMX; return SQ().db ? SQ().lastErr : 2006; }
N(mysql_error) { SetStr(amx, params[1], SQ().lastErrText, params[2]); return 1; }
N(mysql_escape_string)
{
    std::string s = Str(amx, params[1]), e; for(char ch : s) { if(ch == '\'') e += "''"; else e += ch; }
    cell len = NArgs(params) >= 3 ? params[3] : (cell)e.size() + 1;
    SetStr(amx, params[2], e, len); return (cell)e.size();
}
N(mysql_format)
{
    std::string s = SampFormat(amx, params, Str(amx, params[4]), 5, true);
    SetStr(amx, params[2], s, params[3]); return (cell)s.size();
}
static cell SqlQueue(AMX* amx, const cell* params, bool threaded)
{
    std::string q = Str(amx, params[2]);
    SqlState& s = SQ(); int id = s.nextCache++;
    SqlCache& c = s.caches[id];
    bool ok = SqlRun(s.db, q, c, true);
    s.lastErr = ok ? 0 : c.errNo; s.lastErrText = c.err;
    std::string cb = NArgs(params) >= 3 ? Str(amx, params[3]) : "";
    if(!ok)
    {
        S_->SampLog("[samp] query failed: %s | %s", c.err.c_str(), q.c_str());
        CallAll("OnQueryError", { AI(c.errNo), AS(c.err), AS(cb), AS(q), AI(1) });
        s.caches.erase(id);
        return threaded ? 1 : 0;
    }
    if(!threaded) return id;
    if(cb.empty()) { s.caches.erase(id); return 1; }
    SampTimer t; t.id = g_Samp->nextTimer++; t.sc = ScOf(amx); t.fn = "\x01sql"; t.interval = 1; t.repeat = false; t.next = 0;
    t.args.push_back(AI(id)); t.args.push_back(AS(cb));
    std::vector<SampArg> a = ReadArgs(amx, params, NArgs(params) >= 4 ? Str(amx, params[4]) : "", 5);
    for(auto& x : a) t.args.push_back(x);
    g_Samp->timers.push_back(std::move(t));
    return 1;
}
N(mysql_tquery) { return SqlQueue(amx, params, true); }
N(mysql_query)
{
    cell r = SqlQueue(amx, params, false);
    bool useCache = NArgs(params) < 3 || params[3] != 0;
    if(r > 0) { if(useCache) { SQ().active = (int)r; SQ().caches[(int)r].saved = true; } else { SQ().caches.erase((int)r); r = 0; } }
    return r;
}
N(cache_get_row_count) { SqlCache* c = ActiveCache(); cell n = c ? (cell)c->rows.size() : 0; if(NArgs(params) >= 1) { SetRef(amx, params[1], n); return c != nullptr; } return n; }
N(cache_get_field_count) { SqlCache* c = ActiveCache(); cell n = c ? (cell)c->cols.size() : 0; if(NArgs(params) >= 1) { SetRef(amx, params[1], n); return c != nullptr; } return n; }
N(cache_get_result_count) { SqlCache* c = ActiveCache(); if(NArgs(params) >= 1) SetRef(amx, params[1], c ? 1 : 0); return c != nullptr; }
N(cache_get_field_name) { SqlCache* c = ActiveCache(); if(!c || params[1] < 0 || params[1] >= (cell)c->cols.size()) return 0; SetStr(amx, params[2], c->cols[(size_t)params[1]], params[3]); return 1; }
N(cache_get_value_index) { const std::string* v = CacheVal(ActiveCache(), params[1], (int)params[2]); if(!v) return 0; SetStr(amx, params[3], *v, params[4]); return 1; }
N(cache_get_value_index_int) { const std::string* v = CacheVal(ActiveCache(), params[1], (int)params[2]); if(!v) return 0; SetRef(amx, params[3], (cell)strtoll(v->c_str(), nullptr, 10)); return 1; }
N(cache_get_value_index_float) { const std::string* v = CacheVal(ActiveCache(), params[1], (int)params[2]); if(!v) return 0; SetRefF(amx, params[3], strtof(v->c_str(), nullptr)); return 1; }
N(cache_is_value_index_null) { SqlCache* c = ActiveCache(); if(!CacheVal(c, params[1], (int)params[2])) return 0; SetRef(amx, params[3], c->nulls[(size_t)params[1]][(size_t)params[2]] ? 1 : 0); return 1; }
N(cache_get_value_name) { SqlCache* c = ActiveCache(); const std::string* v = CacheVal(c, params[1], ColIdx(c, Str(amx, params[2]))); if(!v) return 0; SetStr(amx, params[3], *v, params[4]); return 1; }
N(cache_get_value_name_int) { SqlCache* c = ActiveCache(); const std::string* v = CacheVal(c, params[1], ColIdx(c, Str(amx, params[2]))); if(!v) return 0; SetRef(amx, params[3], (cell)strtoll(v->c_str(), nullptr, 10)); return 1; }
N(cache_get_value_name_float) { SqlCache* c = ActiveCache(); const std::string* v = CacheVal(c, params[1], ColIdx(c, Str(amx, params[2]))); if(!v) return 0; SetRefF(amx, params[3], strtof(v->c_str(), nullptr)); return 1; }
N(cache_is_value_name_null) { SqlCache* c = ActiveCache(); int col = ColIdx(c, Str(amx, params[2])); if(!CacheVal(c, params[1], col)) return 0; SetRef(amx, params[3], c->nulls[(size_t)params[1]][(size_t)col] ? 1 : 0); return 1; }
// R39 names
N(cache_get_data) { SqlCache* c = ActiveCache(); SetRef(amx, params[1], c ? (cell)c->rows.size() : 0); SetRef(amx, params[2], c ? (cell)c->cols.size() : 0); return 1; }
N(cache_get_field_content) { SqlCache* c = ActiveCache(); const std::string* v = CacheVal(c, params[1], ColIdx(c, Str(amx, params[2]))); SetStr(amx, params[3], v ? *v : "", NArgs(params) >= 5 ? params[5] : 128); return 1; }
N(cache_get_field_content_int) { SqlCache* c = ActiveCache(); const std::string* v = CacheVal(c, params[1], ColIdx(c, Str(amx, params[2]))); return v ? (cell)strtoll(v->c_str(), nullptr, 10) : 0; }
N(cache_get_field_content_float) { SqlCache* c = ActiveCache(); const std::string* v = CacheVal(c, params[1], ColIdx(c, Str(amx, params[2]))); return RF(v ? strtof(v->c_str(), nullptr) : 0.0f); }
N(cache_get_row) { const std::string* v = CacheVal(ActiveCache(), params[1], (int)params[2]); SetStr(amx, params[3], v ? *v : "", NArgs(params) >= 5 ? params[5] : 128); return 1; }
N(cache_get_row_int) { const std::string* v = CacheVal(ActiveCache(), params[1], (int)params[2]); return v ? (cell)strtoll(v->c_str(), nullptr, 10) : 0; }
N(cache_get_row_float) { const std::string* v = CacheVal(ActiveCache(), params[1], (int)params[2]); return RF(v ? strtof(v->c_str(), nullptr) : 0.0f); }
N(cache_save) { UNUSED_AMX; SqlCache* c = ActiveCache(); if(!c) return 0; c->saved = true; return SQ().active; }
N(cache_delete) { SQ().caches.erase((int)params[1]); if(SQ().active == params[1]) SQ().active = 0; return 1; }
N(cache_set_active) { if(!SQ().caches.count((int)params[1])) return 0; SQ().active = (int)params[1]; return 1; }
N(cache_unset_active) { UNUSED_AMX; SQ().active = 0; return 1; }
N(cache_is_any_active) { UNUSED_AMX; return ActiveCache() != nullptr; }
N(cache_is_valid) { return SQ().caches.count((int)params[1]) != 0; }
N(cache_affected_rows) { UNUSED_AMX; SqlCache* c = ActiveCache(); return c ? c->affected : 0; }
N(cache_insert_id) { UNUSED_AMX; SqlCache* c = ActiveCache(); return c ? (cell)c->insertId : -1; }
N(cache_warning_count) { UNUSED_AMX; return 0; }
N(cache_get_query_exec_time) { UNUSED_AMX; return 1; }
N(cache_get_query_string) { SqlCache* c = ActiveCache(); SetStr(amx, params[1], c ? c->query : "", params[2]); return c != nullptr; }
N(mysql_insert_id) { UNUSED_AMX; return SQ().db ? (cell)sqlite3_last_insert_rowid(SQ().db) : 0; }
N(mysql_affected_rows) { UNUSED_AMX; return SQ().db ? sqlite3_changes(SQ().db) : 0; }
N(mysql_stat) { SetStr(amx, params[1], "MP SQLite " SQLITE_VERSION, params[2]); return 1; }
N(mysql_get_charset) { SetStr(amx, params[1], "utf8", params[2]); return 1; }

// ---- SA-MP db_* (SQLite) ------------------------------------------------------------------------------
N(db_open)
{
    std::string p = SfPath(Str(amx, params[1])); if(p.empty()) return 0; MkDirs(p);
    sqlite3* db = nullptr; if(sqlite3_open(p.c_str(), &db) != SQLITE_OK) { if(db) sqlite3_close(db); return 0; }
    SqlFns(db); int id = SQ().nextDb++; SQ().dbs[id] = db; return id;
}
N(db_close) { auto it = SQ().dbs.find((int)params[1]); if(it == SQ().dbs.end()) return 0; sqlite3_close(it->second); SQ().dbs.erase(it); return 1; }
N(db_query)
{
    auto it = SQ().dbs.find((int)params[1]); if(it == SQ().dbs.end()) return 0;
    int id = SQ().nextRes++; SqlCache& c = SQ().results[id];
    if(!SqlRun(it->second, Str(amx, params[2]), c, false)) { S_->SampLog("[samp] db_query: %s", c.err.c_str()); SQ().results.erase(id); return 0; }
    SQ().resRow[id] = 0; return id;
}
static SqlCache* Res(cell id) { auto it = SQ().results.find((int)id); return it == SQ().results.end() ? nullptr : &it->second; }
N(db_free_result) { SQ().results.erase((int)params[1]); SQ().resRow.erase((int)params[1]); return 1; }
N(db_num_rows) { SqlCache* c = Res(params[1]); return c ? (cell)c->rows.size() : 0; }
N(db_num_fields) { SqlCache* c = Res(params[1]); return c ? (cell)c->cols.size() : 0; }
N(db_next_row) { SqlCache* c = Res(params[1]); if(!c) return 0; int& r = SQ().resRow[(int)params[1]]; if(r + 1 >= (int)c->rows.size()) { r = (int)c->rows.size(); return 0; } ++r; return 1; }
N(db_field_name) { SqlCache* c = Res(params[1]); if(!c || params[2] < 0 || params[2] >= (cell)c->cols.size()) return 0; SetStr(amx, params[3], c->cols[(size_t)params[2]], params[4]); return 1; }
N(db_get_field) { SqlCache* c = Res(params[1]); const std::string* v = CacheVal(c, SQ().resRow[(int)params[1]], (int)params[2]); SetStr(amx, params[3], v ? *v : "", params[4]); return v != nullptr; }
N(db_get_field_int) { SqlCache* c = Res(params[1]); const std::string* v = CacheVal(c, SQ().resRow[(int)params[1]], NArgs(params) >= 2 ? (int)params[2] : 0); return v ? (cell)strtoll(v->c_str(), nullptr, 10) : 0; }
N(db_get_field_float) { SqlCache* c = Res(params[1]); const std::string* v = CacheVal(c, SQ().resRow[(int)params[1]], NArgs(params) >= 2 ? (int)params[2] : 0); return RF(v ? strtof(v->c_str(), nullptr) : 0.0f); }
N(db_get_field_assoc) { SqlCache* c = Res(params[1]); const std::string* v = CacheVal(c, SQ().resRow[(int)params[1]], ColIdx(c, Str(amx, params[2]))); SetStr(amx, params[3], v ? *v : "", params[4]); return v != nullptr; }
N(db_get_field_assoc_int) { SqlCache* c = Res(params[1]); const std::string* v = CacheVal(c, SQ().resRow[(int)params[1]], ColIdx(c, Str(amx, params[2]))); return v ? (cell)strtoll(v->c_str(), nullptr, 10) : 0; }
N(db_get_field_assoc_float) { SqlCache* c = Res(params[1]); const std::string* v = CacheVal(c, SQ().resRow[(int)params[1]], ColIdx(c, Str(amx, params[2]))); return RF(v ? strtof(v->c_str(), nullptr) : 0.0f); }

static const AMX_NATIVE_INFO g_SqlNatives[] =
{
    R(mysql_connect), R(mysql_connect_file), R(mysql_close), R(mysql_errno), R(mysql_error), R(mysql_escape_string), R(mysql_format), R(mysql_tquery), RA("mysql_pquery", mysql_tquery),
    R(mysql_query), R(cache_get_row_count), R(cache_get_field_count), R(cache_get_result_count), R(cache_get_field_name), R(cache_get_value_index), R(cache_get_value_index_int),
    R(cache_get_value_index_float), R(cache_is_value_index_null), R(cache_get_value_name), R(cache_get_value_name_int), R(cache_get_value_name_float), R(cache_is_value_name_null),
    R(cache_get_data), R(cache_get_field_content), R(cache_get_field_content_int), R(cache_get_field_content_float), R(cache_get_row), R(cache_get_row_int), R(cache_get_row_float),
    RA("cache_num_rows", cache_get_row_count), RA("cache_num_fields", cache_get_field_count), RA("cache_get_rows", cache_get_row_count), RA("cache_get_fields", cache_get_field_count),
    R(cache_save), R(cache_delete), R(cache_set_active), R(cache_unset_active), R(cache_is_any_active), R(cache_is_valid), R(cache_affected_rows), R(cache_insert_id),
    R(cache_warning_count), R(cache_get_query_exec_time), R(cache_get_query_string), R(mysql_insert_id), R(mysql_affected_rows), R(mysql_stat), R(mysql_get_charset),
    RA("mysql_set_charset", Ret1), RA("mysql_log", Ret1), RA("mysql_init_options", Ret1), RA("mysql_set_option", Ret1), RA("mysql_global_options", Ret1),
    RA("mysql_unprocessed_queries", Ret0), RA("mysql_reconnect", Ret1), RA("mysql_debug", Ret1), RA("mysql_ping", Ret0), RA("mysql_option", Ret1),
    R(db_open), R(db_close), R(db_query), R(db_free_result), R(db_num_rows), R(db_num_fields), R(db_next_row), R(db_field_name), R(db_get_field), R(db_get_field_int),
    R(db_get_field_float), R(db_get_field_assoc), R(db_get_field_assoc_int), R(db_get_field_assoc_float),
    RA("DB_Open", db_open), RA("DB_Close", db_close), RA("DB_ExecuteQuery", db_query), RA("DB_FreeResultSet", db_free_result), RA("DB_GetRowCount", db_num_rows),
    RA("DB_SelectNextRow", db_next_row), RA("DB_GetFieldCount", db_num_fields), RA("DB_GetFieldName", db_field_name), RA("DB_GetFieldString", db_get_field),
    RA("DB_GetFieldInt", db_get_field_int), RA("DB_GetFieldFloat", db_get_field_float), RA("DB_GetFieldStringByName", db_get_field_assoc),
    RA("DB_GetFieldIntByName", db_get_field_assoc_int), RA("DB_GetFieldFloatByName", db_get_field_assoc_float),
};
static void SampRegisterSql(AMX* amx) { amx_Register(amx, g_SqlNatives, (int)(sizeof(g_SqlNatives) / sizeof(g_SqlNatives[0]))); }
static void SampSqlClose()
{
    if(!g_Sql) return;
    if(g_Sql->db) sqlite3_close(g_Sql->db);
    for(auto& d : g_Sql->dbs) sqlite3_close(d.second);
    delete g_Sql; g_Sql = nullptr;
}
static void SampSqlLeave(int) {}
static void SampSqlCallback(const SampTimer& t)
{
    if(t.args.size() < 2) return;
    int id = (int)t.args[0].v; SqlState& s = SQ();
    if(!s.caches.count(id)) return;
    int prev = s.active; s.active = id;
    std::vector<SampArg> a(t.args.begin() + 2, t.args.end());
    CallPub(t.sc, t.args[1].s.c_str(), nullptr, a);
    auto it = s.caches.find(id);
    if(it != s.caches.end() && !it->second.saved) s.caches.erase(it);
    s.active = s.caches.count(prev) ? prev : 0;
}
} // namespace

// 4.11: /mpsamp sql <query> (host / admins): run SQL on the gamemode's database, a short answer back
static std::string SampSqlConsole(const std::string& q)
{
    if(!SQ().db) return "no database (the gamemode didn't call mysql_connect)";
    SqlCache c; if(!SqlRun(SQ().db, q, c, false)) return "error: " + c.err;
    if(c.cols.empty()) { char b[96]; snprintf(b, sizeof(b), "ok - %d rows changed, insert id %lld", c.affected, c.insertId); return b; }
    std::string out; char b[64]; snprintf(b, sizeof(b), "%zu rows: ", c.rows.size()); out = b;
    for(size_t i = 0; i < c.cols.size(); ++i) { out += c.cols[i]; out += i + 1 < c.cols.size() ? "|" : ""; }
    for(size_t r = 0; r < c.rows.size() && r < 5; ++r) { out += " ; "; for(size_t i = 0; i < c.rows[r].size(); ++i) { out += c.rows[r][i]; out += i + 1 < c.rows[r].size() ? "|" : ""; } }
    if(out.size() > 600) out.resize(600);
    return out;
}
