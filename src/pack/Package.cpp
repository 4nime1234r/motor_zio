#include "zio/pack/Package.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <set>

#include "internal/Json.h"
#include "pack/Lz.h"
#include "zio/core/Platform.h"

extern "C"
{
#include "monocypher.h"
}

#if defined(ZIO_PLATFORM_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <bcrypt.h>
#else
#include <sys/random.h>
#endif

namespace fs = std::filesystem;

namespace zio::pack
{

namespace
{

constexpr std::uint8_t kMagic[8]        = {'Z', 'I', 'O', 'P', 'K', 'G', 0, 1};
constexpr std::uint32_t kVersion        = 2;
constexpr std::uint32_t kChunkSize      = 1u << 20;
constexpr std::uint32_t kMaxMeta        = 1u << 20;
constexpr std::uint64_t kMaxPlain       = 64ull << 30;
constexpr std::uint32_t kMaxArgonKiB    = 1u << 20;
constexpr std::uint32_t kMaxArgonPasses = 32;
constexpr std::size_t kMaxEntries       = 200000;
constexpr std::size_t kMaxPathLength    = 240;

void fail(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
}

void put16(std::vector<std::uint8_t>& out, std::uint16_t v)
{
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

void put64(std::vector<std::uint8_t>& out, std::uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

std::uint32_t get32(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t get64(const std::uint8_t* p)
{
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i)
        v = (v << 8) | p[i];
    return v;
}

std::string infoToJson(const PackageInfo& info)
{
    std::string out = "{\n  \"format\": \"zio.package\",\n  \"version\": 1";
    const auto text = [&](const char* key, const std::string& value) {
        out += ",\n  \"";
        out += key;
        out += "\": ";
        json::writeString(out, value);
    };
    text("name", info.name);
    text("author", info.author);
    text("gameVersion", info.version);
    text("description", info.description);
    text("copyright", info.copyright);
    text("website", info.website);
    text("license", info.license);
    text("id", info.id);
    text("engineVersion", info.engineVersion);
    text("entryScene", info.entryScene);
    text("executable", info.executable);
    out += ",\n  \"created\": ";
    json::writeNumber(out, static_cast<double>(info.createdUnix));
    out += ",\n  \"allowImport\": ";
    out += info.allowImport ? "true" : "false";
    out += "\n}\n";
    return out;
}

bool infoFromJson(std::string_view text, PackageInfo& info)
{
    json::Value doc;
    std::string error;
    if (!json::Parser(text).parse(doc, error) || !std::holds_alternative<json::Object>(doc.v))
        return false;
    const auto str = [&](const char* key, std::string& target) {
        if (const json::Value* value = doc.find(key))
            if (const auto* s = std::get_if<std::string>(&value->v))
                target = *s;
    };
    str("name", info.name);
    str("author", info.author);
    str("gameVersion", info.version);
    str("description", info.description);
    str("copyright", info.copyright);
    str("website", info.website);
    str("license", info.license);
    str("id", info.id);
    str("engineVersion", info.engineVersion);
    str("entryScene", info.entryScene);
    str("executable", info.executable);
    if (const json::Value* value = doc.find("created"))
        if (const auto* number = std::get_if<double>(&value->v); number && *number >= 0)
            info.createdUnix = static_cast<std::uint64_t>(*number);
    if (const json::Value* value = doc.find("allowImport"))
        if (const auto* flag = std::get_if<bool>(&value->v))
            info.allowImport = *flag;
    return true;
}

struct RawHeader
{
    std::uint32_t version = kVersion;
    std::vector<std::uint8_t> bytes;
    PackageHeader header;
    std::array<std::uint8_t, 32> salt{};
    std::array<std::uint8_t, 16> noncePrefix{};
    std::uint32_t chunkSize = kChunkSize;
};

std::vector<std::uint8_t> buildHeader(const PackageInfo& info, const WriteOptions& options, const std::array<std::uint8_t, 32>& salt,
                                      const std::array<std::uint8_t, 16>& noncePrefix, std::uint64_t plainSize)
{
    const std::string meta = infoToJson(info);
    std::vector<std::uint8_t> out(kMagic, kMagic + 8);
    put32(out, kVersion);
    out.push_back(static_cast<std::uint8_t>(options.protection));
    out.push_back(options.signer ? 1 : 0);
    put16(out, 0);
    put32(out, options.protection == Protection::Password ? options.argonMemoryKiB : 0);
    put32(out, options.protection == Protection::Password ? options.argonPasses : 0);
    out.insert(out.end(), salt.begin(), salt.end());
    out.insert(out.end(), noncePrefix.begin(), noncePrefix.end());
    put32(out, static_cast<std::uint32_t>(meta.size()));
    out.insert(out.end(), meta.begin(), meta.end());
    if (options.signer)
        out.insert(out.end(), options.signer->publicKey.begin(), options.signer->publicKey.end());
    put32(out, kChunkSize);
    put64(out, plainSize);
    return out;
}

bool readExact(std::istream& in, void* data, std::size_t size)
{
    in.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
    return static_cast<std::size_t>(in.gcount()) == size;
}

bool parseHeader(std::istream& in, RawHeader& raw, std::string* error)
{
    std::vector<std::uint8_t> bytes;
    const auto take = [&](std::size_t count, std::uint8_t* target) {
        if (!readExact(in, target, count))
            return false;
        bytes.insert(bytes.end(), target, target + count);
        return true;
    };

    std::uint8_t fixed[8 + 4 + 1 + 1 + 2 + 4 + 4 + 32 + 16 + 4];
    if (!take(sizeof(fixed), fixed))
    {
        fail(error, "El archivo no es un paquete de Zio (demasiado corto).");
        return false;
    }
    if (std::memcmp(fixed, kMagic, 7) != 0)
    {
        fail(error, "El archivo no es un paquete de Zio.");
        return false;
    }
    const std::uint32_t version = get32(fixed + 8);
    raw.version                 = version;
    if (fixed[7] != kMagic[7] || version < 1 || version > kVersion)
    {
        fail(error, "El paquete es de una versión de Zio no compatible.");
        return false;
    }
    const std::uint8_t kdf = fixed[12];
    if (kdf > 2)
    {
        fail(error, "El paquete usa una protección desconocida.");
        return false;
    }
    raw.header.protection     = static_cast<Protection>(kdf);
    raw.header.signedPackage  = (fixed[13] & 1) != 0;
    raw.header.argonMemoryKiB = get32(fixed + 16);
    raw.header.argonPasses    = get32(fixed + 20);
    std::memcpy(raw.salt.data(), fixed + 24, 32);
    std::memcpy(raw.noncePrefix.data(), fixed + 56, 16);
    const std::uint32_t metaLength = get32(fixed + 72);
    if (metaLength == 0 || metaLength > kMaxMeta)
    {
        fail(error, "El paquete está dañado (datos del juego inválidos).");
        return false;
    }
    if (raw.header.protection == Protection::Password &&
        (raw.header.argonMemoryKiB < 8 || raw.header.argonMemoryKiB > kMaxArgonKiB || raw.header.argonPasses < 1 ||
         raw.header.argonPasses > kMaxArgonPasses))
    {
        fail(error, "El paquete está dañado (parámetros de clave inválidos).");
        return false;
    }

    std::vector<std::uint8_t> meta(metaLength);
    if (!take(metaLength, meta.data()))
    {
        fail(error, "El paquete está dañado (cabecera incompleta).");
        return false;
    }
    if (!infoFromJson(std::string_view(reinterpret_cast<const char*>(meta.data()), meta.size()), raw.header.info))
    {
        fail(error, "El paquete está dañado (datos del juego ilegibles).");
        return false;
    }
    if (raw.header.signedPackage && !take(32, raw.header.publicKey.data()))
    {
        fail(error, "El paquete está dañado (falta la clave de firma).");
        return false;
    }
    std::uint8_t tail[12];
    if (!take(sizeof(tail), tail))
    {
        fail(error, "El paquete está dañado (cabecera incompleta).");
        return false;
    }
    raw.chunkSize             = get32(tail);
    raw.header.plainSize      = get64(tail + 4);
    if (raw.chunkSize < 4096 || raw.chunkSize > (16u << 20) || raw.header.plainSize < 2 || raw.header.plainSize > kMaxPlain)
    {
        fail(error, "El paquete está dañado (tamaños inválidos).");
        return false;
    }
    raw.bytes = std::move(bytes);
    return true;
}

std::array<std::uint8_t, 32> headerHash(const std::vector<std::uint8_t>& header)
{
    std::array<std::uint8_t, 32> digest{};
    crypto_blake2b(digest.data(), digest.size(), header.data(), header.size());
    return digest;
}

bool deriveKey(const RawHeader& raw, const std::string& password, const std::optional<Key32>& key, Key32& out, std::string* error)
{
    Key32 master{};
    switch (raw.header.protection)
    {
    case Protection::Password:
    {
        if (password.empty())
        {
            fail(error, "Este paquete necesita una contraseña.");
            return false;
        }
        const std::size_t bytes = static_cast<std::size_t>(raw.header.argonMemoryKiB) * 1024;
        void* work              = std::malloc(bytes);
        if (!work)
        {
            fail(error, "No hay memoria suficiente para derivar la clave.");
            return false;
        }
        crypto_argon2_config config{CRYPTO_ARGON2_ID, raw.header.argonMemoryKiB, raw.header.argonPasses, 1};
        crypto_argon2_inputs inputs{reinterpret_cast<const std::uint8_t*>(password.data()), raw.salt.data(),
                                    static_cast<std::uint32_t>(password.size()), static_cast<std::uint32_t>(raw.salt.size())};
        crypto_argon2(master.data(), static_cast<std::uint32_t>(master.size()), work, config, inputs, crypto_argon2_no_extras);
        crypto_wipe(work, bytes);
        std::free(work);
        break;
    }
    case Protection::KeyFile:
        if (!key)
        {
            fail(error, "Este paquete necesita su archivo de clave.");
            return false;
        }
        master = *key;
        break;
    case Protection::Embedded:
    {
        std::vector<std::uint8_t> message;
        const char label[] = "zio.pkg.embedded.v1";
        message.insert(message.end(), label, label + sizeof(label) - 1);
        message.insert(message.end(), raw.salt.begin(), raw.salt.end());
        crypto_blake2b(master.data(), master.size(), message.data(), message.size());
        break;
    }
    }

    std::vector<std::uint8_t> message;
    const char label[] = "zio.pkg.enc.v1";
    message.insert(message.end(), label, label + sizeof(label) - 1);
    message.insert(message.end(), raw.salt.begin(), raw.salt.end());
    crypto_blake2b_keyed(out.data(), out.size(), master.data(), master.size(), message.data(), message.size());
    crypto_wipe(master.data(), master.size());
    return true;
}

void chunkNonce(const std::array<std::uint8_t, 16>& prefix, std::uint64_t index, std::uint8_t nonce[24])
{
    std::memcpy(nonce, prefix.data(), 16);
    for (int i = 0; i < 8; ++i)
        nonce[16 + i] = static_cast<std::uint8_t>(index >> (8 * i));
}

void chunkAad(const std::array<std::uint8_t, 32>& hash, std::uint64_t index, bool last, std::uint8_t aad[32 + 8 + 1])
{
    std::memcpy(aad, hash.data(), 32);
    for (int i = 0; i < 8; ++i)
        aad[32 + i] = static_cast<std::uint8_t>(index >> (8 * i));
    aad[40] = last ? 1 : 0;
}

class ChunkWriter
{
public:
    ChunkWriter(std::ofstream& out, const Key32& key, const RawHeader& raw, const std::array<std::uint8_t, 32>& hash,
                crypto_blake2b_ctx& running)
        : out_(out), key_(key), raw_(raw), hash_(hash), running_(running)
    {
        buffer_.reserve(raw.chunkSize);
    }

    bool push(const std::uint8_t* data, std::size_t size)
    {
        while (size > 0)
        {
            const std::size_t room = raw_.chunkSize - buffer_.size();
            const std::size_t take = std::min(room, size);
            buffer_.insert(buffer_.end(), data, data + take);
            data += take;
            size -= take;
            total_ += take;
            if (buffer_.size() == raw_.chunkSize && !flush())
                return false;
        }
        return true;
    }

    bool finish() { return buffer_.empty() ? true : flush(); }
    std::uint64_t total() const { return total_; }

private:
    bool flush()
    {
        const bool last = total_ >= raw_.header.plainSize;
        std::uint8_t nonce[24];
        std::uint8_t aad[41];
        chunkNonce(raw_.noncePrefix, index_, nonce);
        chunkAad(hash_, index_, last, aad);
        std::vector<std::uint8_t> packed = lz::compress(buffer_.data(), buffer_.size());
        const bool useCompressed         = packed.size() < buffer_.size();
        const std::uint8_t* source       = useCompressed ? packed.data() : buffer_.data();
        const std::size_t sourceSize     = useCompressed ? packed.size() : buffer_.size();
        std::vector<std::uint8_t> cipher(sourceSize);
        std::uint8_t mac[16];
        crypto_aead_lock(cipher.data(), mac, key_.data(), nonce, aad, sizeof(aad), source, sourceSize);

        std::vector<std::uint8_t> record;
        put32(record, static_cast<std::uint32_t>(cipher.size()));
        record.insert(record.end(), mac, mac + 16);
        record.insert(record.end(), cipher.begin(), cipher.end());
        out_.write(reinterpret_cast<const char*>(record.data()), static_cast<std::streamsize>(record.size()));
        crypto_blake2b_update(&running_, record.data(), record.size());
        crypto_wipe(buffer_.data(), buffer_.size());
        buffer_.clear();
        ++index_;
        return static_cast<bool>(out_);
    }

    std::ofstream& out_;
    const Key32& key_;
    const RawHeader& raw_;
    const std::array<std::uint8_t, 32>& hash_;
    crypto_blake2b_ctx& running_;
    std::vector<std::uint8_t> buffer_;
    std::uint64_t index_ = 0;
    std::uint64_t total_ = 0;
};

std::string upper(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

}

void randomBytes(void* data, std::size_t size)
{
#if defined(ZIO_PLATFORM_WINDOWS)
    if (BCryptGenRandom(nullptr, static_cast<PUCHAR>(data), static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        std::abort();
#else
    auto* bytes = static_cast<std::uint8_t*>(data);
    while (size > 0)
    {
        const ssize_t got = getrandom(bytes, size, 0);
        if (got <= 0)
            std::abort();
        bytes += got;
        size -= static_cast<std::size_t>(got);
    }
#endif
}

std::string newPackageId()
{
    std::uint8_t id[16];
    randomBytes(id, sizeof(id));
    id[6] = static_cast<std::uint8_t>((id[6] & 0x0F) | 0x40);
    id[8] = static_cast<std::uint8_t>((id[8] & 0x3F) | 0x80);
    char text[40];
    std::snprintf(text, sizeof(text), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", id[0], id[1], id[2], id[3],
                  id[4], id[5], id[6], id[7], id[8], id[9], id[10], id[11], id[12], id[13], id[14], id[15]);
    return text;
}

std::string fingerprint(const Key32& publicKey)
{
    std::uint8_t digest[16];
    crypto_blake2b(digest, sizeof(digest), publicKey.data(), publicKey.size());
    std::string out;
    char pair[3];
    for (std::size_t i = 0; i < sizeof(digest); ++i)
    {
        if (i > 0 && i % 2 == 0)
            out += '-';
        std::snprintf(pair, sizeof(pair), "%02X", digest[i]);
        out += pair;
    }
    return out;
}

SigningKey signingKeyFromSeed(const Key32& seed)
{
    SigningKey key;
    Key32 copy = seed;
    crypto_eddsa_key_pair(key.secret.data(), key.publicKey.data(), copy.data());
    return key;
}

SigningKey generateSigningKey()
{
    Key32 seed{};
    randomBytes(seed.data(), seed.size());
    SigningKey key = signingKeyFromSeed(seed);
    crypto_wipe(seed.data(), seed.size());
    return key;
}

Key32 generateKey()
{
    Key32 key{};
    randomBytes(key.data(), key.size());
    return key;
}

std::string keyToText(const Key32& key)
{
    std::string out;
    char pair[3];
    for (const std::uint8_t byte : key)
    {
        std::snprintf(pair, sizeof(pair), "%02x", byte);
        out += pair;
    }
    return out;
}

std::optional<Key32> keyFromText(const std::string& text)
{
    std::string clean;
    for (const char c : text)
        if (!std::isspace(static_cast<unsigned char>(c)))
            clean.push_back(c);
    if (clean.size() != 64)
        return std::nullopt;
    Key32 key{};
    for (std::size_t i = 0; i < 32; ++i)
    {
        const auto digit = [](char c) -> int {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        };
        const int hi = digit(clean[i * 2]);
        const int lo = digit(clean[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return std::nullopt;
        key[i] = static_cast<std::uint8_t>(hi * 16 + lo);
    }
    return key;
}

bool isSafeArchivePath(const std::string& path)
{
    if (path.empty() || path.size() > kMaxPathLength || path.front() == '/' || path.back() == '/')
        return false;
    std::size_t start = 0;
    while (start <= path.size())
    {
        const std::size_t end        = path.find('/', start);
        const std::string component = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (component.empty() || component == "." || component == "..")
            return false;
        if (component.back() == ' ' || component.back() == '.')
            return false;
        for (const unsigned char c : component)
            if (c < 32 || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                return false;
        std::string stem = upper(component.substr(0, component.find('.')));
        static const char* reserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8",
                                         "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
        for (const char* name : reserved)
            if (stem == name)
                return false;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return true;
}

bool writePackage(const std::vector<FileEntry>& files, const fs::path& output, const PackageInfo& info, const WriteOptions& options,
                  std::string* error)
{
    if (options.protection == Protection::Password && options.password.empty())
    {
        fail(error, "Escribe una contraseña para proteger el paquete.");
        return false;
    }
    if (options.protection == Protection::Password &&
        (options.argonMemoryKiB < 8 || options.argonMemoryKiB > kMaxArgonKiB || options.argonPasses < 1 ||
         options.argonPasses > kMaxArgonPasses))
    {
        fail(error, "Parámetros de contraseña inválidos.");
        return false;
    }

    std::error_code ec;
    std::uint64_t plainSize = 2;
    std::set<std::string> seen;
    for (const FileEntry& entry : files)
    {
        if (!isSafeArchivePath(entry.archivePath))
        {
            fail(error, "Ruta no permitida en el paquete: " + entry.archivePath);
            return false;
        }
        if (!seen.insert(upper(entry.archivePath)).second)
        {
            fail(error, "Archivo repetido en el paquete: " + entry.archivePath);
            return false;
        }
        const std::uintmax_t size = fs::file_size(entry.file, ec);
        if (ec)
        {
            fail(error, "No se puede leer " + entry.file.string());
            return false;
        }
        plainSize += 2 + entry.archivePath.size() + 8 + size;
    }
    if (files.size() > kMaxEntries || plainSize > kMaxPlain)
    {
        fail(error, "El juego es demasiado grande para empaquetarlo.");
        return false;
    }

    PackageInfo stored = info;
    if (stored.id.empty())
        stored.id = newPackageId();

    std::array<std::uint8_t, 32> salt{};
    std::array<std::uint8_t, 16> noncePrefix{};
    randomBytes(salt.data(), salt.size());
    randomBytes(noncePrefix.data(), noncePrefix.size());

    RawHeader raw;
    raw.bytes                  = buildHeader(stored, options, salt, noncePrefix, plainSize);
    raw.header.info            = stored;
    raw.header.protection      = options.protection;
    raw.header.argonMemoryKiB  = options.protection == Protection::Password ? options.argonMemoryKiB : 0;
    raw.header.argonPasses     = options.protection == Protection::Password ? options.argonPasses : 0;
    raw.header.plainSize       = plainSize;
    raw.salt                   = salt;
    raw.noncePrefix            = noncePrefix;
    raw.chunkSize              = kChunkSize;

    Key32 key{};
    if (!deriveKey(raw, options.password, options.protection == Protection::KeyFile ? std::optional<Key32>(options.key) : std::nullopt,
                   key, error))
        return false;
    const std::array<std::uint8_t, 32> hash = headerHash(raw.bytes);

    const fs::path temp = output.string() + ".tmp";
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        fail(error, "No se puede escribir " + temp.string());
        crypto_wipe(key.data(), key.size());
        return false;
    }
    const auto bail = [&](const std::string& message) {
        out.close();
        fs::remove(temp, ec);
        crypto_wipe(key.data(), key.size());
        fail(error, message);
        return false;
    };

    out.write(reinterpret_cast<const char*>(raw.bytes.data()), static_cast<std::streamsize>(raw.bytes.size()));
    crypto_blake2b_ctx running;
    crypto_blake2b_init(&running, 64);
    crypto_blake2b_update(&running, raw.bytes.data(), raw.bytes.size());

    ChunkWriter writer(out, key, raw, hash, running);
    std::vector<std::uint8_t> piece;
    for (const FileEntry& entry : files)
    {
        piece.clear();
        put16(piece, static_cast<std::uint16_t>(entry.archivePath.size()));
        piece.insert(piece.end(), entry.archivePath.begin(), entry.archivePath.end());
        const std::uintmax_t size = fs::file_size(entry.file, ec);
        put64(piece, size);
        if (!writer.push(piece.data(), piece.size()))
            return bail("Fallo al escribir el paquete.");

        std::ifstream in(entry.file, std::ios::binary);
        if (!in)
            return bail("No se puede leer " + entry.file.string());
        std::vector<std::uint8_t> block(64 * 1024);
        std::uintmax_t remaining = size;
        while (remaining > 0)
        {
            const std::size_t want = static_cast<std::size_t>(std::min<std::uintmax_t>(remaining, block.size()));
            if (!readExact(in, block.data(), want))
                return bail("El archivo cambió mientras se empaquetaba: " + entry.file.string());
            if (!writer.push(block.data(), want))
                return bail("Fallo al escribir el paquete.");
            remaining -= want;
        }
    }
    piece.clear();
    put16(piece, 0);
    if (!writer.push(piece.data(), piece.size()) || !writer.finish())
        return bail("Fallo al escribir el paquete.");
    if (writer.total() != plainSize)
        return bail("El contenido cambió mientras se empaquetaba.");

    if (options.signer)
    {
        std::uint8_t digest[64];
        crypto_blake2b_final(&running, digest);
        std::uint8_t signature[64];
        crypto_eddsa_sign(signature, options.signer->secret.data(), digest, sizeof(digest));
        out.write(reinterpret_cast<const char*>(signature), sizeof(signature));
    }
    out.flush();
    if (!out)
        return bail("Fallo al escribir el paquete.");
    out.close();
    crypto_wipe(key.data(), key.size());

    fs::remove(output, ec);
    fs::rename(temp, output, ec);
    if (ec)
    {
        fs::remove(temp, ec);
        fail(error, "No se pudo guardar " + output.string());
        return false;
    }
    return true;
}

bool readHeader(const fs::path& package, PackageHeader& header, std::string* error)
{
    std::ifstream in(package, std::ios::binary);
    if (!in)
    {
        fail(error, "No se puede abrir " + package.string());
        return false;
    }
    RawHeader raw;
    if (!parseHeader(in, raw, error))
        return false;
    header = raw.header;
    return true;
}

namespace
{

bool walkChunks(std::istream& in, const RawHeader& raw, std::uint64_t fileSize, std::uint64_t headerSize, crypto_blake2b_ctx* running,
                const std::function<bool(std::uint64_t index, bool last, std::uint32_t length, const std::uint8_t* mac,
                                         std::vector<std::uint8_t>& cipher)>& onChunk,
                std::string* error)
{
    std::uint64_t consumedPlain = 0;
    std::uint64_t position      = headerSize;
    std::uint64_t index         = 0;
    const std::uint64_t reserve = raw.header.signedPackage ? 64 : 0;
    while (consumedPlain < raw.header.plainSize)
    {
        std::uint8_t lengthBytes[4];
        std::uint8_t mac[16];
        if (!readExact(in, lengthBytes, 4) || !readExact(in, mac, 16))
        {
            fail(error, "El paquete está incompleto o dañado.");
            return false;
        }
        const std::uint32_t length = get32(lengthBytes);
        const std::uint64_t plainLength =
            raw.version >= 2 ? std::min<std::uint64_t>(raw.chunkSize, raw.header.plainSize - consumedPlain) : length;
        if (length == 0 || length > raw.chunkSize || length > plainLength || consumedPlain + plainLength > raw.header.plainSize ||
            position + 20 + length + reserve > fileSize)
        {
            fail(error, "El paquete está incompleto o dañado.");
            return false;
        }
        std::vector<std::uint8_t> cipher(length);
        if (!readExact(in, cipher.data(), length))
        {
            fail(error, "El paquete está incompleto o dañado.");
            return false;
        }
        if (running)
        {
            crypto_blake2b_update(running, lengthBytes, 4);
            crypto_blake2b_update(running, mac, 16);
            crypto_blake2b_update(running, cipher.data(), length);
        }
        const bool last = consumedPlain + plainLength == raw.header.plainSize;
        if (!onChunk(index, last, length, mac, cipher))
            return false;
        consumedPlain += plainLength;
        position += 20 + length;
        ++index;
    }
    if (position + reserve != fileSize)
    {
        fail(error, "El paquete tiene datos de más o está dañado.");
        return false;
    }
    return true;
}

bool checkSignature(std::istream& in, const RawHeader& raw, crypto_blake2b_ctx& running, std::string* error)
{
    std::uint8_t signature[64];
    std::uint8_t digest[64];
    crypto_blake2b_final(&running, digest);
    if (!readExact(in, signature, sizeof(signature)))
    {
        fail(error, "Falta la firma del paquete.");
        return false;
    }
    if (crypto_eddsa_check(signature, raw.header.publicKey.data(), digest, sizeof(digest)) != 0)
    {
        fail(error, "La firma del paquete no es válida: el archivo fue modificado.");
        return false;
    }
    return true;
}

}

bool verifySignature(const fs::path& package, std::string* error)
{
    std::error_code ec;
    const std::uint64_t fileSize = fs::file_size(package, ec);
    std::ifstream in(package, std::ios::binary);
    if (ec || !in)
    {
        fail(error, "No se puede abrir " + package.string());
        return false;
    }
    RawHeader raw;
    if (!parseHeader(in, raw, error))
        return false;
    if (!raw.header.signedPackage)
    {
        fail(error, "El paquete no está firmado.");
        return false;
    }
    crypto_blake2b_ctx running;
    crypto_blake2b_init(&running, 64);
    crypto_blake2b_update(&running, raw.bytes.data(), raw.bytes.size());
    const auto nothing = [](std::uint64_t, bool, std::uint32_t, const std::uint8_t*, std::vector<std::uint8_t>&) { return true; };
    if (!walkChunks(in, raw, fileSize, raw.bytes.size(), &running, nothing, error))
        return false;
    return checkSignature(in, raw, running, error);
}

namespace
{

class ArchiveSink
{
public:
    explicit ArchiveSink(const fs::path& destination) : destination_(destination) {}

    bool consume(const std::uint8_t* data, std::size_t size, std::string* error)
    {
        while (size > 0)
        {
            if (state_ == State::Ended)
            {
                fail(error, "El paquete tiene datos de más.");
                return false;
            }
            if (state_ == State::Data)
            {
                const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(remaining_, size));
                file_.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(take));
                if (!file_)
                {
                    fail(error, "No se pudo escribir " + current_.string());
                    return false;
                }
                remaining_ -= take;
                data += take;
                size -= take;
                if (remaining_ == 0)
                {
                    file_.close();
                    state_ = State::PathLength;
                    need_  = 2;
                    scratch_.clear();
                }
                continue;
            }
            const std::size_t take = std::min(need_ - scratch_.size(), size);
            scratch_.insert(scratch_.end(), data, data + take);
            data += take;
            size -= take;
            if (scratch_.size() < need_)
                continue;
            if (!advance(error))
                return false;
        }
        return true;
    }

    bool ended() const { return state_ == State::Ended; }

private:
    enum class State
    {
        PathLength,
        Path,
        Size,
        Data,
        Ended
    };

    bool advance(std::string* error)
    {
        switch (state_)
        {
        case State::PathLength:
        {
            const std::size_t length = static_cast<std::size_t>(scratch_[0]) | (static_cast<std::size_t>(scratch_[1]) << 8);
            scratch_.clear();
            if (length == 0)
            {
                state_ = State::Ended;
                return true;
            }
            if (length > kMaxPathLength)
            {
                fail(error, "El paquete contiene una ruta inválida.");
                return false;
            }
            state_ = State::Path;
            need_  = length;
            return true;
        }
        case State::Path:
        {
            relative_.assign(scratch_.begin(), scratch_.end());
            scratch_.clear();
            if (!isSafeArchivePath(relative_) || !seen_.insert(upper(relative_)).second || seen_.size() > kMaxEntries)
            {
                fail(error, "El paquete contiene una ruta no permitida: " + relative_);
                return false;
            }
            state_ = State::Size;
            need_  = 8;
            return true;
        }
        case State::Size:
        {
            remaining_ = get64(scratch_.data());
            scratch_.clear();
            current_ = destination_ / fs::path(relative_);
            std::error_code ec;
            fs::create_directories(current_.parent_path(), ec);
            file_.open(current_, std::ios::binary | std::ios::trunc);
            if (!file_)
            {
                fail(error, "No se pudo crear " + current_.string());
                return false;
            }
            if (remaining_ == 0)
            {
                file_.close();
                state_ = State::PathLength;
                need_  = 2;
            }
            else
            {
                state_ = State::Data;
            }
            return true;
        }
        default:
            return true;
        }
    }

    fs::path destination_;
    fs::path current_;
    std::string relative_;
    std::vector<std::uint8_t> scratch_;
    std::set<std::string> seen_;
    std::ofstream file_;
    State state_           = State::PathLength;
    std::size_t need_      = 2;
    std::uint64_t remaining_ = 0;
};

}

bool extractPackage(const fs::path& package, const fs::path& destination, const OpenCredentials& credentials, PackageHeader* headerOut,
                    std::string* error)
{
    std::error_code ec;
    const std::uint64_t fileSize = fs::file_size(package, ec);
    std::ifstream in(package, std::ios::binary);
    if (ec || !in)
    {
        fail(error, "No se puede abrir " + package.string());
        return false;
    }
    RawHeader raw;
    if (!parseHeader(in, raw, error))
        return false;
    if (headerOut)
        *headerOut = raw.header;

    Key32 key{};
    if (!deriveKey(raw, credentials.password, credentials.key, key, error))
        return false;
    const std::array<std::uint8_t, 32> hash = headerHash(raw.bytes);

    const bool createdDestination = !fs::exists(destination, ec);
    fs::create_directories(destination, ec);
    if (ec)
    {
        crypto_wipe(key.data(), key.size());
        fail(error, "No se pudo crear " + destination.string());
        return false;
    }

    crypto_blake2b_ctx running;
    crypto_blake2b_init(&running, 64);
    crypto_blake2b_update(&running, raw.bytes.data(), raw.bytes.size());

    auto sinkPtr    = std::make_unique<ArchiveSink>(destination);
    ArchiveSink& sink = *sinkPtr;
    std::uint64_t producedPlain = 0;
    bool ok = walkChunks(
        in, raw, fileSize, raw.bytes.size(), raw.header.signedPackage ? &running : nullptr,
        [&](std::uint64_t index, bool last, std::uint32_t length, const std::uint8_t* mac, std::vector<std::uint8_t>& cipher) {
            std::uint8_t nonce[24];
            std::uint8_t aad[41];
            chunkNonce(raw.noncePrefix, index, nonce);
            chunkAad(hash, index, last, aad);
            std::vector<std::uint8_t> plain(length);
            if (crypto_aead_unlock(plain.data(), mac, key.data(), nonce, aad, sizeof(aad), cipher.data(), length) != 0)
            {
                fail(error, index == 0 ? "Contraseña o clave incorrecta, o el paquete está dañado."
                                       : "El paquete fue modificado o está dañado.");
                return false;
            }
            if (raw.version >= 2)
            {
                const std::uint64_t expected = std::min<std::uint64_t>(raw.chunkSize, raw.header.plainSize - producedPlain);
                if (length < expected)
                {
                    std::vector<std::uint8_t> unpacked(static_cast<std::size_t>(expected));
                    if (!lz::decompress(plain.data(), plain.size(), unpacked.data(), unpacked.size()))
                    {
                        crypto_wipe(plain.data(), plain.size());
                        fail(error, "El paquete está dañado (datos comprimidos inválidos).");
                        return false;
                    }
                    crypto_wipe(plain.data(), plain.size());
                    plain.swap(unpacked);
                }
                producedPlain += expected;
            }
            const bool consumed = sink.consume(plain.data(), plain.size(), error);
            crypto_wipe(plain.data(), plain.size());
            return consumed;
        },
        error);
    crypto_wipe(key.data(), key.size());
    if (ok && !sink.ended())
    {
        fail(error, "El paquete está incompleto.");
        ok = false;
    }
    if (ok && raw.header.signedPackage)
        ok = checkSignature(in, raw, running, error);
    sinkPtr.reset();
    if (!ok && createdDestination)
        fs::remove_all(destination, ec);
    return ok;
}

}
