#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <string.h>
#include <wchar.h>
#include <algorithm>
#include <memory>
#include "IBonDriver3.h"

namespace
{
const int TSDATASIZE = 48128;
const int BDP_RING_BUFFER_NUM = 8 * 1024 * 1024 / TSDATASIZE;

DWORD Write(HANDLE hPipe, BYTE (&buf)[8 + TSDATASIZE], OVERLAPPED *ol, const void *ret, const void *param = nullptr, DWORD paramSize = 0)
{
    if (paramSize <= 4 + TSDATASIZE) {
        memcpy(buf, ret, 4);
        if (paramSize != 0) {
            memcpy(buf + 4, param, paramSize);
        }
        if (WriteFile(hPipe, buf, 4 + paramSize, nullptr, ol) || GetLastError() == ERROR_IO_PENDING) {
            return 4 + paramSize;
        }
    }
    return 0;
}

enum BDP_STATE {
    BDP_ST_IDLE, BDP_ST_CONNECTING, BDP_ST_CONNECTED, BDP_ST_READING, BDP_ST_READ, BDP_ST_WRITING
};

struct BDP_CONNECTION {
    HANDLE hPipe;
    OVERLAPPED ol;
    BDP_STATE state;
    // 以下はstate>=BDP_ST_CONNECTEDのとき有効
    bool doneOpenTuner;
    DWORD priority;
    // MAXDWORDは未使用を表す
    DWORD ringBufFront;
    DWORD bufCount;
    BYTE buf[8 + TSDATASIZE];
};

struct BDP_RING_BUFFER {
    DWORD bufCount;
    BYTE buf[4 + TSDATASIZE];
};

bool SetPriority(BDP_CONNECTION &conn, DWORD priority, std::unique_ptr<BDP_CONNECTION> *connList)
{
    // 上位16bitは絶対優先度、下位16bitは接続順
    priority <<= 16;
    // 絶対優先度は重複してはならない
    for (int i = 0; connList[i]; ++i) {
        if (connList[i]->state >= BDP_ST_CONNECTED && (connList[i]->priority & 0xFFFF0000) == priority) {
            return false;
        }
    }
    // 接続順を整理
    DWORD reorder = 1;
    for (;; ++reorder) {
        int minIndex = -1;
        DWORD minOrder = 0xFFFF;
        for (int i = 0; connList[i]; ++i) {
            DWORD order = connList[i]->priority & 0xFFFF;
            if (connList[i]->state >= BDP_ST_CONNECTED && order >= reorder && order < minOrder) {
                minOrder = order;
                minIndex = i;
            }
        }
        if (minIndex < 0) {
            break;
        }
        connList[minIndex]->priority = (connList[minIndex]->priority & 0xFFFF0000) | reorder;
    }
    conn.priority = priority | reorder;
    return true;
}

bool IsHighestPriority(DWORD priority, std::unique_ptr<BDP_CONNECTION> *connList, bool forRingBuf = false)
{
    for (int i = 0; connList[i]; ++i) {
        if (connList[i]->state >= BDP_ST_CONNECTED && (!forRingBuf || connList[i]->ringBufFront != MAXDWORD)) {
            // 絶対優先度の上位8bitが大きいものを優先、下位は無視
            if ((connList[i]->priority >> 24) > (priority >> 24)) {
                return false;
            }
            else if ((connList[i]->priority >> 24) == (priority >> 24)) {
                // 絶対優先度の上位8bitが奇数なら先行優先、偶数なら後続優先
                if (priority & 0x01000000) {
                    if ((connList[i]->priority & 0xFFFF) < (priority & 0xFFFF)) {
                        return false;
                    }
                }
                else {
                    if ((connList[i]->priority & 0xFFFF) > (priority & 0xFFFF)) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

bool AnyDoneOpenTuner(std::unique_ptr<BDP_CONNECTION> *connList)
{
    for (int i = 0; connList[i]; ++i) {
        if (connList[i]->state >= BDP_ST_CONNECTED && connList[i]->doneOpenTuner) {
            return true;
        }
    }
    return false;
}

void CloseTuner(BDP_CONNECTION &conn, std::unique_ptr<BDP_CONNECTION> *connList, IBonDriver *bon)
{
    if (conn.doneOpenTuner) {
        conn.doneOpenTuner = false;
        if (!AnyDoneOpenTuner(connList)) {
            bon->CloseTuner();
        }
    }
}

void CloseBonDriver(std::unique_ptr<BDP_CONNECTION> *connList, HMODULE *hLib, IBonDriver **bon, IBonDriver2 **bon2, IBonDriver3 **bon3, bool *doneCreateBon)
{
    for (int i = 0; connList[i]; ++i) {
        if (connList[i]->state >= BDP_ST_CONNECTED) {
            return;
        }
    }
    *doneCreateBon = false;
    if (*bon) {
        (*bon)->Release();
        *bon = nullptr;
        *bon2 = nullptr;
        *bon3 = nullptr;
    }
    if (*hLib) {
        FreeLibrary(*hLib);
        *hLib = nullptr;
    }
}

void RotateRingBuffer(DWORD n, std::unique_ptr<BDP_CONNECTION> *connList, std::unique_ptr<BDP_RING_BUFFER> *ringBuf, DWORD ringBufNum)
{
    for (int i = 0; connList[i]; ++i) {
        if (connList[i]->state >= BDP_ST_CONNECTED && connList[i]->ringBufFront != MAXDWORD) {
            connList[i]->ringBufFront = (connList[i]->ringBufFront + n) % ringBufNum;
        }
    }
    for (; n > 0; --n) {
        for (DWORD i = ringBufNum - 1; i > 0; --i) {
            ringBuf[i].swap(ringBuf[i - 1]);
        }
    }
}

bool ExpandRingBuffer(std::unique_ptr<BDP_CONNECTION> *connList, std::unique_ptr<BDP_RING_BUFFER> *ringBuf, DWORD &ringBufNum, DWORD &ringBufRear)
{
   for (int i = 0; connList[i]; ++i) {
       if (connList[i]->state >= BDP_ST_CONNECTED && (ringBufRear + 1) % ringBufNum == connList[i]->ringBufFront) {
           // 空きがないので増やす
           // ringBufRearが末尾に来るように回転
           RotateRingBuffer((ringBufNum - 1 - ringBufRear) % ringBufNum, connList, ringBuf, ringBufNum);
           ringBufRear = ringBufNum - 1;
           ringBuf[ringBufNum++].reset(new BDP_RING_BUFFER);
           return true;
       }
   }
   return false;
}

void ShrinkRingBuffer(std::unique_ptr<BDP_CONNECTION> *connList, std::unique_ptr<BDP_RING_BUFFER> *ringBuf, DWORD &ringBufNum, DWORD &ringBufRear)
{
    if (ringBufNum > 1) {
        for (int i = 0; connList[i]; ++i) {
            if (connList[i]->state >= BDP_ST_CONNECTED && (ringBufRear + 1) % ringBufNum == connList[i]->ringBufFront) {
                // 空きがない
                return;
            }
        }
        // ringBufRearが末尾の1つ手前に来るように回転
        RotateRingBuffer((ringBufNum * 2 - 2 - ringBufRear) % ringBufNum, connList, ringBuf, ringBufNum);
        ringBufRear = ringBufNum - 2;
        ringBuf[--ringBufNum].reset();
    }
}
}

#ifdef __MINGW32__
__declspec(dllexport) // ASLRを無効にしないため(CVE-2018-5392)
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#else
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
#endif
{
    static_cast<void>(hInstance);
    static_cast<void>(hPrevInstance);
    static_cast<void>(lpCmdLine);
    static_cast<void>(nCmdShow);
    SetDllDirectory(L"");

    WCHAR origin[MAX_PATH] = {};
    {
        int argc;
        LPWSTR *argv = CommandLineToArgvW(GetCommandLine(), &argc);
        if (argv) {
            if (argc >= 2 && wcslen(argv[1]) < MAX_PATH) {
                wcscpy_s(origin, argv[1]);
            }
            LocalFree(argv);
        }
    }
    if (!origin[0]) {
        return 0;
    }

    // BonDriverがCOMを利用するかもしれないため
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    std::unique_ptr<BDP_CONNECTION> connList[MAXIMUM_WAIT_OBJECTS + 1];
    HANDLE hEventList[MAXIMUM_WAIT_OBJECTS];
    int ringBufShrinkCount = 0;
    std::unique_ptr<BDP_RING_BUFFER> ringBuf[BDP_RING_BUFFER_NUM];
    ringBuf[0].reset(new BDP_RING_BUFFER);
    DWORD ringBufNum = 1;
    DWORD ringBufRear = 0;
    HMODULE hLib = nullptr;
    IBonDriver *bon = nullptr;
    IBonDriver2 *bon2 = nullptr;
    IBonDriver3 *bon3 = nullptr;
    CBonStructAdapter bonAdapter;
    CBonStruct2Adapter bon2Adapter;
    CBonStruct3Adapter bon3Adapter;
    bool doneCreateBon = false;
    bool firstConnecting = false;
    BOOL openTunerResult;
    bool initChSet = false;

    for (;;) {
        DWORD connCount = 0;
        bool anyConnected = false;
        bool allReadingOrWriting = true;
        bool allWaiting = true;

        for (; connList[connCount]; ++connCount) {
            BDP_CONNECTION &conn = *connList[connCount];
            if (conn.state == BDP_ST_IDLE) {
                conn.doneOpenTuner = false;
                conn.priority = 0;
                conn.ringBufFront = MAXDWORD;
                conn.bufCount = 0;
                OVERLAPPED olZero = {};
                conn.ol = olZero;
                conn.ol.hEvent = hEventList[connCount];
                if (ConnectNamedPipe(conn.hPipe, &conn.ol)) {
                    conn.state = BDP_ST_CONNECTED;
                }
                else {
                    DWORD err = GetLastError();
                    if (err == ERROR_PIPE_CONNECTED) {
                        conn.state = BDP_ST_CONNECTED;
                    }
                    else if (err == ERROR_IO_PENDING) {
                        conn.state = BDP_ST_CONNECTING;
                    }
                    else {
                        // 失敗時の高負荷を防ぐため
                        Sleep(1);
                    }
                }
            }
            else if (conn.state == BDP_ST_CONNECTED) {
                OVERLAPPED olZero = {};
                conn.ol = olZero;
                conn.ol.hEvent = hEventList[connCount];
                if (ReadFile(conn.hPipe, conn.buf, 12 - conn.bufCount, nullptr, &conn.ol) || GetLastError() == ERROR_IO_PENDING) {
                    conn.state = BDP_ST_READING;
                }
                else {
                    CloseTuner(conn, connList, bon);
                    conn.state = BDP_ST_IDLE;
                    CloseBonDriver(connList, &hLib, &bon, &bon2, &bon3, &doneCreateBon);
                    DisconnectNamedPipe(conn.hPipe);
                }
            }
            else if (conn.state == BDP_ST_READ) {
                OVERLAPPED olZero = {};
                conn.ol = olZero;
                conn.ol.hEvent = hEventList[connCount];
                union {
                    BOOL b;
                    DWORD n;
                } param1, param2;
                char cmd[5] = {};
                memcpy(cmd, conn.buf, 4);
                memcpy(&param1, conn.buf + 4, 4);
                memcpy(&param2, conn.buf + 8, 4);
                conn.bufCount = 0;
                if (!strcmp(cmd, "Crea")) {
                    DWORD type = 0;
                    if (SetPriority(conn, param1.n, connList)) {
                        if (!doneCreateBon) {
                            doneCreateBon = true;
                            WCHAR libPath[MAX_PATH * 2 + 64];
                            DWORD len = GetModuleFileName(nullptr, libPath, MAX_PATH);
                            if (len && len < MAX_PATH && wcsrchr(libPath, L'\\')) {
                                *wcsrchr(libPath, L'\\') = L'\0';
                                wcscat_s(libPath, L"\\BonDriver_");
                                wcscat_s(libPath, origin);
                                wcscat_s(libPath, L".dll");
                                hLib = LoadLibrary(libPath);
                                if (hLib) {
                                    const STRUCT_IBONDRIVER *(*funcCreateBonStruct)() = reinterpret_cast<const STRUCT_IBONDRIVER*(*)()>(GetProcAddress(hLib, "CreateBonStruct"));
                                    if (funcCreateBonStruct) {
                                        // 特定コンパイラに依存しないI/Fを使う
                                        const STRUCT_IBONDRIVER *st = funcCreateBonStruct();
                                        if (st) {
                                            if (bon3Adapter.Adapt(*st)) {
                                                bon = bon2 = bon3 = &bon3Adapter;
                                            }
                                            else if (bon2Adapter.Adapt(*st)) {
                                                bon = bon2 = &bon2Adapter;
                                            }
                                            else {
                                                bonAdapter.Adapt(*st);
                                                bon = &bonAdapter;
                                            }
                                        }
                                    }
#ifdef _MSC_VER
                                    else {
                                        IBonDriver *(*funcCreateBonDriver)() = reinterpret_cast<IBonDriver*(*)()>(GetProcAddress(hLib, "CreateBonDriver"));
                                        if (funcCreateBonDriver) {
                                            bon = funcCreateBonDriver();
                                            if (bon) {
                                                bon2 = dynamic_cast<IBonDriver2*>(bon);
                                                if (bon2) {
                                                    bon3 = dynamic_cast<IBonDriver3*>(bon2);
                                                }
                                            }
                                        }
                                    }
#endif
                                }
                            }
                            initChSet = false;
                        }
                        type = bon3 ? 3 : bon2 ? 2 : bon ? 1 : 0;
                    }
                    conn.bufCount = Write(conn.hPipe, conn.buf, &conn.ol, &type);
                }
                else if (!strcmp(cmd, "GTot")) {
                    if (bon3) {
                        DWORD n = bon3->GetTotalDeviceNum();
                        conn.bufCount = Write(conn.hPipe, conn.buf, &conn.ol, &n);
                    }
                }
                else if (!strcmp(cmd, "GAct")) {
                    if (bon3) {
                        DWORD n = bon3->GetActiveDeviceNum();
                        conn.bufCount = Write(conn.hPipe, conn.buf, &conn.ol, &n);
                    }
                }
                else if (!strcmp(cmd, "SLnb")) {
                    if (bon3) {
                        BOOL b = IsHighestPriority(conn.priority, connList) ? bon3->SetLnbPower(param1.b) : FALSE;
                        conn.bufCount = Write(conn.hPipe, conn.buf, &conn.ol, &b);
                    }
                }
                else if (!strcmp(cmd, "GTun")) {
                    if (bon2) {
                        LPCWSTR tunerName = bon2->GetTunerName();
                        DWORD n = static_cast<DWORD>(tunerName ? wcslen(tunerName) + 1 : 0);
                        n = std::min<DWORD>(n, 255);
                        conn.bufCount = Write(conn.hPipe, conn.buf, &conn.ol, &n, tunerName, n * sizeof(WCHAR));
                    }
                }
                else if (!strcmp(cmd, "ITun")) {
                    if (bon2) {
                        BOOL b = bon2->IsTunerOpening();
                        conn.bufCount = Write(conn.hPipe, conn.buf, &conn.ol, &b);
                    }
                }
                else if (!strcmp(cmd, "ETun")) {
                    if (bon2) {
                        LPCWSTR tuningSpace = bon2->EnumTuningSpace(param1.n);
                        DWORD n = static_cast<DWORD>(tuningSpace ? wcslen(tuningSpace) + 1 : 0);
                        n = std::min<DWORD>(n, 255);
                        conn.bufCount = Write(conn.hPipe, conn.buf, &conn.ol, &n, tuningSpace, n * sizeof(WCHAR));
                    }
                }
                else if (!strcmp(cmd, "ECha")) {
                    if (bon2) {
                        LPCWSTR channelName = bon2->EnumChannelName(param1.n, param2.n);
                        DWORD n = static_cast<DWORD>(channelName ? wcslen(channelName) + 1 : 0);
                        n = std::min<DWORD>(n, 255);
                        conn.bufCount = Write(conn.hPipe, conn.buf, &conn.ol, &n, channelName, n * sizeof(WCHAR));
                    }
                }
                else if (!strcmp(cmd, "SCh2")) {
                    if (bon2) {
                        BOOL b = FALSE;
                        if (IsHighestPriority(conn.priority, connList)) {
                            // 最初のSetChannel()までGetCurChannel()等の結果があまり信用できないため
                            if (initChSet && bon2->GetCurChannel() == param2.n && bon2->GetCurSpace() == param1.n) {
                                b = TRUE;
                            }
                            else if (bon2->SetChannel(param1.n, param2.n)) {
                                b = TRUE;
                                initChSet = true;
                            }
                        }
                        conn.bufCount = Write(conn.hPipe, conn.buf, &conn.ol, &b);
                    }
                }
                else if (!strcmp(cmd, "GCSp")) {
                    if (bon2) {
                        DWORD n = bon2->GetCurSpace();
                        conn.bufCount = Write(conn.hPipe, conn.buf, &conn.ol, &n);
                    }
                }
                else if (!strcmp(cmd, "GCCh")) {
                    if (bon2) {
                        DWORD n = bon2->GetCurChannel();
                        conn.bufCount = Write(conn.hPipe, conn.buf, &conn.ol, &n);
                    }
                }
                else if (!strcmp(cmd, "Open")) {
                    if (bon) {
                        if (!conn.doneOpenTuner) {
                            if (!AnyDoneOpenTuner(connList)) {
                                openTunerResult = bon->OpenTuner();
                                initChSet = false;
                            }
                            conn.doneOpenTuner = true;
                        }
                        conn.bufCount = Write(conn.hPipe, conn.buf, &conn.ol, &openTunerResult);
                    }
                }
                else if (!strcmp(cmd, "Clos")) {
                    if (bon) {
                        CloseTuner(conn, connList, bon);
                        DWORD n = 0;
                        conn.bufCount = Write(conn.hPipe, conn.buf, &conn.ol, &n);
                    }
                }
                else if (!strcmp(cmd, "SCha")) {
                    if (bon) {
                        BOOL b = IsHighestPriority(conn.priority, connList) ? bon->SetChannel(static_cast<BYTE>(param1.n)) : FALSE;
                        conn.bufCount = Write(conn.hPipe, conn.buf, &conn.ol, &b);
                    }
                }
                else if (!strcmp(cmd, "GSig")) {
                    if (bon) {
                        float f = bon->GetSignalLevel();
                        conn.bufCount = Write(conn.hPipe, conn.buf, &conn.ol, &f);
                    }
                }
                else if (!strcmp(cmd, "GRea")) {
                    if (bon) {
                        DWORD n = bon->GetReadyCount() + (conn.ringBufFront == MAXDWORD || conn.ringBufFront == ringBufRear ? 0 : 1);
                        conn.bufCount = Write(conn.hPipe, conn.buf, &conn.ol, &n);
                    }
                }
                else if (!strcmp(cmd, "GTsS")) {
                    if (bon) {
                        if (conn.ringBufFront == MAXDWORD) {
                            // 使用開始
                            conn.ringBufFront = ringBufRear;
                        }
                        if (conn.ringBufFront == ringBufRear && IsHighestPriority(conn.priority, connList, true)) {
                            // 定期的にリングバッファを縮める
                            if (++ringBufShrinkCount > 100) {
                                ShrinkRingBuffer(connList, ringBuf, ringBufNum, ringBufRear);
                                ringBufShrinkCount = 0;
                            }
                            BYTE *buf;
                            DWORD bufSize;
                            DWORD remain;
                            if (bon->GetTsStream(&buf, &bufSize, &remain) && buf) {
                                while (bufSize != 0) {
                                    if (bufSize > sizeof(ringBuf[0]->buf) - 4) {
                                        ++remain;
                                        memcpy(ringBuf[ringBufRear]->buf, &remain, 4);
                                        --remain;
                                        ringBuf[ringBufRear]->bufCount = sizeof(ringBuf[0]->buf);
                                    }
                                    else {
                                        memcpy(ringBuf[ringBufRear]->buf, &remain, 4);
                                        ringBuf[ringBufRear]->bufCount = 4 + bufSize;
                                    }
                                    memcpy(ringBuf[ringBufRear]->buf + 4, buf, ringBuf[ringBufRear]->bufCount - 4);
                                    buf += ringBuf[ringBufRear]->bufCount - 4;
                                    bufSize -= ringBuf[ringBufRear]->bufCount - 4;
                                    // 最長でBDP_RING_BUFFER_NUMまでリングバッファを伸ばす
                                    if (ringBufNum < BDP_RING_BUFFER_NUM && ExpandRingBuffer(connList, ringBuf, ringBufNum, ringBufRear)) {
                                        ringBufShrinkCount = 0;
                                    }
                                    ringBufRear = (ringBufRear + 1) % ringBufNum;
                                }
                            }
                        }
                        if (conn.ringBufFront != ringBufRear) {
                            conn.bufCount = Write(conn.hPipe, conn.buf, &conn.ol, &ringBuf[conn.ringBufFront]->bufCount,
                                                  ringBuf[conn.ringBufFront]->buf, ringBuf[conn.ringBufFront]->bufCount);
                            conn.ringBufFront = (conn.ringBufFront + 1) % ringBufNum;
                        }
                        else {
                            DWORD n = 4;
                            DWORD remain = 0;
                            conn.bufCount = Write(conn.hPipe, conn.buf, &conn.ol, &n, &remain, 4);
                        }
                    }
                }
                else if (!strcmp(cmd, "Purg")) {
                    if (bon) {
                        if (IsHighestPriority(conn.priority, connList)) {
                            bon->PurgeTsStream();
                        }
                        if (conn.ringBufFront != MAXDWORD) {
                            conn.ringBufFront = ringBufRear;
                        }
                        DWORD n = 0;
                        conn.bufCount = Write(conn.hPipe, conn.buf, &conn.ol, &n);
                    }
                }
                if (conn.bufCount != 0) {
                    conn.state = BDP_ST_WRITING;
                }
                else {
                    CloseTuner(conn, connList, bon);
                    conn.state = BDP_ST_IDLE;
                    CloseBonDriver(connList, &hLib, &bon, &bon2, &bon3, &doneCreateBon);
                    DisconnectNamedPipe(conn.hPipe);
                }
            }

            anyConnected = anyConnected || (conn.state >= BDP_ST_CONNECTED);
            allReadingOrWriting = allReadingOrWriting && (conn.state == BDP_ST_READING || conn.state == BDP_ST_WRITING);
            allWaiting = allWaiting && (conn.state == BDP_ST_CONNECTING || conn.state == BDP_ST_READING || conn.state == BDP_ST_WRITING);
        }

        if (!anyConnected && connCount != 0 && !firstConnecting) {
            // 誰も接続していないので終了
            break;
        }
        firstConnecting = false;
        if (allReadingOrWriting && connCount < MAXIMUM_WAIT_OBJECTS) {
            // パイプを増やす
            hEventList[connCount] = CreateEvent(nullptr, TRUE, FALSE, nullptr);
            if (hEventList[connCount]) {
                connList[connCount].reset(new BDP_CONNECTION);
                WCHAR pipeName[MAX_PATH + 64];
                wcscpy_s(pipeName, L"\\\\.\\pipe\\BonDriverLocalProxy_");
                wcscat_s(pipeName, origin);
                connList[connCount]->hPipe = CreateNamedPipe(pipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (connCount == 0 ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                                                             0, PIPE_UNLIMITED_INSTANCES, TSDATASIZE + 256, 256, 0, nullptr);
                if (connList[connCount]->hPipe == INVALID_HANDLE_VALUE) {
                    CloseHandle(hEventList[connCount]);
                    connList[connCount].reset();
                    if (connCount == 0) {
                        break;
                    }
                }
                else {
                    if (connCount == 0) {
                        firstConnecting = true;
                    }
                    connList[connCount++]->state = BDP_ST_IDLE;
                    allWaiting = false;
                }
            }
            else if (connCount == 0) {
                break;
            }
        }

        if (allWaiting) {
            DWORD ret = WaitForMultipleObjects(connCount, hEventList, FALSE, INFINITE);
            if (WAIT_OBJECT_0 <= ret && ret < WAIT_OBJECT_0 + connCount) {
                BDP_CONNECTION &conn = *connList[ret - WAIT_OBJECT_0];
                DWORD xferred;
                if (GetOverlappedResult(conn.hPipe, &conn.ol, &xferred, TRUE)) {
                    if (conn.state == BDP_ST_CONNECTING) {
                        conn.doneOpenTuner = false;
                        conn.priority = 0;
                        conn.ringBufFront = MAXDWORD;
                        conn.bufCount = 0;
                        conn.state = BDP_ST_CONNECTED;
                    }
                    else if (conn.state == BDP_ST_READING) {
                        conn.bufCount += xferred;
                        conn.state = conn.bufCount >= 12 ? BDP_ST_READ : BDP_ST_CONNECTED;
                    }
                    else {
                        if (conn.bufCount == xferred) {
                            conn.bufCount = 0;
                            conn.state = BDP_ST_CONNECTED;
                        }
                        else {
                            CloseTuner(conn, connList, bon);
                            conn.state = BDP_ST_IDLE;
                            CloseBonDriver(connList, &hLib, &bon, &bon2, &bon3, &doneCreateBon);
                            DisconnectNamedPipe(conn.hPipe);
                        }
                    }
                }
                else {
                    if (conn.state >= BDP_ST_CONNECTED) {
                        CloseTuner(conn, connList, bon);
                        conn.state = BDP_ST_IDLE;
                        CloseBonDriver(connList, &hLib, &bon, &bon2, &bon3, &doneCreateBon);
                        DisconnectNamedPipe(conn.hPipe);
                    }
                    conn.state = BDP_ST_IDLE;
                }
            }
            else {
                // 失敗時の高負荷を防ぐため
                Sleep(1);
            }
        }
    }

    for (int i = 0; connList[i]; ++i) {
        CloseHandle(connList[i]->hPipe);
        CloseHandle(hEventList[i]);
    }
    CoUninitialize();
    return 0;
}
