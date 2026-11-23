#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "zio/pack/Package.h"

namespace fs = std::filesystem;
using namespace zio::pack;

namespace
{

int failures = 0;

void check(bool condition, const char* name)
{
    std::printf("%s %s\n", condition ? "[ok]  " : "[FALLO]", name);
    if (!condition)
        ++failures;
}

std::vector<std::uint8_t> readAll(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeAll(const fs::path& path, const std::vector<std::uint8_t>& data)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

std::vector<std::uint8_t> randomData(std::size_t size, unsigned seed)
{
    std::mt19937 rng(seed);
    std::vector<std::uint8_t> data(size);
    for (auto& byte : data)
        byte = static_cast<std::uint8_t>(rng());
    return data;
}

WriteOptions fast(Protection protection)
{
    WriteOptions options;
    options.protection     = protection;
    options.argonMemoryKiB = 256;
    options.argonPasses    = 1;
    return options;
}

}

int main()
{
    const fs::path base = fs::temp_directory_path() / "zio_package_test";
    fs::remove_all(base);
    const fs::path source = base / "src";
    writeAll(source / "project.zio", randomData(300, 1));
    writeAll(source / "assets" / "vacio.bin", {});
    writeAll(source / "assets" / "models" / "grande.bin", randomData(2'500'000, 2));
    writeAll(source / "assets" / "scenes" / "nivel.zscene3d", randomData(70'000, 3));

    std::vector<FileEntry> files = {{source / "project.zio", "project.zio"},
                                    {source / "assets" / "vacio.bin", "assets/vacio.bin"},
                                    {source / "assets" / "models" / "grande.bin", "assets/models/grande.bin"},
                                    {source / "assets" / "scenes" / "nivel.zscene3d", "assets/scenes/nivel.zscene3d"}};

    const auto sameTree = [&](const fs::path& root) {
        for (const FileEntry& entry : files)
            if (readAll(entry.file) != readAll(root / entry.archivePath))
                return false;
        return true;
    };

    PackageInfo info;
    info.name        = "Mi juego";
    info.author      = "4nime1234r";
    info.version     = "2.1.0";
    info.description = "Descripción con ñ y 日本語";
    info.copyright   = "(c) 2026";
    info.website     = "https://example.com";
    info.entryScene  = "assets/scenes/nivel.zscene3d";
    info.allowImport = true;

    std::string error;

    {
        WriteOptions options = fast(Protection::Password);
        options.password     = "correcta horse battery";
        const fs::path pkg   = base / "a.zgame";
        check(writePackage(files, pkg, info, options, &error), "se escribe un paquete con contraseña");

        PackageHeader header;
        check(readHeader(pkg, header, &error), "se lee la cabecera sin contraseña");
        check(header.info.name == "Mi juego" && header.info.author == "4nime1234r" && header.info.version == "2.1.0" &&
                  header.info.description == info.description && header.info.allowImport && !header.info.id.empty(),
              "la cabecera trae nombre, autor, versión y descripción");
        check(header.protection == Protection::Password && !header.signedPackage, "la cabecera indica contraseña y sin firma");

        const auto bytes = readAll(pkg);
        bool plaintextLeak = false;
        const auto contains = [&](const std::vector<std::uint8_t>& needle) {
            return std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end()) != bytes.end();
        };
        const auto big = readAll(files[2].file);
        plaintextLeak  = contains(std::vector<std::uint8_t>(big.begin() + 1000, big.begin() + 1064));
        check(!plaintextLeak, "el contenido no aparece en claro dentro del paquete");

        OpenCredentials good;
        good.password = "correcta horse battery";
        check(extractPackage(pkg, base / "out_ok", good, nullptr, &error), "se extrae con la contraseña correcta");
        check(sameTree(base / "out_ok"), "los archivos extraídos son idénticos");

        OpenCredentials bad;
        bad.password = "incorrecta";
        error.clear();
        check(!extractPackage(pkg, base / "out_bad", bad, nullptr, &error) && !error.empty(), "falla con contraseña incorrecta");
        check(!fs::exists(base / "out_bad"), "no deja archivos tras una contraseña incorrecta");

        OpenCredentials none;
        check(!extractPackage(pkg, base / "out_none", none, nullptr, &error), "falla sin contraseña");

        WriteOptions again = options;
        check(writePackage(files, base / "b.zgame", info, again, &error), "se escribe otra vez el mismo contenido");
        check(readAll(pkg) != readAll(base / "b.zgame"), "dos paquetes iguales no producen los mismos bytes (sal y nonce aleatorios)");

        auto tampered = bytes;
        tampered[tampered.size() / 2] ^= 0x01;
        writeAll(base / "t1.zgame", tampered);
        check(!extractPackage(base / "t1.zgame", base / "out_t1", good, nullptr, &error), "detecta un byte cambiado en el contenido");

        tampered = bytes;
        const std::string marker = "Mi juego";
        const auto at            = std::search(tampered.begin(), tampered.end(), marker.begin(), marker.end());
        check(at != tampered.end(), "el nombre está en la cabecera legible");
        if (at != tampered.end())
        {
            *at = 'X';
            writeAll(base / "t2.zgame", tampered);
            check(!extractPackage(base / "t2.zgame", base / "out_t2", good, nullptr, &error),
                  "detecta que se cambió el nombre del juego en la cabecera");
        }

        tampered = bytes;
        tampered.resize(tampered.size() - 100);
        writeAll(base / "t3.zgame", tampered);
        check(!extractPackage(base / "t3.zgame", base / "out_t3", good, nullptr, &error), "detecta un paquete truncado");

        tampered = bytes;
        tampered.push_back(0);
        writeAll(base / "t4.zgame", tampered);
        check(!extractPackage(base / "t4.zgame", base / "out_t4", good, nullptr, &error), "detecta datos añadidos al final");

        writeAll(base / "t5.zgame", {'n', 'o', ' ', 'e', 's'});
        check(!readHeader(base / "t5.zgame", header, &error), "rechaza un archivo que no es un paquete");
    }

    {
        WriteOptions options = fast(Protection::KeyFile);
        options.key          = generateKey();
        const fs::path pkg   = base / "k.zgame";
        check(writePackage(files, pkg, info, options, &error), "se escribe un paquete con archivo de clave");

        OpenCredentials good;
        good.key = options.key;
        check(extractPackage(pkg, base / "k_ok", good, nullptr, &error) && sameTree(base / "k_ok"), "se extrae con la clave correcta");

        OpenCredentials wrong;
        wrong.key = generateKey();
        check(!extractPackage(pkg, base / "k_bad", wrong, nullptr, &error), "falla con otra clave");
        check(!extractPackage(pkg, base / "k_none", OpenCredentials{}, nullptr, &error), "falla sin clave");

        const std::string text = keyToText(options.key);
        const auto parsed      = keyFromText(text + "\n");
        check(text.size() == 64 && parsed && *parsed == options.key, "la clave se guarda y se lee como texto");
        check(!keyFromText("zz") && !keyFromText(std::string(64, 'g')), "se rechazan claves mal escritas");
    }

    {
        const fs::path pkg = base / "e.zgame";
        check(writePackage(files, pkg, info, fast(Protection::Embedded), &error), "se escribe un paquete sin contraseña");
        check(extractPackage(pkg, base / "e_ok", OpenCredentials{}, nullptr, &error) && sameTree(base / "e_ok"),
              "se extrae sin credenciales");
    }

    {
        WriteOptions options = fast(Protection::Password);
        options.password     = "firma";
        options.signer       = generateSigningKey();
        const fs::path pkg   = base / "s.zgame";
        check(writePackage(files, pkg, info, options, &error), "se escribe un paquete firmado");

        PackageHeader header;
        check(readHeader(pkg, header, &error) && header.signedPackage && header.publicKey == options.signer->publicKey,
              "la cabecera trae la clave pública del autor");
        check(fingerprint(header.publicKey).size() == 39, "la huella tiene formato legible");
        check(verifySignature(pkg, &error), "la firma es válida");

        OpenCredentials good;
        good.password = "firma";
        check(extractPackage(pkg, base / "s_ok", good, nullptr, &error) && sameTree(base / "s_ok"), "se extrae un paquete firmado");

        auto bytes = readAll(pkg);
        bytes[bytes.size() - 10] ^= 0x01;
        writeAll(base / "s_t1.zgame", bytes);
        check(!verifySignature(base / "s_t1.zgame", &error), "detecta una firma alterada");
        check(!extractPackage(base / "s_t1.zgame", base / "s_out1", good, nullptr, &error), "no extrae si la firma no coincide");

        const SigningKey again = signingKeyFromSeed(Key32{1, 2, 3});
        const SigningKey same  = signingKeyFromSeed(Key32{1, 2, 3});
        check(again.publicKey == same.publicKey && fingerprint(again.publicKey) == fingerprint(same.publicKey),
              "la misma semilla da la misma identidad");
    }

    check(isSafeArchivePath("assets/models/a.glb") && isSafeArchivePath("project.zio"), "rutas normales permitidas");
    check(!isSafeArchivePath("../fuera.txt") && !isSafeArchivePath("a/../../b") && !isSafeArchivePath("/etc/passwd") &&
              !isSafeArchivePath("C:/Windows/x") && !isSafeArchivePath("a\\b") && !isSafeArchivePath("") && !isSafeArchivePath("a//b") &&
              !isSafeArchivePath("nul.txt") && !isSafeArchivePath("a/con") && !isSafeArchivePath("espacio ") && !isSafeArchivePath("x/"),
          "rutas peligrosas rechazadas");
    {
        std::vector<FileEntry> evil = {{source / "project.zio", "../escape.bin"}};
        WriteOptions options        = fast(Protection::Embedded);
        check(!writePackage(evil, base / "evil.zgame", info, options, &error), "no se empaqueta una ruta con ..");
        std::vector<FileEntry> duplicated = {{source / "project.zio", "a.bin"}, {source / "project.zio", "A.bin"}};
        check(!writePackage(duplicated, base / "dup.zgame", info, options, &error), "no se empaquetan nombres repetidos");
        WriteOptions noPassword = fast(Protection::Password);
        check(!writePackage(files, base / "np.zgame", info, noPassword, &error), "no se acepta una contraseña vacía");
    }

    {
        std::vector<std::uint8_t> text;
        const std::string line = "{\"type\":\"Mesh\",\"position\":[1.0,2.0,3.0],\"visible\":true}\n";
        while (text.size() < 3'000'000)
            text.insert(text.end(), line.begin(), line.end());
        writeAll(source / "assets" / "repetido.json", text);
        std::vector<std::uint8_t> mixed = randomData(200'000, 9);
        mixed.insert(mixed.end(), text.begin(), text.begin() + 400'000);
        writeAll(source / "assets" / "mixto.bin", mixed);

        std::vector<FileEntry> packed = {{source / "assets" / "repetido.json", "assets/repetido.json"},
                                         {source / "assets" / "mixto.bin", "assets/mixto.bin"},
                                         {source / "assets" / "vacio.bin", "assets/vacio.bin"}};
        WriteOptions options = fast(Protection::Password);
        options.password     = "comprimir paquetes";
        const fs::path pkg   = base / "comprimido.zgame";
        check(writePackage(packed, pkg, info, options, &error), "se escribe un paquete con archivos comprimibles");
        const auto packedSize = fs::file_size(pkg);
        check(packedSize < 1'000'000, "los datos repetidos ocupan mucho menos dentro del paquete");
        OpenCredentials good;
        good.password = "comprimir paquetes";
        check(extractPackage(pkg, base / "out_zip", good, nullptr, &error), "se extrae un paquete comprimido");
        check(readAll(source / "assets" / "repetido.json") == readAll(base / "out_zip" / "assets" / "repetido.json") &&
                  readAll(source / "assets" / "mixto.bin") == readAll(base / "out_zip" / "assets" / "mixto.bin") &&
                  fs::exists(base / "out_zip" / "assets" / "vacio.bin"),
              "los archivos comprimidos se recuperan idénticos");
    }

    {
        const std::string first  = newPackageId();
        const std::string second = newPackageId();
        check(first.size() == 36 && first != second, "los identificadores son únicos");
    }

    fs::remove_all(base);
    std::printf("%s\n", failures == 0 ? "TODO OK" : "HAY FALLOS");
    return failures == 0 ? 0 : 1;
}
