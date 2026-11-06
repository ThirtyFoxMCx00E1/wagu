// Online lobby + relay (version 2.5).
//
// Phones on mobile data (or on Wi-Fi routers that isolate devices) can't reach each other
// directly, so LAN discovery never finds them. This module solves that with no website and no
// VPS: every copy of the mod keeps one outgoing TCP connection to a free, always-on public
// MQTT broker (broker.hivemq.com, with test.mosquitto.org and broker.emqx.io as fallbacks).
//
//  - Lobby:  a phone that hosts a server publishes a small retained "listing" message.
//            Every other phone in the world sees it within a few seconds. The broker removes
//            the listing by itself when the host's game closes or loses its connection (MQTT
//            "last will"), so dead servers never stay in the list.
//  - Relay:  game packets (the same UDP packets as on Wi-Fi) are tunnelled through the broker.
//            The joining phone talks to a local UDP proxy on 127.0.0.1:<free port>, the host phone
//            feeds the packets into its normal MPServer on 127.0.0.1:<port>. MPClient and
//            MPServer don't know the difference.
//
// Servers seen online are returned as LanServer entries with a "relay" IP (127.77.x.y) and a
// non-zero token, so the existing default/custom server logic (join if it exists, otherwise
// host it, takeover, duplicate merge) works the same for Wi-Fi and internet servers.
#pragma once
#include <atomic>
#include <stdint.h>
#include <vector>
#include <string>
#include "lan.h"

namespace Online
{
    void UseLegacyTopics();   // 3.04.6: the old thirtyfox/mp4/ lobby (mp_server --legacy-notice), call before Start
    void Start(const char* brokerFile = nullptr); // starts the background thread (safe to call twice); brokerFile = optional broker.txt (private broker, TLS + login)
    bool IsConnected();                // TCP + MQTT session to the broker is up
    uint32_t NetGeneration();          // 4.05.8: +1 every time the phone's networks change (Wi-Fi <-> mobile data)
    void SetHostDedicated(bool on);    // 2.29: this host is a 24/7 machine (mp_server), announced as "@start!d"
    const char* BrokerName();          // the broker in use ("" while not connected)
    int  BrokerRttMs();                // last broker round trip (-1 = unknown)
    int  BrokerJitterMs();             // 4.06: jitter of the broker round trip (ms)
    int  CongestionMs();               // 2.15: >0 while our uplink to the broker can't keep up

    // Blocks (call from a background thread only). Returns when the lobby is trustworthy:
    // connected for a few seconds (so every live host had time to announce itself), or the
    // broker can't be reached. Returns IsConnected().
    bool WaitReady(int maxMs);

    // Confirmed live servers (not our own one). ip = relay IP, token != 0.
    std::vector<LanServer> Snapshot();

    // Tell the lobby what this phone hosts. Call often (every frame is fine).
    void SetHosted(bool running, const char* name, uint16_t port, int players, int maxPlayers, int region);
    uint32_t MyToken();                // token of our hosted server (0 = not hosting)
    void SetHostIcon(int icon);
    void SetHostLang(const char* lang);
    // 4.03: "v1|cc|modeCol|mode|tags|desc|name1,name2" retained on d/<token> (CS:GO style list)
    void SetHostDetails(const std::string& d);
    void SetHostPreview(const std::string& jpeg);          // 4.04: + Add server > Upload preview image (retained p/<token>)
    bool Preview(uint32_t token, std::string& jpeg);        // 4.04: that server's picture, false = none
    void SetHostLogo(const std::string& img);               // 4.07: + Add server > Server title logo (retained g/<token>)
    bool Logo(uint32_t token, std::string& img);            // 4.07: that server's title logo (PNG / JPG), false = none
    bool Details(uint32_t token, std::string& out);
    void SetHostStart(uint32_t unixStart);   // 2.27: listing "proto:icon@start:language"   // 2.25: language shown in everyone's server list        // 2.24: 0 = default icon, 1..250 = icon_N.png (sent with the listing)

    bool IsRelayIp(const char* ip);    // 127.77.x.y
    // 3.11: the 24/7 server behind this relay address answers over direct UDP (ipNet, measured rtt)
    bool DirectAddr(const char* relayIp, uint16_t port, uint32_t* ipNet, int* rttMs);
    void DirectFailed(const char* relayIp);   // a direct join / session failed: relay for 10 min
    // Must be called before MPClient::Connect(relayIp, port, ..., localPort): the client then
    // sends to 127.0.0.1:localPort (the proxy) while the game logic still sees relayIp:port.
    bool OpenProxy(const char* relayIp, uint16_t port, uint16_t* localPort);
    // Lines for the menu: broker in use, servers seen, result of every broker tried.
    void GetDiagnostics(std::vector<std::string>& out);
    void CloseProxy();

    // 2.20 regional brokers (own HiveMQ per region; list from broker.txt [Region] blocks or the
    // website's regions.txt). Without them everything works as before on the public brokers.
    void SetRegionPrefs(int menuRegion, bool autoPick);   // call often; autoPick = lowest ping
    bool HasRegionalBrokers();
    int  CurrentRegion();              // region of the broker in use (-1 = public / offline)
    int  RegionPingMs(int region);     // -1 unknown / no broker, -2 unreachable
    const char* RegionLabel(int region);   // 4.12: "Asia - Singapore" (regions.txt label = ...)
    int  RegionSameAs(int region);     // 4.12: >= 0 = this region's broker is the SAME machine as that region's (no real VPS there)
    int  BestRegion();                 // lowest ping region (-1 = none)

    // 3.21.6: login system link to the MP Discord bot (own connection, end-to-end encrypted frames)
    void AuthLinkStart(const char* sid);   // (re)start, listen for answers to this session id
    void AuthLinkStop();
    bool AuthLinkUp();
    const char* AuthLinkBroker();
    void AuthLinkSend(const std::string& frame);
    bool AuthLinkRecv(std::string& frame);

    // 3.63: SOCIAL LINK (friends, chat, groups, party, communities - src/net/social.cpp). Its own small MQTT
    // connection to the public HiveMQ broker (everyone on the same one, whatever lobby region). Every message on it
    // is signed by the sender's key and private ones are end-to-end encrypted, so a public broker is fine.
    void SocLinkStart(const char* willTopic);   // will = empty retained message (= offline) on this topic
    void SocLinkStop();
    bool SocLinkUp();
    int  SocLinkGen();                          // +1 on every (re)connect: publish your presence again
    const char* SocLinkBroker();
    void SocLinkSub(const std::string& filter);
    void SocLinkUnsub(const std::string& filter);
    void SocLinkPub(const std::string& topic, const std::string& payload, bool retain);
    bool SocLinkRecv(std::string& topic, std::string& payload);

    // 3.61: plain HTTPS GET (TLS 1.2, system CAs, no redirects), body up to maxBytes. Blocking.
    // got (optional): bytes received so far, updated while it downloads (3.62)
    bool HttpsFetch(const std::string& url, std::string& body, int timeoutMs, std::string& why, size_t maxBytes, std::atomic<size_t>* got = nullptr);
}
