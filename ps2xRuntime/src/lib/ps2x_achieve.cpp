// [ach] RetroAchievements, evaluated locally against the recomp's guest RAM.
//
// WHY OFFLINE. The 154 achievements are defined by RetroAchievements and the definitions are
// public data, but nothing here talks to their server. The patch is a file the build generates,
// progress is a file in savedata/, and there is no login, no token, and no request that can be
// refused. That is a deliberate choice rather than a missing feature: the server's own warning for
// an unrecognised client is "the server does not recognize this client and will not allow hardcore
// unlocks", and whether it accepts the softcore half of that is not something to discover by
// pointing someone's profile at a build whose memory addresses are still unproven.
//
// WHAT IS PROVEN AND WHAT IS NOT. The conditions in the patch use OUR addresses, not the ones the
// achievements were written against. That translation is done by scripts/gen_ach_patch.py, which
// only emits an achievement when every address it touches falls inside a region we have confirmed,
// and it reports the rest as held back. So the engine never evaluates a condition whose meaning is
// a guess. Growing the set is a matter of adding a region to that script's VERIFIED list -- no C++
// here changes, because the engine has no idea an address could be wrong.
//
// THE MEMORY CALLBACK IS THE WHOLE CONTRACT. rcheevos calls read_memory for every operand, once
// per do_frame, on the same guest RAM the rest of the runtime reads. There is no emulation, no
// snapshot and no rollback: the game is single-player and deterministic, so a value read this frame
// is the value the condition sees this frame.

#include "runtime/ps2x_achieve.h"
#include "runtime/ps2_memory.h"       // PS2_RAM_SIZE / PS2_RAM_MASK
#include "runtime/ps2x_notify.h"      // [notify] the popup card and its sound
#include "runtime/ps2x_settings.h"

#include <rc_client.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <vector>

extern "C" const char *ps2xExeDirC();   // main.cpp: <exeDir> (honors PS2X_EXEDIR)

namespace {

constexpr const char *kPatchFile = "assets/bt3-achievements.patch.json";
constexpr const char *kSetsFile = "assets/bt3-achievements.sets.json";
constexpr const char *kProgressFile = "savedata/achievements.progress";
constexpr int kConsolePs2 = 21;         // RC_CONSOLE_PLAYSTATION_2

// The progress helpers, defined below the callbacks that use them. loadProgress() in particular is
// called from achLoadCallback, and it has to run after the patch is in the client, so it cannot be
// inlined at the point of use.
void saveProgress();
void loadProgress();
// The unlock ledger's writer, called from the event handler. Declared here for the same reason;
// the explanatory comment for all three is on loadUnlocked() below.
void appendUnlocked(uint32_t id);

// One row for the overlay. Copied out of rcheevos at load so the UI never holds a pointer into the
// client's structures, which are rebuilt when the patch reloads.
struct Row
{
    std::string title;
    std::string description;
    int points = 0;
    bool unlocked = false;
    bool addrVerified = true;   // false = still pointing at PCSX2's address, reads junk here
    uint32_t id = 0;   // the patch is in ID order; the bucket list is not
};

rc_client_t *s_client = nullptr;
std::string  s_patch;              // the r=patch response body, served verbatim
std::string  s_sets;               // the r=achievementsets response body, likewise
std::string  s_progressPath;
std::string  s_unlockedPath;
std::string  s_verifiedPath;
std::set<uint32_t> s_unlocked;      // the ledger; see loadUnlocked() for why this is ours and not
                                    // rcheevos'
std::set<uint32_t> s_verified;      // ids whose addresses are TRANSLATED to ours; see loadVerified()
std::vector<Row> s_rows;
std::mutex  s_mutex;               // the event handler runs on the client's thread, the UI on ours

// Published by ps2AchFrame for the duration of the call. rcheevos may also read from inside
// rc_client_idle(), which is pumped from the same place, so this is the only place the pointer
// lives and it is never stored anywhere else.
uint8_t    *s_rdram = nullptr;

std::atomic<bool> s_enabled{false};
std::atomic<bool> s_loaded{false};
bool s_wantEnabled = false;         // requested before init ran
bool s_listed = false;              // the list snapshot is taken once per load, not per frame
bool s_trace = false;               // PS2X_ACH_TRACE=1: log every read the engine makes
bool s_prepared = false;            // ps2AchInit resolved the files; the client is made later
uint32_t s_idleTick = 0;            // frames since the last rc_client_idle()

// ---------------------------------------------------------------------------------------------
// rcheevos callbacks
// ---------------------------------------------------------------------------------------------

extern "C" uint32_t RC_CCONV achReadMemory(uint32_t address, uint8_t *buffer, uint32_t num_bytes,
                                           rc_client_t *)
{
    if (!s_rdram || !buffer)
        return 0;

    // A PS2 address is masked into the 32 MB of RDRAM, but rcheevos computes a read length from
    // the value it expects, not from how much room is left. Clamp instead of trusting it: a read
    // that ran off the end would fault, and the fault would look like a bad achievement condition
    // instead of a bad address.
    const uint32_t off = address & PS2_RAM_MASK;
    if (off >= PS2_RAM_SIZE)
        return 0;
    uint32_t n = num_bytes;
    if (n > PS2_RAM_SIZE - off)
        n = PS2_RAM_SIZE - off;

    std::memcpy(buffer, s_rdram + off, n);

    // PS2X_ACH_TRACE=1 logs every read. rcheevos reports an achievement as earned or not and
    // nothing in between, so when a condition that should hold does not, the only way to tell a
    // wrong address from a wrong comparison is to see what it actually read. Verbose by design and
    // off unless asked for; it is the tool for confirming a translated address, not for running.
    if (s_trace)
    {
        char line[96];
        int k = std::snprintf(line, sizeof line, "[ach:trace] %08x ->", off);
        for (uint32_t i = 0; i < n && i < 4; ++i)
            k += std::snprintf(line + k, sizeof line - static_cast<size_t>(k), " %02x", buffer[i]);
        std::fprintf(stderr, "%s\n", line);
    }
    return n;
}

// rcheevos' load path is a four-step conversation with a server: log in, resolve the disc hash to
// a game id, list the user's achievement sets, then fetch the game data. All four are answered
// here, out of memory, so the machine never opens a socket.
//
// The login is not a real account and does not pretend to be one. rcheevos will not fetch game data
// until its user state is LOGGED_IN, so the local identity exists to satisfy that state machine, and
// it is a fixed string with no credentials behind it. There is nothing to leak because there is
// nothing to authenticate: progress is a file on this disk, not an account on someone else's.
extern "C" void RC_CCONV achServerCall(const rc_api_request_t *request,
                                       rc_client_server_callback_t callback, void *callback_data,
                                       rc_client_t *)
{
    static const std::string kIdentity =
        R"({"Success":true,"User":"local","Token":"offline","Score":0,"SoftcoreScore":0})";
    static const std::string kGameId = R"({"Success":true,"GameID":0})";
    // r=startsession. rcheevos pings this when it thinks a play session has begun, and the answer
    // carries the unlocks the server would have accepted -- there are none, because nothing is
    // unlocked anywhere but this disk. ServerNow is the local clock: it is only consulted for
    // hardcore timing, which is off.
    static const std::string kSession =
        R"({"Success":true,"Unlocks":[],"HardcoreUnlocks":[]})";
    static const std::string kRefused = R"({"Success":false,"Error":"offline build"})";

    rc_api_server_response_t response{};
    const char *body = nullptr;
    size_t len = 0;

    const char *post = (request && request->post_data) ? request->post_data : "";
    if (!s_patch.empty() && std::strstr(post, "r=patch"))
    {
        body = s_patch.c_str();
        len = s_patch.size();
    }
    else if (!s_sets.empty() && std::strstr(post, "r=achievementsets"))
    {
        // The sets response is a different shape from the patch -- GameId rather than ID, no
        // Achievements array -- and the generator writes it out ready to serve, so the runtime
        // never has to rename keys.
        body = s_sets.c_str();
        len = s_sets.size();
    }
    else if (std::strstr(post, "r=gameid"))
    {
        // The hash is never checked against anything. GameID 0 is the honest answer: the lookup is
        // satisfied locally and the real data comes from r=patch, so there is no id to report.
        body = kGameId.c_str();
        len = std::strlen(body);
    }
    else if (std::strstr(post, "r=login"))
    {
        body = kIdentity.c_str();
        len = kIdentity.size();
    }
    else if (std::strstr(post, "r=startsession"))
    {
        body = kSession.c_str();
        len = std::strlen(body);
    }
    else if (std::strstr(post, "r=ping"))
    {
        // The keepalive. rcheevos sends it on a timer and treats a failure as the server having gone
        // away, so refusing it is not harmless silence: the client logs an error every interval and
        // sits in a reconnect state it can never leave. A success with no fields is enough -- the
        // response is read for Success alone, and the game hash it echoes back is already known.
        static const std::string kPong = R"({"Success":true})";
        body = kPong.c_str();
        len = kPong.size();
    }
    else
    {
        // Anything else -- an unlock, a score submit, a progress ping -- is a request this build
        // has no business making. Refusing rather than staying silent is deliberate: silence looks
        // like a network stall, rcheevos retries it, and one missing feature becomes a retry loop.
        //
        // The name of the request goes to the log, once. If the load ever fails with "offline
        // build" the line above it is the answer, and having to add a print to find that out is
        // exactly the kind of round trip this module should not need.
        const char *r = std::strstr(post, "r=");
        if (r)
        {
            char what[64];
            std::size_t n = 0;
            for (const char *q = r + 2; *q && *q != '&' && n + 1 < sizeof what; ++q)
                what[n++] = *q;
            what[n] = '\0';
            std::fprintf(stderr, "[ach] refused an unexpected request: r=%s\n", what);
        }
        body = kRefused.c_str();
        len = std::strlen(body);
    }

    response.body = body;
    response.body_length = len;
    response.http_status_code = 200;
    if (callback)
        callback(&response, callback_data);
}

extern "C" void RC_CCONV achEventHandler(const rc_client_event_t *event, rc_client_t *)
{
    if (!event)
        return;

    if (event->type == RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED && event->achievement)
    {
        const rc_client_achievement_t *a = event->achievement;
        std::lock_guard<std::mutex> lock(s_mutex);

        // rcheevos re-raises this event for an achievement it has no record of having earned, which
        // is every one of them after a restart (see loadUnlocked). The ledger is what tells the two
        // apart, so it is consulted before anything is announced. The check is inline rather than a
        // helper because the lock is already held here and std::mutex is not recursive.
        const bool fresh = s_unlocked.count(a->id) == 0;

        // Mark our copy of the row too. s_rows is a snapshot taken when the patch loaded, so without
        // this the counts and the list would keep reporting an achievement as locked after the
        // toast that announced it -- and the progress file, written from the client's own state,
        // would disagree with what the overlay shows for the rest of the session.
        for (auto &row : s_rows)
        {
            if (row.id == a->id)
            {
                row.unlocked = true;
                break;
            }
        }

        if (fresh)
        {
            s_unlocked.insert(a->id);
            appendUnlocked(a->id);
            // The card and the sound are the notification bus's business, not ours. The title goes
            // across with the points appended, because "I. Am. Powerful." on its own does not say
            // what just happened any better than the log line does -- the number does.
            char body[320];
            std::snprintf(body, sizeof body, "%s  --  %d pts", a->description ? a->description : "",
                          static_cast<int>(a->points));
            const NotifyEvent ev{NotifyKind::Achievement, NotifyTone::Great, a->title, body};
            ps2xNotifyPush(ev);
            // Checkpoint rcheevos' partial progress here rather than only at shutdown. It used to be
            // written from the toast consumer, which no longer exists, and an unlock is the one
            // moment where there is definitely something new worth keeping -- a session that ends
            // badly should not also lose the halfway state of everything else.
            saveProgress();
            std::fprintf(stderr, "[ach] unlocked %u \"%s\" (%d pts)\n", a->id, a->title,
                         a->points);
        }
    }
    else if (event->type == RC_CLIENT_EVENT_GAME_COMPLETED)
    {
        std::fprintf(stderr, "[ach] all achievements in the patch earned\n");
    }
}

extern "C" void RC_CCONV achLoginCallback(int result, const char *error_message, rc_client_t *,
                                          void *)
{
    if (result != RC_OK)
        std::fprintf(stderr, "[ach] local login failed (%d): %s\n", result,
                     error_message ? error_message : "?");
    // Success is the expected path and prints nothing: it is a fixed string being handed back to
    // ourselves, and a line in the log every launch would train the reader to ignore.
}

extern "C" void RC_CCONV achLoadCallback(int, const char *error_message, rc_client_t *, void *)
{
    if (error_message && error_message[0])
    {
        std::fprintf(stderr, "[ach] patch load failed: %s\n", error_message);
        s_loaded.store(false, std::memory_order_relaxed);
        return;
    }
    s_loaded.store(true, std::memory_order_relaxed);
    // Restoring has to happen AFTER the patch is in place, or there is nothing for the serialized
    // blob to be applied to.
    loadProgress();
    // No count here: the list the UI reads is built on the next frame, so anything printed from
    // this callback would be a zero that looks like a failure.
    std::fprintf(stderr, "[ach] patch loaded\n");
}

// rcheevos reports a condition it could not parse as a state change (DISABLED, bucket UNSUPPORTED)
// and a WARN log line, never as an error return. Left unhandled that is the worst kind of failure
// for this module: the tracker comes up, the list is populated, and nothing ever fires. The log
// goes to the game's own log so the reason lands next to everything else.
extern "C" void RC_CCONV achLogMessage(const char *message, const rc_client_t *)
{
    if (message && message[0])
        std::fprintf(stderr, "[ach] %s\n", message);
}

// ---------------------------------------------------------------------------------------------
// The unlock ledger
// ---------------------------------------------------------------------------------------------
//
// rcheevos' progress blob deliberately does NOT record which achievements were earned.
// rc_runtime_progress_write_achievements() skips any trigger that is no longer active, with the
// comment "don't store state for inactive or triggered achievements" -- because in a client that
// talks to a server, the server holds the unlock list and the local blob only carries partial
// progress (measured values, hit counts) for the ones still in flight.
//
// This build has no server, so nothing else would remember an unlock. The consequence is not
// subtle: the blob reloads cleanly, reports no error, and every achievement comes back locked, so
// the counts reset each launch and each one toasts again when its condition next holds.
//
// So the ledger is ours. One line per unlock, "<id> <unix seconds>", append-only. Plain text on
// purpose -- it is a list of numbers, it should be readable with cat, and a corrupt line can be
// skipped without losing the rest.
constexpr const char *kUnlockedFile = "savedata/achievements.unlocked";
constexpr const char *kVerifiedFile = "assets/bt3-achievements.verified.txt";

void loadUnlocked()
{
    if (s_unlockedPath.empty())
        return;
    std::ifstream in(s_unlockedPath);
    if (!in.is_open())
        return;
    std::string line;
    while (std::getline(in, line))
    {
        const uint32_t id = static_cast<uint32_t>(std::strtoul(line.c_str(), nullptr, 10));
        if (id)
            s_unlocked.insert(id);
    }
    if (!s_unlocked.empty())
        std::fprintf(stderr, "[ach] %zu achievement(s) already earned\n", s_unlocked.size());
}

void appendUnlocked(uint32_t id)
{
    if (s_unlockedPath.empty())
        return;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(s_unlockedPath).parent_path(), ec);
    std::ofstream out(s_unlockedPath, std::ios::app);
    if (!out.is_open())
    {
        std::fprintf(stderr, "[ach] cannot append to %s\n", s_unlockedPath.c_str());
        return;
    }
    out << id << ' ' << static_cast<long long>(std::time(nullptr)) << '\n';
}

// The snapshot the UI reads. The ledger is merged in here rather than trusted later, because this
// is the one moment all three sources are known: the patch (which rows exist), rcheevos' bucket
// list (what it thinks is locked right now), and the ledger (what this install has actually
// earned). A row in the ledger stays marked earned even though rcheevos will re-raise the event.
void markEarnedFromLedger()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    for (auto &row : s_rows)
    {
        if (s_unlocked.count(row.id))
            row.unlocked = true;
    }
}

// Which achievements have translated addresses.
//
// rcheevos parses the patch into its own structures and does not surface a field we added, so the
// flag the generator writes cannot be read back off the achievement. It gets its own file instead:
// a list of ids, small (five of them today), written next to the patch. A separate file rather than
// a marker inside the title, because the title is the thing the player reads and "??" in front of
// 149 rows would be worse than useless.
void loadVerified()
{
    if (s_verifiedPath.empty())
        return;
    std::ifstream in(s_verifiedPath);
    if (!in.is_open())
    {
        std::fprintf(stderr, "[ach] no %s; every achievement will show as unverified\n",
                     s_verifiedPath.c_str());
        return;
    }
    // One id per line. A line that is not a number is skipped rather than fatal: this file is
    // generated, and a bad line should cost one flag, not the whole list.
    std::string line;
    while (std::getline(in, line))
    {
        const uint32_t id = static_cast<uint32_t>(std::strtoul(line.c_str(), nullptr, 10));
        if (id)
            s_verified.insert(id);
    }
}

// ---------------------------------------------------------------------------------------------
// Progress
// ---------------------------------------------------------------------------------------------

void saveProgress()
{
    if (!s_client || s_progressPath.empty())
        return;

    const size_t need = rc_client_progress_size(s_client);
    if (!need)
        return;

    std::vector<uint8_t> buf(need);
    if (rc_client_serialize_progress_sized(s_client, buf.data(), buf.size()) != RC_OK)
        return;

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(s_progressPath).parent_path(), ec);
    std::ofstream out(s_progressPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        std::fprintf(stderr, "[ach] cannot write %s\n", s_progressPath.c_str());
        return;
    }
    out.write(reinterpret_cast<const char *>(buf.data()), static_cast<std::streamsize>(buf.size()));
}

void loadProgress()
{
    if (!s_client || s_progressPath.empty())
        return;
    std::ifstream in(s_progressPath, std::ios::binary);
    if (!in.is_open())
        return;
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    if (buf.empty())
        return;
    if (rc_client_deserialize_progress_sized(s_client, buf.data(), buf.size()) == RC_OK)
        std::fprintf(stderr, "[ach] restored %zu bytes of progress from %s\n", buf.size(),
                     s_progressPath.c_str());
    else
        std::fprintf(stderr, "[ach] progress file unreadable, starting fresh\n");
}

// ---------------------------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------------------------

std::filesystem::path deployRoot(const char *exeDir)
{
    if (exeDir && exeDir[0])
        return std::filesystem::path(exeDir);
    const char *xd = ps2xExeDirC();
    if (xd && xd[0])
        return std::filesystem::path(xd);
    return std::filesystem::current_path();
}

// The patch sits next to the executable. The CWD is tried first for the same reason the overlay
// tries it first for settings.toml: a portable deploy can be launched from anywhere, and a dev
// build run from the build tree should pick up the tree's assets.
std::string readFile(const std::filesystem::path &p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in.is_open())
        return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string findAsset(const std::filesystem::path &root, const char *rel)
{
    namespace fs = std::filesystem;
    const fs::path candidates[] = {
        fs::current_path() / rel,
        root / rel,
        root / ".." / rel,
    };
    std::error_code ec;
    for (const auto &c : candidates)
    {
        if (fs::exists(c, ec))
            return c.string();
    }
    return {};
}

}   // namespace

// ---------------------------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------------------------

void ps2AchInit(const char *exeDir)
{
    if (s_client || s_prepared)
        return;
    s_prepared = true;

    const std::filesystem::path root = deployRoot(exeDir);

    // The env is the default and the settings key is the override, matching every other PS2X_*
    // switch in the tree, so a one-session experiment does not have to touch settings.toml.
    s_wantEnabled = s_wantEnabled || []() {
        const char *v = std::getenv("ACHIEVEMENTS");
        return v && v[0] && v[0] != '0';
    }();
    if (const char *t = std::getenv("PS2X_ACH_TRACE"); t && t[0] && t[0] != '0')
        s_trace = true;

    const std::string patchPath = findAsset(root, kPatchFile);
    if (patchPath.empty())
    {
        std::fprintf(stderr, "[ach] no %s under the deploy root; achievements are off\n",
                     kPatchFile);
        s_prepared = false;
        return;
    }

    s_patch = readFile(patchPath);
    if (s_patch.empty())
    {
        std::fprintf(stderr, "[ach] %s is empty\n", patchPath.c_str());
        s_prepared = false;
        return;
    }

    // The sets response is a second file rather than a slice of the first because the two have
    // different key names, and renaming them at runtime is how a tracker ends up loading nothing
    // without saying why. Missing is survivable -- rcheevos then gets a refusal for that step and
    // says so -- so this is a warning, not a fatal error.
    const std::string setsPath = findAsset(root, kSetsFile);
    if (!setsPath.empty())
        s_sets = readFile(setsPath);
    else
        std::fprintf(stderr, "[ach] no %s; the load will stop at the achievement-sets step\n",
                     kSetsFile);

    s_progressPath = (root / kProgressFile).string();
    s_unlockedPath = (root / kUnlockedFile).string();
    s_verifiedPath = findAsset(root, kVerifiedFile);
    loadVerified();
    loadUnlocked();

    // A caller that asked for it before init (the launcher does) still gets what it asked for.
    s_enabled.store(s_wantEnabled, std::memory_order_relaxed);
}

// Creating the client is deferred to the first frame, and that is not a style choice.
//
// rc_client_validate_addresses() runs as part of the load and tests every address the patch
// mentions by calling read_memory on it. An address whose read comes back short is marked invalid
// and every achievement using it is disabled -- silently, as a state rather than an error, with one
// WARN line. So a load started before the guest's RAM exists validates every address against a null
// pointer, all four achievements land in the UNSUPPORTED bucket, and the tracker comes up looking
// healthy while evaluating nothing. Doing it on the first frame means the reads are real.
static void achStartLocked(uint8_t *rdram)
{
    (void)rdram;
    s_client = rc_client_create(achReadMemory, achServerCall);
    if (!s_client)
    {
        std::fprintf(stderr, "[ach] rc_client_create failed\n");
        return;
    }
    rc_client_set_event_handler(s_client, achEventHandler);
    // WARN, not VERBOSE: the lines that matter are the parse failures, and there is exactly one per
    // broken condition. VERBOSE would bury them under a line per memory read.
    rc_client_enable_logging(s_client, RC_CLIENT_LOG_LEVEL_WARN, achLogMessage);

    // Log in first: rcheevos refuses to fetch game data until its user state is LOGGED_IN, and the
    // load started below would otherwise sit at AWAIT_LOGIN and fail with "Login required". The
    // token is not a credential -- achServerCall answers r=login2 with a fixed local identity, and
    // no request leaves the process.
    rc_client_begin_login_with_token(s_client, "local", "offline", achLoginCallback, nullptr);

    // The hash is only ever used to ask which game this is, and the answer comes from the patch.
    // The disc hash is passed anyway so a future online build can drop the stub without having to
    // find the string again.
    rc_client_begin_load_game(s_client, "18df2548cc72b98287170b19f73c502b", achLoadCallback,
                              nullptr);
}

void ps2AchShutdown()
{
    if (!s_client)
        return;
    saveProgress();
    rc_client_destroy(s_client);
    s_client = nullptr;
    s_loaded.store(false, std::memory_order_relaxed);
    s_listed = false;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_rows.clear();
    }
}

void ps2AchFrame(uint8_t *rdram)
{
    if (!s_enabled.load(std::memory_order_relaxed) || !rdram)
        return;

    // Published before the client exists, because the load's own address validation reads through
    // this pointer. Cleared at the end of the call: it is the only place the pointer lives, so a
    // stray read after the guest has moved on cannot silently return plausible garbage.
    s_rdram = rdram;
    if (!s_client)
        achStartLocked(rdram);
    if (!s_client)
    {
        s_rdram = nullptr;
        return;
    }

    // One snapshot of the list per load, not per frame: walking the client's achievement list is a
    // pointer chase, and the UI only redraws when it is open.
    if (!s_listed && s_loaded.load(std::memory_order_relaxed))
    {
        rc_client_achievement_list_t *list =
            rc_client_create_achievement_list(s_client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
                                              RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
        if (list)
        {
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                s_rows.clear();
                for (uint32_t b = 0; b < list->num_buckets; ++b)
                {
                    const rc_client_achievement_bucket_t *bucket = &list->buckets[b];
                    for (uint32_t i = 0; i < bucket->num_achievements; ++i)
                    {
                        const rc_client_achievement_t *a = bucket->achievements[i];
                        if (!a)
                            continue;
                        s_rows.push_back(
                            {a->title ? a->title : "", a->description ? a->description : "",
                             static_cast<int>(a->points),
                             a->unlocked != RC_CLIENT_ACHIEVEMENT_UNLOCKED_NONE,
                             s_verified.count(a->id) != 0, a->id});
                    }
                }
                // The bucket list groups by lock state, so it comes back unlocked-first. The overlay
                // shows the whole set at once and patch order is the order the author numbered them.
                std::sort(s_rows.begin(), s_rows.end(),
                          [](const Row &a, const Row &b) { return a.id < b.id; });
            }
            rc_client_destroy_achievement_list(list);
        }
        // Merged after the lock above is released: markEarnedFromLedger() takes the mutex itself.
        markEarnedFromLedger();
        s_listed = true;
    }

    rc_client_do_frame(s_client);
    // rc_client_idle() is what rcheevos calls from its own thread; pumping it here keeps the event
    // queue draining on the guest thread instead of needing a second one. Every 64 frames is often
    // enough -- offline there is no network work for it to schedule. It runs before s_rdram is
    // cleared because idle can also touch memory, and a read against a null pointer returns short,
    // which is the same "invalid address" signal that disables achievements.
    if ((++s_idleTick & 63u) == 0u)
        rc_client_idle(s_client);

    s_rdram = nullptr;
}

bool ps2xAchEnabled() { return s_enabled.load(std::memory_order_relaxed); }

void ps2xSetAchEnabled(bool on)
{
    s_wantEnabled = on;
    s_enabled.store(on, std::memory_order_relaxed);
    if (on)
        ps2AchInit(nullptr);
    else
        saveProgress();
}

bool ps2xAchLoaded() { return s_loaded.load(std::memory_order_relaxed); }

int ps2xAchTotal() { return static_cast<int>(s_rows.size()); }

int ps2xAchUnlocked()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    int n = 0;
    for (const auto &r : s_rows)
        n += r.unlocked ? 1 : 0;
    return n;
}

int ps2xAchPointsEarned()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    int n = 0;
    for (const auto &r : s_rows)
        n += r.unlocked ? r.points : 0;
    return n;
}

int ps2xAchPointsTotal()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    int n = 0;
    for (const auto &r : s_rows)
        n += r.points;
    return n;
}

int ps2xAchListCount()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return static_cast<int>(s_rows.size());
}

namespace {
// The rcheevos achievement for one of our rows, or NULL. The lookup is by id rather than by index
// because our list is sorted and the client's own arrays are not in the same order.
const rc_client_achievement_t *infoFor(int index)
{
    if (!s_client || index < 0 || index >= static_cast<int>(s_rows.size()))
        return nullptr;
    return rc_client_get_achievement_info(s_client, s_rows[static_cast<size_t>(index)].id);
}
}   // namespace

int ps2xAchState(int index)
{
    const rc_client_achievement_t *a = infoFor(index);
    return a ? static_cast<int>(a->state) : -1;
}

int ps2xAchBucket(int index)
{
    const rc_client_achievement_t *a = infoFor(index);
    return a ? static_cast<int>(a->bucket) : -1;
}

unsigned ps2xAchCategory(int index)
{
    const rc_client_achievement_t *a = infoFor(index);
    return a ? static_cast<unsigned>(a->category) : 0u;
}

bool ps2xAchListGet(int index, char *title, size_t titleCap, char *description,
                    size_t descriptionCap, int *points, bool *unlocked)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (index < 0 || index >= static_cast<int>(s_rows.size()))
        return false;
    const Row &r = s_rows[static_cast<size_t>(index)];
    if (title && titleCap)
        std::snprintf(title, titleCap, "%s", r.title.c_str());
    if (description && descriptionCap)
        std::snprintf(description, descriptionCap, "%s", r.description.c_str());
    if (points)
        *points = r.points;
    if (unlocked)
        *unlocked = r.unlocked;
    return true;
}

int ps2xAchFindByTitle(const char *title)
{
    if (!title || !title[0])
        return -1;
    std::lock_guard<std::mutex> lock(s_mutex);
    for (size_t i = 0; i < s_rows.size(); ++i)
    {
        if (s_rows[i].title == title)
            return static_cast<int>(i);
    }
    return -1;
}

uint32_t ps2xAchIdAt(int index)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (index < 0 || index >= static_cast<int>(s_rows.size()))
        return 0u;
    return s_rows[static_cast<size_t>(index)].id;
}

bool ps2xAchAddrVerified(int index)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (index < 0 || index >= static_cast<int>(s_rows.size()))
        return false;
    return s_rows[static_cast<size_t>(index)].addrVerified;
}
