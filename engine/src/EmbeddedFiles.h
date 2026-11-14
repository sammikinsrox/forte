#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace forte::detail
{
/** A data file built into the engine (SPEC §3.5). */
struct EmbeddedFile
{
    const char* name;
    const unsigned char* data;
    std::size_t size;
};

/** Every embedded file. Generated at build time by tools/embed (engine/CMakeLists.txt lists the
    files). */
[[nodiscard]] std::span<const EmbeddedFile> embeddedFiles() noexcept;

/** The embedded file called name, or nullptr. */
[[nodiscard]] inline const EmbeddedFile* findEmbeddedFile (std::string_view name) noexcept
{
    for (const auto& file : embeddedFiles())
        if (name == file.name)
            return &file;

    return nullptr;
}
} // namespace forte::detail
