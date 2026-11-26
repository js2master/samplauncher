// End-to-end probe for the Android omp-cef transport.
//
// Runs a minimal omp-cef *server* on 127.0.0.1 (ephemeral UDP port) inside the
// same process and drives the real CefNetwork client against it over actual
// sockets: RequestJoin -> HandshakeChallenge -> HandshakeFinalize ->
// JoinResponse -> KCP + secretbox -> ServerConfig -> EmitEvent(CreateBrowser)
// -> ClientEmitEvent(BrowserCreateResult).
//
// Purpose: prove the client half (CefNetwork + CefCrypto + CefProtocol)
// interoperates with a server that follows the omp-cef server plugin's wire
// behaviour, without needing the Windows plugin or a game session.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

extern "C" {
#include "../../app/src/main/cpp/SAMPMOBILE/Cef/thirdparty/ikcp.h"
#include "../../app/src/main/cpp/SAMPMOBILE/Cef/thirdparty/blake2.h"
#include "../../app/src/main/cpp/SAMPMOBILE/Cef/thirdparty/tweetnacl.h"
}

#include "../../app/src/main/cpp/SAMPMOBILE/Cef/CefNetwork.h"
#include "../../app/src/main/cpp/SAMPMOBILE/Cef/CefProtocol.h"

using namespace cefproto;

namespace
{

std::atomic<bool> g_done{false};
std::atomic<int> g_failures{0};

uint64_t NowMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void Check(const char* name, bool ok, const std::string& detail = {})
{
    std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name,
                detail.empty() ? "" : " : ", detail.c_str());
    if (!ok)
        ++g_failures;
}

// Minimal omp-cef server: plaintext handshake, then KCP with secretbox.
class MiniServer
{
public:
    MiniServer()
    {
        fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

        sockaddr_in bound{};
        socklen_t len = sizeof(bound);
        getsockname(fd_, reinterpret_cast<sockaddr*>(&bound), &len);
        port_ = ntohs(bound.sin_port);

        timeval tv{};
        tv.tv_usec = 10 * 1000;
        setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        cefcrypto::GenerateKeyPair(keys_);
    }

    ~MiniServer()
    {
        Stop();
        if (fd_ >= 0) close(fd_);
    }

    void Start() { thread_ = std::thread([this] { Loop(); }); }

    void Stop()
    {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

    uint16_t Port() const { return port_; }
    const uint8_t* PublicKey() const { return keys_.publicKey; }

private:
    void Loop()
    {
        running_ = true;
        uint8_t buf[65536];
        uint64_t nextSend = 0;

        while (running_)
        {
            const uint64_t now = NowMs();
            {
                std::lock_guard<std::mutex> lock(kcpMutex_);
                if (kcp_) ikcp_update(kcp_, static_cast<IUINT32>(now));
            }

            sockaddr_in from{};
            socklen_t fromLen = sizeof(from);
            const ssize_t n = recvfrom(fd_, buf, sizeof(buf), 0,
                                       reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (n > 0)
            {
                clientAddr_ = from;
                Handle(buf, static_cast<size_t>(n));
            }

            if (connected_ && nextSend == 0)
                nextSend = now + 150; // let the client's KCP settle first

            if (connected_ && nextSend != 0 && now >= nextSend)
            {
                nextSend = 0;
                SendServerConfig();
                SendCreateBrowser();
            }
        }
    }

    void Handle(const uint8_t* data, size_t size)
    {
        {
            std::lock_guard<std::mutex> lock(kcpMutex_);
            if (kcp_)
            {
                if (ikcp_input(kcp_, reinterpret_cast<const char*>(data), (long)size) >= 0)
                {
                    char out[65536];
                    int len;
                    std::vector<std::vector<uint8_t>> msgs;
                    while ((len = ikcp_recv(kcp_, out, sizeof(out))) > 0)
                        msgs.emplace_back(out, out + len);
                    // Decrypt outside the lock below.
                    for (auto& m : msgs)
                        DecryptAndHandle(m);
                }
                return;
            }
        }

        NetworkPacket pkt;
        if (!DeserializePacket(data, size, pkt))
            return;

        if (pkt.type == PacketType::RequestJoin)
        {
            Check("server got RequestJoin(playerid, version)",
                  pkt.requestJoin.playerid == 42 &&
                  pkt.requestJoin.client_version == PLUGIN_VERSION_U32,
                  "playerid=" + std::to_string(pkt.requestJoin.playerid));

            NetworkPacket challenge;
            challenge.type = PacketType::HandshakeChallenge;
            challenge.handshakeChallenge.cookie.assign(24, 0xC0);
            challenge.handshakeChallenge.server_public_key.assign(
                keys_.publicKey, keys_.publicKey + 32);
            SendRaw(challenge);
        }
        else if (pkt.type == PacketType::HandshakeFinalize)
        {
            const auto& clientPk = pkt.handshakeFinalize.client_public_key;
            Check("server got HandshakeFinalize with matching cookie",
                  pkt.handshakeFinalize.cookie == std::vector<uint8_t>(24, 0xC0));
            Check("client public key is 32 bytes", clientPk.size() == 32);

            if (clientPk.size() != 32)
                return;

            // Server-side crypto_kx_session_keys: rx = keys[32..63], tx = keys[0..31]
            uint8_t q[32];
            crypto_scalarmult(q, keys_.secretKey, clientPk.data());
            blake2b_state st;
            uint8_t keys[64];
            blake2b_init(&st, sizeof(keys));
            blake2b_update(&st, q, sizeof(q));
            blake2b_update(&st, clientPk.data(), 32);
            blake2b_update(&st, keys_.publicKey, 32);
            blake2b_final(&st, keys, sizeof(keys));
            std::memcpy(tx_, keys, 32);
            std::memcpy(rx_, keys + 32, 32);

            NetworkPacket join;
            join.type = PacketType::JoinResponse;
            join.joinResponse.accepted = true;
            join.joinResponse.kcp_conv_id = 0x1234;
            join.joinResponse.manifest_json = "{\"files\":[]}";
            join.joinResponse.reject_reason = 0;
            join.joinResponse.server_version = PLUGIN_VERSION_U32;
            join.joinResponse.client_version = PLUGIN_VERSION_U32;
            SendRaw(join);

            std::lock_guard<std::mutex> lock(kcpMutex_);
            kcp_ = ikcp_create(0x1234, this);
            kcp_->output = &MiniServer::Output;
            ikcp_nodelay(kcp_, 1, 10, 2, 1);
            ikcp_wndsize(kcp_, 128, 128);
            connected_ = true;
        }
    }

    void DecryptAndHandle(const std::vector<uint8_t>& msg)
    {
        const std::vector<uint8_t> plain = cefcrypto::DecryptPacket(msg.data(), msg.size(), rx_);
        if (plain.empty())
        {
            Check("server decrypts KCP message", false);
            return;
        }

        NetworkPacket pkt;
        if (!DeserializePacket(plain.data(), plain.size(), pkt))
        {
            Check("server deserializes KCP message", false);
            return;
        }

        if (pkt.type == PacketType::ClientEmitEvent)
        {
            Check("server got ClientEmitEvent BrowserCreateResult",
                  pkt.emitEvent.name == ClientEvent::BrowserCreateResult &&
                  pkt.emitEvent.browserId == 7 && pkt.emitEvent.args.size() == 3);
            if (pkt.emitEvent.args.size() == 3)
            {
                Check("BrowserCreateResult args(browser id, success, code, reason)",
                      pkt.emitEvent.args[0].boolValue &&
                      pkt.emitEvent.args[1].intValue == 0 &&
                      pkt.emitEvent.args[2].stringValue == "ok");
            }
            g_done = true;
        }
        else if (pkt.type == PacketType::RequestFiles)
        {
            Check("server got RequestFiles", pkt.requestFiles.files.size() >= 1);
        }
    }

    void SendServerConfig()
    {
        NetworkPacket pkt;
        pkt.type = PacketType::ServerConfig;
        pkt.serverConfig.master_resource_key.assign(16, 0x5A);
        pkt.serverConfig.resources_loader_ui = true;
        SendKcp(pkt);
    }

    void SendCreateBrowser()
    {
        // Exactly what the server plugin sends for CEF_CreateBrowser(id, url).
        NetworkPacket pkt;
        pkt.type = PacketType::EmitEvent;
        pkt.emitEvent.browserId = 7;
        pkt.emitEvent.name = ServerEvent::CreateBrowser;
        pkt.emitEvent.args.push_back(Argument::FromInt(7));
        pkt.emitEvent.args.push_back(Argument::FromString("http://cef/main/index.html"));
        pkt.emitEvent.args.push_back(Argument::FromBool(true));
        pkt.emitEvent.args.push_back(Argument::FromBool(false));
        SendKcp(pkt);
    }

    void SendKcp(const NetworkPacket& pkt)
    {
        std::vector<uint8_t> raw;
        SerializePacket(pkt, raw);
        const std::vector<uint8_t> enc = cefcrypto::EncryptPacket(raw.data(), raw.size(), tx_);
        std::lock_guard<std::mutex> lock(kcpMutex_);
        if (!kcp_) return;
        ikcp_send(kcp_, reinterpret_cast<const char*>(enc.data()), (int)enc.size());
        ikcp_flush(kcp_);
    }

    void SendRaw(const NetworkPacket& pkt)
    {
        std::vector<uint8_t> raw;
        SerializePacket(pkt, raw);
        sendto(fd_, raw.data(), raw.size(), 0,
               reinterpret_cast<sockaddr*>(&clientAddr_), sizeof(clientAddr_));
    }

    static int Output(const char* buf, int len, ikcpcb*, void* user)
    {
        auto* self = static_cast<MiniServer*>(user);
        sendto(self->fd_, buf, (size_t)len, 0,
               reinterpret_cast<sockaddr*>(&self->clientAddr_), sizeof(self->clientAddr_));
        return 0;
    }

    int fd_ = -1;
    uint16_t port_ = 0;
    std::thread thread_;
    std::atomic<bool> running_{false};
    cefcrypto::KeyPair keys_{};
    uint8_t rx_[32]{};
    uint8_t tx_[32]{};
    sockaddr_in clientAddr_{};
    std::mutex kcpMutex_;
    ikcpcb* kcp_ = nullptr;
    bool connected_ = false;
};

} // namespace

int main()
{
    std::printf("omp-cef transport E2E probe\n");

    MiniServer server;
    server.Start();
    std::printf("mini server on 127.0.0.1:%u\n", (unsigned)server.Port());

    CefNetwork client;
    std::atomic<bool> gotJoin{false}, gotConfig{false}, gotCreate{false};

    client.SetSessionHandler([](bool active) {
        // Initialize() fires `false` (reset), JoinResponse fires `true`,
        // same order as the PC client's FireSessionActive calls.
        Check(active ? "session active fired on join" : "session reset fired on init", true);
    });

    client.SetPacketHandler([&](const NetworkPacket& pkt) {
        switch (pkt.type)
        {
            case PacketType::JoinResponse:
                Check("client JoinResponse accepted + conv 0x1234",
                      pkt.joinResponse.accepted && pkt.joinResponse.kcp_conv_id == 0x1234);
                Check("client manifest delivered",
                      pkt.joinResponse.manifest_json == "{\"files\":[]}");
                gotJoin = true;
                break;

            case PacketType::ServerConfig:
                Check("client ServerConfig master key via KCP",
                      pkt.serverConfig.master_resource_key == std::vector<uint8_t>(16, 0x5A));
                gotConfig = true;
                // Ask for a file list like the PC client does after manifest diff.
                client.SendRequestFiles({{"main", "index.html"}});
                break;

            case PacketType::EmitEvent:
                if (pkt.emitEvent.name == ServerEvent::CreateBrowser)
                {
                    Check("client CreateBrowser args",
                          pkt.emitEvent.browserId == 7 &&
                          pkt.emitEvent.args.size() == 4 &&
                          pkt.emitEvent.args[0].intValue == 7 &&
                          pkt.emitEvent.args[1].stringValue == "http://cef/main/index.html" &&
                          pkt.emitEvent.args[2].boolValue == true &&
                          pkt.emitEvent.args[3].boolValue == false);
                    gotCreate = true;
                    // Answer like the browser layer will once stage 3 lands.
                    client.SendBrowserCreateResult(7, true, 0, "ok");
                }
                break;

            default:
                break;
        }
    });

    if (!client.Initialize("127.0.0.1", server.Port()))
    {
        Check("client Initialize", false);
        return 1;
    }
    client.Connect(42);

    const uint64_t deadline = NowMs() + 8000;
    while (NowMs() < deadline && !g_done)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

    Check("client received JoinResponse", gotJoin);
    Check("client received ServerConfig", gotConfig);
    Check("client received CreateBrowser", gotCreate);
    Check("full round trip completed", g_done);

    client.Disconnect();
    server.Stop();

    std::printf("%s (%d failures)\n", g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures.load());
    return g_failures == 0 ? 0 : 1;
}
