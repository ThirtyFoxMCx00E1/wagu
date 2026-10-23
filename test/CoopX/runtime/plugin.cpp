#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "../transport/coop_client.h"
#include "../transport/embedded_server.h"
#include "../world/game_hooks.h"
#include "../diagnostics/weapon_log.h"
#include <isautils.h>
#include "../ui/coop_ui.h"
#include "../platform/psdk_extern.h"

MYMODCFG(net.coopfusion32.mobile, CoopFusion32, 1.0.0, CoopFusion)
NEEDGAME(com.rockstargames.gtasa)
BEGIN_DEPLIST()
    ADD_DEPENDENCY_VER(net.rusjj.gtasa.utils, 1.6)
END_DEPLIST()

uintptr_t g_pGTASA = 0;
char g_szConnectionStatus[64]{0};
char g_szCurHost[64]{0};
char g_szCurPort[64]{0};
void (*StopExtraColor)(bool);
bool g_bDisplayConnectionStatus = true;


std::atomic<bool> g_running{false};
CoopClient g_client;
EmbeddedServer g_embeddedServer;
std::thread g_networkThread;


ISAUtils* sautils = nullptr;
eTypeOfSettings g_eCoopFusionTab;
#define sizeofA(__aVar) ((int)(sizeof(__aVar) / sizeof(__aVar[0])))

const char* pYesNo[] = {
    "FEM_OFF",
    "FEM_ON",
};

const char* pNameColors[] = {
    "White",
    "Yellow",
    "Green",
    "Cyan",
    "Blue",
    "Purple",
    "Orange",
    "Red",
};

void OnDisplayConnectChanged(int oldValue, int newValue, void* userData) {
    g_bDisplayConnectionStatus = static_cast<bool>(newValue);
}


ConfigEntry* g_pCfgHost;
ConfigEntry* g_pCfgPort;
ConfigEntry* g_pCfgName;
ConfigEntry* g_pCfgNameTags;
ConfigEntry* g_pCfgNameTagSize;
ConfigEntry* g_pCfgNameColor;
ConfigEntry* g_pCfgHostServer;
ConfigEntry* g_pCfgHealthBars;

void EstablishConnection(uintptr_t);
static void StopNetworkThread();

void OnNameTagSizeChanged(int, int newValue, void*) {
    const int sizePercent = std::max(50, std::min(200, newValue));

    if (g_pCfgNameTagSize) {
        g_pCfgNameTagSize->SetInt(sizePercent);
    }

    SetRemoteNameTagScale(static_cast<float>(sizePercent) / 100.0f);
}

const char* DrawNameTagSize(int newValue, void*) {
    static char label[16];
    snprintf(label, sizeof(label), "%d%%", newValue);
    return label;
}

void OnNameColorChanged(int, int newValue, void*) {
    const int colorIndex = std::max(0, std::min(7, newValue));

    if (g_pCfgNameColor) {
        g_pCfgNameColor->SetInt(colorIndex);
    }

    g_client.SetLocalColorIndex(static_cast<uint8_t>(colorIndex));
}

void StopHostedServer() {
    if (g_running) {
        StopNetworkThread();
        snprintf(g_szConnectionStatus, sizeof(g_szConnectionStatus),
                 "Status: Not connected");
    }

    g_embeddedServer.Stop();
}

void OnHostServerChanged(int, int newValue, void*) {
    const bool enableHosting = newValue != 0;

    if (g_pCfgHostServer) {
        g_pCfgHostServer->SetBool(enableHosting);
    }

    if (!enableHosting) {
        StopHostedServer();
        return;
    }

    const uint16_t configuredPort = static_cast<uint16_t>(
        std::max(1, std::min(65535, g_pCfgPort->GetInt())));
    uint16_t activePort = configuredPort;



    for (int candidatePort = configuredPort;
         candidatePort <= std::min(65535, static_cast<int>(configuredPort) + 10);
         ++candidatePort) {
        if (g_embeddedServer.Start(static_cast<uint16_t>(candidatePort))) {
            activePort = static_cast<uint16_t>(candidatePort);
            break;
        }
    }

    if (!g_embeddedServer.IsRunning()) {
        logger->Print(
            LogP_Error,
            "Could not start the in-game LAN server near port %u; stop the old server or choose another port",
            configuredPort);

        if (g_pCfgHostServer) {
            g_pCfgHostServer->SetBool(false);
        }

        snprintf(g_szConnectionStatus, sizeof(g_szConnectionStatus),
                 "Host failed near %u", configuredPort);
        return;
    }

    if (activePort != configuredPort) {
        logger->Print(LogP_Warn,
                      "Port %u is busy; in-game LAN host moved to port %u",
                      configuredPort, activePort);

        if (g_pCfgPort) {
            g_pCfgPort->SetInt(activePort);
        }

        snprintf(g_szCurPort, sizeof(g_szCurPort), "Server Port: %u", activePort);
    }

    g_client.Configure("127.0.0.1", activePort, g_pCfgName->GetString());
    EstablishConnection(0);

    if (g_running) {
        snprintf(g_szConnectionStatus, sizeof(g_szConnectionStatus),
                 "Hosting LAN on %u", activePort);
    } else {
        snprintf(g_szConnectionStatus, sizeof(g_szConnectionStatus),
                 "Host started on %u; client failed", activePort);
    }
}

static void NetworkLoop() {
    while (g_running.load()) {
        g_client.Pump();
        g_client.PumpGameTime();

        if (!g_client.IsConnected()) {
            g_running = false;
            snprintf(g_szConnectionStatus, sizeof(g_szConnectionStatus),
                     "Status: Disconnected");
            break;
        }


        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
}

static void StopNetworkThread() {
    g_running = false;



    g_client.ShutdownSocket();

    if (g_networkThread.joinable()) {
        g_networkThread.join();
    }

    g_client.CloseSocket();
}

void EstablishConnection(uintptr_t) {
    if (g_running) {
        StopNetworkThread();
        snprintf(g_szConnectionStatus, sizeof(g_szConnectionStatus),
                 "Status: Not connected");
        return;
    }

    if (g_client.Connect()) {
        g_running = true;

        if (g_networkThread.joinable()) {
            g_networkThread.join();
        }

        g_networkThread = std::thread(NetworkLoop);
        snprintf(g_szConnectionStatus, sizeof(g_szConnectionStatus),
                 "Status: Connected");
    } else {
        logger->Print(LogP_Warn,
                      "Co-op server unavailable; waiting for reconnect");
        snprintf(g_szConnectionStatus, sizeof(g_szConnectionStatus),
                 "Status: Connect failed");
    }
}

static void UiToggleConnection() {
    EstablishConnection(0);
}

static void UiSetHosting(bool enabled) {
    OnHostServerChanged(0, enabled ? 1 : 0, NULL);
}

static bool UiIsHosting() {
    return g_pCfgHostServer && g_pCfgHostServer->GetBool();
}

static bool UiNameTagsVisible() {
    return !g_pCfgNameTags || g_pCfgNameTags->GetBool();
}

static bool UiHealthBarsVisible() {
    return !g_pCfgHealthBars || g_pCfgHealthBars->GetBool();
}

static void UiSetNameTags(bool enabled) {
    if (g_pCfgNameTags) {
        g_pCfgNameTags->SetBool(enabled);
    }

    SetRemoteNameTagsVisible(enabled);
}

static void UiSetHealthBars(bool enabled) {
    if (g_pCfgHealthBars) {
        g_pCfgHealthBars->SetBool(enabled);
    }

    SetRemoteHealthBarsVisible(enabled);
}

static void UiSetConnectionSettings(const char* host, int port,
                                    const char* nickname) {
    if (g_pCfgHost && host && *host) {
        g_pCfgHost->SetString(host);
    }

    if (g_pCfgPort && port > 0 && port <= 65535) {
        g_pCfgPort->SetInt(port);
    }

    if (g_pCfgName && nickname && *nickname) {
        g_pCfgName->SetString(nickname);
    }

    g_client.Configure(
        g_pCfgHost ? g_pCfgHost->GetString() : (host ? host : "127.0.0.1"),
        g_pCfgPort ? g_pCfgPort->GetInt() : port,
        g_pCfgName ? g_pCfgName->GetString() : (nickname ? nickname : "CJ"));

    snprintf(g_szCurHost, sizeof(g_szCurHost), "Server Host: %s",
             g_pCfgHost ? g_pCfgHost->GetString() : "127.0.0.1");
    snprintf(g_szCurPort, sizeof(g_szCurPort), "Server Port: %d",
             g_pCfgPort ? g_pCfgPort->GetInt() : port);
}


void AddCoopFusionTab() {
    g_eCoopFusionTab = sautils->AddSettingsTab("CoopFusion32");

    sautils->AddClickableItem(
        g_eCoopFusionTab, "Show Connection Status", g_bDisplayConnectionStatus,
        0, sizeofA(pYesNo) - 1, pYesNo, OnDisplayConnectChanged, NULL);
    sautils->AddClickableItem(
        g_eCoopFusionTab, "Show Player Name Tags", g_pCfgNameTags->GetBool(),
        0, sizeofA(pYesNo) - 1, pYesNo,
        [](int, int newValue, void*) {
            if (g_pCfgNameTags) {
                g_pCfgNameTags->SetBool(newValue != 0);
            }
            SetRemoteNameTagsVisible(newValue != 0);
        },
        NULL);
    sautils->AddClickableItem(
        g_eCoopFusionTab, "Show Health / Armour Bars",
        g_pCfgHealthBars->GetBool(), 0, sizeofA(pYesNo) - 1, pYesNo,
        [](int, int newValue, void*) { UiSetHealthBars(newValue != 0); }, NULL);
    sautils->AddSliderItem(
        g_eCoopFusionTab, "Name Tag Size", g_pCfgNameTagSize->GetInt(),
        50, 200, OnNameTagSizeChanged, DrawNameTagSize, NULL);
    sautils->AddClickableItem(
        g_eCoopFusionTab, "My Name Color", g_pCfgNameColor->GetInt(),
        0, sizeofA(pNameColors) - 1, pNameColors, OnNameColorChanged, NULL);
    sautils->AddClickableItem(
        g_eCoopFusionTab, "Host LAN Server", g_pCfgHostServer->GetBool(),
        0, sizeofA(pYesNo) - 1, pYesNo, OnHostServerChanged, NULL);
    sautils->AddButton(g_eCoopFusionTab, g_szCurHost, nullptr);
    sautils->AddButton(g_eCoopFusionTab, g_szCurPort, nullptr);
    sautils->AddButton(g_eCoopFusionTab, "Connect/Disconnect", EstablishConnection);
}

ON_MOD_LOAD() {
    logger->SetTag("CoopFusion32");
    WeaponFileLogReset();
    WeaponFileLog("MOD_LOAD diagnostic_file=/sdcard/CoopFusion32_weapon_sync.log");

    g_pCfgHost = cfg->Bind("ServerHost", "127.0.0.1");
    g_pCfgPort = cfg->Bind("ServerPort", 6767);
    g_pCfgName = cfg->Bind("Nickname", "CJ");
    g_pCfgNameTags = cfg->Bind("ShowNameTags", true);
    g_pCfgNameTagSize = cfg->Bind("NameTagSizePercent", 50);
    g_pCfgNameColor = cfg->Bind("NameColor", 0);
    g_pCfgHostServer = cfg->Bind("HostLANServer", false);
    g_pCfgHealthBars = cfg->Bind("ShowHealthBars", true);

    snprintf(g_szConnectionStatus, sizeof(g_szConnectionStatus),
             "Status: Not connected");
    snprintf(g_szCurHost, sizeof(g_szCurHost), "Server Host: %s",
             g_pCfgHost->GetString());
    snprintf(g_szCurPort, sizeof(g_szCurPort), "Server Port: %d",
             g_pCfgPort->GetInt());

    sautils = (ISAUtils*)(GetInterface("SAUtils"));
    if (sautils) {
        AddCoopFusionTab();
    } else {
        logger->Error(
            "SAUtils interface is unavailable; NPC spawning disabled safely");
    }

    g_pGTASA = aml->GetLib("libGTASA.so");
    if (!g_pGTASA) {
        logger->Error("libGTASA.so is not loaded; aborting native hooks");
        return;
    }

    g_client.Configure(g_pCfgHost->GetString(), g_pCfgPort->GetInt(),
                       g_pCfgName->GetString());
    InstallGameHooks(g_pGTASA, g_client, sautils);
    SetRemoteNameTagsVisible(g_pCfgNameTags->GetBool());
    SetRemoteHealthBarsVisible(g_pCfgHealthBars->GetBool());
    OnNameTagSizeChanged(50, g_pCfgNameTagSize->GetInt(), NULL);
    OnNameColorChanged(0, g_pCfgNameColor->GetInt(), NULL);

    if (g_pCfgHostServer->GetBool()) {
        OnHostServerChanged(0, 1, NULL);
    }

    CoopUI::Actions uiActions{};
    uiActions.toggleConnection = UiToggleConnection;
    uiActions.setHosting = UiSetHosting;
    uiActions.setConnectionSettings = UiSetConnectionSettings;
    uiActions.setNameTags = UiSetNameTags;
    uiActions.setHealthBars = UiSetHealthBars;
    uiActions.isHosting = UiIsHosting;
    uiActions.nameTagsVisible = UiNameTagsVisible;
    uiActions.healthBarsVisible = UiHealthBarsVisible;

    CoopUI::Install(g_pGTASA, aml->GetLibHandle("libGTASA.so"), &g_client,
                    g_pCfgHost->GetString(), g_pCfgPort->GetInt(),
                    g_pCfgName->GetString(), uiActions);


    SET_TO(StopExtraColor, g_pGTASA + 0x19E6DC);
}

ON_ALL_MODS_LOAD() {
    Events::drawHudEvent += []() {
        DrawRemoteWeaponIcons();
        CoopUI::Render();

        if (g_bDisplayConnectionStatus) {
            char statusText[32];
            if (g_running) {
                snprintf(statusText, sizeof(statusText),
                         "Co-op Server: ~g~Connected");
            } else {
                snprintf(statusText, sizeof(statusText),
                         "Co-op Server: ~r~Not connected");
            }

            CFont::SetScale(2.1f);
            CFont::SetWrapx(1000000.0f);
            CFont::SetJustify(0);
            CFont::SetEdge(1);
            CFont::SetFontStyle(FO_FONT_STYLE_STANDARD);
            CFont::SetOrientation(ALIGN_LEFT);
            CFont::SetProportional(0);
            CFont::PrintString(854.0f, 0.0f, statusText);
            CFont::RenderFontBuffer();
        }
    };

    Events::gameProcessEvent += []() {
        static bool alreadyTeleportedToSpawn = false;

        if (g_running == true && alreadyTeleportedToSpawn == false) {
            CPlayerPed* player = FindPlayerPed(-1);



            if (!player) {
                return;
            }

            CGame::currArea = 0;
            player->m_nAreaCode = 0;
            player->m_pEnex = nullptr;
            StopExtraColor(false);
            player->Teleport(CVector(2489.888672f, -1668.296997f, 15.000000f));
            alreadyTeleportedToSpawn = true;
        } else {
            if (g_running == false) {
                alreadyTeleportedToSpawn = false;
            }
        }
    };
}

ON_MOD_UNLOAD() {
    CoopUI::Shutdown();
    StopNetworkThread();
    g_embeddedServer.Stop();

    if (g_pCfgHost) delete g_pCfgHost;
    if (g_pCfgPort) delete g_pCfgPort;
    if (g_pCfgName) delete g_pCfgName;
    if (g_pCfgNameTags) delete g_pCfgNameTags;
    if (g_pCfgNameTagSize) delete g_pCfgNameTagSize;
    if (g_pCfgNameColor) delete g_pCfgNameColor;
    if (g_pCfgHostServer) delete g_pCfgHostServer;
    if (g_pCfgHealthBars) delete g_pCfgHealthBars;
}
