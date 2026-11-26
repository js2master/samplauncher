// Known-answer test for the Android omp-cef protocol module.
// Runs on a real Android device (see run_kat.sh); vectors are generated on the
// host by gen_vectors.py from libsodium (PyNaCl) and an independent Python
// implementation of the omp-cef wire format.
//
// Build (host, arm64):
//   clang++ --target=aarch64-linux-android24 --sysroot=$NDK/... \
//     -std=c++20 -O2 cef_kat_main.cpp \
//     ../../app/src/main/cpp/SAMPMOBILE/Cef/CefProtocol.cpp \
//     ../../app/src/main/cpp/SAMPMOBILE/Cef/CefCrypto.cpp \
//     ../../app/src/main/cpp/SAMPMOBILE/Cef/thirdparty/{tweetnacl,ikcp,aes,blake2b-ref}.c

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "cef_kat_vectors.h"
#include "../../app/src/main/cpp/SAMPMOBILE/Cef/CefProtocol.h"
#include "../../app/src/main/cpp/SAMPMOBILE/Cef/CefCrypto.h"
#include "../../app/src/main/cpp/SAMPMOBILE/Cef/CefJson.h"
#include "../../app/src/main/cpp/SAMPMOBILE/Cef/CefPak.h"

using namespace cefproto;

namespace
{

int g_failures = 0;

std::string ToHex(const std::vector<uint8_t>& data)
{
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (uint8_t b : data)
    {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0xF]);
    }
    return out;
}

std::vector<uint8_t> FromHex(const char* hex)
{
    std::vector<uint8_t> out;
    const size_t len = std::strlen(hex);
    for (size_t i = 0; i + 1 < len; i += 2)
    {
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        out.push_back(static_cast<uint8_t>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
    }
    return out;
}

bool Check(const char* name, bool ok, const std::string& detail = {})
{
    if (ok)
    {
        std::printf("PASS %s\n", name);
    }
    else
    {
        ++g_failures;
        std::printf("FAIL %s%s%s\n", name, detail.empty() ? "" : " : ", detail.c_str());
    }
    return ok;
}

void CheckHex(const char* name, const std::vector<uint8_t>& actual, const char* expectedHex)
{
    const std::string actualHex = ToHex(actual);
    if (actualHex == expectedHex)
    {
        Check(name, true);
    }
    else
    {
        Check(name, false, "got " + actualHex + " want " + std::string(expectedHex));
    }
}

// Builds the packet that corresponds to each vector name.
bool BuildVectorPacket(const std::string& name, NetworkPacket& pkt)
{
    if (name == "RequestJoin")
    {
        pkt.type = PacketType::RequestJoin;
        pkt.requestJoin.playerid = 7;
        pkt.requestJoin.client_version = PLUGIN_VERSION_U32;
        return true;
    }
    if (name == "HandshakeFinalize")
    {
        pkt.type = PacketType::HandshakeFinalize;
        pkt.handshakeFinalize.cookie.assign(32, 0xAA);
        pkt.handshakeFinalize.client_public_key.assign(32, 0x11);
        return true;
    }
    if (name == "JoinResponse")
    {
        pkt.type = PacketType::JoinResponse;
        pkt.joinResponse.accepted = true;
        pkt.joinResponse.kcp_conv_id = 0x11223344;
        pkt.joinResponse.manifest_json = "{\"files\":[]}";
        pkt.joinResponse.reject_reason = 0;
        pkt.joinResponse.server_version = PLUGIN_VERSION_U32;
        pkt.joinResponse.client_version = PLUGIN_VERSION_U32;
        pkt.joinResponse.message.clear();
        return true;
    }
    if (name == "ServerConfig")
    {
        pkt.type = PacketType::ServerConfig;
        pkt.serverConfig.master_resource_key.assign(16, 0x22);
        pkt.serverConfig.resources_loader_ui = true;
        return true;
    }
    if (name == "FileData")
    {
        pkt.type = PacketType::FileData;
        pkt.fileData.resourceName = "main";
        pkt.fileData.relativePath = "index.html";
        pkt.fileData.fileHash = "deadbeef";
        pkt.fileData.chunkIndex = 0;
        pkt.fileData.totalChunks = 2;
        const char* html = "<html></html>";
        pkt.fileData.data.assign(html, html + std::strlen(html));
        return true;
    }
    if (name == "EmitEvent_CreateBrowser")
    {
        pkt.type = PacketType::EmitEvent;
        pkt.emitEvent.browserId = 3;
        pkt.emitEvent.name = "CreateBrowser";
        pkt.emitEvent.args.push_back(Argument::FromInt(3));
        pkt.emitEvent.args.push_back(Argument::FromString("http://cef/main/index.html"));
        pkt.emitEvent.args.push_back(Argument::FromBool(true));
        pkt.emitEvent.args.push_back(Argument::FromBool(false));
        return true;
    }
    if (name == "ClientEmitEvent_Mixed")
    {
        pkt.type = PacketType::ClientEmitEvent;
        pkt.emitEvent.browserId = 5;
        pkt.emitEvent.name = "myEvent";
        pkt.emitEvent.args.push_back(Argument::FromInt(-5));
        pkt.emitEvent.args.push_back(Argument::FromFloat(1.5f));
        pkt.emitEvent.args.push_back(Argument::FromBool(true));
        pkt.emitEvent.args.push_back(Argument::FromString("x"));
        return true;
    }
    if (name == "DownloadComplete")
    {
        pkt.type = PacketType::DownloadComplete;
        return true;
    }
    if (name == "RequestFiles")
    {
        pkt.type = PacketType::RequestFiles;
        pkt.requestFiles.files.emplace_back("main", "index.html");
        pkt.requestFiles.files.emplace_back("main", "css/app.css");
        return true;
    }
    return false;
}

void RunPacketVectors()
{
    for (short i = 0; i < kCefKatVectorCount; ++i)
    {
        const CefKatVector& v = kCefKatVectors[i];

        NetworkPacket pkt;
        if (!BuildVectorPacket(v.name, pkt))
        {
            Check(v.name, false, "no builder for vector");
            continue;
        }

        std::vector<uint8_t> serialized;
        if (!SerializePacket(pkt, serialized))
        {
            Check(v.name, false, "serialize failed");
            continue;
        }
        CheckHex(v.name, serialized, v.hex);

        // Round trip: parse the reference bytes back and compare field-by-field
        // by re-serializing.
        const std::vector<uint8_t> reference = FromHex(v.hex);
        NetworkPacket parsed;
        if (!DeserializePacket(reference.data(), reference.size(), parsed))
        {
            Check((std::string(v.name) + " parse").c_str(), false, "deserialize failed");
            continue;
        }
        std::vector<uint8_t> reserialized;
        SerializePacket(parsed, reserialized);
        CheckHex((std::string(v.name) + " reserialize").c_str(), reserialized, v.hex);
    }
}

void RunCryptoVectors()
{
    cefcrypto::KeyPair client{};
    const std::vector<uint8_t> clientSk = FromHex(kClientSk);
    std::memcpy(client.secretKey, clientSk.data(), 32);

    cefcrypto::DerivePublicKey(client.secretKey, client.publicKey);
    CheckHex("X25519 client public key",
             std::vector<uint8_t>(client.publicKey, client.publicKey + 32), kClientPk);

    cefcrypto::KeyPair server{};
    const std::vector<uint8_t> serverSk = FromHex(kServerSk);
    std::memcpy(server.secretKey, serverSk.data(), 32);
    cefcrypto::DerivePublicKey(server.secretKey, server.publicKey);
    CheckHex("X25519 server public key",
             std::vector<uint8_t>(server.publicKey, server.publicKey + 32), kServerPk);

    cefcrypto::SessionKeys keys{};
    if (!cefcrypto::GenerateClientSessionKeys(client, server.publicKey, keys))
    {
        Check("client session keys", false, "derivation failed");
    }
    else
    {
        CheckHex("session rx (BLAKE2b KDF)", std::vector<uint8_t>(keys.rx, keys.rx + 32), kSessionRx);
        CheckHex("session tx (BLAKE2b KDF)", std::vector<uint8_t>(keys.tx, keys.tx + 32), kSessionTx);
    }

    // secretbox: decrypt a libsodium-produced nonce||mac||ciphertext blob.
    const std::vector<uint8_t> boxKey = FromHex(kBoxKey);
    const std::vector<uint8_t> boxNonce = FromHex(kBoxNonce);
    const std::vector<uint8_t> boxMsg = FromHex(kBoxMsg);
    const std::vector<uint8_t> boxCt = FromHex(kBoxCt);

    std::vector<uint8_t> blob(boxNonce);
    blob.insert(blob.end(), boxCt.begin(), boxCt.end());

    const std::vector<uint8_t> decrypted =
        cefcrypto::DecryptPacket(blob.data(), blob.size(), boxKey.data());
    CheckHex("secretbox open (libsodium ct)", decrypted, kBoxMsg);

    // Tamper detection: flipping one ciphertext byte must fail.
    std::vector<uint8_t> tampered = blob;
    tampered[tampered.size() - 1] ^= 0x01;
    const std::vector<uint8_t> rejected =
        cefcrypto::DecryptPacket(tampered.data(), tampered.size(), boxKey.data());
    Check("secretbox rejects tampered ct", rejected.empty());

    // Encrypt with our implementation and decrypt with our implementation.
    const std::vector<uint8_t> encrypted =
        cefcrypto::EncryptPacket(boxMsg.data(), boxMsg.size(), boxKey.data());
    const std::vector<uint8_t> roundTrip =
        cefcrypto::DecryptPacket(encrypted.data(), encrypted.size(), boxKey.data());
    CheckHex("secretbox roundtrip", roundTrip, kBoxMsg);
    Check("secretbox wire shape (24+16+len)",
          encrypted.size() == 24 + 16 + boxMsg.size());

    // Empty message edge case (server can send empty payloads).
    const std::vector<uint8_t> emptyCt = cefcrypto::EncryptPacket(nullptr, 0, boxKey.data());
    const std::vector<uint8_t> emptyOut =
        cefcrypto::DecryptPacket(emptyCt.data(), emptyCt.size(), boxKey.data());
    Check("secretbox empty payload", emptyOut.empty());
}

void RunResourceEntryVectors()
{
    // Unencrypted OCEF2 entry: magic "OCEF2" + flags 0 + payload.
    std::vector<uint8_t> entry = {'O', 'C', 'E', 'F', '2', 0};
    const char* payload = "{\"ok\":true}";
    entry.insert(entry.end(), payload, payload + std::strlen(payload));

    std::vector<uint8_t> out;
    const bool ok = cefcrypto::DecodeResourceEntry(entry, {}, out);
    Check("resource entry plain", ok && out.size() == std::strlen(payload) &&
          std::memcmp(out.data(), payload, out.size()) == 0);

    // Encrypted flag set but no key -> must fail, not crash.
    std::vector<uint8_t> encryptedEntry = {'O', 'C', 'E', 'F', '2', 1};
    encryptedEntry.insert(encryptedEntry.end(), 48, 0x5A);
    out.clear();
    const bool failed = !cefcrypto::DecodeResourceEntry(encryptedEntry, {}, out);
    Check("resource entry encrypted w/o key rejected", failed);

    // sha256 sanity: empty string hash.
    const std::string hash = cefcrypto::Sha256Hex(nullptr, 0);
    Check("sha256 of empty", hash ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

void RunPakVectors()
{
    // Stage 5: a real .pak (zip with deflate + stored entries, each wrapped in
    // an OCEF2 header) must parse into the same files the server packed.
    const std::vector<uint8_t> pak = FromHex(kCefPak);
    std::vector<cefpak::Entry> entries;
    std::string error;
    if (!Check("pak parses", cefpak::ReadArchive(pak, entries, &error), error))
        return;

    Check("pak entry count", entries.size() == 3, std::to_string(entries.size()));

    struct Expect
    {
        const char* name;
        const char* hex; // expected decoded payload
    };
    const Expect expected[] = {
        {"index.html", kCefPakHtml},
        {"stored.txt", kCefPakStored},
        {"sub/app.js", kCefPakScript},
    };

    for (const auto& want : expected)
    {
        const cefpak::Entry* found = nullptr;
        for (const auto& entry : entries)
        {
            if (entry.name == want.name)
            {
                found = &entry;
                break;
            }
        }
        const std::string label = std::string("pak entry ") + want.name;
        if (!Check(label.c_str(), found != nullptr))
            continue;

        std::vector<uint8_t> decoded;
        const bool ok = cefcrypto::DecodeResourceEntry(found->data, {}, decoded);
        Check((label + " decodes").c_str(), ok && ToHex(decoded) == want.hex);
    }

    // Zip-slip guard: "../evil.html" must be refused, not written outside root.
    std::vector<cefpak::Entry> evil;
    std::string evilError;
    Check("pak rejects ../ entry", !cefpak::ReadArchive(FromHex(kCefPakEvil), evil, &evilError),
          evilError);

    Check("pak entry name validation",
          cefpak::IsSafeEntryName("a/b.js") &&
          !cefpak::IsSafeEntryName("/abs.js") &&
          !cefpak::IsSafeEntryName("../x.js") &&
          !cefpak::IsSafeEntryName("a//b.js") &&
          !cefpak::IsSafeEntryName("a\\b.js"));
}

void RunJsonVectors()
{
    // Stage 4 argument channel: JSON.stringify([...]) from the page must be
    // decoded back to the same wire types renderer/main.cpp would produce.
    cefjson::Value v;
    std::string err;
    const bool ok = cefjson::Parse(
        " [1, 1.5, \"a\\\"b\\u4e2d\", true, null] ", v, &err);
    Check("json array parses", ok, err);
    Check("json array size", v.IsArray() && v.array.size() == 5);
    if (!v.IsArray() || v.array.size() != 5)
        return;

    Check("json 1 -> Int", v.array[0].type == cefjson::Value::Type::Int && v.array[0].intValue == 1);
    Check("json 1.5 -> Double",
          v.array[1].type == cefjson::Value::Type::Double && v.array[1].doubleValue == 1.5);
    // "a\"b中" decoded: quote, b, U+4E2D as 3 utf-8 bytes.
    const std::string want = std::string("a\"b") + "\xE4\xB8\xAD";
    Check("json string escapes", v.array[2].IsString() && v.array[2].stringValue == want,
          v.array[2].stringValue);
    Check("json true -> Bool", v.array[3].IsBool() && v.array[3].boolValue);
    Check("json null", v.array[4].IsNull());

    // Object member lookup (manifest shape, stage 5).
    cefjson::Value obj;
    Check("json object parses",
          cefjson::Parse("{\"files\":[{\"name\":\"ui\",\"size\":12}],\"v\":1}", obj, &err), err);
    const cefjson::Value* files = obj.Find("files");
    Check("json object member", files && files->IsArray() && files->array.size() == 1);
    if (files && files->array.size() == 1)
    {
        const cefjson::Value* name = files->array[0].Find("name");
        Check("json nested string", name && name->AsString() == "ui");
        const cefjson::Value* size = files->array[0].Find("size");
        Check("json nested int", size && size->AsInt(-1) == 12);
    }

    // Writer: Escape() must produce something Parse() reads back unchanged.
    const std::string raw = "line1\n\"q\"\\ \xE4\xB8\xAD \x01";
    const std::string quoted = cefjson::Escape(raw);
    cefjson::Value back;
    Check("json escape roundtrip",
          cefjson::Parse(quoted, back, &err) && back.IsString() && back.stringValue == raw,
          quoted + " / " + err);

    // Rejections: malformed input must fail, never crash or silently truncate.
    cefjson::Value bad;
    Check("json rejects empty", !cefjson::Parse("", bad));
    Check("json rejects trailing data", !cefjson::Parse("{}x", bad));
    Check("json rejects bad object", !cefjson::Parse("{\"a\":}", bad));
    Check("json rejects unterminated", !cefjson::Parse("\"abc", bad));
    std::string deep(200, '[');
    Check("json rejects deep nesting", !cefjson::Parse(deep, bad));
}

} // namespace

int main()
{
    std::printf("cef protocol KAT (%d packet vectors)\n", (int)kCefKatVectorCount);
    RunPacketVectors();
    RunCryptoVectors();
    RunResourceEntryVectors();
    RunPakVectors();
    RunJsonVectors();

    std::printf("%s (%d failures)\n", g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
