// 3.21.6: login system - the phone side (no game / UI code: the screens are in mp_auth.inc).
//
//  server sends PKT_AUTH challenge -> Begin()
//    no account on this phone yet  -> CODE:   "type /gen XXXXXXXX in #verification" (new random code)
//        Discord /gen matches      -> SIGNUP: user, password, gender, age, [Create]
//        (Discord already has one) -> LOGIN
//    account on this phone         -> LOGIN:  user, password, [Login]  ("new account" -> CODE)
//    logged in this game session   -> a ticket straight from the session (no screen)
//  ticket -> PKT_AUTH to the server -> server says OK -> DONE (then < Spawn >)
#pragma once
#include <stdint.h>
#include <string>
#include <map>
#include "authcrypto.h"

class AuthClient
{
public:
    enum State { IDLE, LINKING, CODE, SIGNUP, LOGIN, BUSY, TICKET, WAIT_SERVER, DONE, FAILED };

    void Begin(const char chalHex[33], const char* serverName, bool accountOnPhone, const char* lastUser);
    void Tick(uint64_t nowMs);          // call every frame while a login screen is up
    void Stop();                        // left the server
    void NewCode();                     // a new random code (the old one stops working)
    void Signup(const char* user, const char* pass, const char* gender, int age);
    void Login(const char* user, const char* pass);
    void WantNewAccount();              // LOGIN -> CODE
    void WantLogin();                   // CODE -> LOGIN
    // the server's answer to our ticket
    void ServerResult(bool ok, const char* msg);

    // a ticket is ready for the server (once)
    bool TakeTicket(std::string& payload, uint8_t sig[64]);

    void RequestStats();                // /stats: answer in Stats()
    bool LoggedIn() const { return !m_Sess.empty(); }
    void ForgetSession() { m_Sess.clear(); }

    State       state = IDLE;
    char        code[MP_AUTH_CODE_LEN + 1] = "";
    uint64_t    codeAt = 0;             // when this code was made (it works 10 min)
    std::string err, info;              // red error / green info line
    std::string discordName, user, prefillUser;
    bool        createdNow = false;     // DONE after a sign up ("account created")
    std::map<std::string, std::string> stats;   // op=stats answer (empty = none yet)
    bool        statsPending = false;
    uint64_t    stateAt = 0;

private:
    void Send(const std::map<std::string, std::string>& kv);
    void OnAnswer(std::map<std::string, std::string>& a);
    void Set(State s);
    AuthCrypto::Session m_S;
    std::string m_Chal, m_Server, m_Sess, m_VToken, m_Ticket;
    uint8_t     m_Sig[64] = { 0 };
    bool        m_bTicket = false;
    int         m_nRid = 0;
    std::string m_GenRid;               // rid of the last gen request
    uint64_t    m_nLastHello = 0, m_nBusySince = 0;
    bool        m_bLinked = false, m_bAccountHint = false;
    State       m_AfterLink = LOGIN;
};
