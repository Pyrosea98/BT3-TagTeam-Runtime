#pragma once
#include "ps2_ui_strings.h"
#include <array>
#include <cstdint>
#include <mutex>
#include <span>

namespace ps2x::ui {
enum class Screen : uint8_t { Hidden, Loading, Settings, About };
enum class PreparationPhase : uint8_t { Idle, Preparing, Ready, Failed, Released };
enum class BattleMode : uint8_t { Teams, Coop, FreeForAll, Training, TrainingCoop };
struct HudPreferences {
    uint8_t names=0,detail=1,scale=65,opacity=50,shape=1;bool kiPips=true;
    bool portraits=true,list=false,fusion=true,friends=true,enemies=true;
};
struct ScreenSnapshot {
    Screen screen = Screen::Hidden;
    PreparationPhase phase = PreparationPhase::Idle;
    Language language = Language::English;
    uint64_t generation = 0;
    uint64_t revision = 0;
    uint64_t heartbeatMs = 0;
    uint8_t progress = 0, stage = 0;
    uint8_t teamOneCount = 0, teamTwoCount = 0;
    std::array<uint16_t, 10> fighters{};
    TextId message = TextId::PreparingMatch;
    bool ownerExpired = false;
    bool startAccepted = false;
    BattleMode mode = BattleMode::Teams;
    uint8_t humans = 1;
    uint8_t hudOptions = 7;
    HudPreferences hud;
    std::array<uint8_t,10> seats{};
};
// Presentation state only. This never releases actors, restores a match, or
// acknowledges game readiness. The native match owner controls those actions.
class ScreenLifecycle {
    mutable std::mutex mutex_;
    ScreenSnapshot state_;
    bool ownsLoading() const { return state_.screen == Screen::Loading; }
    void touch(uint64_t now) { state_.heartbeatMs = now; ++state_.revision; }
public:
    bool matchDetails(uint64_t generation,BattleMode mode,uint8_t humans,std::array<uint8_t,10> seats,uint8_t hudOptions=7,HudPreferences hud={}) {
        std::lock_guard lock(mutex_);
        if(generation!=state_.generation || !ownsLoading() || uint8_t(mode)>4 || humans>4 || hudOptions>7 || hud.names>2 || hud.detail>3 || hud.shape>3 || hud.scale<50 || hud.scale>130 || hud.opacity<20 || hud.opacity>100)return false;
        for(auto seat:seats)if(seat>4)return false;
        state_.mode=mode;state_.humans=humans;state_.seats=seats;state_.hudOptions=hudOptions;state_.hud=hud;++state_.revision;return true;
    }
    bool begin(uint64_t generation, uint64_t now, std::span<const uint16_t> one,
               std::span<const uint16_t> two) {
        std::lock_guard lock(mutex_);
        if (!generation || generation <= state_.generation || ownsLoading() || now < state_.heartbeatMs ||
            one.empty() || two.empty() || one.size() > 5 || two.size() > 5) return false;
        for (auto id : one) if (id > 252) return false;
        for (auto id : two) if (id > 252) return false;
        const auto language = state_.language;
        const auto revision = state_.revision;
        state_ = {};
        state_.generation = generation; state_.revision = revision;
        state_.language = language; state_.screen = Screen::Loading;
        state_.phase = PreparationPhase::Preparing;
        state_.teamOneCount = uint8_t(one.size()); state_.teamTwoCount = uint8_t(two.size());
        size_t i = 0;
        for (auto id : one) state_.fighters[i++] = id;
        for (auto id : two) state_.fighters[i++] = id;
        touch(now); return true;
    }
    bool progress(uint64_t generation, uint8_t percent, uint8_t stage, uint64_t now) {
        std::lock_guard lock(mutex_);
        if (!ownsLoading() || generation != state_.generation || state_.phase != PreparationPhase::Preparing ||
            percent > 100 || stage > 7 || percent < state_.progress || stage < state_.stage || now < state_.heartbeatMs) return false;
        state_.progress = percent; state_.stage = stage; touch(now); return true;
    }
    bool ready(uint64_t generation, uint64_t now) {
        std::lock_guard lock(mutex_);
        if (!ownsLoading() || generation != state_.generation || state_.phase != PreparationPhase::Preparing || now < state_.heartbeatMs) return false;
        state_.phase = PreparationPhase::Ready; state_.progress = 100; state_.stage = 7;
        state_.message = TextId::Ready; touch(now); return true;
    }
    bool heartbeat(uint64_t generation, uint64_t now) {
        std::lock_guard lock(mutex_);
        if (!ownsLoading() || generation != state_.generation || now < state_.heartbeatMs) return false;
        touch(now); return true;
    }
    bool fail(uint64_t generation, uint64_t now) {
        std::lock_guard lock(mutex_);
        if (!ownsLoading() || generation != state_.generation || state_.phase == PreparationPhase::Failed || state_.startAccepted || now < state_.heartbeatMs) return false;
        state_.phase = PreparationPhase::Failed; state_.message = TextId::PreparationFailed; state_.startAccepted = false;
        touch(now); return true;
    }
    // Guest intro state proves the start request was consumed. This changes
    // presentation only; ownership and actor/input holds remain until release.
    bool startAccepted(uint64_t generation,uint64_t now) {
        std::lock_guard lock(mutex_);
        if(!ownsLoading() || generation!=state_.generation || state_.phase!=PreparationPhase::Ready || now<state_.heartbeatMs)return false;
        state_.startAccepted=true;touch(now);return true;
    }
    // Call only after the owning preparation worker has observed actual match
    // release. 100% progress by itself deliberately never closes the cover.
    bool released(uint64_t generation, uint64_t now) {
        std::lock_guard lock(mutex_);
        if (!ownsLoading() || generation != state_.generation || state_.phase != PreparationPhase::Ready || now < state_.heartbeatMs) return false;
        state_.phase = PreparationPhase::Released; state_.screen = Screen::Hidden; touch(now); return true;
    }
    // Explicit owner teardown also handles cancellation/error cleanup. It only
    // closes the presentation; it does not mark the match ready or released.
    bool teardown(uint64_t generation, uint64_t now) {
        std::lock_guard lock(mutex_);
        if (!ownsLoading() || generation != state_.generation || now < state_.heartbeatMs) return false;
        state_.screen = Screen::Hidden; touch(now); return true;
    }
    bool open(Screen screen, uint64_t now) {
        std::lock_guard lock(mutex_);
        if (ownsLoading() || now < state_.heartbeatMs || (screen != Screen::Settings && screen != Screen::About)) return false;
        state_.screen = screen; touch(now); return true;
    }
    bool close(uint64_t now) {
        std::lock_guard lock(mutex_);
        if (now < state_.heartbeatMs || (state_.screen != Screen::Settings && state_.screen != Screen::About)) return false;
        state_.screen = Screen::Hidden; touch(now); return true;
    }
    void language(Language language) {
        std::lock_guard lock(mutex_);
        if (language != Language::English && language != Language::Spanish) return;
        state_.language = language; ++state_.revision;
    }
    ScreenSnapshot snapshot(uint64_t now, uint64_t leaseMs = 5000) const {
        std::lock_guard lock(mutex_);
        auto result = state_;
        // Flag stale ownership; never infer game readiness or dismiss errors.
        result.ownerExpired = result.screen == Screen::Loading && now >= result.heartbeatMs && now - result.heartbeatMs > leaseMs;
        return result;
    }
};
} // namespace ps2x::ui
