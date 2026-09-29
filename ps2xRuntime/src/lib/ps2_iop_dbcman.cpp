#include "runtime/ps2_iop_dbcman.h"
#include "runtime/ps2_memory.h"

#include <cstring>
#include <iostream>
#include <cstdlib>
#include <atomic>
#include <map>
#include <mutex>

namespace
{
    constexpr uint32_t kDbcManSid = 0x80001300u;
    constexpr uint32_t kRpcCheckVersion = 0x80001363u;
    // sceDbc pad transaction RPC (FUN_00295c60): status/data exchange on the
    // shared 0x90-byte buffer. Layout (relative to the buffer base):
    //   +0x08 len, +0x0c data[0..], +0x8c return status.
    // The pad handler treats data[0]==1 as "controller ready" and breaks its
    // boot wait; when no button data is present the read path degrades to
    // "no input". So respond as a connected controller with neutral input.
    constexpr uint32_t kRpcPadTransaction = 0x8000131au;
    // sceDbc connect probe FUN_00295160 does this RPC once the connection-state
    // table reports the port connected; it reads the reply's +0x04 word as the
    // result (>=0 == success).
    constexpr uint32_t kRpcPadConnect = 0x80001303u;
    // Guest connection-state table (DAT_00323dc0). FUN_00295160 gates on
    // *(0x323dc0 + port*4) == 1. Report controller port 0 connected.
    constexpr uint32_t kPadConnStateAddr = 0x00323dc0u;
    constexpr uint32_t kDbcManVersion = 0x0320u;
    constexpr uint32_t kMaxUnknownRpcLogs = 32u;

    uint32_t g_unknownRpcLogCount = 0u;

    bool writeGuestU32(uint8_t *rdram, uint32_t addr, uint32_t value)
    {
        uint8_t *ptr = getMemPtr(rdram, addr);
        if (!ptr)
        {
            return false;
        }

        std::memcpy(ptr, &value, sizeof(value));
        return true;
    }

    void writeVersionResponse(uint8_t *rdram, uint32_t recvBufAddr, uint32_t recvSize)
    {
        const uint32_t words = recvSize / sizeof(uint32_t);
        const uint32_t count = words < 4u ? words : 4u;
        for (uint32_t index = 0; index < count; ++index)
        {
            writeGuestU32(rdram, recvBufAddr + (index * sizeof(uint32_t)), kDbcManVersion);
        }
    }
}

namespace ps2_iop_dbcman
{
    void reset()
    {
        g_unknownRpcLogCount = 0u;
    }

    bool handleDbcManRpc(uint8_t *rdram,
                         uint32_t sid,
                         uint32_t rpcNum,
                         uint32_t sendBufAddr,
                         uint32_t sendSize,
                         uint32_t recvBufAddr,
                         uint32_t recvSize,
                         uint32_t &resultPtr)
    {
        if (sid != kDbcManSid)
        {
            return false;
        }

        // NOTE: reporting the pad connected here (writing kPadConnStateAddr and
        // answering kRpcPadConnect) advances past the black-screen controller
        // wait, but a "connected" pad without a working data channel
        // (sceDbcReceiveData) destabilizes early init and hangs in a CDV read.
        // Left disabled until the full sceDbc data path is emulated (task #18).

        resultPtr = recvBufAddr;
        if (recvBufAddr == 0u || recvSize == 0u)
        {
            return true;
        }

        switch (rpcNum)
        {
        case kRpcCheckVersion:
            // libdbc expects dbcman.irx version 3.20.
            writeVersionResponse(rdram, recvBufAddr, recvSize);
            return true;

        case kRpcPadTransaction:
        {
            // PS2X_DBC_PROBE: dump the send buffer (command) so we can see what the
            // game actually requests (scePad2GetState uses cmd=0x101800c; Read uses
            // another). The handler must answer per-command.
            static const bool s_dbcProbe = [](){ const char *v=std::getenv("PS2X_DBC_PROBE"); return v&&v[0]&&v[0]!='0'; }();
            if (s_dbcProbe)
            {
                static std::mutex s_dm; static std::map<uint64_t,uint32_t> s_cmds;
                std::lock_guard<std::mutex> lk(s_dm);
                uint32_t w[6]={0,0,0,0,0,0};
                for (int i=0;i<6;i++){ const uint8_t*p=getMemPtr(rdram, sendBufAddr+i*4u); if(p) std::memcpy(&w[i],p,4); }
                uint64_t key = ((uint64_t)w[1]<<32) | w[0]; // w[0]=socket, w[1]=command
                uint32_t &c = s_cmds[key]; c++;
                if (c <= 3u)
                {   // [rumble] the first occurrences of each (socket, command) with the whole send buffer
                    std::cerr << "[dbc-cmd] first sock=" << w[0] << " cmd=0x" << std::hex << w[1] << " sendSize=0x" << sendSize << " words:";
                    for (uint32_t i = 0; i < 12u && i * 4u < sendSize; ++i) { uint32_t x = 0; if (const uint8_t *p = getMemPtr(rdram, sendBufAddr + i * 4u)) std::memcpy(&x, p, 4); std::cerr << " " << x; }
                    std::cerr << std::dec << std::endl;
                }
                static std::atomic<uint32_t> s_n{0};
                if ((s_n.fetch_add(1)%4000u)==1u){
                    std::cerr<<"[dbc-cmd] sendSize=0x"<<std::hex<<sendSize<<" recvSize=0x"<<recvSize<<" (sock:cmd=count):";
                    for(auto&kv:s_cmds) std::cerr<<" "<<std::hex<<(uint32_t)(kv.first&0xffffffff)<<":"<<(uint32_t)(kv.first>>32)<<std::dec<<"="<<kv.second;
                    std::cerr<<" | w2-5="<<std::hex<<w[2]<<","<<w[3]<<","<<w[4]<<","<<w[5]<<std::dec<<std::endl;
                }
            }
            // Report a connected controller with neutral input.
            //   +0x8c = return status 0 (>=0 success)
            //   +0x08 = data length (1 status byte)
            //   +0x0c = data[0] = 1 (controller ready)
            if (recvSize >= 0x90u)
            {
                writeGuestU32(rdram, recvBufAddr + 0x8cu, 0u);
                writeGuestU32(rdram, recvBufAddr + 0x08u, 1u);
                writeGuestU32(rdram, recvBufAddr + 0x0cu, 1u);
            }
            return true;
        }

        default:
            if (g_unknownRpcLogCount < kMaxUnknownRpcLogs)
            {
                std::cerr << "[DBCMAN:stub]"
                          << " sid=0x" << std::hex << sid
                          << " rpc=0x" << rpcNum
                          << " send=0x" << sendBufAddr
                          << " sendSize=0x" << sendSize
                          << " recv=0x" << recvBufAddr
                          << " recvSize=0x" << recvSize
                          << std::dec << std::endl;
                ++g_unknownRpcLogCount;
            }
            return true;
        }
    }
}
