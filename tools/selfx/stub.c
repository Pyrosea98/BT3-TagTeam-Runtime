/*
 * Self-extracting stub for the BT3-Recomp Linux release.
 *
 * Layout of the final executable (assembled by the packaging scripts):
 *     [stub ELF][zstd-compressed ustar payload][footer]
 *   footer = "BT3SELFX" + uint64le(len(stub)) + uint64le(len(payload)) + uint64le(seed)
 *   seed  = first 16 hex chars of the payload sha256 (changes => re-extract)
 *
 * Two payload layouts:
 *
 * 1) game mode  -- payload = "ps2EntryRunner" + "lib/...". Extract to
 *    /tmp/bt3-sel-<seed>, then exec the runner. savedata/, assets/ and data/
 *    stay NEXT to this executable (PS2X_EXEDIR), written by the old launcher.
 *
 * 2) front-end mode -- payload = "ps2EntryRunner" + "lib/..." + "assets/...".
 *    Extract the whole run tree to a persistent, writable per-user cache and
 *    exec the runner from there: the front-end lives inside it and shows up
 *    when it gets no boot ELF on argv. The runner resolves everything from
 *    applicationDirPath()/PS2X_EXEDIR, so a read-only mount would break
 *    install/settings; the XDG cache is the writable run root. data/ and
 *    savedata/ are symlinked to a stable per-user dir so an update (new seed)
 *    keeps the installed game data.
 *
 * Usage: ./<self-extract>            (game mode boots the game)
 *        ./<self-extract> --dir-echo (print the run dir and exit, for tests)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>

#include "zstd.h"

#define MAGIC "BT3SELFX"
#define FOOTER_LEN (8 + 3 * 8)

typedef struct
{
    uint64_t stubLen;
    uint64_t payloadLen;
    uint64_t seed;
} Footer;

typedef struct
{
    uint8_t *data;
    size_t   len;
    size_t   cap;
} Buffer;

static void die(const char *msg)
{
    fprintf(stderr, "BT3 sel-extractor: %s\n", msg);
    _exit(1);
}

static void buf_reserve(Buffer *b, size_t need)
{
    if (need <= b->cap)
        return;
    size_t cap = b->cap ? b->cap : (1u << 20);
    while (cap < need)
        cap <<= 1;
    void *p = realloc(b->data, cap);
    if (!p)
        die("out of memory");
    b->data = (uint8_t *)p;
    b->cap = cap;
}

static int rd_u64le(const uint8_t *p, uint64_t *out)
{
    *out = 0;
    for (int i = 7; i >= 0; --i)
        *out = (*out << 8) | p[i];
    return 0;
}

/* Decompress the whole zstd payload into an in-memory buffer. */
static Buffer decompress(const uint8_t *src, size_t srcLen)
{
    Buffer out = {0};
    ZSTD_DStream *ds = ZSTD_createDStream();
    if (!ds)
        die("ZSTD_createDStream failed");
    if (ZSTD_initDStream(ds) == 0)
        die("ZSTD_initDStream failed");

    ZSTD_inBuffer in = {src, srcLen, 0};
    size_t lastRet = 0;
    for (;;)
    {
        buf_reserve(&out, out.len + ZSTD_DStreamOutSize());
        ZSTD_outBuffer ob = {out.data + out.len, ZSTD_DStreamOutSize(), 0};
        size_t ret = ZSTD_decompressStream(ds, &ob, &in);
        if (ZSTD_isError(ret))
            die(ZSTD_getErrorName(ret));
        out.len += ob.pos;
        lastRet = ret;
        if (ret == 0 || in.pos == in.size)
            break;
    }
    if (lastRet != 0 && in.pos == in.size)
        die("truncated zstd stream");
    ZSTD_freeDStream(ds);
    return out;
}

static void extract_tar(const Buffer *tar, const char *dir)
{
    size_t pos = 0;
    char name[256];
    while (pos + 512 <= tar->len)
    {
        const uint8_t *h = tar->data + pos;
        if (h[0] == 0) /* end-of-archive */
            break;

        char sizeBuf[13] = {0};
        size_t nameLen = h[124];
        if (nameLen > 99)
            nameLen = 99;
        memcpy(name, h, nameLen);
        name[nameLen] = 0;
        memcpy(sizeBuf, h + 124, 12);
        unsigned long size = strtoul(sizeBuf, NULL, 8);
        char modeBuf[9] = {0};
        memcpy(modeBuf, h + 100, 8);
        unsigned long mode = strtoul(modeBuf, NULL, 8) & 07777;
        char typeflag = h[156];

        pos += 512;
        size_t dataLen = ((size + 511) / 512) * 512;
        if (pos + dataLen > tar->len)
            die("truncated tar data");

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, name);

        if (typeflag == '5')
        {
            mkdir(path, 0755);
        }
        else if (typeflag == '0' || typeflag == '7' || typeflag == 0)
        {
            char dirBuf[512];
            snprintf(dirBuf, sizeof(dirBuf), "%s", path);
            char *slash = strrchr(dirBuf, '/');
            if (slash && slash != dirBuf)
            {
                *slash = 0;
                mkdir(dirBuf, 0755);
            }
            FILE *f = fopen(path, "wb");
            if (!f)
                die("cannot create extracted file");
            fwrite(tar->data + pos, 1, size, f);
            fclose(f);
            if (mode)
                chmod(path, mode);
            else
                chmod(path, typeflag == '7' ? 0755 : 0644);
        }
        /* skip pax/global headers and unknown -> just advance */
        pos += dataLen;
    }
}

static void mkdir_p(const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* Recursively remove a directory tree (only used on our own cache dirs). */
static void rm_rf(const char *path)
{
    struct dirent *e;
    DIR *d = opendir(path);
    if (!d)
    {
        remove(path);
        return;
    }
    while ((e = readdir(d)) != NULL)
    {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char child[1024];
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        rm_rf(child);
    }
    closedir(d);
    rmdir(path);
}

static void symlink_or_copy(const char *target, const char *link)
{
    struct stat st;
    if (lstat(link, &st) == 0)
    {
        /* The tree copy lands a real savedata/ dir (already has the placeholder
         * subdir); remove() only handles empty dirs, so wipe any leftover tree. */
        if (S_ISDIR(st.st_mode))
            rm_rf(link);
        else
            remove(link);
    }
    if (symlink(target, link) != 0)
        die("cannot create stable-data symlink");
}

static void copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in)
        return;
    FILE *out = fopen(dst, "wb");
    if (!out)
    {
        fclose(in);
        return;
    }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
    {
        if (fwrite(buf, 1, n, out) != n)
            break;
    }
    fclose(out);
    fclose(in);
    chmod(dst, 0644);
}

static const char *seed_dir(char *buf, size_t len, uint64_t seed)
{
    /* Persistent, writable, per-user run root: $XDG_DATA_HOME/bt3-recomp/<seed>. */
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg)
    {
        snprintf(buf, len, "%s/bt3-recomp-%08llx", xdg, (unsigned long long)seed);
    }
    else if (home && *home)
    {
        snprintf(buf, len, "%s/.local/share/bt3-recomp-%08llx", home, (unsigned long long)seed);
    }
    else
    {
        snprintf(buf, len, "/tmp/bt3-recomp-%08llx", (unsigned long long)seed);
    }
    return buf;
}

int main(int argc, char **argv)
{
    (void)argc;

    char own[4096];
    ssize_t ownLen = readlink("/proc/self/exe", own, sizeof(own) - 1);
    if (ownLen <= 0)
        die("cannot locate own executable");
    own[ownLen] = 0;

    FILE *f = fopen(own, "rb");
    if (!f)
        die("cannot open own executable");
    fseek(f, 0, SEEK_END);
    long fileLen = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fileLen <= 0 || fileLen <= (long)FOOTER_LEN)
        die("file too small");

    uint8_t footer[FOOTER_LEN];
    fseek(f, fileLen - FOOTER_LEN, SEEK_SET);
    if (fread(footer, 1, FOOTER_LEN, f) != FOOTER_LEN)
        die("cannot read footer");

    Footer ft;
    if (memcmp(footer, MAGIC, 8) != 0)
        die("not a BT3SELFX self-extracting binary");
    rd_u64le(footer + 8, &ft.stubLen);
    rd_u64le(footer + 16, &ft.payloadLen);
    rd_u64le(footer + 24, &ft.seed);
    if (ft.stubLen + ft.payloadLen + FOOTER_LEN != (uint64_t)fileLen)
        die("size mismatch in footer");

    uint8_t *payload = malloc(ft.payloadLen);
    if (!payload)
        die("out of memory");
    fseek(f, (long)ft.stubLen, SEEK_SET);
    if (fread(payload, 1, ft.payloadLen, f) != ft.payloadLen)
        die("cannot read payload");
    fclose(f);

    Buffer tar = decompress(payload, ft.payloadLen);
    free(payload);

    char dir[1024];
    seed_dir(dir, sizeof(dir), ft.seed);

    /* Fresh extraction for this seed. */
    rm_rf(dir);
    mkdir_p(dir);

    extract_tar(&tar, dir);
    free(tar.data);

    /* The front-end lives in the runner, so the only difference between the two payloads is
     * whether assets/ came along: with it, boot with no argv (the front-end opens); without it,
     * the caller passes the boot ELF. The CWD is not dir yet, so probe it absolutely. */
    char assetsProbe[1024];
    snprintf(assetsProbe, sizeof(assetsProbe), "%s/assets", dir);
    int frontEndMode = (access(assetsProbe, F_OK) == 0);

    if (frontEndMode)
    {
        /* Stable per-user dirs (NO seed) for installed game data / settings, so
         * an update keeps them. Base = <XDG_DATA_HOME|~/.local/share>. */
        char compatBase[1024];
        snprintf(compatBase, sizeof(compatBase), "%s", dir);
        char *sep = strstr(compatBase, "/bt3-recomp-");
        if (sep)
            *sep = 0;
        char compat[1024];
        snprintf(compat, sizeof(compat), "%s/bt3-data", compatBase);
        mkdir_p(compat);
        char compatData[1024], compatSave[1024];
        snprintf(compatData, sizeof(compatData), "%s/data", compat);
        snprintf(compatSave, sizeof(compatSave), "%s/savedata", compat);
        mkdir_p(compatData);
        mkdir_p(compatSave);

        /* The front-end writes savedata/ + data/ next to the runner; make it
         * land in the stable dir so updates (new seed) keep the user data. */
        char dataLink[1024], saveLink[1024];
        snprintf(dataLink, sizeof(dataLink), "%s/data", dir);
        snprintf(saveLink, sizeof(saveLink), "%s/savedata", dir);
        symlink_or_copy(compatData, dataLink);
        symlink_or_copy(compatSave, saveLink);

        /* Desktop integration: a stable wrapper + .desktop entry so the game
         * appears in the app menu with its icon, regardless of the seed dir. */
        char compatApps[1024], compatIcons[1024];
        snprintf(compatApps, sizeof(compatApps), "%s/applications", compatBase);
        snprintf(compatIcons, sizeof(compatIcons), "%s/icons", compatBase);
        mkdir_p(compatApps);
        mkdir_p(compatIcons);

        char wrapper[1024];
        snprintf(wrapper, sizeof(wrapper), "%s/bt3-recomp.sh", compatBase);
        FILE *w = fopen(wrapper, "w");
        if (w)
        {
            fprintf(w,
                    "#!/usr/bin/env bash\n"
                    "set -euo pipefail\n"
                    "cd '%s'\n"
                    "export LD_LIBRARY_PATH=\"$PWD/lib\"\n"
                    "export PS2X_EXEDIR=\"$PWD\"\n"
                    "exec ./ps2EntryRunner \"$@\"\n",
                    dir);
            fclose(w);
            chmod(wrapper, 0755);
        }

        char iconFrom[1024], iconTo[1024];
        snprintf(iconFrom, sizeof(iconFrom), "%s/assets/icon.png", dir);
        snprintf(iconTo, sizeof(iconTo), "%s/bt3.png", compatIcons);
        copy_file(iconFrom, iconTo);

        char desk[1024];
        snprintf(desk, sizeof(desk), "%s/Dragon-Ball-Budokai-Tenkaichi-3.desktop", compatApps);
        FILE *d = fopen(desk, "w");
        if (d)
        {
            fprintf(d,
                    "[Desktop Entry]\n"
                    "Type=Application\n"
                    "Version=1.0\n"
                    "Name=Dragon Ball Budokai Tenkaichi 3\n"
                    "Comment=Play Dragon Ball Budokai Tenkaichi 3 (recompiled)\n"
                    "Exec=%s\n"
                    "Icon=%s\n"
                    "Terminal=false\n"
                    "Categories=Game;\n",
                    wrapper, iconTo);
            fclose(d);
        }
    }

    /* Own directory: game mode uses it for the portable savedata/data/assets. */
    char launchDir[4096];
    snprintf(launchDir, sizeof(launchDir), "%s", own);
    char *slash = strrchr(launchDir, '/');
    if (slash && slash != launchDir)
        *slash = 0;
    else if (slash == launchDir)
        launchDir[1] = 0;

    setenv("PS2X_EXEDIR", dir, 1);
    chdir(dir);
    setenv("LD_LIBRARY_PATH", "lib", 1);

    if (frontEndMode)
    {
        /* No boot ELF on argv: the runner opens its own front-end. */
        if (access("./ps2EntryRunner", X_OK) != 0)
        {
            snprintf(launchDir, sizeof(launchDir), "run dir: %s", dir);
            die(launchDir);
        }
        char *frontArgv[] = {"./ps2EntryRunner", NULL};
        execv("./ps2EntryRunner", frontArgv);
        perror("execv ps2EntryRunner");
        return 1;
    }

    /* The runner needs the guest boot ELF as argv[1]. */
    char bootElf[4096];
    snprintf(bootElf, sizeof(bootElf), "%s/data/SLUS_216.78", launchDir);
    char *runnerArgv[] = {"./ps2EntryRunner", bootElf, NULL};
    execv("./ps2EntryRunner", runnerArgv);
    perror("execv");
    return 1;
}