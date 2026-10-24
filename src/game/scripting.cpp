#include <mod/amlmod.h>
#include <mod/logger.h>
#include <stdarg.h>
#include <string.h>
#include "scripting.h"

#ifdef AML32
struct GAME_SCRIPT_THREAD
{
    uint8_t   pad1[20];
    uintptr_t dwScriptIP;
    uint8_t   pad2[36];
    uint32_t  dwLocalVar[32];
    uint32_t  dwTimers[2];
    uint8_t   pad3[33];
    uint8_t   condResult;
    uint8_t   pad4[10];
    uint16_t  logicalOp;
    uint8_t   notFlag;
    uint8_t   pad5[13];
};
#else
struct GAME_SCRIPT_THREAD
{
    uint8_t   pad1[32];
    uintptr_t dwScriptIP;
    uint8_t   pad2[68];
    uint32_t  dwLocalVar[32];
    uint32_t  dwTimers[2];
    uint8_t   pad3[33];
    uint8_t   condResult;
    uint8_t   pad4[26];
};
#endif

static GAME_SCRIPT_THREAD* gst = nullptr;
static uint8_t  ScriptBuf[512];
static uint32_t* pdwParamVars[18];
static void (*ProcessOneCommand)(GAME_SCRIPT_THREAD*) = nullptr;

bool Scripting::Init(void* gameHandle)
{
    gst = new GAME_SCRIPT_THREAD;
    memset(gst, 0, sizeof(GAME_SCRIPT_THREAD));
    SET_TO(ProcessOneCommand, aml->GetSym(gameHandle, "_ZN14CRunningScript17ProcessOneCommandEv"));
    if(!ProcessOneCommand) logger->Error("CRunningScript::ProcessOneCommand not found");
    return ProcessOneCommand != nullptr;
}

bool Scripting::IsReady() { return ProcessOneCommand != nullptr; }
bool Scripting::IsOwnThread(const void* script) { return script && script == (const void*)gst; }

int Scripting::Command(const SCRIPT_COMMAND* cmd, ...)
{
    if(!ProcessOneCommand) return 0;

    va_list ap;
    va_start(ap, cmd);
    const char* p = cmd->params;
    memcpy(&ScriptBuf[0], &cmd->opCode, 2);
    int pos = 2;
    uint16_t varPos = 0;
    for(int i = 0; i < 18; ++i) gst->dwLocalVar[i] = 0;

    while(*p)
    {
        switch(*p)
        {
            case 'i':
            case 'b':
            {
                int v = va_arg(ap, int);
                if(*p == 'b') v = v ? 1 : 0;
                ScriptBuf[pos++] = 0x01;
                memcpy(&ScriptBuf[pos], &v, 4); pos += 4;
                break;
            }
            case 'f':
            {
                float f = (float)va_arg(ap, double);
                ScriptBuf[pos++] = 0x06;
                memcpy(&ScriptBuf[pos], &f, 4); pos += 4;
                break;
            }
            case 'v':
            {
                uint32_t* v = va_arg(ap, uint32_t*);
                ScriptBuf[pos++] = 0x03;
                pdwParamVars[varPos] = v;
                gst->dwLocalVar[varPos] = *v;
                memcpy(&ScriptBuf[pos], &varPos, 2); pos += 2;
                ++varPos;
                break;
            }
            case 's':
            {
                const char* sz = va_arg(ap, const char*);
                uint8_t len = (uint8_t)strlen(sz);
                ScriptBuf[pos++] = 0x0E;
                ScriptBuf[pos++] = len;
                memcpy(&ScriptBuf[pos], sz, len); pos += len;
                break;
            }
            default:
                va_end(ap);
                logger->Error("Opcode %04X: bad params '%s'", cmd->opCode, cmd->params);
                return 0;
        }
        ++p;
    }
    va_end(ap);

    gst->dwScriptIP = (uintptr_t)ScriptBuf;
    ProcessOneCommand(gst);
    int result = gst->condResult;
    for(int i = 0; i < varPos; ++i) *pdwParamVars[i] = gst->dwLocalVar[i];
    return result;
}
