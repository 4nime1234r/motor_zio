#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "zio/core/Platform.h"

namespace zio::pack
{

enum class Protection : std::uint8_t
{
    KeyFile  = 0,
    Password = 1,
    Embedded = 2,
};

struct PackageInfo
{
    std::string name;
    std::string author;
    std::string version = "1.0.0";
    std::string description;
    std::string copyright;
    std::string website;
    std::string license;
    std::string id;
    std::string engineVersion;
    std::string entryScene;
    std::string executable;
    std::uint64_t createdUnix = 0;
    bool allowImport          = false;
};

using Key32 = std::array<std::uint8_t, 32>;

struct SigningKey
{
    std::array<std::uint8_t, 64> secret{};
    Key32 publicKey{};
};

struct WriteOptions
{
    Protection protection = Protection::Password;
    std::string password;
    Key32 key{};
    std::optional<SigningKey> signer;
    std::uint32_t argonMemoryKiB = 262144;
    std::uint32_t argonPasses    = 3;
};

struct FileEntry
{
    std::filesystem::path file;
    std::string archivePath;
};

struct PackageHeader
{
    PackageInfo info;
    Protection protection = Protection::Password;
    bool signedPackage    = false;
    Key32 publicKey{};
    std::uint32_t argonMemoryKiB = 0;
    std::uint32_t argonPasses    = 0;
    std::uint64_t plainSize      = 0;
};

struct OpenCredentials
{
    std::string password;
    std::optional<Key32> key;
};

ZIO_API void randomBytes(void* data, std::size_t size);

ZIO_API std::string newPackageId();
ZIO_API std::string fingerprint(const Key32& publicKey);

ZIO_API SigningKey generateSigningKey();
ZIO_API SigningKey signingKeyFromSeed(const Key32& seed);
ZIO_API std::string keyToText(const Key32& key);
ZIO_API std::optional<Key32> keyFromText(const std::string& text);
ZIO_API Key32 generateKey();

ZIO_API bool isSafeArchivePath(const std::string& path);

ZIO_API bool writePackage(const std::vector<FileEntry>& files, const std::filesystem::path& output, const PackageInfo& info,
                          const WriteOptions& options, std::string* error = nullptr);

ZIO_API bool readHeader(const std::filesystem::path& package, PackageHeader& header, std::string* error = nullptr);

ZIO_API bool verifySignature(const std::filesystem::path& package, std::string* error = nullptr);

ZIO_API bool extractPackage(const std::filesystem::path& package, const std::filesystem::path& destination,
                            const OpenCredentials& credentials, PackageHeader* header = nullptr, std::string* error = nullptr);

}
