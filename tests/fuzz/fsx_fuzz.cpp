#include "fsx.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    if (size > 4096)
        return 0;
    // mkdtemp creates an owned directory; the extractor never creates links.
    char pattern[] = "/tmp/toolx-fsx-fuzz-XXXXXX";
    const char* created = mkdtemp(pattern);
    if (!created)
        std::abort();
    const std::filesystem::path root(created);
    const auto archive = root / "input.tar";
    {
        std::ofstream out(archive, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    (void)fsx::ExtractArchive(archive.string(), (root / "output").string());
    std::filesystem::remove_all(root);
    return 0;
}
