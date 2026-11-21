#include "update/Updater.h"

#include "update/ReleaseNotes.h"

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <vector>

#ifndef ZIO_VERSION
#define ZIO_VERSION "0.0.0"
#endif

namespace fs = std::filesystem;

namespace zio_editor
{

namespace
{

constexpr const wchar_t* kHost          = L"github.com";
constexpr const wchar_t* kApiHost       = L"api.github.com";
constexpr const wchar_t* kNotesPrefix   = L"/repos/4nime1234r/motor_zio/releases/tags/v";
constexpr std::uint64_t kMaxNotesBytes  = 512 * 1024;
constexpr const wchar_t* kLatestPath    = L"/4nime1234r/motor_zio/releases/latest/download/latest.yml";
constexpr const wchar_t* kReleasePrefix = L"/4nime1234r/motor_zio/releases/download/v";
constexpr const wchar_t* kAssetName     = L"/zio_editor.exe";

constexpr std::uint64_t kMaxManifestBytes = 64 * 1024;
constexpr std::uint64_t kMaxBinaryBytes   = 512ull * 1024 * 1024;
constexpr auto kRecheckInterval           = std::chrono::hours(4);
constexpr auto kStartupDelay              = std::chrono::seconds(3);

struct Version
{
    int major = 0;
    int minor = 0;
    int patch = 0;

    bool operator<(const Version& other) const
    {
        if (major != other.major)
            return major < other.major;
        if (minor != other.minor)
            return minor < other.minor;
        return patch < other.patch;
    }
};

bool parseVersion(const std::string& text, Version& out)
{
    int parts[3] = {0, 0, 0};
    int index    = 0;
    bool digits  = false;
    for (char c : text)
    {
        if (c >= '0' && c <= '9')
        {
            if (parts[index] > 100000)
                return false;
            parts[index] = parts[index] * 10 + (c - '0');
            digits       = true;
        }
        else if (c == '.' && digits && index < 2)
        {
            ++index;
            digits = false;
        }
        else
        {
            return false;
        }
    }
    if (!digits || index != 2)
        return false;
    out = {parts[0], parts[1], parts[2]};
    return true;
}

std::wstring widen(const std::string& text)
{
    return std::wstring(text.begin(), text.end());
}

fs::path exePath()
{
    wchar_t buffer[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer))
        return {};
    return fs::path(std::wstring(buffer, length));
}

fs::path sibling(const wchar_t* suffix)
{
    fs::path path = exePath();
    path += suffix;
    return path;
}

struct InternetHandle
{
    HINTERNET handle = nullptr;
    explicit InternetHandle(HINTERNET h = nullptr) : handle(h) {}
    ~InternetHandle()
    {
        if (handle)
            WinHttpCloseHandle(handle);
    }
    InternetHandle(const InternetHandle&)            = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
};

using Sink = std::function<bool(const char*, std::size_t)>;

bool httpGet(const wchar_t* host, const std::wstring& path, std::uint64_t maxBytes, const Sink& sink,
             const std::function<void(std::uint64_t, std::uint64_t)>& progress, const std::atomic<bool>& cancel,
             std::string& error)
{
    const std::wstring agent = L"ZioEditor/" + widen(ZIO_VERSION);
    InternetHandle session(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.handle)
    {
        error = "No se pudo iniciar la conexión";
        return false;
    }
    WinHttpSetTimeouts(session.handle, 10000, 10000, 15000, 30000);

    InternetHandle connection(WinHttpConnect(session.handle, host, INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection.handle)
    {
        error = "No se pudo conectar con GitHub";
        return false;
    }

    InternetHandle request(WinHttpOpenRequest(connection.handle, L"GET", path.c_str(), nullptr,
                                              WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              WINHTTP_FLAG_SECURE));
    if (!request.handle)
    {
        error = "No se pudo preparar la solicitud";
        return false;
    }

    if (!WinHttpSendRequest(request.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.handle, nullptr))
    {
        error = "No hay conexión con GitHub";
        return false;
    }

    DWORD status     = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) ||
        status != 200)
    {
        error = "GitHub respondió con el código " + std::to_string(status);
        return false;
    }

    DWORD lengthValue = 0;
    DWORD lengthSize  = sizeof(lengthValue);
    std::uint64_t total = 0;
    if (WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &lengthValue, &lengthSize, WINHTTP_NO_HEADER_INDEX))
        total = lengthValue;
    if (total > maxBytes)
    {
        error = "El archivo es demasiado grande";
        return false;
    }

    std::vector<char> buffer(64 * 1024);
    std::uint64_t received = 0;
    for (;;)
    {
        if (cancel)
        {
            error = "Cancelado";
            return false;
        }
        DWORD read = 0;
        if (!WinHttpReadData(request.handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read))
        {
            error = "Se interrumpió la descarga";
            return false;
        }
        if (read == 0)
            break;
        received += read;
        if (received > maxBytes)
        {
            error = "El archivo es demasiado grande";
            return false;
        }
        if (!sink(buffer.data(), read))
        {
            error = "No se pudo guardar el archivo";
            return false;
        }
        if (progress)
            progress(received, total);
    }
    return true;
}

class Sha512
{
public:
    Sha512()
    {
        if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA512_ALGORITHM, nullptr, 0) != 0)
            return;
        if (BCryptCreateHash(algorithm_, &hash_, nullptr, 0, nullptr, 0, 0) != 0)
            hash_ = nullptr;
    }

    ~Sha512()
    {
        if (hash_)
            BCryptDestroyHash(hash_);
        if (algorithm_)
            BCryptCloseAlgorithmProvider(algorithm_, 0);
    }

    Sha512(const Sha512&)            = delete;
    Sha512& operator=(const Sha512&) = delete;

    bool ok() const { return hash_ != nullptr; }

    void update(const char* data, std::size_t size)
    {
        if (hash_)
            BCryptHashData(hash_, reinterpret_cast<PUCHAR>(const_cast<char*>(data)), static_cast<ULONG>(size), 0);
    }

    std::string finishBase64()
    {
        unsigned char digest[64] = {};
        if (!hash_ || BCryptFinishHash(hash_, digest, sizeof(digest), 0) != 0)
            return {};
        static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (std::size_t i = 0; i < sizeof(digest); i += 3)
        {
            const std::size_t left = std::min<std::size_t>(3, sizeof(digest) - i);
            std::uint32_t chunk    = static_cast<std::uint32_t>(digest[i]) << 16;
            if (left > 1)
                chunk |= static_cast<std::uint32_t>(digest[i + 1]) << 8;
            if (left > 2)
                chunk |= digest[i + 2];
            out += kTable[(chunk >> 18) & 63];
            out += kTable[(chunk >> 12) & 63];
            out += left > 1 ? kTable[(chunk >> 6) & 63] : '=';
            out += left > 2 ? kTable[chunk & 63] : '=';
        }
        return out;
    }

private:
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_     = nullptr;
};

std::string manifestField(const std::string& text, const std::string& key)
{
    const std::string prefix = key + ":";
    std::size_t position     = 0;
    while (position < text.size())
    {
        std::size_t end = text.find('\n', position);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(position, end - position);
        position         = end + 1;
        if (line.compare(0, prefix.size(), prefix) != 0)
            continue;
        std::string value = line.substr(prefix.size());
        auto isBlank      = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\'' || c == '"'; };
        while (!value.empty() && isBlank(value.front()))
            value.erase(value.begin());
        while (!value.empty() && isBlank(value.back()))
            value.pop_back();
        return value;
    }
    return {};
}

bool isBase64(const std::string& text)
{
    if (text.size() != 88)
        return false;
    return std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/' ||
               c == '=';
    });
}

}

bool Updater::enabled()
{
#ifdef ZIO_EDITOR_UPDATER
    return true;
#else
    return false;
#endif
}

void Updater::cleanupLeftovers()
{
    std::error_code ignore;
    fs::remove(sibling(L".old"), ignore);
    fs::remove(sibling(L".download"), ignore);
    fs::remove(sibling(L".update"), ignore);
}

Updater::~Updater()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cancel_ = true;
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

void Updater::start()
{
    if (!enabled() || worker_.joinable())
        return;
    worker_ = std::thread([this] { run(); });
}

UpdateStatus Updater::status() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void Updater::setState(UpdateState state, std::string error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    status_.state = state;
    status_.error = std::move(error);
    if (state != UpdateState::Downloading)
        status_.progress = state == UpdateState::Downloaded ? 1.f : 0.f;
}

void Updater::download()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_.state != UpdateState::Available)
            return;
        downloadRequested_ = true;
        status_.state      = UpdateState::Downloading;
        status_.progress   = 0.f;
        status_.error.clear();
    }
    wake_.notify_all();
}

void Updater::retry()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_.state != UpdateState::Error)
            return;
        recheckRequested_ = true;
        status_.state     = UpdateState::Checking;
        status_.error.clear();
    }
    wake_.notify_all();
}

bool Updater::checkNow()
{
    if (!enabled() || !worker_.joinable())
        return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_.state != UpdateState::Idle && status_.state != UpdateState::Error)
            return false;
        status_.state    = UpdateState::Checking;
        status_.error.clear();
        status_.upToDate = false;
        recheckRequested_ = true;
    }
    wake_.notify_all();
    return true;
}

void Updater::run()
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (wake_.wait_for(lock, kStartupDelay, [this] { return stop_ || recheckRequested_; }) && stop_)
            return;
        recheckRequested_ = false;
    }
    check();

    for (;;)
    {
        bool download = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_for(lock, kRecheckInterval,
                           [this] { return stop_ || downloadRequested_ || recheckRequested_; });
            if (stop_)
                return;
            download           = downloadRequested_;
            downloadRequested_ = false;
            recheckRequested_  = false;
        }
        if (download)
            fetch();
        else
            check();
    }
}

void Updater::check()
{
    UpdateState previous;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        previous = status_.state;
        if (previous == UpdateState::Downloading || previous == UpdateState::Downloaded)
            return;
        if (previous == UpdateState::Idle || previous == UpdateState::Error)
            status_.state = UpdateState::Checking;
    }

    std::string manifest;
    std::string error;
    const bool ok = httpGet(
        kHost, kLatestPath, kMaxManifestBytes,
        [&manifest](const char* data, std::size_t size) {
            manifest.append(data, size);
            return true;
        },
        nullptr, cancel_, error);
    if (!ok)
    {
        if (previous != UpdateState::Available)
            setState(UpdateState::Error, "No se pudo comprobar si hay actualizaciones: " + error);
        return;
    }

    const std::string remote = manifestField(manifest, "version");
    const std::string hash   = manifestField(manifest, "sha512");
    Version current;
    Version latest;
    if (!parseVersion(ZIO_VERSION, current) || !parseVersion(remote, latest) || !isBase64(hash))
    {
        setState(UpdateState::Error, "La información de actualización no es válida");
        return;
    }

    if (!(current < latest))
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.checked  = true;
            status_.upToDate = true;
            status_.notes.clear();
        }
        setState(UpdateState::Idle);
        return;
    }

    loadNotes(remote);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.checked  = true;
        status_.upToDate = false;
        status_.version = remote;
        status_.state   = UpdateState::Available;
        status_.error.clear();
        expectedHash_ = hash;
    }
}

void Updater::loadNotes(const std::string& version)
{
    std::string body;
    std::string error;
    const bool ok = httpGet(
        kApiHost, std::wstring(kNotesPrefix) + widen(version), kMaxNotesBytes,
        [&body](const char* data, std::size_t size) {
            body.append(data, size);
            return true;
        },
        nullptr, cancel_, error);
    std::string notes = ok ? releaseBodyFromJson(body) : std::string();
    std::lock_guard<std::mutex> lock(mutex_);
    status_.notes = std::move(notes);
}

void Updater::fetch()
{
    std::string version;
    std::string expected;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        version  = status_.version;
        expected = expectedHash_;
    }

    const fs::path partial = sibling(L".download");
    const fs::path ready   = sibling(L".update");
    std::error_code ignore;
    fs::remove(partial, ignore);
    fs::remove(ready, ignore);

    Sha512 hash;
    if (!hash.ok())
    {
        setState(UpdateState::Error, "No se pudo verificar la descarga");
        return;
    }

    std::ofstream out(partial, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        setState(UpdateState::Error, "No se puede escribir junto al editor; muévelo a una carpeta con permisos");
        return;
    }

    const std::wstring path = std::wstring(kReleasePrefix) + widen(version) + kAssetName;
    std::string error;
    const bool ok = httpGet(
        kHost, path, kMaxBinaryBytes,
        [&](const char* data, std::size_t size) {
            hash.update(data, size);
            out.write(data, static_cast<std::streamsize>(size));
            return static_cast<bool>(out);
        },
        [this](std::uint64_t received, std::uint64_t total) {
            if (total == 0)
                return;
            std::lock_guard<std::mutex> lock(mutex_);
            status_.progress = static_cast<float>(received) / static_cast<float>(total);
        },
        cancel_, error);
    out.close();

    if (!ok)
    {
        fs::remove(partial, ignore);
        setState(UpdateState::Error, "No se pudo descargar la actualización: " + error);
        return;
    }

    if (hash.finishBase64() != expected)
    {
        fs::remove(partial, ignore);
        setState(UpdateState::Error, "La descarga está dañada y se descartó");
        return;
    }

    fs::rename(partial, ready, ignore);
    if (ignore)
    {
        fs::remove(partial, ignore);
        setState(UpdateState::Error, "No se pudo preparar la actualización");
        return;
    }
    setState(UpdateState::Downloaded);
}

void Updater::finish(bool restart)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_.state != UpdateState::Downloaded)
            return;
    }

    const fs::path exe     = exePath();
    const fs::path pending = sibling(L".update");
    const fs::path old     = sibling(L".old");
    std::error_code ec;
    if (exe.empty() || !fs::exists(pending, ec))
        return;

    fs::remove(old, ec);
    if (!MoveFileExW(exe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING))
        return;
    if (!MoveFileExW(pending.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        MoveFileExW(old.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);
        return;
    }

    if (restart)
    {
        std::wstring command = L"\"" + exe.wstring() + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                           exe.parent_path().c_str(), &startup, &process))
        {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
    }
}

}
