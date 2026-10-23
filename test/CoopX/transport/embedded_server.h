#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

class EmbeddedServer {
public:
    EmbeddedServer();
    ~EmbeddedServer();
    bool Start(uint16_t port);
    void Stop();
    bool IsRunning() const { return m_running.load(); }
    uint16_t Port() const { return m_port; }

private:
    void Run();
    void CloseClients();
    int m_listenSocket = -1;
    uint16_t m_port = 0;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};
