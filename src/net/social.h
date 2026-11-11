// 3.63: MP SOCIAL - friends, private chat, group chats, party chat, communities and the inbox.
//
// How it works (no server of ours needed):
//   - Every phone makes its own key (ECDSA / ECDH P-256, saved in <MP data>/social/key.txt). Your player ID is the
//     first 8 digits of SHA-256(public key): nobody can use your ID without your key.
//   - Everything goes over the public HiveMQ broker (Online::SocLink*). Every message is signed; private ones
//     (friend requests, chats, group / party invites) are end-to-end encrypted with AES-256-GCM (ECDH between the
//     two keys), so the broker only sees random bytes.
//   - Mailbox = retained messages on wagu/soc1/m/<your id>/<message id>: they wait on the broker while you are
//     offline, you get them when you come online and clear them after reading.
//   - Directory (search by name / ID), presence (online / offline) and communities are retained messages too.
//   - Your friends, chats and communities are also kept on the phone (<MP data>/social/state.txt).
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace Social
{
    enum { CONV_DM = 0, CONV_GROUP = 1, CONV_PARTY = 2 };
    enum { N_FRIEND_REQ = 0, N_FRIEND_ACC, N_FRIEND_DEC, N_MSG, N_GROUP, N_PARTY_INV, N_PARTY_ACC, N_COMMUNITY, N_INFO };

    struct Person
    {
        std::string id, name, pub;   // pub = hex public key (130 chars)
        int64_t     since = 0;       // friends since / request time (unix s)
    };
    struct Msg
    {
        std::string fromId, fromName, text;
        int64_t     ts = 0;
        bool        mine = false;
    };
    struct Member { std::string id, name; };
    struct Conv
    {
        std::string key;             // "u<id>" = private chat, "g<gid>" = group / party
        int         kind = CONV_DM;
        std::string gid, title, owner;
        std::vector<Member> members; // groups / party (with you)
        std::vector<Msg>    msgs;    // last 150
        int         unread = 0;
        int64_t     last = 0;
    };
    struct Post { std::string id, fromId, fromName, text; int64_t ts = 0; };
    struct Community
    {
        std::string cid, name, desc, ownerId, ownerName;
        int64_t     ts = 0;
        bool        joined = false, followed = false;   // you
        int         members = 0, followers = 0;          // counted from the broker (when you open it)
        std::vector<Post> posts;                          // newest first
        int64_t     seen = 0;                             // newest post you have seen
        bool        deleted = false;
        std::vector<Member> memberList;                   // joined members (when you open it)
    };
    struct Notif
    {
        int         kind = N_INFO;
        std::string fromId, fromName, ref, text;          // ref = conv key / gid / cid
        int64_t     ts = 0;
        bool        read = false, done = false;           // done = request answered
    };

    void Init(const char* dataDir);
    void Tick();                         // every frame (cheap)
    void Shutdown();
    bool Up();                           // connected to the broker
    const char* Broker();

    const std::string& MyId();
    const std::string& MyName();
    void SetName(const char* name);      // your player / account name (shown to others)
    bool IsOnline(const std::string& id);
    std::string NameOf(const std::string& id);   // friend / directory name ("" unknown)

    // ---- friends ----
    void Search(const std::string& query);       // ID (8 digits, optional #) or part of a player name
    const std::string& SearchQuery();
    std::vector<Person> SearchResults();         // filled as answers arrive
    bool Searching();                            // waiting for the first answers
    const std::vector<Person>& Friends();
    const std::vector<Person>& Incoming();       // waiting for YOUR accept / decline
    const std::vector<Person>& Outgoing();       // you asked, waiting for theirs
    bool IsFriend(const std::string& id);
    bool IsOutgoing(const std::string& id);
    bool IsIncoming(const std::string& id);
    bool AddFriend(const std::string& id);       // sends the request (needs their profile: search first)
    void Accept(const std::string& id);
    void Decline(const std::string& id);
    void CancelRequest(const std::string& id);
    void RemoveFriend(const std::string& id);

    // ---- chats ----
    std::vector<Conv*> Convs();                  // newest first
    Conv* FindConv(const std::string& key);
    Conv* OpenDm(const std::string& friendId);   // makes the private chat
    void  MarkRead(const std::string& key);
    bool  SendText(const std::string& key, const std::string& text);
    std::string CreateGroup(const std::string& name, const std::vector<std::string>& friendIds);   // -> conv key
    void  AddToGroup(const std::string& key, const std::string& friendId);
    void  LeaveGroup(const std::string& key);
    int   UnreadChats();

    // ---- party (Valorant style: up to 5, the leader invites, invites need an accept) ----
    Conv* Party();                               // nullptr = not in a party
    std::string CreateParty();
    bool  InviteToParty(const std::string& friendId);
    void  AcceptParty(const std::string& gid);
    void  DeclineParty(const std::string& gid);
    void  LeaveParty();

    // ---- communities ----
    std::vector<Community*> Communities();       // every listed community (newest first)
    Community* FindCommunity(const std::string& cid);
    std::string CreateCommunity(const std::string& name, const std::string& desc);
    void  JoinCommunity(const std::string& cid, bool join);
    void  FollowCommunity(const std::string& cid, bool follow);
    bool  PostCommunity(const std::string& cid, const std::string& text);
    void  DeleteCommunity(const std::string& cid);   // owner only
    void  ViewCommunity(const std::string& cid);     // load its posts + member counts ("" = none)

    // ---- 4.13: Moquil (the MP social app for Android / the web, moquil/) ----
    struct Rich                                  // a player's Moquil profile (p/<id>, kind pf2)
    {
        std::string display, pronouns, bio, status, statusEmoji, app;
        int64_t     statusUntil = 0;             // unix s, 0 = never clears
        int64_t     since = 0;                   // member since (unix s)
        bool        has = false;
    };
    const Rich* RichOf(const std::string& id);   // nullptr = no Moquil profile seen
    std::string StatusOf(const std::string& id); // custom status text ("" none / cleared)
    bool        OnMoquil(const std::string& id); // their presence comes from Moquil (not the game)
    // link code: Moquil types it in Settings > Connections and becomes this same player (same ID, friends, chats)
    const std::string& LinkCode();               // "" = none; makes nothing by itself
    void        MakeLinkCode();                  // new 10 character code, valid 10 minutes
    int         LinkCodeLeft();                  // seconds left (0 = expired / none)
    void        ClearLinkCode();

    // ---- inbox ----
    std::vector<Notif*> Inbox();                 // newest first
    int   UnreadInbox();
    void  MarkInboxRead();
    void  ClearInbox();
}
