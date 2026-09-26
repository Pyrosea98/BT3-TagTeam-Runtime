#pragma once

#include <cstdio>

// 64-bit file positioning for the installer's readers.
//
// std::fseek and std::ftell take a 32-bit long on Windows (LLP64), so anything past 2 GB either
// fails to seek or reports a negative size. That is not hypothetical here: PZS3US2.ALG sits at
// byte 2,686,441,232 of a BT3 ISO, and it is the first file in the image to cross the line, which
// is why extraction died on it and on nothing before it.
namespace feio
{
#if defined(_WIN32)
    inline int seek(std::FILE *f, long long offset, int whence)
    {
        return _fseeki64(f, offset, whence);
    }

    inline long long tell(std::FILE *f)
    {
        return _ftelli64(f);
    }
#else
    inline int seek(std::FILE *f, long long offset, int whence)
    {
        return ::fseeko(f, static_cast<off_t>(offset), whence);
    }

    inline long long tell(std::FILE *f)
    {
        return ::ftello(f);
    }
#endif

    // File length through a 64-bit tell. False when the handle is not seekable.
    inline bool size(std::FILE *f, long long *out)
    {
        const long long here = tell(f);
        if (here < 0)
            return false;
        if (seek(f, 0, SEEK_END) != 0)
            return false;
        const long long end = tell(f);
        if (seek(f, here, SEEK_SET) != 0)
            return false;
        if (end < 0)
            return false;
        if (out)
            *out = end;
        return true;
    }
}
