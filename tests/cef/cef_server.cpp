// Standalone omp-cef *server* for on-device verification of the browser layer.
//
// Runs on the phone (arm64) and listens on a fixed UDP port. The app connects to
// it through the CEF debug entry (am broadcast ... --es action connect), which
// bypasses the SA-MP session watch so a full round trip can be observed without
// a live game server.
//
// Timeline after the client joins (~55s total):
//   +0ms     ServerConfig(master key)
//   +300ms   EmitEvent(CreateBrowser, 1, http://cef/test/index.html, focused, !controlsChat)
//            -> the client must QUEUE this until the resource download finished
//   on create result:
//            EmitEvent(SetKeyCapture, true) + EmitEvent(EnableKey, VK, true) for
//            A/H/E/L/O/ENTER, then EmitBrowserEvent(test:early, "early-tag", 7)
//   +6000ms  EmitBrowserEvent(test:hello, "服务端", 42, 3.5, true)   <- four wire types
//   +9000ms  EmitBrowserEvent(test:command, "flash", 1500)
//   +12000ms EmitBrowserEvent(test:ping)                    <- the page removed its first
//                                                            handler with cef.off, so the
//                                                            ack must report 0 old hits
//   +30000ms EmitEvent(SetBrowserVisible, 1, false)         <- host script injects the
//   +35000ms EmitEvent(SetBrowserVisible, 1, true)             keys/IME/chat steps
//   +41000ms EmitEvent(DestroyBrowser, 1)                      between t=10s and t=27s
//   +55000ms summary + exit
//
// Stage 6 (input): the host script injects real Android key events
// (`input keyevent 29` = KEYCODE_A, `input keyevent 54` = KEYCODE_Z which is NOT
// enabled, `input text hello` after tapping the page input) and toggles the game
// chat keyboard through the app's own UI.showKeyboard path. The assertions below
// verify the Windows VK mapping, the EnableKey whitelist, the IME text round trip
// and the ChatInputState uplink.
//
// Resource channel (stage 5): the JoinResponse manifest announces
// {"test":[{"path":"test.pak","hash":...,"size":...}]}. The client checks its
// local cache (empty on a fresh install), sends RequestFiles, this server
// streams the real .pak in 1200-byte FileData chunks exactly like
// plugin.cpp ProcessFileTransfers, and expects a single DownloadComplete.
// The page therefore only renders if zip + inflate + OCEF2/AES-128-CBC
// decryption + on-disk extraction all worked.
//
// Every ClientEmitEvent received from the device is printed and fed into the
// assertions (probe:ready / earlyAck / ack types / commandAck / pingAck),
// so a JS<->Pawn style round trip is verified end to end on the wire.
//
// Build + run: bash tests/cef/run_browser_test.sh

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
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

#include "../../app/src/main/cpp/SAMPMOBILE/Cef/CefCrypto.h"
#include "../../app/src/main/cpp/SAMPMOBILE/Cef/CefProtocol.h"

using namespace cefproto;

namespace
{

constexpr uint16_t LISTEN_PORT = 17900;
constexpr int32_t TEST_BROWSER_ID = 1;
constexpr const char* TEST_URL = "http://cef/test/index.html";

// Stage 7: the World2D browser is created with an explicit viewport size in
// pixels; dpr on this device is 3, so the page must report width/3 x height/3.
constexpr int32_t WORLD2D_ID = 20;
constexpr int WORLD2D_W = 384;
constexpr int WORLD2D_H = 192;

// Collected from ClientEmitEvent packets; asserted in Report().
struct Stats
{
    int createResults = 0;   // BrowserCreateResult
    int probeReady = 0;      // page -> wire on load
    int earlyAck = 0;        // ack for an event sent before the page finished
    int helloAck = 0;        // ack for the four-type event
    bool helloTypesOk = false;
    int commandAck = 0;
    int pingAck = 0;
    int pingOldHits = -1;    // value reported by the page for the removed handler
    int clicks = 0;          // touch-driven, informational only

    // Resource channel (stage 5).
    int requestFiles = 0;
    bool requestedPak = false;
    int chunksSent = 0;
    int downloadComplete = 0;
    int downloadStart = 0;      // ClientEmitEvent DownloadStart
    int downloadFinish = 0;     // ClientEmitEvent DownloadFinish
    bool createResultBeforeDownload = false;

    // Input channel (stage 6).
    int pressKeyPackets = 0;
    bool pressKeyArgsOk = true;     // exactly 5 args: int,int,int,bool,bool
    int pressKeyDowns = 0;
    int pressKeyUps = 0;
    bool sawVkA = false;            // injected KEYCODE_A must map to VK 0x41
    int disabledKeyPackets = 0;     // VK 0x5A: never EnableKey'd -> must stay silent
    int chatOpen = 0;               // ChatInputState open=true
    int chatClose = 0;              // ChatInputState open=false
    int chatBrowserId = -999;       // PC uses -1
    int chatOpenAtHide = -1;        // chatOpen when SetBrowserVisible(false) was sent
    int inputEvents = 0;            // test:input from the page
    std::string lastInputText;      // last IME value seen by the page

    // Viewport probe (stage 6, IME). The page reports innerHeight on every
    // resize; if the keyboard only pans the window, innerHeight never changes
    // and the focused input stays underneath the keyboard.
    int viewportEvents = 0;
    int viewportMaxInner = 0;       // tallest innerHeight seen (no keyboard)
    int viewportMinInner = 0;       // shortest innerHeight seen (keyboard up)
    int viewportMinVv = 0;          // matching visualViewport.height
    std::string lastViewportTag;

    // Stage 7 (World2D): the small browser must load and size itself to the
    // width/height the server asked for (px), i.e. innerWidth == width / dpr.
    int world2dVpW = 0;
    int world2dVpH = 0;
    int world2dReady = 0;

    // Debug-broadcast World2D: the client reports the projected rect over the
    // wire (debug:world2d). The host script reads these to aim its tap, since
    // the client log is not reliably readable on this ROM.
    int dbgWorldReports = 0;
    int dbgWorldId = -1;
    int dbgWorldX = 0;
    int dbgWorldY = 0;
    int dbgWorldW = 0;
    int dbgWorldH = 0;
    bool dbgWorldOn = false;

    // WorldObject3D: stage 1 = material swapped, stage 2 = first page frame
    // uploaded into the RenderWare texture.
    int worldObjAttach = 0;
    int worldObjFirstFrame = 0;
    int worldObjModel = 0;
    int worldObjRemoved = 0;        // stage 6: texture + schedule released

    // Stage 8 throughput report: uploads per 3s window and the measured cost of
    // one Java-side rasterisation.
    int worldObjPerfReports = 0;
    int worldObjPerfFrames = 0;     // uploads in the last reported window
    int worldObjPerfCostMs = 0;     // draw+copy cost of the last capture
    int worldObjPerfWindowMs = 0;
    int worldObjPerfBestFrames = 0; // best window seen (page warm-up excluded by taking max)
    int worldObjPerfBestCost = 0;   // lowest cost seen once frames flow
};

uint64_t NowMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// Every log line carries the elapsed time so the ordering of the resource
// channel (RequestFiles -> FileData -> DownloadComplete -> BrowserCreateResult)
// is readable without a second clock.
uint64_t g_startMs = NowMs();

void Log(const char* fmt, ...)
{
    std::printf("[t=%6llums] ", static_cast<unsigned long long>(NowMs() - g_startMs));
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    std::printf("\n");
    std::fflush(stdout);
    va_end(ap);
}

class TestServer
{
public:
    // Loads the .pak the client will download and derive the manifest hash
    // from. Same file the server plugin would serve from scriptfiles/cef/.
    bool LoadPak(const char* path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            Log("!! cannot open pak '%s': %s", path, strerror(errno));
            return false;
        }
        pak_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (pak_.empty())
        {
            Log("!! pak '%s' is empty", path);
            return false;
        }
        pakHash_ = cefcrypto::Sha256Hex(pak_.data(), pak_.size());
        Log("pak %s: %zu bytes, sha256=%s", path, pak_.size(), pakHash_.c_str());
        return true;
    }

    // The pak is already in the client's cache (second join): the manifest
    // hash matches, so the client must skip RequestFiles and go straight to
    // DownloadComplete with the page still rendering from local files.
    void SetExpectCache(bool value) { expectCache_ = value; }

    bool Start()
    {
        fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd_ < 0) return false;

        int reuse = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(LISTEN_PORT);
        if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        {
            Log("bind 127.0.0.1:%u failed: %s", (unsigned)LISTEN_PORT, strerror(errno));
            close(fd_);
            fd_ = -1;
            return false;
        }

        timeval tv{};
        tv.tv_usec = 5 * 1000;
        setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        cefcrypto::GenerateKeyPair(keys_);
        Log("listening on 127.0.0.1:%u", (unsigned)LISTEN_PORT);
        return true;
    }

    void Run()
    {
        uint8_t buf[65536];
        int step = 0;
        uint64_t joinedAt = 0;
        // When SetBrowserVisible(false) actually went out. The later steps are
        // relative to it: the hide waits for the host script's chat open/close
        // pair, which slips a few seconds depending on page load / IME speed.
        uint64_t hideAt = 0;

        while (!quit_)
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

            PumpTransfer();

            if (joined_ && joinedAt == 0)
            {
                joinedAt = now;
                Log("client joined, running script");
            }
            if (joinedAt == 0)
                continue;

            const uint64_t t = now - joinedAt;
            if (step == 0)
            {
                SendServerConfig();
                step = 1;
            }
            else if (step == 1 && t >= 300)
            {
                SendCreateBrowser();
                step = 2;
            }
            else if (step == 2 && stats_.createResults > 0)
            {
                // CreateBrowser is now held back until the resource download
                // finished, so an event sent right after it would reach a
                // browser id that does not exist yet and be dropped (which is
                // what the PC client does too). Send it the moment the browser
                // reports success: that is still before onPageFinished, so the
                // WebView-side queue is what has to deliver it.
                //
                // Stage 6: turn on key capture and whitelist the keys the host
                // script will inject. Z (0x5A) is deliberately left out.
                SendSetKeyCapture(true);
                for (int vk : {0x41, 0x48, 0x45, 0x4C, 0x4F, 0x0D})
                    SendEnableKey(vk, true);

                SendBrowserEvent("test:early",
                                 {Argument::FromString("early-tag"), Argument::FromInt(7)});
                // Stage 7: a second, world-anchored browser. It gets its own
                // browser id and an explicit px viewport, so the page can prove
                // the client honoured the size (and later that it survived a
                // SetWorld2DBrowserPos move).
                SendCreateWorld2D();
                step = 3;
            }
            else if (step == 3 && t >= 6000)
            {
                SendBrowserEvent("test:hello",
                                 {Argument::FromString("服务端"),
                                  Argument::FromInt(42),
                                  Argument::FromFloat(3.5f),
                                  Argument::FromBool(true)});
                SendSetWorld2DPos(131.5f, -77.0f, 13.0f);
                step = 4;
            }
            else if (step == 4 && t >= 9000)
            {
                SendBrowserEvent("test:command",
                                 {Argument::FromString("flash"), Argument::FromInt(1500)});
                step = 5;
            }
            else if (step == 5 && t >= 12000)
            {
                SendBrowserEvent("test:ping", {});
                step = 6;
            }
            else if (step == 6 && t >= 30000)
            {
                // Everything the host script injects before this point must have
                // been reported; the controlsChat block test runs after it.
                //
                // The chat open/close pair is the last thing the host script does
                // (t~=28s), and how long it takes to get there depends on page
                // load + IME animation. Waiting for the uplink instead of firing
                // at a fixed 30s removes that race; the 40s deadline keeps a
                // genuinely broken client failing instead of hanging.
                const bool sawChatPair = stats_.chatOpen >= 1 && stats_.chatClose >= 1;
                if (sawChatPair || t >= 40000)
                {
                    if (!sawChatPair)
                        Log("!! chat open/close never arrived, hiding anyway at t=%llums",
                            static_cast<unsigned long long>(t));
                    stats_.chatOpenAtHide = stats_.chatOpen;
                    SendVisible(false);
                    hideAt = t;
                    step = 7;
                }
            }
            else if (step == 7 && hideAt != 0 && t >= hideAt + 5000)
            {
                SendVisible(true);
                step = 8;
            }
            else if (step == 8 && hideAt != 0 && t >= hideAt + 11000)
            {
                SendDestroy();
                SendDestroyWorld2D();
                step = 9;
            }
            else if (step == 9 && hideAt != 0 && t >= hideAt + 58000)
            {
                Report();
                step = 10;
                quit_ = true;
            }
        }
    }

private:
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
                    for (auto& m : msgs)
                        HandleKcpMessage(m);
                }
                return;
            }
        }

        NetworkPacket pkt;
        if (!DeserializePacket(data, size, pkt))
            return;

        switch (pkt.type)
        {
            case PacketType::RequestJoin:
                Log("<- RequestJoin playerid=%d version=0x%08x",
                    pkt.requestJoin.playerid, pkt.requestJoin.client_version);
                SendChallenge();
                break;

            case PacketType::HandshakeFinalize: {
                const auto& clientPk = pkt.handshakeFinalize.client_public_key;
                Log("<- HandshakeFinalize clientPk=%zu bytes", clientPk.size());
                if (clientPk.size() != 32) return;

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
                join.joinResponse.kcp_conv_id = 0xC0FFEE;
                join.joinResponse.manifest_json = BuildManifest();
                join.joinResponse.server_version = PLUGIN_VERSION_U32;
                join.joinResponse.client_version = PLUGIN_VERSION_U32;
                SendRaw(join);

                {
                    std::lock_guard<std::mutex> lock(kcpMutex_);
                    kcp_ = ikcp_create(0xC0FFEE, this);
                    kcp_->output = &TestServer::Output;
                    ikcp_nodelay(kcp_, 1, 10, 2, 1);
                    ikcp_wndsize(kcp_, 128, 128);
                }
                joined_ = true;
                Log("<- JoinResponse sent, KCP up (conv=0x%X)", (unsigned)0xC0FFEE);
                break;
            }

            default:
                Log("<- (raw) packet type %d ignored", (int)pkt.type);
                break;
        }
    }

    void HandleKcpMessage(const std::vector<uint8_t>& msg)
    {
        const std::vector<uint8_t> plain = cefcrypto::DecryptPacket(msg.data(), msg.size(), rx_);
        if (plain.empty())
        {
            Log("!! cannot decrypt KCP message (%zu bytes)", msg.size());
            return;
        }

        NetworkPacket pkt;
        if (!DeserializePacket(plain.data(), plain.size(), pkt))
        {
            Log("!! cannot deserialize decrypted packet");
            return;
        }

        if (pkt.type == PacketType::ClientEmitEvent)
        {
            const auto& ev = pkt.emitEvent;
            Log("<- ClientEmitEvent name=%s browserId=%d args=%zu",
                ev.name.c_str(), ev.browserId, ev.args.size());
            for (size_t i = 0; i < ev.args.size(); ++i)
            {
                const auto& a = ev.args[i];
                switch (a.type)
                {
                    case ArgumentType::String:
                        Log("     arg[%zu] string \"%s\"", i, a.stringValue.c_str());
                        break;
                    case ArgumentType::Integer:
                        Log("     arg[%zu] int %d", i, a.intValue);
                        break;
                    case ArgumentType::Float:
                        Log("     arg[%zu] float %f", i, (double)a.floatValue);
                        break;
                    case ArgumentType::Bool:
                        Log("     arg[%zu] bool %d", i, a.boolValue ? 1 : 0);
                        break;
                }
            }
            Record(ev);
        }
        else
        {
            switch (pkt.type)
            {
                case PacketType::RequestFiles:
                    OnRequestFiles(pkt.requestFiles);
                    break;
                case PacketType::DownloadComplete:
                    ++stats_.downloadComplete;
                    Log("<- DownloadComplete");
                    break;
                default:
                    Log("<- (kcp) packet type %d ignored", (int)pkt.type);
                    break;
            }
        }
    }

    // Stage-4 assertions, evaluated from the actual wire arguments.
    void Record(const EmitEventPacket& ev)
    {
        if (ev.name == ClientEvent::BrowserCreateResult)
        {
            ++stats_.createResults;
            // The client must not create a browser before it has the resource
            // tree (app.cpp queues CreateBrowser until DownloadComplete).
            if (stats_.downloadComplete == 0)
                stats_.createResultBeforeDownload = true;
            return;
        }

        if (ev.name == ClientEvent::DownloadStart)
        {
            ++stats_.downloadStart;
            return;
        }

        if (ev.name == ClientEvent::DownloadFinish)
        {
            ++stats_.downloadFinish;
            return;
        }

        if (ev.name == "debug:worldobj")
        {
            // [browserId, objectId|model, stage]
            if (ev.args.size() >= 3 && ev.args[2].type == ArgumentType::Integer)
            {
                const int id = ev.args[0].intValue;
                const int second = ev.args[1].intValue;
                const int stage = ev.args[2].intValue;
                if (stage == 1)
                {
                    ++stats_.worldObjAttach;
                    stats_.worldObjModel = second;
                }
                else if (stage == 2)
                {
                    ++stats_.worldObjFirstFrame;
                }
                else if (stage == 3)
                {
                    Log("INFO  debug:worldobj diagnostics: emptyFrames=%d", second);
                    return;
                }
                else if (stage == 6)
                {
                    ++stats_.worldObjRemoved;
                }
                Log("INFO  debug:worldobj id=%d object/model=%d stage=%d", id, second, stage);
            }
            return;
        }

        if (ev.name == "debug:worldobjperf")
        {
            // [browserId, framesInWindow, costMs, windowMs]
            if (ev.args.size() >= 4 &&
                ev.args[1].type == ArgumentType::Integer &&
                ev.args[2].type == ArgumentType::Integer &&
                ev.args[3].type == ArgumentType::Integer)
            {
                const int id = ev.args[0].intValue;
                const int frames = ev.args[1].intValue;
                const int cost = ev.args[2].intValue;
                const int windowMs = ev.args[3].intValue;
                ++stats_.worldObjPerfReports;
                stats_.worldObjPerfFrames = frames;
                stats_.worldObjPerfCostMs = cost;
                stats_.worldObjPerfWindowMs = windowMs;
                if (frames > stats_.worldObjPerfBestFrames)
                    stats_.worldObjPerfBestFrames = frames;
                if (frames > 0 && (stats_.worldObjPerfBestCost == 0 || cost < stats_.worldObjPerfBestCost))
                    stats_.worldObjPerfBestCost = cost;
                if (windowMs > 0)
                {
                    Log("INFO  debug:worldobjperf id=%d frames=%d/%dms (%.1f fps) rasterMs=%d",
                        id, frames, windowMs, frames * 1000.0 / windowMs, cost);
                }
            }
            return;
        }

        if (ev.name == "debug:world2d")
        {
            // [id, x, y, w, h, onScreen] - the client's own projection result,
            // sent only for World2D pages created by the debug broadcast.
            if (ev.args.size() >= 6 &&
                ev.args[0].type == ArgumentType::Integer &&
                ev.args[1].type == ArgumentType::Integer &&
                ev.args[2].type == ArgumentType::Integer &&
                ev.args[3].type == ArgumentType::Integer &&
                ev.args[4].type == ArgumentType::Integer)
            {
                const int id = ev.args[0].intValue;
                const int x = ev.args[1].intValue;
                const int y = ev.args[2].intValue;
                const int w = ev.args[3].intValue;
                const int h = ev.args[4].intValue;
                const bool on = (ev.args[5].type == ArgumentType::Bool && ev.args[5].boolValue) ||
                                (ev.args[5].type == ArgumentType::Integer && ev.args[5].intValue != 0);
                const bool changed = id != stats_.dbgWorldId || x != stats_.dbgWorldX ||
                                     y != stats_.dbgWorldY || w != stats_.dbgWorldW ||
                                     h != stats_.dbgWorldH || on != stats_.dbgWorldOn;
                stats_.dbgWorldId = id;
                stats_.dbgWorldX = x;
                stats_.dbgWorldY = y;
                stats_.dbgWorldW = w;
                stats_.dbgWorldH = h;
                stats_.dbgWorldOn = on;
                ++stats_.dbgWorldReports;
                // ~2.5Hz uplink; only log transitions so the log stays readable.
                if (changed || stats_.dbgWorldReports == 1)
                {
                    Log("INFO  debug:world2d id=%d pos=(%d,%d) size=%dx%d on=%d",
                        id, x, y, w, h, on ? 1 : 0);
                }
            }
            return;
        }

        if (ev.name == "probe:ready")
        {
            ++stats_.probeReady;
            // [title, innerWidth, innerHeight, dpr, isWebView]
            if (ev.browserId == WORLD2D_ID && ev.args.size() >= 3 &&
                ev.args[1].type == ArgumentType::Integer &&
                ev.args[2].type == ArgumentType::Integer)
            {
                ++stats_.world2dReady;
                stats_.world2dVpW = ev.args[1].intValue;
                stats_.world2dVpH = ev.args[2].intValue;
                Log("INFO  world2d viewport %dx%d css (requested %dx%d px)",
                    stats_.world2dVpW, stats_.world2dVpH, WORLD2D_W, WORLD2D_H);
            }
            return;
        }
        if (ev.name == "test:earlyAck")
        {
            ++stats_.earlyAck;
            return;
        }
        if (ev.name == "test:ack")
        {
            ++stats_.helloAck;
            // expected: ["ack:服务端🚀", 43, 7.5, false]
            // The rocket is U+1F680 (4-byte UTF-8): inside the JVM it is a
            // surrogate pair, so this also proves the CESU-8 -> UTF-8 fixup.
            // The float is deliberately fractional: JS integral numbers (7.0)
            // become Integer on the wire, exactly like CefV8Value::IsInt().
            if (ev.args.size() == 4 &&
                ev.args[0].type == ArgumentType::String &&
                ev.args[0].stringValue == "ack:服务端🚀" &&
                ev.args[1].type == ArgumentType::Integer &&
                ev.args[1].intValue == 43 &&
                ev.args[2].type == ArgumentType::Float &&
                ev.args[2].floatValue > 7.49f && ev.args[2].floatValue < 7.51f &&
                ev.args[3].type == ArgumentType::Bool &&
                ev.args[3].boolValue == false)
            {
                stats_.helloTypesOk = true;
            }
            return;
        }
        if (ev.name == "test:commandAck")
        {
            ++stats_.commandAck;
            return;
        }
        if (ev.name == "test:pingAck")
        {
            ++stats_.pingAck;
            if (!ev.args.empty() && ev.args[0].type == ArgumentType::Integer)
                stats_.pingOldHits = ev.args[0].intValue;
            return;
        }
        if (ev.name == "test:click")
        {
            ++stats_.clicks;
            return;
        }

        // ---- stage 6: input channel -------------------------------------
        if (ev.name == ClientEvent::PressKey)
        {
            ++stats_.pressKeyPackets;
            // PC: [vk:int, scancode:int, modifiers:int, down:bool, repeat:bool]
            if (ev.args.size() != 5 ||
                ev.args[0].type != ArgumentType::Integer ||
                ev.args[1].type != ArgumentType::Integer ||
                ev.args[2].type != ArgumentType::Integer ||
                ev.args[3].type != ArgumentType::Bool ||
                ev.args[4].type != ArgumentType::Bool)
            {
                stats_.pressKeyArgsOk = false;
            }
            if (ev.args.size() >= 4)
            {
                const int vk = ev.args[0].intValue;
                if (ev.args[3].boolValue)
                    ++stats_.pressKeyDowns;
                else
                    ++stats_.pressKeyUps;
                if (vk == 0x41) stats_.sawVkA = true;
                if (vk == 0x5A) ++stats_.disabledKeyPackets;
            }
            return;
        }

        if (ev.name == ClientEvent::ChatInputState)
        {
            stats_.chatBrowserId = ev.browserId;
            if (!ev.args.empty() && ev.args[0].type == ArgumentType::Bool && ev.args[0].boolValue)
                ++stats_.chatOpen;
            else
                ++stats_.chatClose;
            return;
        }

        if (ev.name == "test:input")
        {
            ++stats_.inputEvents;
            if (!ev.args.empty() && ev.args[0].type == ArgumentType::String)
                stats_.lastInputText = ev.args[0].stringValue;
            return;
        }

        // Stage 6 viewport probe: [tag:string, innerHeight:int, visualViewport:int, screen:int]
        if (ev.name == "test:viewport")
        {
            if (ev.args.size() >= 4 &&
                ev.args[0].type == ArgumentType::String &&
                ev.args[1].type == ArgumentType::Integer &&
                ev.args[2].type == ArgumentType::Integer)
            {
                const int inner = ev.args[1].intValue;
                const int vv = ev.args[2].intValue;
                stats_.lastViewportTag = ev.args[0].stringValue;
                ++stats_.viewportEvents;
                if (inner > stats_.viewportMaxInner) stats_.viewportMaxInner = inner;
                if (stats_.viewportMinInner == 0 || inner < stats_.viewportMinInner)
                    stats_.viewportMinInner = inner;
                if (vv > 0 && (stats_.viewportMinVv == 0 || vv < stats_.viewportMinVv))
                    stats_.viewportMinVv = vv;
                Log("INFO  viewport tag=%s innerHeight=%d visualViewport=%d screen=%d",
                    stats_.lastViewportTag.c_str(), inner, vv, ev.args[3].intValue);
            }
            return;
        }
    }

    bool Report()
    {
        auto check = [](bool ok, const char* what) {
            Log("%s  %s", ok ? "PASS" : "FAIL", what);
            return ok;
        };

        bool ok = true;
        ok = check(stats_.createResults > 0, "BrowserCreateResult received");
        ok = check(stats_.probeReady > 0, "page sent probe:ready (JS -> JNI -> wire)");
        ok = check(stats_.earlyAck > 0, "event sent before page load was queued and delivered");
        ok = check(stats_.helloAck > 0, "test:hello acked by the page");
        ok = check(stats_.helloTypesOk, "ack args kept string/int/float/bool types");
        ok = check(stats_.commandAck > 0, "test:command acked by the page");
        ok = check(stats_.pingAck > 0 && stats_.pingOldHits == 0,
                   "cef.off removed the first handler (0 stale hits)");

        // Stage 5: the page above only renders if the .pak download, SHA-256
        // check, zip inflate and OCEF2/AES decryption all succeeded.
        if (expectCache_)
        {
            ok = check(stats_.requestFiles == 0, "cache hit: client requested nothing");
            ok = check(stats_.chunksSent == 0, "cache hit: no FileData streamed");
        }
        else
        {
            ok = check(stats_.requestFiles > 0, "client sent RequestFiles");
            ok = check(stats_.requestedPak, "client requested test/test.pak");
            ok = check(stats_.chunksSent == (int)transfer_.totalChunks,
                       "all FileData chunks streamed (no gaps)");
        }
        ok = check(stats_.downloadComplete == 1, "client sent exactly one DownloadComplete");
        if (!expectCache_)
        {
            ok = check(stats_.downloadStart == 1, "client emitted DownloadStart once");
            ok = check(stats_.downloadFinish == 1, "client emitted DownloadFinish once");
        }
        ok = check(!stats_.createResultBeforeDownload,
                   "CreateBrowser was queued until resources were ready");

        // Stage 6: input. The host script drives these with real `input keyevent`
        // / `input text` / the app's own keyboard toggle, so a PASS here means the
        // whole Android key -> VK -> ClientEmitEvent path really worked.
        ok = check(stats_.pressKeyPackets >= 2,
                   "PressKey forwarded (SetKeyCapture + EnableKey honoured)");
        ok = check(stats_.pressKeyArgsOk,
                   "PressKey carried [vk,scancode,modifiers,down,repeat] with PC types");
        ok = check(stats_.sawVkA, "injected KEYCODE_A arrived as Windows VK 0x41");
        ok = check(stats_.pressKeyDowns > 0 && stats_.pressKeyUps > 0,
                   "PressKey has both down and up transitions");
        ok = check(stats_.disabledKeyPackets == 0,
                   "key without EnableKey was not forwarded (VK 0x5A)");
        ok = check(stats_.chatOpenAtHide == 1 && stats_.chatClose >= 1,
                   "ChatInputState reported open and close before the browser hid");
        ok = check(stats_.chatOpen == 1,
                   "controlsChat page never reopened the game chat (ShouldBlockChat)");
        ok = check(stats_.chatBrowserId == -1,
                   "ChatInputState used browserId -1 like the PC client");
        ok = check(stats_.lastInputText.find("hello") != std::string::npos,
                   "IME text reached the page and came back (test:input)");
        ok = check(stats_.viewportEvents >= 2,
                   "page reported its viewport (test:viewport on resize)");
        ok = check(stats_.worldObjAttach > 0,
                   "WorldObject3D page swapped onto the object material");
        ok = check(stats_.worldObjFirstFrame > 0,
                   "WebView frame reached the RenderWare texture (first upload)");

        // Stage 8: the capture pipeline reports its own throughput over the wire.
        // The old schedule was a fixed 250 ms (12 uploads per 3 s window, and the
        // in-flight guard made the real rate lower); the floor below only passes
        // if the adaptive schedule actually beats that ceiling.
        ok = check(stats_.worldObjPerfReports > 0,
                   "WorldObject3D reported capture throughput (debug:worldobjperf)");
        ok = check(stats_.worldObjPerfBestFrames >= 18,
                   "WorldObject3D sustained >6 fps (previous fixed ceiling was 4 Hz)");
        ok = check(stats_.worldObjPerfBestCost > 0 && stats_.worldObjPerfBestCost <= 200,
                   "one 512x256 page rasterisation measured under 200 ms");
        // The debug destroy only reaches the Java browser. Without the
        // ForgetWorldObject round trip the entry (texture + 10Hz capture attempts
        // that can never succeed) stays behind forever.
        ok = check(stats_.worldObjRemoved > 0,
                   "WorldObject3D entry released after the browser was destroyed");

        ok = check(stats_.viewportMinInner > 0 &&
                   stats_.viewportMaxInner - stats_.viewportMinInner > 150,
                   "IME shrunk the page viewport instead of only panning the window");

        // Stage 7: the World2D browser loads in a server-sized viewport. dpr on
        // this device is 3, so 384x192 CSS-pixel-wise must come back as 128x64.
        ok = check(stats_.world2dReady > 0,
                   "World2D browser loaded (probe:ready from its own browser id)");
        ok = check(std::abs(stats_.world2dVpW - WORLD2D_W / 3) <= 2 &&
                   std::abs(stats_.world2dVpH - WORLD2D_H / 3) <= 2,
                   "World2D viewport honours the requested px size (384x192 -> 128x64 css)");

        Log("INFO  input events=%d lastInput=\"%s\"", stats_.inputEvents,
            stats_.lastInputText.c_str());
        Log("INFO  viewport innerHeight %d -> %d (visualViewport min %d, %d samples)",
            stats_.viewportMaxInner, stats_.viewportMinInner,
            stats_.viewportMinVv, stats_.viewportEvents);
        Log("INFO  touch-driven test:click events: %d (manual, not required)", stats_.clicks);
        Log("=== %s", ok ? "STAGE 4+5+6 ROUND TRIP OK" : "STAGE 4+5+6 FAILED");
        return ok;
    }

    void SendServerConfig()
    {
        NetworkPacket pkt;
        pkt.type = PacketType::ServerConfig;
        // Same default the server plugin ships (cef.master_resource_key), so a
        // pak built by tests/cef/make_pak.py decodes without extra flags.
        const char* key = "ThisIsA16ByteKey";
        pkt.serverConfig.master_resource_key.assign(key, key + 16);
        pkt.serverConfig.resources_loader_ui = true;
        SendKcp(pkt);
        Log("-> ServerConfig (master key %zu bytes)", pkt.serverConfig.master_resource_key.size());
    }

    void SendCreateBrowser()
    {
        NetworkPacket pkt;
        pkt.type = PacketType::EmitEvent;
        pkt.emitEvent.browserId = TEST_BROWSER_ID;
        pkt.emitEvent.name = ServerEvent::CreateBrowser;
        pkt.emitEvent.args.push_back(Argument::FromInt(TEST_BROWSER_ID));
        pkt.emitEvent.args.push_back(Argument::FromString(TEST_URL));
        pkt.emitEvent.args.push_back(Argument::FromBool(true));   // focused
        pkt.emitEvent.args.push_back(Argument::FromBool(false));  // controls_chat
        SendKcp(pkt);
        Log("-> CreateBrowser id=%d url=%s", TEST_BROWSER_ID, TEST_URL);
    }

    void SendVisible(bool visible)
    {
        NetworkPacket pkt;
        pkt.type = PacketType::EmitEvent;
        pkt.emitEvent.browserId = TEST_BROWSER_ID;
        pkt.emitEvent.name = ServerEvent::SetBrowserVisible;
        pkt.emitEvent.args.push_back(Argument::FromInt(TEST_BROWSER_ID));
        pkt.emitEvent.args.push_back(Argument::FromBool(visible));
        SendKcp(pkt);
        Log("-> SetBrowserVisible %d", visible ? 1 : 0);
    }

    // Stage 7: CEF_CreateWorld2DBrowser (api.cpp CreateWorld2DBrowser).
    // Args are exactly app.cpp's: [id, url, wx, wy, wz, width, height, offsetZ,
    // pivotX, pivotY]. width/height are the browser *viewport* size in pixels
    // (manager.cpp clamps them to 1..1024), not a world size.
    void SendCreateWorld2D()
    {
        NetworkPacket pkt;
        pkt.type = PacketType::EmitEvent;
        pkt.emitEvent.browserId = WORLD2D_ID;
        pkt.emitEvent.name = ServerEvent::CreateWorld2DBrowser;
        pkt.emitEvent.args.push_back(Argument::FromInt(WORLD2D_ID));
        pkt.emitEvent.args.push_back(Argument::FromString(TEST_URL));
        pkt.emitEvent.args.push_back(Argument::FromFloat(120.0f));  // world x
        pkt.emitEvent.args.push_back(Argument::FromFloat(-80.0f));  // world y
        pkt.emitEvent.args.push_back(Argument::FromFloat(12.5f));   // world z
        pkt.emitEvent.args.push_back(Argument::FromFloat((float)WORLD2D_W));
        pkt.emitEvent.args.push_back(Argument::FromFloat((float)WORLD2D_H));
        pkt.emitEvent.args.push_back(Argument::FromFloat(1.0f));    // offsetZ
        pkt.emitEvent.args.push_back(Argument::FromFloat(0.5f));    // pivotX
        pkt.emitEvent.args.push_back(Argument::FromFloat(1.0f));    // pivotY
        SendKcp(pkt);
        Log("-> CreateWorld2DBrowser id=%d size=%dx%d world=(120,-80,12.5)",
            WORLD2D_ID, WORLD2D_W, WORLD2D_H);
    }

    void SendSetWorld2DPos(float x, float y, float z)
    {
        NetworkPacket pkt;
        pkt.type = PacketType::EmitEvent;
        pkt.emitEvent.browserId = WORLD2D_ID;
        pkt.emitEvent.name = ServerEvent::SetWorld2DBrowserPos;
        pkt.emitEvent.args.push_back(Argument::FromInt(WORLD2D_ID));
        pkt.emitEvent.args.push_back(Argument::FromFloat(x));
        pkt.emitEvent.args.push_back(Argument::FromFloat(y));
        pkt.emitEvent.args.push_back(Argument::FromFloat(z));
        SendKcp(pkt);
        Log("-> SetWorld2DBrowserPos id=%d (%.1f,%.1f,%.1f)", WORLD2D_ID, x, y, z);
    }

    void SendDestroyWorld2D()
    {
        NetworkPacket pkt;
        pkt.type = PacketType::EmitEvent;
        pkt.emitEvent.browserId = WORLD2D_ID;
        pkt.emitEvent.name = ServerEvent::DestroyBrowser;
        pkt.emitEvent.args.push_back(Argument::FromInt(WORLD2D_ID));
        SendKcp(pkt);
        Log("-> DestroyBrowser id=%d (world2d)", WORLD2D_ID);
    }

    void SendBrowserEvent(const char* name, std::initializer_list<Argument> args)
    {
        NetworkPacket pkt;
        pkt.type = PacketType::EmitBrowserEvent;
        pkt.emitEvent.browserId = TEST_BROWSER_ID;
        pkt.emitEvent.name = name;
        pkt.emitEvent.args.assign(args.begin(), args.end());
        SendKcp(pkt);
        Log("-> EmitBrowserEvent %s (%zu args)", name, pkt.emitEvent.args.size());
    }

    // Stage 6: CEF_SetKeyCapture / CEF_EnableKey (api.cpp SetKeyCapture/EnableKey).
    // Neither carries a browser id that the client uses, but the PC API sets it to
    // the browser being configured; the client reads only the args.
    void SendSetKeyCapture(bool enabled)
    {
        NetworkPacket pkt;
        pkt.type = PacketType::EmitEvent;
        pkt.emitEvent.name = ServerEvent::SetKeyCapture;
        pkt.emitEvent.args.push_back(Argument::FromBool(enabled));
        SendKcp(pkt);
        Log("-> SetKeyCapture %d", enabled ? 1 : 0);
    }

    void SendEnableKey(int vk, bool enabled)
    {
        NetworkPacket pkt;
        pkt.type = PacketType::EmitEvent;
        pkt.emitEvent.name = ServerEvent::EnableKey;
        pkt.emitEvent.args.push_back(Argument::FromInt(vk));
        pkt.emitEvent.args.push_back(Argument::FromBool(enabled));
        SendKcp(pkt);
        Log("-> EnableKey vk=%d (0x%X) %d", vk, (unsigned)vk, enabled ? 1 : 0);
    }

    void SendDestroy()    {        NetworkPacket pkt;
        pkt.type = PacketType::EmitEvent;
        pkt.emitEvent.browserId = TEST_BROWSER_ID;
        pkt.emitEvent.name = ServerEvent::DestroyBrowser;
        pkt.emitEvent.args.push_back(Argument::FromInt(TEST_BROWSER_ID));
        SendKcp(pkt);
        Log("-> DestroyBrowser %d", TEST_BROWSER_ID);
    }

    // ------------------------------------------------------------------
    // Resource channel (stage 5) - mirrors plugin.cpp ProcessFileTransfers:
    // FILE_CHUNK_SIZE bytes per FileData packet, resourceName/relativePath
    // taken from the manifest entry.
    // ------------------------------------------------------------------
    static constexpr size_t FILE_CHUNK_SIZE = 1200;
    static constexpr int MAX_CHUNKS_PER_TICK = 16;
    // Hold the stream back for a moment so the CreateBrowser gate is provable:
    // without the pending-create queue the browser would be created at ~300ms
    // with no resource on disk (503 page); with it, the create result can only
    // arrive after the download finished.
    static constexpr uint64_t TRANSFER_START_DELAY_MS = 2500;

    std::string BuildManifest() const
    {
        if (pak_.empty())
            return "{}";

        char buffer[512];
        std::snprintf(buffer, sizeof(buffer),
                      "{\"test\":[{\"path\":\"test.pak\",\"hash\":\"%s\",\"size\":%llu}]}",
                      pakHash_.c_str(), static_cast<unsigned long long>(pak_.size()));
        return buffer;
    }

    void OnRequestFiles(const RequestFilesPacket& request)
    {
        ++stats_.requestFiles;
        for (const auto& [resource, path] : request.files)
        {
            Log("<- RequestFiles %s/%s", resource.c_str(), path.c_str());
            if (resource == "test" && path == "test.pak" && !pak_.empty())
            {
                stats_.requestedPak = true;
                transfer_.active = true;
                transfer_.nextChunk = 0;
                transfer_.startAtMs = NowMs() + TRANSFER_START_DELAY_MS;
                transfer_.totalChunks = static_cast<uint32_t>(
                    (pak_.size() + FILE_CHUNK_SIZE - 1) / FILE_CHUNK_SIZE);
                Log("-> request accepted, transfer starts in %llums (%u chunks of %zu bytes)",
                    static_cast<unsigned long long>(TRANSFER_START_DELAY_MS),
                    transfer_.totalChunks, FILE_CHUNK_SIZE);
            }
        }
    }

    void PumpTransfer()
    {
        if (!transfer_.active)
            return;
        if (NowMs() < transfer_.startAtMs)
            return;

        for (int i = 0; i < MAX_CHUNKS_PER_TICK && transfer_.nextChunk < transfer_.totalChunks; ++i)
        {
            const size_t offset = static_cast<size_t>(transfer_.nextChunk) * FILE_CHUNK_SIZE;
            const size_t size = std::min(FILE_CHUNK_SIZE, pak_.size() - offset);

            FileDataPacket data;
            data.resourceName = "test";
            data.relativePath = "test.pak";
            data.fileHash = pakHash_;
            data.chunkIndex = transfer_.nextChunk;
            data.totalChunks = transfer_.totalChunks;
            data.data.assign(pak_.begin() + static_cast<std::ptrdiff_t>(offset),
                             pak_.begin() + static_cast<std::ptrdiff_t>(offset + size));

            NetworkPacket pkt;
            pkt.type = PacketType::FileData;
            pkt.fileData = data;
            SendKcp(pkt);

            ++transfer_.nextChunk;
            ++stats_.chunksSent;
        }

        if (transfer_.nextChunk >= transfer_.totalChunks)
        {
            transfer_.active = false;
            Log("-> FileData transfer complete (%d chunks, %zu bytes)",
                stats_.chunksSent, pak_.size());
        }
    }

    void SendChallenge()
    {
        NetworkPacket challenge;
        challenge.type = PacketType::HandshakeChallenge;
        challenge.handshakeChallenge.cookie.assign(24, 0xC0);
        challenge.handshakeChallenge.server_public_key.assign(
            keys_.publicKey, keys_.publicKey + 32);
        SendRaw(challenge);
        Log("-> HandshakeChallenge");
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
        auto* self = static_cast<TestServer*>(user);
        sendto(self->fd_, buf, (size_t)len, 0,
               reinterpret_cast<sockaddr*>(&self->clientAddr_), sizeof(self->clientAddr_));
        return 0;
    }

private:
    int fd_ = -1;
    cefcrypto::KeyPair keys_{};
    uint8_t rx_[32]{};
    uint8_t tx_[32]{};
    sockaddr_in clientAddr_{};
    std::mutex kcpMutex_;
    ikcpcb* kcp_ = nullptr;
    std::atomic<bool> joined_{false};
    bool quit_ = false;
    bool expectCache_ = false;
    Stats stats_{};

    // Resource channel state.
    std::vector<uint8_t> pak_;
    std::string pakHash_;
    struct
    {
        bool active = false;
        uint32_t nextChunk = 0;
        uint32_t totalChunks = 0;
        uint64_t startAtMs = 0;
    } transfer_{};
};

} // namespace

int main(int argc, char** argv)
{
    Log("omp-cef test server (browser + resource channel verification)");

    TestServer server;
    if (argc > 1)
    {
        if (!server.LoadPak(argv[1]))
            return 1;
        for (int i = 2; i < argc; ++i)
        {
            if (std::strcmp(argv[i], "--cached") == 0)
                server.SetExpectCache(true);
        }
    }
    else
    {
        Log("!! no .pak argument: the client will download nothing (manifest {})");
    }

    if (!server.Start())
        return 1;

    server.Run();

    Log("exit");
    return 0;
}
