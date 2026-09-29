// [notify] The popup bus. See include/runtime/ps2x_notify.h for what this is for.
//
// This file is the queue and nothing else, which is why the tones live in ps2x_notify_sound.cpp: the
// bus has no audio dependency at all, so anything that wants to raise a notification -- including a
// headless probe -- links this and stops there. The drawing is the overlay's, because the overlay
// is the only thing in the tree that owns an ImGui frame.

#include "runtime/ps2x_notify.h"
#include "runtime/ps2_memory.h"      // PS2_RAM_SIZE, for the `dump` verb

#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int kMaxQueued = 8;

std::mutex  s_mutex;
// [notifyown] The queue OWNS its text. NotifyEvent carries const char* for the producers' convenience,
// but a producer's strings only live as long as its own scope (the drop box parses into locals and
// pushes inside the loop), and the overlay drains on a later frame: a queue of raw pointers handed the
// cards freed memory, which drew as "]???" on the title line. The bytes are copied on push and handed
// back from storage that lives until the next drain.
struct Stored
{
    NotifyKind kind = NotifyKind::Netplay;
    NotifyTone tone = NotifyTone::None;
    std::string title, body;
};
std::deque<Stored> s_queue;
std::vector<Stored> s_drained;   // what the last drain handed out; valid until the next drain
bool s_soundEnabled = true;

}   // namespace

void ps2xNotifyPush(const NotifyEvent &ev)
{
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        // Queued whether or not sound is on: muting the sound must not also silence the popup.
        s_queue.push_back(Stored{ev.kind, ev.tone, ev.title ? ev.title : "", ev.body ? ev.body : ""});
        while (static_cast<int>(s_queue.size()) > kMaxQueued)
            s_queue.pop_front();
    }
    // Outside the lock. The tone player takes the audio backend's own mutex, and a notification
    // raised from inside an audio callback would otherwise be a lock-order inversion waiting to
    // happen.
    ps2xNotifyPlayTone(ev.tone);
}

void ps2xNotifySetSoundEnabled(bool on)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_soundEnabled = on;
}

bool ps2xNotifySoundEnabled()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_soundEnabled;
}

int ps2xNotifyDrain(NotifyEvent *out, int max)
{
    if (!out || max <= 0)
        return 0;
    std::lock_guard<std::mutex> lock(s_mutex);
    s_drained.clear();
    while (static_cast<int>(s_drained.size()) < max && !s_queue.empty())
    {
        s_drained.push_back(std::move(s_queue.front()));
        s_queue.pop_front();
    }
    int n = 0;
    for (const Stored &st : s_drained)   // pointers into s_drained: the caller copies them this frame
        out[n++] = NotifyEvent{st.kind, st.tone, st.title.c_str(), st.body.c_str()};
    return n;
}

int ps2xNotifyPending()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return static_cast<int>(s_queue.size());
}

// ---------------------------------------------------------------------------------------------
// The drop box
// ---------------------------------------------------------------------------------------------

namespace {

constexpr const char *kDropFile = "notify.request";
constexpr int kPollEvery = 15;      // calls; the tick calls once a frame, so ~4x a second
constexpr int kMaxPerRead = 8;      // the queue's own bound
int s_dumpSeq = 0;                  // dump.<n>.bin, so a second dump does not overwrite the first

NotifyKind parseKind(const std::string &s)
{
    // Anything that is not explicitly netplay is an achievement, so a typo in a request from a
    // shell cannot silently produce a card with no identity. The kind is the one thing on the card
    // that is not in the text, so guessing wrong here is worse than picking a default.
    return (s == "net" || s == "netplay") ? NotifyKind::Netplay : NotifyKind::Achievement;
}

NotifyTone parseTone(const std::string &s)
{
    if (s == "info")  return NotifyTone::Info;
    if (s == "good")  return NotifyTone::Good;
    if (s == "great") return NotifyTone::Great;
    if (s == "bad")   return NotifyTone::Bad;
    return NotifyTone::None;
}

std::string trim(const std::string &s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

// "kind tone title :: body" -> the parts. Tolerant on purpose: a request from a shell is hand-typed
// far more often than it is generated, and a card with a slightly wrong title beats a dropped one.
//
// The title and body come back as std::string, not as the const char* that NotifyEvent carries.
// That is deliberate and it is the whole reason this function is shaped this way: NotifyEvent holds
// pointers, so a producer has to own the bytes for as long as the push takes. Returning owning
// strings and letting the caller push inside the same scope makes that impossible to get wrong,
// where filling a NotifyEvent here and pushing later would leave the queue holding freed memory.
bool parseLine(const std::string &line, NotifyKind &kind, NotifyTone &tone, std::string &title,
               std::string &body)
{
    const std::string l = trim(line);
    if (l.empty() || l[0] == '#')
        return false;

    std::istringstream in(l);
    std::string k, t, rest;
    if (!(in >> k >> t))
        return false;
    std::getline(in, rest);
    rest = trim(rest);

    kind = parseKind(k);
    tone = parseTone(t);

    const size_t sep = rest.find("::");
    if (sep == std::string::npos)
    {
        title = rest;
        body.clear();
    }
    else
    {
        title = trim(rest.substr(0, sep));
        body = trim(rest.substr(sep + 2));
    }
    return !title.empty();
}

}   // namespace

int ps2xNotifyPollDropBox(const char *dir, const uint8_t *rdram)
{
    static int s_tick = 0;
    if (++s_tick < kPollEvery)
        return 0;

    if (!dir || !dir[0])
        return 0;

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path path = fs::path(dir) / kDropFile;

    // exists() rather than a read attempt: on a frame where there is nothing to say this is one
    // stat and no allocation, which is the whole reason the file is a drop box and not a pipe.
    if (!fs::exists(path, ec))
        return 0;

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
        return 0;
    const std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    // Removed before anything is done with it, so a request cannot run twice if the process is
    // interrupted between here and the pushes. Losing a card is better than repeating one.
    fs::remove(path, ec);

    // Parsed and pushed outside the lock: ps2xNotifyPush takes it itself, and it is not recursive.
    int raised = 0;
    std::istringstream lines(body);
    std::string line;
    while (std::getline(lines, line))
    {
        const std::string l = trim(line);
        if (l.empty() || l[0] == '#')
            continue;

        // "dump <prefix>" -- the same bytes PS2X_DUMPKEY + F9 writes, reachable without a keypress.
        if (l.rfind("dump", 0) == 0 && (l.size() == 4 || l[4] == ' ' || l[4] == '\t'))
        {
            if (!rdram)
                continue;
            const std::string prefix = trim(l.substr(4));
            if (prefix.empty())
                continue;
            char out[512];
            std::snprintf(out, sizeof out, "%s.%d.bin", prefix.c_str(), s_dumpSeq++);
            std::FILE *f = std::fopen(out, "wb");
            if (!f)
            {
                std::fprintf(stderr, "[notify] cannot write %s\n", out);
                continue;
            }
            std::fwrite(rdram, 1, PS2_RAM_SIZE, f);
            std::fclose(f);
            std::fprintf(stderr, "[notify] wrote %u bytes of guest RAM -> %s\n",
                         (unsigned)PS2_RAM_SIZE, out);
            continue;
        }

        if (raised >= kMaxPerRead)
            continue;

        NotifyKind kind;
        NotifyTone tone;
        std::string title, text;
        if (!parseLine(l, kind, tone, title, text))
            continue;
        // title and text are locals of this iteration and are alive across the push, which is the
        // contract NotifyEvent's const char* fields depend on.
        const NotifyEvent ev{kind, tone, title.c_str(), text.c_str()};
        ps2xNotifyPush(ev);
        ++raised;
    }
    return raised;
}

