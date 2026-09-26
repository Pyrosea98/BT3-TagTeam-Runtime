#include "frontend/fe_hash.h"

#include <cstdio>
#include <vector>

// FFmpeg's public headers carry no extern "C" guard of their own, so the symbols come out
// C++-mangled and the import libs cannot resolve them (same wrap as ps2_fmv_override.cpp).
extern "C" {
#include <libavutil/mem.h>
#include <libavutil/sha.h>
}

namespace fe
{
    Sha256::Sha256()
    {
        struct AVSHA *ctx = av_sha_alloc();
        if (ctx && av_sha_init(ctx, 256) == 0)
        {
            m_ctx = ctx;
            m_ok = true;
        }
        else if (ctx)
        {
            av_free(ctx);
        }
    }

    Sha256::~Sha256()
    {
        if (m_ctx)
            av_free(m_ctx);
    }

    void Sha256::update(const char *data, std::size_t size)
    {
        if (m_ok)
            av_sha_update((struct AVSHA *)m_ctx, (const uint8_t *)data, size);
    }

    std::string Sha256::finalHex()
    {
        if (!m_ok)
            return {};
        unsigned char digest[32];
        av_sha_final((struct AVSHA *)m_ctx, digest);
        m_ok = false;
        m_ctx = nullptr;

        char hex[65];
        for (int i = 0; i < 32; ++i)
            std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
        return hex;
    }

    std::string sha256Hex(const std::filesystem::path &file, bool &ok)
    {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file, ec))
        {
            ok = false;
            return {};
        }
        std::FILE *f = std::fopen(file.string().c_str(), "rb");
        if (!f)
        {
            ok = false;
            return {};
        }

        Sha256 hash;
        std::vector<char> buf(256 * 1024);
        for (;;)
        {
            const std::size_t n = std::fread(buf.data(), 1, buf.size(), f);
            if (n == 0)
                break;
            hash.update(buf.data(), n);
        }
        std::fclose(f);

        ok = hash.valid();
        return hash.finalHex();
    }
}
