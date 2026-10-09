#include "ps2_waitprof.h"   // [waitprof]
#include "runtime/ps2_statesync.h"   // [statesync]
#include "ps2_runtime_macros.h"
#include "Common.h"
#include "Thread.h"
#include "ps2_trace_diagnostics.h"

extern std::atomic<uint32_t> g_bt3StateLive; // [eeround2] gate (ps2_runtime.cpp)

extern "C" void ps2xSchedSignal();   // [fibers] ps2_runtime.cpp: a blocked fiber may now be runnable
namespace ps2_syscalls
{
    static void applySuspendStatusLocked(ThreadInfo &info)
    {
        if (info.waitType != TSW_NONE)
        {
            info.status = THS_WAITSUSPEND;
        }
        else
        {
            info.status = THS_SUSPEND;
        }
    }

    static void notifyThreadWaitObject(int waitType, int waitId)
    {
        if (waitType == TSW_SEMA)
        {
            auto sema = lookupSemaInfo(waitId);
            if (sema)
            {
                sema->cv.notify_all();
            }
        }
        else if (waitType == TSW_EVENT)
        {
            auto eventFlag = lookupEventFlagInfo(waitId);
            if (eventFlag)
            {
                eventFlag->cv.notify_all();
            }
        }
    }

    static void runExitHandlersForThread(int tid, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (!runtime || !ctx)
            return;

        std::vector<ExitHandlerEntry> handlers;
        {
            std::lock_guard<std::mutex> lock(g_exit_handler_mutex);
            auto it = g_exit_handlers.find(tid);
            if (it == g_exit_handlers.end())
                return;
            handlers = std::move(it->second);
            g_exit_handlers.erase(it);
        }

        for (const auto &handler : handlers)
        {
            if (!handler.func)
                continue;
            try
            {
                rpcInvokeFunction(rdram, ctx, runtime, handler.func, handler.arg, 0, 0, 0, nullptr);
            }
            catch (const ThreadExitException &)
            {
                // ignore
            }
            catch (const std::exception &)
            {
            }
        }
    }

    void FlushCache(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, KE_OK);
    }

    void iFlushCache(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        FlushCache(rdram, ctx, runtime);
    }

    void EnableCache(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, KE_OK);
    }

    void DisableCache(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, KE_OK);
    }

    void ResetEE(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        std::cerr << "Syscall: ResetEE - requesting runtime stop" << std::endl;
        // runtime->requestStop();
        setReturnS32(ctx, KE_OK);
    }

    void SetMemoryMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, KE_OK);
    }

    void InitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // This is a common ps2sdk helper that some games link against.
        setReturnS32(ctx, 1);
    }

    void CreateThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t paramAddr = getRegU32(ctx, 4); // $a0 points to ThreadParam
        if (paramAddr == 0u)
        {
            std::cerr << "CreateThread error: null ThreadParam pointer" << std::endl;
            setReturnS32(ctx, KE_ERROR);
            return;
        }

        const uint32_t *param = reinterpret_cast<const uint32_t *>(getConstMemPtr(rdram, paramAddr));

        if (!param)
        {
            std::cerr << "CreateThread error: invalid ThreadParam address 0x" << std::hex << paramAddr << std::dec << std::endl;
            setReturnS32(ctx, KE_ERROR);
            return;
        }

        auto info = std::make_shared<ThreadInfo>();
        info->attr = param[0];
        info->entry = param[1];
        info->stack = param[2];
        info->stackSize = param[3];

        auto looksLikeGuestPtr = [](uint32_t v) -> bool
        {
            if (v == 0)
            {
                return true;
            }
            const uint32_t norm = v & 0x1FFFFFFFu;
            return norm < PS2_RAM_SIZE && norm >= 0x10000u;
        };

        auto looksLikePriority = [](uint32_t v) -> bool
        {
            // Typical EE priorities are very small integers (1..127).
            return v <= 0x400u;
        };

        const uint32_t gpA = param[4];
        const uint32_t prioA = param[5];
        const uint32_t gpB = param[5];
        const uint32_t prioB = param[4];

        // Prefer the standard EE layout (gp at +0x10, priority at +0x14),
        // but keep a fallback for callsites that used the swapped decode.
        if (looksLikeGuestPtr(gpA) && looksLikePriority(prioA))
        {
            info->gp = gpA;
            info->priority = prioA;
        }
        else if (looksLikeGuestPtr(gpB) && looksLikePriority(prioB))
        {
            info->gp = gpB;
            info->priority = prioB;
        }
        else
        {
            info->gp = gpA;
            info->priority = prioA;
        }

        info->option = param[6];
        if (info->priority == 0)
        {
            info->priority = 1;
        }
        if (info->priority >= 128)
        {
            info->priority = 127;
        }
        info->currentPriority = static_cast<int>(info->priority);

        int id = 0;
        {
            std::lock_guard<std::mutex> lock(g_thread_map_mutex);
            // Keep IDs in the classic low range used by patched libkernel helpers.
            for (int attempts = 0; attempts < 0xFE; ++attempts)
            {
                if (g_nextThreadId < 2 || g_nextThreadId > 0xFF)
                {
                    g_nextThreadId = 2;
                }

                const int candidate = g_nextThreadId;
                g_nextThreadId = (g_nextThreadId >= 0xFF) ? 2 : (g_nextThreadId + 1);

                if (g_threads.find(candidate) == g_threads.end())
                {
                    id = candidate;
                    break;
                }
            }

            if (id == 0)
            {
                setReturnS32(ctx, KE_ERROR);
                return;
            }

            g_threads[id] = info;
        }

        // [threadlog] PS2X_THREADLOG=1: which guest threads exist and where they start.
        // Needed for the audio work: BT3's sound service thread (entry 0x26d070) must run
        // for the preload-completion handler to fire on the right thread. RUNTIME_LOG is
        // compile-time gated and far too noisy, so log just this, opt-in.
        {
            static const bool s_tl = [](){ const char *v = std::getenv("PS2X_THREADLOG"); return v && v[0] == '1'; }();
            if (s_tl)
                std::cerr << "[threadlog] CreateThread id=" << id << " entry=0x" << std::hex
                          << info->entry << " gp=0x" << info->gp << std::dec
                          << " prio=" << info->priority << std::endl;
        }
        RUNTIME_LOG("[CreateThread] id=" << id
                                         << " entry=0x" << std::hex << info->entry
                                         << " stack=0x" << info->stack
                                         << " size=0x" << info->stackSize
                                         << " gp=0x" << info->gp
                                         << " prio=" << std::dec << info->priority << std::endl);

        setReturnS32(ctx, id);
    }

    void DeleteThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4)); // $a0
        if (tid == 0)
        {
            setReturnS32(ctx, KE_ILLEGAL_THID);
            return;
        }

        auto info = lookupThreadInfo(tid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_THID);
            return;
        }

        uint32_t autoStackToFree = 0;
        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->started || info->status != THS_DORMANT)
            {
                setReturnS32(ctx, KE_NOT_DORMANT);
                return;
            }

            if (info->ownsStack && info->stack != 0)
            {
                autoStackToFree = info->stack;
                info->stack = 0;
                info->stackSize = 0;
                info->ownsStack = false;
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_thread_map_mutex);
            g_threads.erase(tid);
        }

        {
            std::lock_guard<std::mutex> lock(g_exit_handler_mutex);
            g_exit_handlers.erase(tid);
        }

        if (runtime && autoStackToFree != 0)
        {
            runtime->guestFree(autoStackToFree);
        }

        setReturnS32(ctx, KE_OK);
    }

    // [statesync] Every running worker's R5900 context, by tid. The context is a local on the
    // worker's own fiber stack (it travels with the in-process fiber snapshot); the state sync
    // needs to read and overwrite it in place, so the worker registers its address for its lifetime.
    static std::mutex g_workerCtxM;
    static std::map<int, R5900Context *> g_workerCtx;
    extern "C" R5900Context *ps2xWorkerContext(int tid)
    {
        std::lock_guard<std::mutex> lk(g_workerCtxM);
        auto it = g_workerCtx.find(tid);
        return it == g_workerCtx.end() ? nullptr : it->second;
    }
    extern "C" int ps2xWorkerContextTids(int *out, int cap)
    {
        std::lock_guard<std::mutex> lk(g_workerCtxM);
        int n = 0;
        for (const auto &kv : g_workerCtx) if (n < cap) out[n++] = kv.first;
        return n;
    }
    // [statesync] The kernel's view of a thread's park (status / wait kind / wait object), for the
    // structural check before a synced state is adopted.
    extern "C" bool ps2xKernelThreadWait(int tid, int *status, int *waitType, int *waitId)
    {
        std::lock_guard<std::mutex> lk(g_thread_map_mutex);
        auto it = g_threads.find(tid);
        if (it == g_threads.end() || !it->second) return false;
        ThreadInfo &i = *it->second;
        std::lock_guard<std::mutex> il(i.m);
        *status = i.status; *waitType = i.waitType; *waitId = i.waitId;
        return true;
    }
    extern "C" uint64_t *ps2xSchedStepCount(int tid);   // [statesync] ps2_runtime.cpp: the worker loop's yield quantum counter, runtime-owned
    extern "C" int ps2xSchedTraceOn();                   // ps2_runtime.cpp: PS2X_SCHEDTRACE window
    extern "C" uint32_t *ps2xSchedU32(int tid, int which);   // [statesync] 0 = same-pc counter, 1 = last pc (they gate the spin sleeps)
    void StartThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4)); // $a0 = thread id
        uint32_t arg = getRegU32(ctx, 5);              // $a1 = user arg
        if (tid == 0)
        {
            setReturnS32(ctx, KE_ILLEGAL_THID);
            return;
        }

        auto info = lookupThreadInfo(tid);
        if (!info)
        {
            std::cerr << "StartThread error: unknown thread id " << tid << std::endl;
            setReturnS32(ctx, KE_UNKNOWN_THID);
            return;
        }

        if (!runtime || !runtime->hasFunction(info->entry))
        {
            std::cerr << "[StartThread] entry 0x" << std::hex << info->entry << std::dec << " is not registered" << std::endl;
            setReturnS32(ctx, KE_ERROR);
            return;
        }
        if (runtime->isStopRequested())
        {
            setReturnS32(ctx, KE_ERROR);
            return;
        }

        joinHostThreadById(tid);

        const uint32_t callerSp = getRegU32(ctx, 29);
        const uint32_t callerGp = getRegU32(ctx, 28);

        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->started || info->status != THS_DORMANT)
            {
                setReturnS32(ctx, KE_NOT_DORMANT);
                return;
            }

            info->started = true;
            info->status = THS_READY;
            info->arg = arg;
            info->terminated = false;
            info->forceRelease = false;
            info->waitType = TSW_NONE;
            info->waitId = 0;
            info->wakeupCount = 0;
            info->suspendCount = 0;
            {   // [minstack] PS2X_MINSTACK=<bytes>: a guest thread created with a smaller stack gets a private larger one.
                // BT3's sound threads are created with 0x800/0x1000-byte stacks right above the sound stream control block
                // 0x2c9350; the loading hangs show a 64-bit register spill landing on that block (see bt3-loading-stall).
                static const uint32_t s_minStack = [](){ const char *v = std::getenv("PS2X_MINSTACK"); return v && v[0] ? (uint32_t)std::strtoul(v, nullptr, 0) : 0u; }();
                if (s_minStack != 0u && !info->ownsStack && info->stackSize != 0u && info->stackSize < s_minStack)
                {
                    const uint32_t big = runtime->guestMalloc(s_minStack, 16u);
                    if (big != 0)
                    {
                        std::fprintf(stderr, "[minstack] tid=%d stack 0x%x size 0x%x -> private 0x%x size 0x%x\n", tid, info->stack, info->stackSize, big, s_minStack);
                        info->stack = big; info->stackSize = s_minStack; info->ownsStack = true;
                    }
                }
            }
            if (info->stack == 0 && info->stackSize != 0)
            {
                const uint32_t autoStack = runtime->guestMalloc(info->stackSize, 16u);
                if (autoStack != 0)
                {
                    info->stack = autoStack;
                    info->ownsStack = true;
                    RUNTIME_LOG("[StartThread] id=" << tid
                                                    << " auto-stack=0x" << std::hex << autoStack
                                                    << " size=0x" << info->stackSize << std::dec << std::endl);
                }
            }

            if (info->stack != 0 && info->stackSize == 0)
            {
                // Some games leave size zero in the thread param even though a stack
                // buffer is supplied; use a conservative default instead of caller SP.
                info->stackSize = 0x800u;
            }
        }

        g_activeThreads.fetch_add(1, std::memory_order_relaxed);
        try
        {
            // [fibers] The body is identical either way; only who drives it changes -- a host
            // std::thread, or a fiber the scheduler switches to on its own thread.
            auto workerBody = [=]() mutable
                               {
            {
                std::string name = "PS2Thread_" + std::to_string(tid);
                ThreadNaming::SetCurrentThreadName(name);
                {   // [eeround] guest threads run recompiled FPU/VU0 math too
                    static const bool s_eeRound = [](){ const char *v = std::getenv("PS2X_EEROUND"); return v && v[0] && v[0] != '0'; }();
                    if (s_eeRound) _mm_setcsr((_mm_getcsr() & ~0x6000u) | 0x6000u | 0x8040u);
                }
            }
            R5900Context threadCtxCopy{};
            R5900Context *threadCtx = &threadCtxCopy;
            { std::lock_guard<std::mutex> lk(g_workerCtxM); g_workerCtx[tid] = threadCtx; }   // [statesync]
            // VU0 vf0 is hardwired read-only to (0,0,0,1) on real hardware; a zero-init
            // context leaves it (0,0,0,0), which poisons every VU0 macro-mode matrix (the
            // identity basis is built by rotating vf0). Seed the constant for this thread.
            threadCtx->vu0_vf[0] = _mm_set_ps(1.0f, 0.0f, 0.0f, 0.0f);
                {
                    // [vf0basis] PS2X_VF0BASIS=1: seed the persistent VU0 identity basis rows the
                    // game establishes once via FUN_00120088 (MR32 chain) — hardware shares ONE
                    // physical VU0 across threads; per-thread contexts otherwise start them zero
                    // and func_121E50's normalize drops z^2 (vf3.x==0) => terrain band tears.
                    static const bool s_vb = [](){ const char *v = std::getenv("PS2X_VF0BASIS"); return v && v[0] && v[0] != '0'; }();
                    if (s_vb)
                    {
                        threadCtx->vu0_vf[1] = _mm_set_ps(0.0f, 1.0f, 0.0f, 0.0f); // (0,0,1,0)
                        threadCtx->vu0_vf[2] = _mm_set_ps(0.0f, 0.0f, 1.0f, 0.0f); // (0,1,0,0)
                        threadCtx->vu0_vf[3] = _mm_set_ps(0.0f, 0.0f, 0.0f, 1.0f); // (1,0,0,0)
                    }
                }


            {
                std::lock_guard<std::mutex> lock(info->m);
                info->status = THS_RUN;
            }

            uint32_t threadSp = callerSp;
            if (info->stack)
            {
                const uint32_t stackSize = (info->stackSize != 0) ? info->stackSize : 0x800u;
                threadSp = (info->stack + stackSize) & ~0xFu;
            }
            uint32_t threadGp = info->gp;
            const uint32_t normalizedGp = threadGp & 0x1FFFFFFFu;
            if (threadGp == 0 || normalizedGp < 0x10000u || normalizedGp >= PS2_RAM_SIZE)
            {
                threadGp = callerGp;
            }

            SET_GPR_U32(threadCtx, 29, threadSp);
            SET_GPR_U32(threadCtx, 28, threadGp);
            SET_GPR_U32(threadCtx, 4, info->arg);
            SET_GPR_U32(threadCtx, 31, 0);
            threadCtx->pc = info->entry;
            {   // [threadlog] a thread actually being dispatched (not merely created)
                static const bool s_tl = [](){ const char *v = std::getenv("PS2X_THREADLOG"); return v && v[0] == '1'; }();
                if (s_tl)
                    std::cerr << "[threadlog] StartThread id=" << tid << " entry=0x" << std::hex
                              << info->entry << std::dec << std::endl;
            }

            g_currentThreadId = tid;

            std::cerr << "[thd-start] tid=" << tid << " entry=0x" << std::hex << info->entry << std::dec << std::endl;

            RUNTIME_LOG("[StartThread] id=" << tid
                      << " entry=0x" << std::hex << info->entry
                      << " sp=0x" << GPR_U32(threadCtx, 29)
                      << " gp=0x" << GPR_U32(threadCtx, 28)
                      << " arg=0x" << info->arg << std::dec << std::endl);

            bool exited = false;
            // Deterministic scheduler: register this worker + take the token
            // before running any guest code (parks until it is scheduled).
            const bool schedOn = runtime && runtime->schedEnabled();
            if (schedOn)
            {
                runtime->schedSetTid(tid);
                runtime->schedAcquire(tid, static_cast<int>(info->currentPriority));
            }
            try
            {
                uint32_t &lastPc = *ps2xSchedU32(tid, 1); lastPc = 0xFFFFFFFFu;        // [statesync] runtime-owned
                uint32_t &samePcCount = *ps2xSchedU32(tid, 0); samePcCount = 0;
                constexpr uint32_t kSamePcYieldMask = 0xFFu;
                constexpr uint32_t kSamePcWarnInterval = 0x20000u;
                constexpr uint64_t kSchedQuantum = 1024u;
                // [statesync] runtime-owned (a synced peer adopts it with the rest of the scheduler state)
                uint64_t &stepCount = *ps2xSchedStepCount(tid);
                stepCount = 0u;

                while (runtime && !runtime->isStopRequested())
                {
                    ++stepCount;
        // [eeround2] PS2X_EEROUND2=1: EE chop+FTZ/DAZ only while in-fight (bt3state 0x2d).
        // EEROUND (global, at thread start) was falsified on runs now known to be rig flakes;
        // this variant flips MXCSR at the dispatch boundary from the live state gate.
        {
            static const bool s_eer2 = [](){ const char *v = std::getenv("PS2X_EEROUND2"); return v && v[0] && v[0] != '0'; }();
            if (s_eer2)
            {
                const bool want = g_bt3StateLive.load(std::memory_order_relaxed) == 0x2du;
                static thread_local bool s_eer2Cur = false;
                if (want != s_eer2Cur)
                {
                    s_eer2Cur = want;
                    if (want) _mm_setcsr((_mm_getcsr() & ~0x6000u) | 0x6000u | 0x8040u);
                    else      _mm_setcsr(_mm_getcsr() & ~0xE040u);
                    static std::atomic<bool> s_eer2Said{false}; bool e = false;
                    if (want && s_eer2Said.compare_exchange_strong(e, true))
                        std::fprintf(stderr, "[eeround2] ACTIVE (state=0x2d): EE chop+FTZ\n");
                }
            }
        }
                    if (schedOn && (stepCount % kSchedQuantum) == 0u)
                    {
                        runtime->schedYield(tid);
                    }
                    if (info->terminated.load(std::memory_order_relaxed))
                    {
                        throw ThreadExitException();
                    }

                    waitWhileSuspended(info, runtime);

                    const uint32_t pc = threadCtx->pc;
                    info->currentPc.store(pc, std::memory_order_relaxed);
                    if (pc == 0u)
                    {
                        break;
                    }

                    if ((stepCount & 0x1FFFFFu) == 0u)
                    {
                        RUNTIME_LOG("[StartThread] id=" << tid
                                  << " heartbeat pc=0x" << std::hex << pc
                                  << " ra=0x" << GPR_U32(threadCtx, 31)
                                  << " sp=0x" << GPR_U32(threadCtx, 29)
                                  << " gp=0x" << GPR_U32(threadCtx, 28)
                                  << std::dec << std::endl);
                    }

                    if (pc == lastPc)
                    {
                        ++samePcCount;
                        if ((samePcCount & kSamePcYieldMask) == 0u)
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                        if (samePcCount > kSamePcWarnInterval)
                        {
                            // If a thread is spinning for an extremely long time (e.g. idle thread),
                            // force a 1ms sleep to prevent host CPU starvation.
                            if ((samePcCount % (kSamePcWarnInterval * 8u)) == 0u)
                            {
                                static std::atomic<uint32_t> s_spinDbg{0};
                                if (ps2xContinuousTrace() || s_spinDbg.fetch_add(1) < 400u)
                                    std::cerr << "[spin] tid=" << tid << " pc=0x" << std::hex << pc
                                              << " ra=0x" << GPR_U32(threadCtx, 31) << std::dec << std::endl;
                                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                            }
                            else if ((samePcCount % (kSamePcWarnInterval)) == 0u)
                            {
                                std::this_thread::yield();
                            }
                        }
                    }
                    else
                    {
                        samePcCount = 0;
                        lastPc = pc;
                    }

                    PS2Runtime::RecompiledFunction step = runtime->lookupFunction(pc);
                    if (!step)
                    {
                        std::cerr << "[StartThread] id=" << tid << " missing function for pc=0x"
                                  << std::hex << pc << std::dec << std::endl;
                        throw ThreadExitException();
                    }
                    {
                        PS2Runtime::GuestExecutionScope guestExecution(runtime);
                        step(rdram, threadCtx, runtime);
                    }
                }
            }
            catch (const ThreadExitException &)
            {
                exited = true;
            }
            catch (const std::exception &e)
            {
                std::cerr << "[StartThread] id=" << tid << " exception: " << e.what() << std::endl;
            }

            if (schedOn)
                runtime->schedUnregister(tid);

            std::cerr << "[thd-exit] tid=" << tid << " entry=0x" << std::hex << info->entry
                      << " lastPc=0x" << threadCtx->pc << std::dec
                      << (exited ? " (ExitThread/exception)" : " (fell off / returned)") << std::endl;

            if (!exited)
            {
                RUNTIME_LOG("[StartThread] id=" << tid << " returned (pc=0x"
                          << std::hex << threadCtx->pc << std::dec << ")" << std::endl);
            }

            runExitHandlersForThread(tid, rdram, threadCtx, runtime);

            uint32_t detachedAutoStack = 0;
            {
                std::lock_guard<std::mutex> lock(info->m);
                info->started = false;
                info->status = THS_DORMANT;
                info->waitType = TSW_NONE;
                info->waitId = 0;
                info->wakeupCount = 0;
                info->suspendCount = 0;
                info->forceRelease = false;
                info->terminated = false;
            }

            bool stillRegistered = false;
            {
                std::lock_guard<std::mutex> lock(g_thread_map_mutex);
                stillRegistered = (g_threads.find(tid) != g_threads.end());
            }
            if (!stillRegistered)
            {
                // ExitDeleteThread removes the record immediately; reclaim auto stack here.
                std::lock_guard<std::mutex> lock(info->m);
                if (info->ownsStack && info->stack != 0)
                {
                    detachedAutoStack = info->stack;
                    info->stack = 0;
                    info->stackSize = 0;
                    info->ownsStack = false;
                }
            }

            if (detachedAutoStack != 0 && runtime)
            {
                runtime->guestFree(detachedAutoStack);
            }

            // Notify anybody waiting for termination (like TerminateThread)
            info->cv.notify_all();

            { std::lock_guard<std::mutex> lk(g_workerCtxM); g_workerCtx.erase(tid); }   // [statesync]
            g_activeThreads.fetch_sub(1, std::memory_order_relaxed); };
            if (!(runtime && runtime->fibersEnabled() &&
                  runtime->schedFiberSpawn(tid, static_cast<int>(info->currentPriority), workerBody)))
            {
                std::thread worker(workerBody);
                registerHostThread(tid, std::move(worker));
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "[StartThread] failed to spawn host thread for tid=" << tid << ": " << e.what() << std::endl;
            g_activeThreads.fetch_sub(1, std::memory_order_relaxed);
            std::lock_guard<std::mutex> lock(info->m);
            info->started = false;
            info->status = THS_DORMANT;
            info->waitType = TSW_NONE;
            info->waitId = 0;
            info->wakeupCount = 0;
            info->suspendCount = 0;
            info->forceRelease = false;
            info->terminated = false;
            setReturnS32(ctx, KE_ERROR);
            return;
        }

        setReturnS32(ctx, KE_OK);
    }

    void ExitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2xSchedSignal();   // [fibers] wake site
        RUNTIME_LOG("[ExitThread] Game requested thread exit! PC=0x" << std::hex << ctx->pc
                                                                     << " RA=0x" << getRegU32(ctx, 31) << std::dec << " tid=" << g_currentThreadId << std::endl);

        runExitHandlersForThread(g_currentThreadId, rdram, ctx, runtime);
        auto info = ensureCurrentThreadInfo(ctx);
        if (info)
        {
            std::lock_guard<std::mutex> lock(info->m);
            info->terminated = true;
            info->forceRelease = true;
            info->waitType = TSW_NONE;
            info->waitId = 0;
            info->wakeupCount = 0;
        }
        if (info)
        {
            info->cv.notify_all();
        }
        throw ThreadExitException();
    }

    void ExitDeleteThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2xSchedSignal();   // [fibers] wake site
        int tid = g_currentThreadId;
        RUNTIME_LOG("[ExitDeleteThread] Game requested thread exit & delete! PC=0x" << std::hex << ctx->pc
                                                                                    << " RA=0x" << getRegU32(ctx, 31) << std::dec << " tid=" << tid << std::endl);

        runExitHandlersForThread(tid, rdram, ctx, runtime);
        auto info = ensureCurrentThreadInfo(ctx);
        if (info)
        {
            std::lock_guard<std::mutex> lock(info->m);
            info->terminated = true;
            info->forceRelease = true;
            info->waitType = TSW_NONE;
            info->waitId = 0;
            info->wakeupCount = 0;
        }
        if (info)
        {
            info->cv.notify_all();
        }
        {
            std::lock_guard<std::mutex> lock(g_thread_map_mutex);
            g_threads.erase(tid);
        }
        throw ThreadExitException();
    }

    void TerminateThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2xSchedSignal();   // [fibers] wake site
        int tid = static_cast<int>(getRegU32(ctx, 4));
        if (tid == 0)
            tid = g_currentThreadId;

        auto info = (tid == g_currentThreadId) ? ensureCurrentThreadInfo(ctx) : lookupThreadInfo(tid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_THID);
            return;
        }

        int waitType = TSW_NONE;
        int waitId = 0;
        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->status == THS_DORMANT)
            {
                setReturnS32(ctx, KE_DORMANT);
                return;
            }
            waitType = info->waitType;
            waitId = info->waitId;
            info->terminated = true;
            info->forceRelease = true;
        }
        info->cv.notify_all();
        notifyThreadWaitObject(waitType, waitId);

        if (tid == g_currentThreadId)
        {
            runExitHandlersForThread(tid, rdram, ctx, runtime);
            throw ThreadExitException();
        }
        else
        {
            // Block until the target thread actually finishes unwinding and becomes dormant.
            // Drop the thread mutex before reacquiring GuestExecutionScope to avoid lock inversion.
            std::unique_lock<std::mutex> lock(info->m);
            // [fibers] Woken by another guest thread (whoever makes this one dormant).
            waitGuestUntil(
                runtime, lock, info->cv,
                [&]() { return !info->started && info->status == THS_DORMANT; },
                WP_SYNC_OTHER);
        }

        setReturnS32(ctx, KE_OK);
    }

    // The EE kernel returns the THREAD ID (not 0) from SuspendThread/ResumeThread on
    // success. BT3's sound middleware depends on it: the sound service thread (tid6) is
    // created, started, then suspended, and its only waker is
    //     FUN_0026e160:  if (sub_0026D338(tid6) == tid6) FUN_0026d2d0(tid6);
    //     sub_0026D338:  if (status == THS_SUSPEND || status == THS_WAITSUSPEND)
    //                        return ResumeThread(tid);        // must yield tid
    //                    return 0;
    // Returning KE_OK(0) made that guard dead code AND consumed the suspend on the first
    // kick, leaving tid6 in plain THS_WAIT(4) so every later kick short-circuited before
    // even calling ResumeThread -> the sound thread slept forever, its slot-6 handler was
    // never registered, the preload completion never ran, and the game never requested a
    // single sound. Four independent call sites use the `== tid` idiom, so this is the ABI.
    // Set PS2X_THIDRET=0 to restore the old KE_OK returns.
    static bool thidRetEnabled()
    {
        static const bool s_on = []() {
            const char *v = std::getenv("PS2X_THIDRET");
            return !(v && v[0] == '0');
        }();
        return s_on;
    }

    void SuspendThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ps2xSchedTraceOn()) std::fprintf(stderr, "[schedtrace] SuspendThread tid=%d by=%d\n", (int)getRegU32(ctx, 4), g_currentThreadId);
        int tid = static_cast<int>(getRegU32(ctx, 4));
        if (tid == 0)
            tid = g_currentThreadId;

        auto info = (tid == g_currentThreadId) ? ensureCurrentThreadInfo(ctx) : lookupThreadInfo(tid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_THID);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->status == THS_DORMANT)
            {
                setReturnS32(ctx, KE_DORMANT);
                return;
            }
            info->suspendCount++;
            applySuspendStatusLocked(*info);
        }
        info->cv.notify_all();

        if (tid == g_currentThreadId)
        {
            std::unique_lock<std::mutex> lock(info->m);
            bool terminated = false;
            // [fibers] ResumeThread is issued by another guest thread, so this must park in the
            // scheduler rather than block the shared host thread.
            waitGuestUntil(
                runtime, lock, info->cv,
                [&]() { return info->suspendCount == 0 || info->terminated.load(); },
                WP_SYNC_OTHER,
                [&]()
                {
                    terminated = info->terminated.load();
                    if (!terminated)
                    {
                        info->status = THS_RUN;
                    }
                });

            if (terminated)
            {
                throw ThreadExitException();
            }
        }

        setReturnS32(ctx, thidRetEnabled() ? tid : KE_OK);
    }

    void ResumeThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ps2xSchedTraceOn()) std::fprintf(stderr, "[schedtrace] ResumeThread tid=%d by=%d\n", (int)getRegU32(ctx, 4), g_currentThreadId);
        ps2xSchedSignal();   // [fibers] wake site
        int tid = static_cast<int>(getRegU32(ctx, 4));
        if (tid == 0)
            tid = g_currentThreadId;

        auto info = (tid == g_currentThreadId) ? ensureCurrentThreadInfo(ctx) : lookupThreadInfo(tid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_THID);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->status == THS_DORMANT)
            {
                setReturnS32(ctx, KE_DORMANT);
                return;
            }
            if (info->suspendCount <= 0)
            {
                setReturnS32(ctx, KE_NOT_SUSPEND);
                return;
            }
            info->suspendCount--;
            if (info->suspendCount == 0)
            {
                if (info->waitType != TSW_NONE)
                {
                    info->status = THS_WAIT;
                }
                else
                {
                    info->status = (tid == g_currentThreadId) ? THS_RUN : THS_READY;
                }
            }
        }
        info->cv.notify_all();
        setReturnS32(ctx, thidRetEnabled() ? tid : KE_OK);
    }

    void GetThreadId(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, g_currentThreadId);
    }

    void ReferThreadStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        uint32_t statusAddr = getRegU32(ctx, 5);

        if (tid == 0) // TH_SELF
        {
            tid = g_currentThreadId;
        }

        auto info = (tid == g_currentThreadId) ? ensureCurrentThreadInfo(ctx) : lookupThreadInfo(tid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_THID);
            return;
        }

        ee_thread_status_t *status = reinterpret_cast<ee_thread_status_t *>(getMemPtr(rdram, statusAddr));
        if (!status)
        {
            setReturnS32(ctx, KE_ERROR);
            return;
        }

        std::lock_guard<std::mutex> lock(info->m);
        status->status = info->status;
        status->func = info->entry;
        status->stack = info->stack;
        status->stack_size = info->stackSize;
        status->gp_reg = info->gp;
        status->initial_priority = info->priority;
        status->current_priority = info->currentPriority;
        status->attr = info->attr;
        status->option = info->option;
        status->waitType = info->waitType;
        status->waitId = info->waitId;
        status->wakeupCount = info->wakeupCount;
        setReturnS32(ctx, KE_OK);
    }

    void iReferThreadStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ReferThreadStatus(rdram, ctx, runtime);
    }

    void SleepThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        auto info = ensureCurrentThreadInfo(ctx);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_THID);
            return;
        }

        throwIfTerminated(info);

        int ret = 0;
        int wakeupCountAfter = 0;
        bool terminated = false;
        std::unique_lock<std::mutex> lock(info->m);

        if (info->wakeupCount > 0)
        {
            info->wakeupCount--;
            info->status = THS_RUN;
            info->waitType = TSW_NONE;
            info->waitId = 0;
            ret = 0;
            wakeupCountAfter = info->wakeupCount;
        }
        else
        {
            static std::atomic<uint32_t> s_sleepBlockLogs{0};
            const uint32_t sleepBlockLog = s_sleepBlockLogs.fetch_add(1, std::memory_order_relaxed);
            if (ps2xContinuousTrace())
                std::cerr << "[thread-trace] sleep-block tid=" << g_currentThreadId
                          << " pc=0x" << std::hex << ctx->pc
                          << " ra=0x" << getRegU32(ctx,31) << std::dec << std::endl;
            if (sleepBlockLog < 256u)
            {
                RUNTIME_LOG("[SleepThread:block] tid=" << g_currentThreadId
                                                       << " pc=0x" << std::hex << ctx->pc
                                                       << " ra=0x" << getRegU32(ctx, 31)
                                                       << std::dec << std::endl);
            }

            static std::atomic<uint32_t> s_sleepDbg{0};
            if (g_currentThreadId == 1 && s_sleepDbg.fetch_add(1) < 3u)
            {
                std::cerr << "[sleep-main] ra=0x" << std::hex << getRegU32(ctx, 31)
                          << " sp=0x" << getRegU32(ctx, 29) << " stack-retaddrs:";
                const uint32_t sp = getRegU32(ctx, 29);
                for (uint32_t off = 0; off < 0x200; off += 4)
                {
                    const uint8_t *p = getMemPtr(rdram, sp + off);
                    if (!p) continue;
                    uint32_t v; std::memcpy(&v, p, 4);
                    if (v >= 0x100008u && v < 0x2bf69cu && (v & 3) == 0)
                        std::cerr << " 0x" << v;
                }
                std::cerr << std::dec << std::endl;
            }

            info->status = THS_WAIT;
            info->waitType = TSW_SLEEP;
            info->waitId = 0;
            info->forceRelease = false;

            // [fibers] SleepThread is woken by ANOTHER GUEST THREAD, so under PS2X_FIBERS the waker
            // is a fiber on this very host thread -- a condition_variable wait here would park the
            // only thread that could ever deliver the wakeup. waitGuestUntil parks in the scheduler
            // instead. The predicate only reads state, so re-checking it is free of side effects.
            waitGuestUntil(
                runtime,
                lock,
                info->cv,
                [&]()
                { return info->wakeupCount > 0 || info->forceRelease.load() || info->terminated.load(); },
                WP_THREAD_SLEEP,
                [&]()
                {
                    terminated = info->terminated.load();
                    if (terminated)
                    {
                        return;
                    }

                    info->status = THS_RUN;
                    info->waitType = TSW_NONE;
                    info->waitId = 0;

                    if (info->forceRelease.load())
                    {
                        info->forceRelease = false;
                        ret = KE_RELEASE_WAIT;
                    }
                    else
                    {
                        if (info->wakeupCount > 0)
                        {
                            info->wakeupCount--;
                        }
                        ret = 0;
                    }
                    wakeupCountAfter = info->wakeupCount;
                });
        }

        if (terminated)
        {
            throw ThreadExitException();
        }

        static std::atomic<uint32_t> s_sleepWakeLogs{0};
        const uint32_t sleepWakeLog = s_sleepWakeLogs.fetch_add(1, std::memory_order_relaxed);
        if (ps2xContinuousTrace())
            std::cerr << "[thread-trace] sleep-wake tid=" << g_currentThreadId
                      << " ret=" << ret << " wakeupCount=" << wakeupCountAfter << std::endl;
        if (sleepWakeLog < 256u)
        {
            RUNTIME_LOG("[SleepThread:wake] tid=" << g_currentThreadId
                                                  << " ret=" << ret
                                                  << " wakeupCount=" << wakeupCountAfter
                                                  << std::endl);
        }

        if (lock.owns_lock())
        {
            lock.unlock();
        }
        waitWhileSuspended(info, runtime);
        setReturnS32(ctx, ret);
    }

    void WakeupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ps2xSchedTraceOn()) std::fprintf(stderr, "[schedtrace] WakeupThread tid=%d by=%d\n", (int)getRegU32(ctx, 4), g_currentThreadId);
        ps2xSchedSignal();   // [fibers] wake site
        int tid = static_cast<int>(getRegU32(ctx, 4));
        if (tid == 0)
        {
            setReturnS32(ctx, KE_ILLEGAL_THID);
            return;
        }
        if (tid == g_currentThreadId)
        {
            setReturnS32(ctx, KE_ILLEGAL_THID);
            return;
        }

        auto info = lookupThreadInfo(tid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_THID);
            return;
        }

        static std::atomic<uint32_t> s_wakeDbg{0};
        uint32_t wn = s_wakeDbg.fetch_add(1);
        if (ps2xContinuousTrace() || wn < 200u || (wn % 200u) == 0u)
            std::cerr << "[wake] n=" << wn << " caller=" << g_currentThreadId << " target=" << tid << std::endl;

        int newWakeupCount = 0;
        int statusAfter = THS_DORMANT;
        bool wokeSleeper = false;
        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->status == THS_DORMANT)
            {
                setReturnS32(ctx, KE_DORMANT);
                return;
            }
            if (info->status == THS_WAIT && info->waitType == TSW_SLEEP)
            {
                if (info->suspendCount > 0)
                {
                    info->status = THS_SUSPEND;
                }
                else
                {
                    info->status = THS_READY;
                }
                info->waitType = TSW_NONE;
                info->waitId = 0;
                info->wakeupCount++;
                info->cv.notify_one();
                wokeSleeper = true;
            }
            else
            {
                info->wakeupCount++;
            }
            newWakeupCount = info->wakeupCount;
            statusAfter = info->status;
        }

        static std::atomic<uint32_t> s_wakeupLogs{0};
        const uint32_t wakeupLog = s_wakeupLogs.fetch_add(1, std::memory_order_relaxed);
        if (ps2xContinuousTrace())
            std::cerr << "[thread-trace] wake tid=" << g_currentThreadId
                      << " target=" << tid << " status=" << statusAfter
                      << " wakeupCount=" << newWakeupCount << std::endl;
        if (wakeupLog < 256u)
        {
            RUNTIME_LOG("[WakeupThread] tid=" << g_currentThreadId
                                              << " target=" << tid
                                              << " status=" << statusAfter
                                              << " wakeupCount=" << newWakeupCount
                                              << std::endl);
        }
        setReturnS32(ctx, KE_OK);
        if (wokeSleeper)
        {
            yieldGuestExecutionAfterWake(runtime);
        }
    }

    void iWakeupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ps2xSchedTraceOn()) std::fprintf(stderr, "[schedtrace] iWakeupThread tid=%d by=%d\n", (int)getRegU32(ctx, 4), g_currentThreadId);
        ps2xSchedSignal();   // [fibers] wake site
        WakeupThread(rdram, ctx, runtime);
    }

    void CancelWakeupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        if (tid == 0)
            tid = g_currentThreadId;

        auto info = (tid == g_currentThreadId) ? ensureCurrentThreadInfo(ctx) : lookupThreadInfo(tid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_THID);
            return;
        }

        int previous = 0;
        {
            std::lock_guard<std::mutex> lock(info->m);
            previous = info->wakeupCount;
            info->wakeupCount = 0;
        }
        setReturnS32(ctx, previous);
    }

    void iCancelWakeupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        if (tid == 0)
        {
            setReturnS32(ctx, KE_ILLEGAL_THID);
            return;
        }

        auto info = lookupThreadInfo(tid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_THID);
            return;
        }

        int previous = 0;
        {
            std::lock_guard<std::mutex> lock(info->m);
            previous = info->wakeupCount;
            info->wakeupCount = 0;
        }
        setReturnS32(ctx, previous);
    }

    void ChangeThreadPriority(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        int newPrio = static_cast<int>(getRegU32(ctx, 5));

        if (tid == 0)
            tid = g_currentThreadId;

        auto info = (tid == g_currentThreadId) ? ensureCurrentThreadInfo(ctx) : lookupThreadInfo(tid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_THID);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->status == THS_DORMANT)
            {
                setReturnS32(ctx, KE_DORMANT);
                return;
            }

            if (newPrio == 0)
            {
                newPrio = (info->currentPriority > 0) ? info->currentPriority : 1;
            }
            if (newPrio <= 0 || newPrio >= 128)
            {
                setReturnS32(ctx, KE_ILLEGAL_PRIORITY);
                return;
            }

            info->currentPriority = newPrio;
        }

        setReturnS32(ctx, KE_OK);
    }

    void iChangeThreadPriority(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ChangeThreadPriority(rdram, ctx, runtime);
    }

    void RotateThreadReadyQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        int prio = static_cast<int>(getRegU32(ctx, 4));
        if (prio == 0)
        {
            auto current = ensureCurrentThreadInfo(ctx);
            if (current)
            {
                std::lock_guard<std::mutex> lock(current->m);
                prio = (current->currentPriority > 0) ? current->currentPriority : 1;
            }
        }
        if (logCount < 16)
        {
            RUNTIME_LOG("[RotateThreadReadyQueue] prio=" << prio);
            ++logCount;
        }
        if (prio <= 0 || prio >= 128)
        {
            setReturnS32(ctx, KE_ILLEGAL_PRIORITY);
            return;
        }

        setReturnS32(ctx, KE_OK);
        yieldGuestExecutionAfterWake(runtime);
    }

    void iRotateThreadReadyQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        RotateThreadReadyQueue(rdram, ctx, runtime);
    }

    void ReleaseWaitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2xSchedSignal();   // [fibers] wake site
        int tid = static_cast<int>(getRegU32(ctx, 4));
        if (tid == 0 || tid == g_currentThreadId)
        {
            setReturnS32(ctx, KE_ILLEGAL_THID);
            return;
        }

        auto info = lookupThreadInfo(tid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_THID);
            return;
        }

        bool wasWaiting = false;
        int waitType = 0;
        int waitId = 0;

        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->status == THS_WAIT || info->status == THS_WAITSUSPEND)
            {
                wasWaiting = true;
                waitType = info->waitType;
                waitId = info->waitId;
                info->forceRelease = true;
                info->waitType = TSW_NONE;
                info->waitId = 0;
                if (info->suspendCount > 0)
                {
                    info->status = THS_SUSPEND;
                }
                else
                {
                    info->status = THS_READY;
                }
            }
        }

        if (!wasWaiting)
        {
            setReturnS32(ctx, KE_NOT_WAIT);
            return;
        }

        info->cv.notify_all();
        notifyThreadWaitObject(waitType, waitId);
        setReturnS32(ctx, KE_OK);
        yieldGuestExecutionAfterWake(runtime);
    }

    void iReleaseWaitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2xSchedSignal();   // [fibers] wake site
        ReleaseWaitThread(rdram, ctx, runtime);
    }

    std::string dumpAllThreadStates()
    {
        static const char *stName[] = {"?", "RUN", "RDY", "?", "WAIT", "?", "?", "?", "SUSP", "?", "?", "?", "WSUS", "?", "?", "?", "DORM"};
        std::lock_guard<std::mutex> lock(g_thread_map_mutex);
        std::string out;
        char buf[160];
        for (const auto &kv : g_threads)
        {
            const auto &info = kv.second;
            if (!info) continue;
            const int st = info->status & 0x1f;
            snprintf(buf, sizeof(buf), "t%d[e=%x pc=%x %s wt=%d wc=%d] ",
                     kv.first, info->entry, info->currentPc.load(std::memory_order_relaxed),
                     (st < 17 ? stName[st] : "?"), info->waitType, info->wakeupCount);
            out += buf;
        }
        return out;
    }

    // BT3 experiment: nudge a SLEEPING thread identified by its entry point so it
    // runs another loop iteration of its real code. Non-destructive: only bumps a
    // sleeping thread (exactly like WakeupThread), never fakes state. Used to test
    // whether the sound service thread (entry 0x26d070) just needs to be pumped.
    // Returns true if a matching sleeping thread was woken.
    bool bt3WakeThreadByEntry(uint32_t entry)
    {
        std::shared_ptr<ThreadInfo> target;
        {
            std::lock_guard<std::mutex> lock(g_thread_map_mutex);
            for (auto &kv : g_threads)
            {
                if (kv.second && kv.second->entry == entry)
                {
                    target = kv.second;
                    break;
                }
            }
        }
        if (!target) return false;
        std::lock_guard<std::mutex> lock(target->m);
        if (target->status == THS_WAIT && target->waitType == TSW_SLEEP)
        {
            target->status = (target->suspendCount > 0) ? THS_SUSPEND : THS_READY;
            target->waitType = TSW_NONE;
            target->waitId = 0;
            target->wakeupCount++;
            target->cv.notify_one();
            return true;
        }
        return false;
    }
}

// [schedwhy2] wait reason of a guest thread for the [sched-state] dump: wait type / id, and for a semaphore wait the
// semaphore's current count and waiter count. Returns false if the thread is unknown.
extern "C" bool ps2xThreadWaitInfo(int tid, int *waitType, int *waitId, int *semaCount, int *semaWaiters, int *wakeupCount)
{
    auto info = lookupThreadInfo(tid);
    if (!info) return false;
    std::lock_guard<std::mutex> lock(info->m);
    *waitType = info->waitType; *waitId = info->waitId; *wakeupCount = info->wakeupCount; *semaCount = -1; *semaWaiters = -1;
    if (info->waitType == TSW_SEMA)
    {
        auto sema = lookupSemaInfo(info->waitId);
        if (sema) { *semaCount = sema->count; *semaWaiters = sema->waiters; }
    }
    return true;
}

// [fibers] The kernel's per-thread identity, exposed so PS2Runtime::schedFiberLoop can swap it per
// fiber. g_currentThreadId is thread_local, and under PS2X_FIBERS every guest thread shares one
// host thread: without the swap the last StartThread's tid became everyone's identity, and
// SleepThread/GetThreadId/ensureCurrentThreadInfo (which key on it) all acted on the wrong thread.
int  ps2xKernelCurrentTid() { return g_currentThreadId; }
void ps2xKernelSetCurrentTid(int tid) { g_currentThreadId = tid; }

// [rollback] The host side of the EE kernel: thread records, semaphores, event flags and the id
// counters, plus the vsync tick/registration (Interrupt.cpp). A rolled-back fiber parked inside
// SleepThread is inconsistent with a live ThreadInfo that says RUN, and the game's timer service
// consults ReferThreadStatus before waking anyone -- that was the first post-rollback freeze.
// Alarms are not covered (BT3 sets none). Records the live maps have that the snapshot lacks are
// left alone; ones the snapshot has that vanished are reported and skipped.
namespace
{
    struct KThread { int tid; uint32_t entry, stack, stackSize, gp, priority, attr, option, arg, tlsBase, currentPc;
                     bool started, ownsStack, terminated, forceRelease; int status, waitType, waitId, wakeupCount, currentPriority, suspendCount; };
    struct KSema   { int id; int count, maxCount, initCount; uint32_t attr, option; int waiters; bool deleted; };
    struct KEvf    { int id; uint32_t attr, option, initBits, bits; int waiters; bool deleted; };
    struct KernelSnap { std::vector<KThread> th; std::vector<KSema> se; std::vector<KEvf> ev;
                        int nextThread = 2, nextSema = 1, nextEvf = 1; uint64_t vsyncTick = 0; uint32_t vsFlag = 0, vsTick = 0;
                        uint64_t cdFrame = 0; uint32_t cdPumps = 0, cdTicks = 0;   // [cdgate] the CD pump's gate counters
                        // SIF RPC bookkeeping (State.h) and the SIF stub's own state (SIF.cpp)
                        std::unordered_map<uint32_t, RpcServerState> rpcServers; std::unordered_map<uint32_t, RpcClientState> rpcClients;
                        uint64_t rpcSeq = 0; bool rpcInit = false; uint32_t rpcNextId = 1, rpcPacket = 0, rpcServer = 0, rpcQueue = 0;
                        void *sif = nullptr;
                        void *mc = nullptr;    // [statesync] Kernel/Stubs/MemoryCard.cpp
                        ~KernelSnap(); };
    extern "C" void ps2xSifStateFree(void *);
    extern "C" void ps2xMcStateFree(void *);
    KernelSnap::~KernelSnap() { if (sif) ps2xSifStateFree(sif); if (mc) ps2xMcStateFree(mc); }
}
extern "C" void *ps2xMcStateCapture();
extern "C" bool ps2xMcStateRestore(void *);
extern "C" bool ps2xMcStateSerialize(const void *, std::vector<uint8_t> &);
extern "C" void *ps2xMcStateDeserialize(const uint8_t *, size_t, size_t *);
extern "C" void ps2xVsyncStateGet(uint64_t *tick, uint32_t *flagAddr, uint32_t *tickAddr, uint64_t *cdFrame, uint32_t *cdPumps, uint32_t *cdTicks);
extern "C" void ps2xVsyncStateSet(uint64_t tick, uint32_t flagAddr, uint32_t tickAddr, uint64_t cdFrame, uint32_t cdPumps, uint32_t cdTicks);
extern "C" void *ps2xSifStateCapture();
extern "C" bool ps2xSifStateRestore(void *);
extern "C" void *ps2xKernelStateCapture()
{
    KernelSnap *s = new KernelSnap();
    {
        std::lock_guard<std::mutex> lk(g_rpc_mutex);
        s->rpcServers = g_rpc_servers; s->rpcClients = g_rpc_clients; s->rpcSeq = g_sif_rpc_debug_next_seq;
        s->rpcInit = g_rpc_initialized; s->rpcNextId = g_rpc_next_id; s->rpcPacket = g_rpc_packet_index;
        s->rpcServer = g_rpc_server_index; s->rpcQueue = g_rpc_active_queue;
    }
    s->sif = ps2xSifStateCapture();
    s->mc = ps2xMcStateCapture();
    {
        std::lock_guard<std::mutex> lk(g_thread_map_mutex);
        for (auto &kv : g_threads)
        {
            if (!kv.second) continue;
            ThreadInfo &i = *kv.second;
            std::lock_guard<std::mutex> il(i.m);
            s->th.push_back(KThread{ kv.first, i.entry, i.stack, i.stackSize, i.gp, i.priority, i.attr, i.option, i.arg, i.tlsBase,
                                     i.currentPc.load(), i.started, i.ownsStack, i.terminated.load(), i.forceRelease.load(),
                                     i.status, i.waitType, i.waitId, i.wakeupCount, i.currentPriority, i.suspendCount });
        }
        s->nextThread = g_nextThreadId;
    }
    {
        std::lock_guard<std::mutex> lk(g_sema_map_mutex);
        for (auto &kv : g_semas)
        {
            if (!kv.second) continue;
            SemaInfo &i = *kv.second; std::lock_guard<std::mutex> il(i.m);
            s->se.push_back(KSema{ kv.first, i.count, i.maxCount, i.initCount, i.attr, i.option, i.waiters, i.deleted });
        }
        s->nextSema = g_nextSemaId;
    }
    {
        std::lock_guard<std::mutex> lk(g_event_flag_map_mutex);
        for (auto &kv : g_eventFlags)
        {
            if (!kv.second) continue;
            EventFlagInfo &i = *kv.second; std::lock_guard<std::mutex> il(i.m);
            s->ev.push_back(KEvf{ kv.first, i.attr, i.option, i.initBits, i.bits, i.waiters, i.deleted });
        }
        s->nextEvf = g_nextEventFlagId;
    }
    ps2xVsyncStateGet(&s->vsyncTick, &s->vsFlag, &s->vsTick, &s->cdFrame, &s->cdPumps, &s->cdTicks);
    {   // PS2X_KSNAPLOG=1: what the snapshot holds per thread (status / wait / wakeups / suspends)
        static const bool s_log = [](){ const char *v = std::getenv("PS2X_KSNAPLOG"); return v && v[0] && v[0] != '0'; }();
        if (s_log)
        {
            std::fprintf(stderr, "[ksnap] capture:");
            for (const KThread &t : s->th) std::fprintf(stderr, " tid%d(st=%d wt=%d wid=%d wk=%d sus=%d pc=0x%x)", t.tid, t.status, t.waitType, t.waitId, t.wakeupCount, t.suspendCount, t.currentPc);
            std::fprintf(stderr, "\n");
        }
    }
    return s;
}
extern "C" bool ps2xKernelStateRestore(void *h)
{
    const KernelSnap *s = static_cast<const KernelSnap *>(h);
    if (!s) return false;
    {
        std::lock_guard<std::mutex> lk(g_thread_map_mutex);
        for (const KThread &t : s->th)
        {
            auto it = g_threads.find(t.tid);
            if (it == g_threads.end() || !it->second) { std::fprintf(stderr, "[rollback] kernel: thread %d vanished\n", t.tid); continue; }
            ThreadInfo &i = *it->second;
            { std::lock_guard<std::mutex> il(i.m);
              i.entry = t.entry; i.stack = t.stack; i.stackSize = t.stackSize; i.gp = t.gp; i.priority = t.priority; i.attr = t.attr;
              i.option = t.option; i.arg = t.arg; i.tlsBase = t.tlsBase; i.currentPc.store(t.currentPc); i.started = t.started;
              i.ownsStack = t.ownsStack; i.terminated.store(t.terminated); i.forceRelease.store(t.forceRelease);
              i.status = t.status; i.waitType = t.waitType; i.waitId = t.waitId; i.wakeupCount = t.wakeupCount;
              i.currentPriority = t.currentPriority; i.suspendCount = t.suspendCount; }
            i.cv.notify_all();
        }
        g_nextThreadId = s->nextThread;
    }
    {
        std::lock_guard<std::mutex> lk(g_sema_map_mutex);
        for (const KSema &t : s->se)
        {
            auto it = g_semas.find(t.id);
            if (it == g_semas.end() || !it->second)
            {   // [statesync] a synced peer created it after we diverged: make the record
                std::fprintf(stderr, "[rollback] kernel: sema %d missing here, created\n", t.id);
                g_semas[t.id] = std::make_shared<SemaInfo>(); it = g_semas.find(t.id);
            }
            SemaInfo &i = *it->second;
            { std::lock_guard<std::mutex> il(i.m); i.count = t.count; i.maxCount = t.maxCount; i.initCount = t.initCount; i.attr = t.attr; i.option = t.option; i.waiters = t.waiters; i.deleted = t.deleted; }
            i.cv.notify_all();
        }
        g_nextSemaId = s->nextSema;
    }
    {
        std::lock_guard<std::mutex> lk(g_event_flag_map_mutex);
        for (const KEvf &t : s->ev)
        {
            auto it = g_eventFlags.find(t.id);
            if (it == g_eventFlags.end() || !it->second)
            {
                std::fprintf(stderr, "[rollback] kernel: evf %d missing here, created\n", t.id);
                g_eventFlags[t.id] = std::make_shared<EventFlagInfo>(); it = g_eventFlags.find(t.id);
            }
            EventFlagInfo &i = *it->second;
            { std::lock_guard<std::mutex> il(i.m); i.attr = t.attr; i.option = t.option; i.initBits = t.initBits; i.bits = t.bits; i.waiters = t.waiters; i.deleted = t.deleted; }
            i.cv.notify_all();
        }
        g_nextEventFlagId = s->nextEvf;
    }
    ps2xVsyncStateSet(s->vsyncTick, s->vsFlag, s->vsTick, s->cdFrame, s->cdPumps, s->cdTicks);
    {
        std::lock_guard<std::mutex> lk(g_rpc_mutex);
        g_rpc_servers = s->rpcServers; g_rpc_clients = s->rpcClients; g_sif_rpc_debug_next_seq = s->rpcSeq;
        g_rpc_initialized = s->rpcInit; g_rpc_next_id = s->rpcNextId; g_rpc_packet_index = s->rpcPacket;
        g_rpc_server_index = s->rpcServer; g_rpc_active_queue = s->rpcQueue;
    }
    if (s->sif && !ps2xSifStateRestore(s->sif)) return false;
    if (s->mc && !ps2xMcStateRestore(s->mc)) return false;
    {
        static const bool s_log = [](){ const char *v = std::getenv("PS2X_KSNAPLOG"); return v && v[0] && v[0] != '0'; }();
        if (s_log)
        {
            std::fprintf(stderr, "[ksnap] restore:");
            for (const KThread &t : s->th) std::fprintf(stderr, " tid%d(st=%d wt=%d wid=%d wk=%d sus=%d)", t.tid, t.status, t.waitType, t.waitId, t.wakeupCount, t.suspendCount);
            std::fprintf(stderr, "\n");
        }
    }
    return true;
}
extern "C" void ps2xKernelStateFree(void *h) { delete static_cast<KernelSnap *>(h); }
// [statesync] portable form (the SIF part is its own sub-blob)
extern "C" bool ps2xKernelStateSerialize(const void *h, std::vector<uint8_t> &out)
{
    const KernelSnap *s = static_cast<const KernelSnap *>(h);
    if (!s) return false;
    Ps2xByteW w(out);
    w.u32(0x4b524e31u);   // 'KRN1'
    w.podVec(s->th); w.podVec(s->se); w.podVec(s->ev);
    w.pod(s->nextThread); w.pod(s->nextSema); w.pod(s->nextEvf);
    w.u64(s->vsyncTick); w.u32(s->vsFlag); w.u32(s->vsTick);
    w.u64(s->cdFrame); w.u32(s->cdPumps); w.u32(s->cdTicks);
    w.podUMap(s->rpcServers); w.podUMap(s->rpcClients);
    w.u64(s->rpcSeq); w.u8(s->rpcInit); w.u32(s->rpcNextId); w.u32(s->rpcPacket); w.u32(s->rpcServer); w.u32(s->rpcQueue);
    w.u8(s->sif != nullptr);
    if (s->sif && !ps2xSifStateSerialize(s->sif, out)) return false;
    w.u8(s->mc != nullptr);
    if (s->mc && !ps2xMcStateSerialize(s->mc, out)) return false;
    return true;
}
extern "C" void *ps2xKernelStateDeserialize(const uint8_t *data, size_t n, size_t *used)
{
    Ps2xByteR r(data, n);
    if (r.u32() != 0x4b524e31u) return nullptr;
    KernelSnap *s = new KernelSnap();
    r.podVec(s->th); r.podVec(s->se); r.podVec(s->ev);
    s->nextThread = r.pod<int>(); s->nextSema = r.pod<int>(); s->nextEvf = r.pod<int>();
    s->vsyncTick = r.u64(); s->vsFlag = r.u32(); s->vsTick = r.u32();
    s->cdFrame = r.u64(); s->cdPumps = r.u32(); s->cdTicks = r.u32();
    r.podUMap(s->rpcServers); r.podUMap(s->rpcClients);
    s->rpcSeq = r.u64(); s->rpcInit = r.u8() != 0; s->rpcNextId = r.u32(); s->rpcPacket = r.u32(); s->rpcServer = r.u32(); s->rpcQueue = r.u32();
    const bool hasSif = r.u8() != 0;
    if (!r.ok) { delete s; return nullptr; }
    if (hasSif)
    {
        size_t sub = 0;
        s->sif = ps2xSifStateDeserialize(r.p, r.left(), &sub);
        if (!s->sif) { delete s; return nullptr; }
        r.p += sub;
    }
    const bool hasMc = r.u8() != 0;
    if (hasMc && r.ok)
    {
        size_t sub = 0;
        s->mc = ps2xMcStateDeserialize(r.p, r.left(), &sub);
        if (!s->mc) { delete s; return nullptr; }
        r.p += sub;
    }
    if (!r.ok) { delete s; return nullptr; }
    if (used) *used = (size_t)(r.p - data);
    return s;
}
