#include "frontend/fe_background.h"

#include <SDL_opengl.h>

#include <cstdio>
#include <vector>

// bt3gl's rtextures.c compiles its own stb_image with external linkage, so a second external
// copy here is a duplicate-symbol link error (GNU ld "multiple definition of `stbi_*'"; MSVC
// would be LNK2005). Keep ours file-local, same as gfx/image_io.cpp.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "external/stb_image.h"

namespace frontend
{
    std::uint32_t loadBackground(const std::filesystem::path &png, int *outW, int *outH)
    {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(png, ec))
        {
            std::fprintf(stderr, "[fe] no menu background at %s\n", png.string().c_str());
            return 0;
        }

        std::vector<unsigned char> bytes;
        const auto size = std::filesystem::file_size(png, ec);
        if (ec || size == 0 || size > (std::uintmax_t)256 * 1024 * 1024)
        {
            std::fprintf(stderr, "[fe] menu background has a bad size (%s)\n", ec ? ec.message().c_str() : "empty");
            return 0;
        }
        bytes.resize((std::size_t)size);

        // stdio is off in stb_image, so the file is read here and handed over as memory.
        std::FILE *f = std::fopen(png.string().c_str(), "rb");
        if (!f)
        {
            std::fprintf(stderr, "[fe] cannot open menu background %s\n", png.string().c_str());
            return 0;
        }
        const std::size_t got = std::fread(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
        if (got != bytes.size())
        {
            std::fprintf(stderr, "[fe] short read on menu background\n");
            return 0;
        }

        int w = 0, h = 0, comp = 0;
        unsigned char *px = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
        if (!px)
        {
            std::fprintf(stderr, "[fe] menu background decode failed: %s\n", stbi_failure_reason());
            return 0;
        }

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glBindTexture(GL_TEXTURE_2D, 0);
        stbi_image_free(px);

        std::fprintf(stderr, "[fe] menu background %dx%d (tex %u)\n", w, h, (unsigned)tex);
        if (outW) *outW = w;
        if (outH) *outH = h;
        return (std::uint32_t)tex;
    }

    void freeBackground(std::uint32_t tex)
    {
        if (!tex)
            return;
        GLuint t = (GLuint)tex;
        glDeleteTextures(1, &t);
    }
}
