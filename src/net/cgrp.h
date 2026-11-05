#pragma once
// 4.09: CGRP systems (from CG-RP / UG-EDIT, research/CGRP(UG-EDIT).rar) for the default server and
// Mega Vil Futuristic. The server keeps every dynamic thing (businesses, garages, motels, buildings,
// entrances, HQs, gates, ATMs, map boxes, map icons, info boards, OOC shops, gas stations, drive-thrus,
// money bags, gift boxes, points, house sale signs, pickups, speed cameras, group vehicles, mapped
// objects) in cgrp_<server>.txt and sends them to every phone (PKT_CG_ENT). Families, factions (default
// server; Mega Vil keeps its own factions), gold bars, the CGRP admin levels and the bank (default
// server; Mega Vil keeps its own bank) are per account. The phone forwards the commands below as
// text (PKT_CG_CMD); the server checks the level, does the work and answers with chat lines, dialogs
// and PKT_CG_ACT. Houses use the Home Kit (both servers have it), the dealership stays as it was.
#include <stdint.h>
#include <string.h>

#define CG_NAME_LEN   32
#define CG_TEXT_LEN   64
#define CG_PAGE       5
#define CG_MAX_ENTS   3000
#define CG_STREAM_M   220.0f   // objects / pickups made within this distance

enum eCgKind : uint8_t
{
    CGK_NONE = 0,
    CGK_BIZ,        // business: enter, buy, products, owner till
    CGK_GARAGE,     // garage: enter with the car
    CGK_MOTEL,      // motel: rent a room, enter
    CGK_ATM,        // ATM machine (default server: CGRP bank)
    CGK_MAPBOX,     // map box: /buymap, then /viewmap
    CGK_MAPICON,    // radar icon
    CGK_HQ,         // family / faction HQ (sub = CGG_*)
    CGK_GATE,       // gate (sub = CGG_*), x..a = closed, ix..ia = open
    CGK_ENTRANCE,   // entrance (outside <-> inside)
    CGK_BUILDING,   // building (name, lock, VIP, music)
    CGK_INFOBOARD,  // information board (text)
    CGK_OOCSHOP,    // OOC shop (gold bars)
    CGK_GSTATION,   // gas station (/refuel)
    CGK_DRIVETHRU,  // drive-thru (/order in a car)
    CGK_MONEYBAG,   // money bag (val = cash, first one takes it)
    CGK_GIFTBOX,    // gift box (/getgift)
    CGK_POINT,      // point (families capture it, the owner earns every payday)
    CGK_HSSIGN,     // house sale sign
    CGK_PICKUP,     // pickup (sub = CGP_*)
    CGK_SPEEDCAM,   // speed camera (val = limit km/h, price = fine)
    CGK_CARSPOT,    // vehicle spot (sub = CGG_* owner, model = car)
    CGK_OBJECT,     // mapped object (/editor): ix iy iz = rotation
    CGK_COUNT
};
static const char* const kCgKindName[CGK_COUNT] = { "", "Business", "Garage", "Motel", "ATM", "Map-Box", "Map-Icon", "HQ", "Gate", "Entrance",
    "Building", "Information Board", "OOC Shop", "Gas Station", "Drive-Thru", "Money Bag", "Gift Box", "Point", "House Sale Sign", "Pickup",
    "Speed Camera", "Vehicle", "Object" };

enum eCgGroup : uint8_t { CGG_PUBLIC = 0, CGG_FAMILY = 1, CGG_FACTION = 2, CGG_PLAYER = 3 };
enum eCgFlag : uint8_t { CGF_LOCKED = 1, CGF_VIP = 2, CGF_OPEN = 4, CGF_SALE = 8, CGF_MINE = 16, CGF_INUSE = 32, CGF_MEMBER = 64 };   // MINE / MEMBER: per phone
enum eCgPickup : uint8_t { CGP_INFO = 0, CGP_HEALTH, CGP_ARMOUR, CGP_WEAPON, CGP_CASH };
enum eCgBizType : uint8_t { CGB_247 = 0, CGB_AMMU, CGB_CLOTHES, CGB_FOOD, CGB_BAR, CGB_GYM, CGB_ELECTRO, CGB_CASINO, CGB_COUNT };
static const char* const kCgBizType[CGB_COUNT] = { "24/7", "Ammunation", "Clothes Shop", "Restaurant", "Bar / Club", "Gym", "Electronics", "Casino" };

// one entity on the wire (176 bytes)
struct CgRow
{
    uint16_t id; uint8_t kind, flags;
    uint8_t  sub, area, iarea, pad;
    uint16_t model, group;
    uint16_t level, vw;
    int32_t  price, val;
    float    x, y, z, a;          // outside (gate: closed, object: position)
    float    ix, iy, iz, ia;      // inside (gate: open, object: rotation x y z)
    char     name[CG_NAME_LEN];
    char     owner[24];
    char     text[CG_TEXT_LEN];
};
enum eCgEntOp : uint8_t { CGO_LIST = 1, CGO_MORE, CGO_SET, CGO_DEL };
struct PktCgEnt { uint8_t type, op, n, pad; uint16_t total, pad2; CgRow r[CG_PAGE]; };

// C->S: a command line (op CGC_TEXT) or the 2 s heartbeat (op CGC_BEAT); the phone's own facts ride along
enum eCgCmdOp : uint8_t { CGC_TEXT = 1, CGC_BEAT, CGC_EDITOR, CGC_SPEED };
struct PktCgCmd
{
    uint8_t type, op, area, inCar;
    int32_t cash;                 // GTA money (default server)
    float   pos[3], heading;
    uint16_t carModel, speed;     // km/h
    uint16_t edId, edModel;       // CGC_EDITOR: object id (0 = new), model
    float   ed[6];                // CGC_EDITOR: x y z rx ry rz
    char    text[128];
};

// S->C: me
struct PktCgMe
{
    uint8_t  type, level, helper, famRank;   // level = CGRP admin level 0..9
    uint16_t family, faction;                // 0 = none
    uint8_t  facRank, vip, flags, jailMin;   // flags: 1 frozen, 2 muted, 4 has map, 8 on admin duty, 16 masked
    int32_t  gold, bank, cookies;
    uint16_t insideId, vw;                   // CG entity I am inside (0 = none)
    char     famName[CG_NAME_LEN];
    char     facName[CG_NAME_LEN];
};

// S->C: actions
enum eCgAct : uint8_t
{
    CGA_TELE = 1,    // pos + heading, a = area (255 = keep), b = 1: take the car too
    CGA_HEALTH,      // val
    CGA_ARMOUR,      // val
    CGA_MONEY,       // val (+ / -)
    CGA_SETMONEY,    // val
    CGA_GUN,         // a = weapon, val = ammo
    CGA_DISARM,
    CGA_SKIN,        // val
    CGA_FREEZE,      // a = 1 / 0
    CGA_WEATHER,     // val (-1 = the server's)
    CGA_WANTED,      // val
    CGA_GAMETEXT,    // text, val = ms, a = style (0 big, 1 small news line)
    CGA_COUNTDOWN,   // val seconds
    CGA_FIXCAR,
    CGA_CARCOLOR,    // a, b
    CGA_SPAWNCAR,    // val = model
    CGA_DELCAR,      // remove my car (the one I sit in / my last one)
    CGA_EXPLODE,     // pos, a = type
    CGA_FIRE,        // pos
    CGA_SPEC,        // a = player id, b = 1 on / 0 off
    CGA_PARACHUTE,   // skydive: pos high + parachute
    CGA_ANIM,        // text = "anim file"
    CGA_FIGHT,       // val = style (stored, walk style)
    CGA_MVADD,       // val = model (default server: My Vehicles)
    CGA_MVDEL,       // val = model (default server: My Vehicles, the first one with it)
    CGA_FUEL,        // fill every car (default server)
    CGA_FEED,        // val = hunger %
    CGA_GPS,         // pos (0 0 0 = off)
    CGA_JAIL,        // val = seconds (0 = out), pos = cell, a = area
    CGA_CARHP,       // val
    CGA_CARTELE,     // pos + a = area (my car + me)
    CGA_MASK,        // a = 1 / 0 (name tag hidden)
    CGA_KILL,
    CGA_EDITOR,      // a = 1 open (val = object id, 0 = new, b = model) / 0 close
    CGA_WALK,        // text = walk style
    CGA_EJECT,       // out of the car
    CGA_GOINCAR,     // a = player id: into his car (passenger seat when taken)
    CGA_SETCASH,     // val = the Mega Vil RP cash shown (info only)
};
struct PktCgAct { uint8_t type, act, a, b; int32_t val; float pos[4]; char text[96]; };

// ---- commands the phone sends to the server (the rest stay with the old handlers) ----
// mask: 1 = default server, 2 = Mega Vil Futuristic. lvl: 0 = everybody, 1..9 = CGRP admin level.
enum { CGM_D = 1, CGM_M = 2, CGM_B = 3 };
struct CgCmdDef { const char* name; uint8_t lvl, mask; const char* usage; };
static const char* const kCgLevelName[10] = { "Player", "Server Moderator", "Junior Administrator", "General Administrator", "Senior Administrator",
    "Head Administrator", "Community Manager", "Co-Executive Admin", "Executive Administrator", "Community Scripter" };
static const CgCmdDef kCgCmds[] =
{
    // ---- players ----
    { "cgrp", 0, CGM_B, "the CGRP commands" },
    { "buy", 0, CGM_B, "buy in this business" },
    { "acceptcar", 0, CGM_B, "accept the car offer" }, { "acceptfamily", 0, CGM_B, "accept the family invite" }, { "acceptfaction", 0, CGM_D, "accept the faction invite" },
    { "refuel", 0, CGM_M, "refuel at a gas station" },
    { "getcar", 0, CGM_B, "take the family / faction car at this spot" },
    { "takebag", 0, CGM_B, "take the money bag here" }, { "usepickup", 0, CGM_B, "use the pickup here" },
    { "freeze", 2, CGM_B, "[id]" }, { "weather", 5, CGM_B, "[id] [weather]" }, { "veh", 4, CGM_M, "[model] spawn a car" },
    { "enter", 0, CGM_B, "go in (business, garage, motel, building, entrance, HQ)" },
    { "exit", 0, CGM_B, "go out" },
    { "buybiz", 0, CGM_B, "buy the business you stand at" },
    { "sellbiz", 0, CGM_B, "sell your business (half the price back)" },
    { "bizinfo", 0, CGM_B, "your business" },
    { "bizname", 0, CGM_B, "[name]" },
    { "bizfee", 0, CGM_B, "[entrance fee]" },
    { "bizwithdraw", 0, CGM_B, "[amount] from the till" },
    { "bizdeposit", 0, CGM_B, "[amount] to the till" },
    { "buyprods", 0, CGM_B, "[amount] products for your business ($5 each)" },
    { "buygarage", 0, CGM_B, "buy the garage you stand at" },
    { "sellgarage", 0, CGM_B, "sell your garage" },
    { "lockgarage", 0, CGM_B, "lock / unlock your garage" },
    { "rentroom", 0, CGM_B, "rent a motel room (paid every payday)" },
    { "unrentroom", 0, CGM_B, "stop renting" },
    { "families", 0, CGM_B, "every family" },
    { "family", 0, CGM_B, "your family" },
    { "fam", 0, CGM_B, "[text] family chat" },
    { "finvite", 0, CGM_B, "[id] invite to your family (rank 5+)" },
    { "fkick", 0, CGM_B, "[id/name] remove from your family (rank 5+)" },
    { "frank", 0, CGM_B, "[id] [1-6] family rank (leader)" },
    { "fquit", 0, CGM_B, "leave your family" },
    { "fdeposit", 0, CGM_B, "[amount] to the family safe" },
    { "fwithdraw", 0, CGM_B, "[amount] from the family safe (rank 5+)" },
    { "fgate", 0, CGM_B, "open / close your family gate" },
    { "facgate", 0, CGM_B, "open / close your faction gate" },
    { "gate", 0, CGM_B, "open / close a public gate" },
    { "lockhq", 0, CGM_B, "lock / unlock your HQ (rank 5+)" },
    { "famcar", 0, CGM_B, "take the family vehicle you stand at" },
    { "faccar", 0, CGM_B, "take the faction vehicle you stand at" },
    { "park", 0, CGM_B, "park the group vehicle back" },
    { "points", 0, CGM_B, "every point + owner" },
    { "capture", 0, CGM_B, "capture the point you stand at (family)" },
    { "goldbars", 0, CGM_B, "your gold bars" },
    { "oocshop", 0, CGM_B, "the OOC shop (gold bars)" },
    { "order", 0, CGM_B, "order food at a drive-thru (in a car)" },
    { "getgift", 0, CGM_B, "open the gift box" },
    { "buymap", 0, CGM_B, "buy a map at a map box ($25)" },
    { "viewmap", 0, CGM_B, "places on your map (GPS)" },
    { "sellcar", 0, CGM_B, "[player id] [price] [vehicle slot] sell a car to a player" },
    { "cancelcar", 0, CGM_B, "cancel your car sale offer" },
    { "atm", 0, CGM_D, "use the ATM you stand at" },
    { "factions", 0, CGM_D, "every faction" },
    { "invite", 0, CGM_D, "[id] invite to your faction (leader)" },
    { "uninvite", 0, CGM_D, "[id/name] remove from your faction (leader)" },
    { "giverank", 0, CGM_D, "[id] [1-6] faction rank (leader)" },
    { "quitfaction", 0, CGM_D, "leave your faction" },
    { "r", 0, CGM_D, "[text] faction radio" },
    { "dept", 0, CGM_D, "[text] department radio (every faction)" },
    { "editor", 6, CGM_B, "[model] map in game: place / move objects (no model = the nearest)" },
    { "cgadmin", 1, CGM_B, "the CGRP admin commands for your level" },
    // ---- 1 Server Moderator ----
    { "listguns", 1, CGM_B, "the weapon ids" },
    { "check", 1, CGM_B, "[id] stats" },
    { "spec", 1, CGM_B, "[id/off] spectate" },
    { "setint", 1, CGM_B, "[id] [interior]" },
    { "setvw", 1, CGM_B, "[id] [virtual world]" },
    { "nmute", 1, CGM_B, "[id] newbie / help mute" },
    { "a", 1, CGM_B, "[text] staff chat" },
    { "admin", 1, CGM_B, "[text] staff chat" },
    { "warnings", 1, CGM_B, "[id] warnings" },
    { "ch", 1, CGM_B, "[id] check for hacks (money, health, weapons)" },
    { "kcc", 1, CGM_B, "[id] kick from a car (eject)" },
    { "senddmv", 1, CGM_B, "[id] send to the DMV" },
    // ---- 2 Junior Administrator ----
    { "skydive", 2, CGM_B, "[id] up in the sky with a parachute" },
    { "bigears", 2, CGM_B, "hear every chat" },
    { "unfreeze", 2, CGM_B, "[id]" },
    { "slap", 2, CGM_B, "[id]" },
    { "cnn", 2, CGM_B, "[text] news for everybody (~n~ new line)" },
    { "rmute", 2, CGM_B, "[id] report mute" },
    { "sendtols", 2, CGM_B, "[id] send to Los Santos" },
    { "refund", 2, CGM_B, "[id] [amount] [reason]" },
    { "tuteject", 2, CGM_B, "[id] end the tutorial" },
    { "hoseject", 2, CGM_B, "[id] out of the hospital" },
    { "fuelcars", 2, CGM_B, "fill every car" },
    { "setjob", 2, CGM_B, "[id] [job]" },
    { "forcetut", 2, CGM_B, "[id] show the tutorial again" },
    { "freezeplayers", 2, CGM_B, "[distance]" },
    { "unfreezeplayers", 2, CGM_B, "[distance]" },
    { "setskin", 2, CGM_B, "[id] [skin]" },
    { "muteplayers", 2, CGM_B, "[distance]" },
    { "unmuteplayers", 2, CGM_B, "[distance]" },
    { "admute", 2, CGM_B, "[id] advert mute" },
    { "listmuted", 2, CGM_B, "muted players" },
    { "listmasked", 2, CGM_B, "masked players" },
    { "aduty", 2, CGM_B, "admin duty on" },
    { "adutyoff", 2, CGM_B, "admin duty off" },
    { "checkprison", 2, CGM_B, "[id] prison time" },
    { "afgate", 2, CGM_B, "open the nearest family / faction gate" },
    { "agate", 2, CGM_B, "open the nearest gate" },
    // ---- 3 General Administrator ----
    { "fws", 3, CGM_B, "[id] full weapon set" },
    { "atazer", 3, CGM_B, "[id] taze" },
    { "amask", 3, CGM_B, "admin mask (hide your name)" },
    { "rhw", 3, CGM_B, "[id] remove the hidden weapons" },
    { "cp", 3, CGM_B, "[id] checkpoint to you" },
    { "mole", 3, CGM_B, "[text] SMS from the mole to everybody" },
    { "had", 3, CGM_B, "[text] headquarters advert" },
    { "ipcheck", 3, CGM_B, "[id]" },
    { "countdown", 3, CGM_B, "[seconds] countdown for the players around you" },
    { "disarm", 3, CGM_B, "[id]" },
    { "nohospital", 3, CGM_B, "[id] heal + revive here" },
    { "setaccent", 3, CGM_B, "[id] [accent]" },
    { "acolorcar", 3, CGM_B, "[color1] [color2] your car" },
    { "setgas", 3, CGM_B, "[percent] your car's fuel" },
    { "setcarint", 3, CGM_B, "[interior] move your car into it" },
    { "setcarvw", 3, CGM_B, "[virtual world]" },
    // ---- 4 Senior Administrator ----
    { "vehmenu", 4, CGM_B, "vehicle menu" },
    { "fixveh", 4, CGM_B, "repair your car" },
    { "givegun", 4, CGM_B, "[id] [weapon] [ammo]" },
    { "givegunall", 4, CGM_B, "[weapon] [ammo]" },
    { "setgunskill", 4, CGM_B, "[id] [weapon] [skill]" },
    { "setmoney", 4, CGM_B, "[id] [money]" },
    { "setstat", 4, CGM_B, "[id] [gold/bank/cookies/level] [value]" },
    { "setfightstyle", 4, CGM_B, "[id] [style 4-7, 15, 16]" },
    { "setanim", 4, CGM_B, "[id] [anim] [file]" },
    { "givecookieall", 4, CGM_B, "[amount]" },
    { "givegoldbarall", 4, CGM_B, "[amount]" },
    { "setplayercolor", 4, CGM_B, "[id] [RRGGBB]" },
    { "setplayerwanted", 4, CGM_B, "[id] [0-6]" },
    { "adivorce", 4, CGM_B, "[id]" },
    { "makeircadmin", 4, CGM_B, "[id] (the staff chat relay)" },
    { "destroycar", 4, CGM_B, "[id] remove his car" },
    { "destroycars", 4, CGM_B, "remove every empty spawned car" },
    { "eventhelp", 4, CGM_B, "event commands" },
    { "setcrime", 4, CGM_B, "[id] [crime]" },
    { "gotobluebus", 4, CGM_B, "the blue bus" },
    { "gotoblackbus", 4, CGM_B, "the black bus" },
    { "fartbomb", 4, CGM_B, "[id]" },
    // ---- 5 Head Administrator ----
    { "weatherall", 5, CGM_B, "[id / -1]" },
    { "tod", 5, CGM_B, "[hour / -1]" },
    { "dedit", 5, CGM_B, "[name] [amount] money of an offline account (bank)" },
    { "pointtime", 5, CGM_B, "[point id] [minutes] time to capture" },
    { "adetain", 5, CGM_B, "[id] [minutes] admin jail" },
    { "gotocar", 5, CGM_B, "[id] to his car" },
    { "pban", 5, CGM_B, "[id] [reason] permanent ban" },
    { "banaccount", 5, CGM_B, "[account] [reason]" },
    { "wl", 5, CGM_B, "[id] [level] wanted level" },
    { "setcolor", 5, CGM_B, "[RRGGBB] your name colour" },
    { "banip", 5, CGM_B, "[ip] [reason]" },
    { "unbanip", 5, CGM_B, "[ip]" },
    { "deleteaccount", 5, CGM_B, "[account]" },
    { "rangeban", 5, CGM_B, "[id] ban the IP range" },
    { "giftbox", 5, CGM_B, "a gift box here" },
    { "editpoint", 5, CGM_B, "[name] / [id] [name/pos/owner/delete] [value] (new point here)" },
    { "setgoldbars", 5, CGM_B, "[id] [amount]" },
    { "givegoldbar", 5, CGM_B, "[id] [amount]" },
    { "goincar", 5, CGM_B, "[id] into his car" },
    { "fire", 5, CGM_B, "a fire where you stand" },
    { "re", 5, CGM_B, "resources: server stats" },
    { "resources", 5, CGM_B, "server stats" },
    // ---- 6 Community Manager ----
    { "hqcreate", 6, CGM_B, "[fam/fac] [id] HQ entrance here (then /hqedit [id] inside)" },
    { "hqedit", 6, CGM_B, "[hq id] the inside is where you stand" },
    { "hqdelete", 6, CGM_B, "[hq id]" },
    { "ganghqid", 6, CGM_B, "the HQ near you" },
    { "createfamgate", 6, CGM_B, "[family id] [model] gate here (closed)" },
    { "deletefamgate", 6, CGM_B, "[gate id]" },
    { "editfamgate", 6, CGM_B, "[gate id] the open position is here" },
    { "famgateeditor", 6, CGM_B, "[gate id] [x/y/z/rot] [value] fine tune" },
    { "famgateid", 6, CGM_B, "the family gate near you" },
    { "famegateid", 6, CGM_B, "the family gate near you" },
    { "fedit", 6, CGM_B, "[family id] [name/leader/color/safe] [value]" },
    { "createfacgate", 6, CGM_B, "[faction id] [model]" },
    { "deletefacgate", 6, CGM_B, "[gate id]" },
    { "facgateeditor", 6, CGM_B, "[gate id] [x/y/z/rot] [value]" },
    { "editfacgate", 6, CGM_B, "[gate id] the open position is here" },
    { "facgateid", 6, CGM_B, "the faction gate near you" },
    { "editfaction", 6, CGM_D, "[faction id] [name/type/leader] [value] (new: /editfaction new [name])" },
    { "editfactionpay", 6, CGM_D, "[faction id] [rank 1-6] [pay]" },
    { "createfcar", 6, CGM_B, "[fam/fac] [id] [model] [color1] [color2] vehicle spot here" },
    { "deletefcar", 6, CGM_B, "[vehicle spot id]" },
    { "creategate", 6, CGM_B, "[model] public gate here (closed)" },
    { "deletegate", 6, CGM_B, "[gate id]" },
    { "editgate", 6, CGM_B, "[gate id] the open position is here" },
    { "gateid", 6, CGM_B, "the gate near you" },
    { "acceptrequest", 6, CGM_B, "[family name] [id] make him the family leader" },
    // ---- 7 Co-Executive Admin ----
    { "bizint", 7, CGM_B, "[biz id] [interior 1-30] / list" },
    { "createbiz", 7, CGM_B, "[name] business here" },
    { "deletebiz", 7, CGM_B, "[biz id]" },
    { "bizlevel", 7, CGM_B, "[biz id] [level]" },
    { "bizprice", 7, CGM_B, "[biz id] [price]" },
    { "biztype", 7, CGM_B, "[biz id] [type 0-7]" },
    { "bizprods", 7, CGM_B, "[biz id] [products]" },
    { "movebiz", 7, CGM_B, "[biz id] [out/in] to where you stand" },
    { "clearbiz", 7, CGM_B, "[biz id] no owner, for sale" },
    { "setbizowner", 7, CGM_B, "[biz id] [player id]" },
    { "gotobiz", 7, CGM_B, "[biz id]" },
    { "lockbiz", 0, CGM_B, "lock / unlock your business ([biz id] for staff)" },
    { "houseint", 7, CGM_B, "[house id] [interior] Home Kit house" },
    { "chouseint", 7, CGM_B, "[house id] [interior]" },
    { "createhouse", 7, CGM_B, "[price] [interior] Home Kit house here, for sale" },
    { "deletehouse", 7, CGM_B, "[house id]" },
    { "houselevel", 7, CGM_B, "[house id] [level]" },
    { "houseprice", 7, CGM_B, "[house id] [price]" },
    { "movehouse", 7, CGM_B, "[house id] to where you stand" },
    { "clearhouse", 7, CGM_B, "[house id] no owner, for sale" },
    { "sethouseowner", 7, CGM_B, "[house id] [player id]" },
    { "gotohouse", 7, CGM_B, "[house id]" },
    { "createfamily", 7, CGM_B, "[name] [leader id]" },
    { "deletefamily", 7, CGM_B, "[family id]" },
    { "createbuilding", 7, CGM_B, "[name] building here (then /movebuilding [id] in)" },
    { "deletebuilding", 7, CGM_B, "[id]" },
    { "buildingname", 7, CGM_B, "[id] [name]" },
    { "lockbuilding", 7, CGM_B, "[id]" },
    { "movebuilding", 7, CGM_B, "[id] [out/in]" },
    { "buildingvw", 7, CGM_B, "[id] [virtual world]" },
    { "makebuildingvip", 7, CGM_B, "[id] VIP lounge on / off" },
    { "sbm", 7, CGM_B, "[id] [radio 0-12 / -1] building music" },
    { "setbuildingmusic", 7, CGM_B, "[id] [radio 0-12 / -1]" },
    { "gotobuilding", 7, CGM_B, "[id]" },
    { "createentrance", 7, CGM_B, "[name] entrance here (then /editentrance [id] in)" },
    { "editentrance", 7, CGM_B, "[id] [out/in/name/vip/lock] [value]" },
    { "entranceid", 7, CGM_B, "the entrance near you" },
    { "deleteentrance", 7, CGM_B, "[id]" },
    { "gotoentrance", 7, CGM_B, "[id]" },
    { "creategarage", 7, CGM_B, "[price] garage here (then /editgarage [id] in)" },
    { "deletegarage", 7, CGM_B, "[id]" },
    { "editgarage", 7, CGM_B, "[id] [out/in/price/clear] [value]" },
    { "gotogarage", 7, CGM_B, "[id]" },
    { "garageid", 7, CGM_B, "the garage near you" },
    { "createmotel", 7, CGM_B, "[name] motel here (then /movemotel [id] in)" },
    { "deletemotel", 7, CGM_B, "[id]" },
    { "movemotel", 7, CGM_B, "[id] [out/in]" },
    { "motelvw", 7, CGM_B, "[id] [virtual world]" },
    { "lockmotel", 7, CGM_B, "[id]" },
    { "makemotelvip", 7, CGM_B, "[id]" },
    { "motelname", 7, CGM_B, "[id] [name]" },
    { "gotomotel", 7, CGM_B, "[id]" },
    { "createcar", 7, CGM_B, "[model] [price] public / rental vehicle spot here" },
    { "deletecar", 7, CGM_B, "[spot id]" },
    { "setcarinfo", 7, CGM_B, "[spot id] [name]" },
    { "createpcar", 7, CGM_B, "[player id] [model] personal vehicle spot here" },
    { "deletepcar", 7, CGM_B, "[spot id]" },
    { "parkpcar", 7, CGM_B, "[spot id] the spot moves to where you stand" },
    { "createdrivethru", 7, CGM_B, "[biz id] [name] drive-thru here" },
    { "deletedrivethru", 7, CGM_B, "[id]" },
    { "editdrivethru", 7, CGM_B, "[id] [location/name/biz] [value]" },
    { "createhss", 7, CGM_B, "[house id] house sale sign here" },
    { "deletehss", 7, CGM_B, "[id]" },
    { "hssignid", 7, CGM_B, "the sign near you" },
    { "createmapicon", 7, CGM_B, "[name] [icon 0-63]" },
    { "editmapicon", 7, CGM_B, "[id] [icon/pos/name/delete] [value]" },
    { "nearmapicon", 7, CGM_B, "the map icon near you" },
    { "createmapb", 7, CGM_B, "map box here" },
    { "deletemapb", 7, CGM_B, "[id]" },
    { "createinfoboard", 7, CGM_B, "[text] information board here" },
    { "deleteinfoboard", 7, CGM_B, "[id]" },
    { "setinfoboardstring", 7, CGM_B, "[id] [text] (| = new line)" },
    { "creategstation", 7, CGM_B, "gas station here" },
    { "deletegstation", 7, CGM_B, "[id]" },
    { "createoocshop", 7, CGM_B, "OOC shop here" },
    { "deleteoocshop", 7, CGM_B, "[id]" },
    { "gcam", 7, CGM_B, "[limit km/h] [fine] speed camera here (/gcam delete [id])" },
    { "createpickup", 7, CGM_B, "[model] [info/health/armour/weapon/cash] [value / text]" },
    { "createatm", 7, CGM_D, "ATM here" },
    { "deleteatm", 7, CGM_D, "[id]" },
    // ---- 8 Executive Administrator ----
    { "payday", 8, CGM_B, "payday now for everybody" },
    { "oprison", 8, CGM_B, "[account] [minutes] prison (offline too)" },
    { "lethimtalk", 8, CGM_B, "[id] unmute every mute" },
    // ---- 9 Community Scripter ----
    { "shawntermination", 9, CGM_B, "[id] remove every staff level" },
    { "forcerelog", 9, CGM_B, "[id] reconnect" },
    { "makeleader", 9, CGM_D, "[id] [faction id] faction leader" },
    { "savechars", 9, CGM_B, "save everything now" },
    { "makemoneybag", 9, CGM_B, "[amount] money bag here" },
    { "moneybag", 9, CGM_B, "[amount] money bag here" },
    { "deletebag", 9, CGM_B, "[id]" },
    { "deletebags", 9, CGM_B, "every money bag" },
    { "makehelper", 9, CGM_B, "[id] [0/1]" },
};
static inline const CgCmdDef* CgFindCmd(const char* name, int mask)
{
    for(const auto& c : kCgCmds) if(!strcasecmp(c.name, name) && (c.mask & mask)) return &c;
    return nullptr;
}

// ---- OOC shop (gold bars) ----
struct CgOocItem { const char* name; int gold; uint8_t kind; int val; };   // kind: 0 cash, 1 cookies, 2 VIP, 3 health + armour, 4 weapon set, 5 name colour, 6 bank
static const CgOocItem kCgOoc[] =
{
    { "$25,000 cash", 5, 0, 25000 }, { "$100,000 cash", 15, 0, 100000 }, { "5 cookies", 3, 1, 5 },
    { "VIP (forever)", 50, 2, 1 }, { "Full health + armour", 1, 3, 100 }, { "Weapon set (deagle, M4, shotgun)", 8, 4, 0 },
    { "$250,000 bank", 30, 6, 250000 },
};
// ---- drive-thru menu ----
struct CgFood { const char* name; int price; int hunger, hp; };
static const CgFood kCgFood[] = { { "Burger", 15, 30, 20 }, { "Fries", 6, 15, 8 }, { "Cola", 4, 10, 5 }, { "Big Meal", 30, 70, 50 }, { "Salad", 10, 25, 25 } };
// ---- business products ----
struct CgBizItem { uint8_t type; const char* name; int price; uint8_t kind; int a, b; };   // kind: 0 health, 1 armour, 2 weapon (a, b ammo), 3 skin (a), 4 feed (a), 5 dice / phone (text only), 6 casino
static const CgBizItem kCgBizItems[] =
{
    { CGB_247, "Snack (hunger +20)", 10, 4, 20, 0 }, { CGB_247, "Water bottle (health +10)", 5, 0, 10, 0 }, { CGB_247, "Dice", 50, 5, 0, 0 }, { CGB_247, "Camera", 200, 2, 43, 50 },
    { CGB_247, "Flowers", 30, 2, 14, 1 }, { CGB_247, "Baseball bat", 120, 2, 5, 1 },
    { CGB_AMMU, "9mm (100)", 300, 2, 22, 100 }, { CGB_AMMU, "Desert Eagle (60)", 900, 2, 24, 60 }, { CGB_AMMU, "Shotgun (40)", 1200, 2, 25, 40 },
    { CGB_AMMU, "MP5 (200)", 1800, 2, 29, 200 }, { CGB_AMMU, "M4 (250)", 3500, 2, 31, 250 }, { CGB_AMMU, "Country rifle (30)", 2000, 2, 33, 30 }, { CGB_AMMU, "Body armour", 400, 1, 100, 0 },
    { CGB_CLOTHES, "Skin: street (7)", 150, 3, 7, 0 }, { CGB_CLOTHES, "Skin: business (17)", 300, 3, 17, 0 }, { CGB_CLOTHES, "Skin: gangsta (102)", 200, 3, 102, 0 },
    { CGB_CLOTHES, "Skin: girl (12)", 200, 3, 12, 0 }, { CGB_CLOTHES, "Skin: biker (247)", 250, 3, 247, 0 }, { CGB_CLOTHES, "Skin: rich (295)", 900, 3, 295, 0 },
    { CGB_FOOD, "Burger menu (hunger +60)", 25, 4, 60, 0 }, { CGB_FOOD, "Pizza (hunger +45)", 18, 4, 45, 0 }, { CGB_FOOD, "Salad (health +30)", 12, 0, 30, 0 },
    { CGB_BAR, "Beer (health +5)", 8, 0, 5, 0 }, { CGB_BAR, "Whisky (health +10)", 20, 0, 10, 0 }, { CGB_BAR, "Pool cue", 60, 2, 7, 1 },
    { CGB_GYM, "Training (health 100)", 100, 0, 100, 0 }, { CGB_GYM, "Brass knuckles", 80, 2, 1, 1 },
    { CGB_ELECTRO, "Phone", 250, 5, 0, 0 }, { CGB_ELECTRO, "Night vision", 1200, 2, 44, 1 }, { CGB_ELECTRO, "Spray can", 150, 2, 41, 500 },
    { CGB_CASINO, "Bet $100 (double or nothing)", 100, 6, 100, 0 }, { CGB_CASINO, "Bet $1,000", 1000, 6, 1000, 0 },
};

// business interiors (the CGRP /bizint list): area, x y z heading
struct CgInt { uint8_t area; float x, y, z, a; const char* name; };
static const CgInt kCgInts[] =
{
    { 0, 0, 0, 0, 0, "None" },
    { 18, -30.95f, -91.71f, 1003.55f, 0.0f, "24/7 (1)" },          { 6, -26.69f, -55.71f, 1003.55f, 0.0f, "24/7 (2)" },
    { 1, 285.80f, -41.80f, 1001.52f, 0.0f, "Ammunation (1)" },      { 4, 285.80f, -84.55f, 1001.52f, 0.0f, "Ammunation (2)" },
    { 7, 315.38f, -143.07f, 999.60f, 0.0f, "Ammunation (range)" }, { 15, 207.73f, -110.90f, 1005.13f, 0.0f, "Binco" },
    { 14, 204.33f, -168.69f, 1000.52f, 0.0f, "Didier Sachs" },    { 18, 161.39f, -96.68f, 1001.80f, 0.0f, "Zip" },
    { 3, 207.06f, -140.37f, 1003.51f, 0.0f, "Pro-Laps" },          { 5, 372.33f, -133.52f, 1001.49f, 0.0f, "Well Stacked Pizza" },
    { 9, 365.67f, -11.61f, 1001.85f, 0.0f, "Cluckin' Bell" },      { 10, 363.11f, -74.88f, 1001.50f, 0.0f, "Burger Shot" },
    { 17, 377.08f, -193.30f, 1000.64f, 0.0f, "Rusty Brown's Donuts" }, { 11, 501.98f, -67.75f, 998.75f, 0.0f, "Ten Green Bottles bar" },
    { 17, 493.39f, -22.72f, 1000.67f, 0.0f, "Alhambra club" },     { 3, 1212.02f, -26.89f, 1000.95f, 0.0f, "Big Spread Ranch" },
    { 5, 772.11f, -3.90f, 1000.73f, 0.0f, "Ganton gym" },          { 6, 774.21f, -48.92f, 1000.59f, 0.0f, "Cobra gym" },
    { 7, 773.58f, -77.10f, 1000.66f, 0.0f, "Below the Belt gym" }, { 6, -2240.47f, 137.06f, 1035.41f, 270.0f, "Zero RC (electronics)" },
    { 10, 2018.45f, 1017.83f, 996.88f, 90.0f, "Four Dragons casino" }, { 1, 2233.94f, 1714.69f, 1012.38f, 180.0f, "Caligula's casino" },
    { 12, 1133.07f, -15.21f, 1000.68f, 0.0f, "Small casino" },     { 3, 1494.33f, 1304.16f, 1093.29f, 0.0f, "Bike school / office" },
    { 18, 1727.04f, -1637.84f, 20.22f, 180.0f, "Atrium" },         { 3, 384.81f, 173.80f, 1008.38f, 90.0f, "City Hall" },
    { 6, 246.78f, 63.90f, 1003.64f, 0.0f, "LSPD" },                { 10, 246.38f, 110.85f, 1003.22f, 0.0f, "SFPD" },
    { 3, 238.66f, 138.62f, 1003.02f, 0.0f, "LVPD" },               { 2, 2541.70f, -1303.85f, 1025.07f, 270.0f, "Crack factory" },
    { 5, 2233.80f, -1115.26f, 1050.88f, 0.0f, "Motel room (Jefferson)" }, { 15, 2215.45f, -1150.48f, 1025.79f, 270.0f, "Jefferson motel" },
    { 1, 2525.04f, -1679.12f, 1015.49f, 270.0f, "Gang house" },    { 17, -959.56f, 1955.98f, 9.00f, 180.0f, "Sherman dam" },
    { 1, 1416.11f, -15.75f, 1000.93f, 0.0f, "Warehouse 1" },       { 18, 1302.52f, -1.79f, 1001.03f, 0.0f, "Warehouse 2" },
    { 2, 2577.43f, -1287.24f, 1044.12f, 90.0f, "Garage / big office" }, { 1, -2158.72f, 643.15f, 1052.38f, 180.0f, "Wu Zi Mu's" },
};
static const int kCgIntCount = (int)(sizeof(kCgInts) / sizeof(kCgInts[0]));
