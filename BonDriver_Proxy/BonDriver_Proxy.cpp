#include "BonDriver_Proxy.h"
#include <string.h>
#include <wchar.h>

namespace
{
IBonDriver *g_this;
HINSTANCE g_hModule;
}

CProxyClient3::CProxyClient3(HANDLE hPipe)
    : m_hPipe(hPipe)
{
    InitializeCriticalSection(&m_cs);
}

DWORD CProxyClient3::CreateBon(LPCWSTR param)
{
    DWORD priority = 0xFF00;
    if (param) {
        WCHAR c = param[0];
        if (L'a' <= c && c <= L'z') {
            c = c - L'a' + L'A';
        }
        priority = L'0' <= c && c <= L'4' ? 0x0200 + c - L'0' :
                   L'5' <= c && c <= L'9' ? 0x0300 + c - L'5' :
                   L'A' <= c && c <= L'G' ? 0x0400 + c - L'A' :
                   L'H' <= c && c <= L'N' ? 0x0500 + c - L'H' :
                   L'O' <= c && c <= L'T' ? 0x0600 + c - L'O' :
                   L'U' <= c && c <= L'Z' ? 0x0700 + c - L'U' : 0x0100;
    }
    CBlockLock lock(&m_cs);
    DWORD n;
    return WriteAndRead4(&n, "Crea", &priority) ? n : 0xFFFFFFFF;
}

const DWORD CProxyClient3::GetTotalDeviceNum()
{
    CBlockLock lock(&m_cs);
    DWORD n;
    return WriteAndRead4(&n, "GTot") ? n : 0;
}

const DWORD CProxyClient3::GetActiveDeviceNum()
{
    CBlockLock lock(&m_cs);
    DWORD n;
    return WriteAndRead4(&n, "GAct") ? n : 0;
}

const BOOL CProxyClient3::SetLnbPower(const BOOL bEnable)
{
    CBlockLock lock(&m_cs);
    BOOL b;
    return WriteAndRead4(&b, "SLnb", &bEnable) ? b : FALSE;
}

LPCWSTR CProxyClient3::GetTunerName()
{
    CBlockLock lock(&m_cs);
    if (WriteAndReadString(m_tunerName, "GTun")) {
        return m_tunerName;
    }
    return nullptr;
}

const BOOL CProxyClient3::IsTunerOpening()
{
    CBlockLock lock(&m_cs);
    BOOL b;
    return WriteAndRead4(&b, "ITun") ? b : FALSE;
}

LPCWSTR CProxyClient3::EnumTuningSpace(const DWORD dwSpace)
{
    CBlockLock lock(&m_cs);
    if (WriteAndReadString(m_tuningSpace, "ETun", &dwSpace)) {
        return m_tuningSpace;
    }
    return nullptr;
}

LPCWSTR CProxyClient3::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
    CBlockLock lock(&m_cs);
    if (WriteAndReadString(m_channelName, "ECha", &dwSpace, &dwChannel)) {
        return m_channelName;
    }
    return nullptr;
}

const BOOL CProxyClient3::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
    CBlockLock lock(&m_cs);
    BOOL b;
    return WriteAndRead4(&b, "SCh2", &dwSpace, &dwChannel) ? b : FALSE;
}

const DWORD CProxyClient3::GetCurSpace()
{
    CBlockLock lock(&m_cs);
    DWORD n;
    return WriteAndRead4(&n, "GCSp") ? n : 0xFFFFFFFF;
}

const DWORD CProxyClient3::GetCurChannel()
{
    CBlockLock lock(&m_cs);
    DWORD n;
    return WriteAndRead4(&n, "GCCh") ? n : 0xFFFFFFFF;
}

const BOOL CProxyClient3::OpenTuner()
{
    CBlockLock lock(&m_cs);
    BOOL b;
    return WriteAndRead4(&b, "Open") ? b : FALSE;
}

void CProxyClient3::CloseTuner()
{
    CBlockLock lock(&m_cs);
    DWORD n;
    WriteAndRead4(&n, "Clos");
}

const BOOL CProxyClient3::SetChannel(const BYTE bCh)
{
    CBlockLock lock(&m_cs);
    DWORD ch = bCh;
    BOOL b;
    return WriteAndRead4(&b, "SCha", &ch) ? b : FALSE;
}

const float CProxyClient3::GetSignalLevel()
{
    CBlockLock lock(&m_cs);
    float f;
    return WriteAndRead4(&f, "GSig") ? f : 0;
}

const DWORD CProxyClient3::WaitTsStream(const DWORD dwTimeOut)
{
    // 実装しない(中断用のイベントを指定できないなど使い勝手が悪く、利用例を知らないため)
    static_cast<void>(dwTimeOut);
    return WAIT_ABANDONED;
}

const DWORD CProxyClient3::GetReadyCount()
{
    CBlockLock lock(&m_cs);
    DWORD n;
    return WriteAndRead4(&n, "GRea") ? n : 0;
}

const BOOL CProxyClient3::GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain)
{
    // 実装しない(仕様上、安全な利用法がおそらく無く、利用例を知らないため)
    static_cast<void>(pDst);
    static_cast<void>(pdwSize);
    static_cast<void>(pdwRemain);
    return FALSE;
}

const BOOL CProxyClient3::GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain)
{
    CBlockLock lock(&m_cs);
    if (ppDst && pdwSize) {
        DWORD tsBufSize = 0;
        DWORD tsRemain = 0;
        DWORD n;
        if (WriteAndRead4(&n, "GTsS")) {
            if (n < 4 || n - 4 > sizeof(m_tsBuf)) {
                // 戻り値が異常
                CloseHandle(m_hPipe);
                m_hPipe = INVALID_HANDLE_VALUE;
            }
            else if (ReadAll(&tsRemain, 4) && ReadAll(m_tsBuf, n - 4)) {
                tsBufSize = n - 4;
            }
        }
        *ppDst = m_tsBuf;
        *pdwSize = tsBufSize;
        if (pdwRemain) {
            *pdwRemain = tsBufSize == 0 ? 0 : tsRemain;
        }
        return TRUE;
    }
    return FALSE;
}

void CProxyClient3::PurgeTsStream()
{
    CBlockLock lock(&m_cs);
    DWORD n;
    WriteAndRead4(&n, "Purg");
}

void CProxyClient3::Release()
{
    DWORD n;
    if (WriteAndRead4(&n, "Rele")) {
        CloseHandle(m_hPipe);
    }
    DeleteCriticalSection(&m_cs);
    g_this = nullptr;
    delete this;
}

bool CProxyClient3::WriteAndRead4(void *buf, const char (&cmd)[5], const void *param1, const void *param2)
{
    return Write(cmd, param1, param2) && ReadAll(buf, 4);
}

bool CProxyClient3::WriteAndReadString(WCHAR (&buf)[256], const char (&cmd)[5], const void *param1, const void *param2)
{
    DWORD n;
    if (WriteAndRead4(&n, cmd, param1, param2)) {
        n %= 256;
        if (n != 0 && ReadAll(buf, n * sizeof(WCHAR))) {
            buf[n] = L'\0';
            return true;
        }
    }
    return false;
}

bool CProxyClient3::Write(const char (&cmd)[5], const void *param1, const void *param2)
{
    if (m_hPipe != INVALID_HANDLE_VALUE) {
        BYTE buf[12] = {};
        memcpy(buf, cmd, 4);
        if (param1) {
            memcpy(buf + 4, param1, 4);
        }
        if (param2) {
            memcpy(buf + 8, param2, 4);
        }
        DWORD n;
        if (WriteFile(m_hPipe, buf, 12, &n, nullptr) && n == 12) {
            return true;
        }
        CloseHandle(m_hPipe);
        m_hPipe = INVALID_HANDLE_VALUE;
    }
    return false;
}

bool CProxyClient3::ReadAll(void *buf, DWORD len)
{
    if (m_hPipe != INVALID_HANDLE_VALUE) {
        for (DWORD n = 0, m; n < len; n += m) {
            if (!ReadFile(m_hPipe, static_cast<BYTE*>(buf) + n, len - n, &m, nullptr)) {
                CloseHandle(m_hPipe);
                m_hPipe = INVALID_HANDLE_VALUE;
                return false;
            }
        }
        return true;
    }
    return false;
}

BOOL APIENTRY DllMain(HINSTANCE hModule, DWORD dwReason, LPVOID lpReserved)
{
    static_cast<void>(lpReserved);
    switch (dwReason) {
    case DLL_PROCESS_ATTACH:
        g_hModule = hModule;
        break;
    case DLL_PROCESS_DETACH:
        if (g_this) {
            OutputDebugString(L"BonDriver_Proxy::DllMain(): Driver Is Not Released!\n");
            g_this->Release();
        }
        break;
    }
    return TRUE;
}

// CreateBonDriver()は呼出規約等がMSVC仕様なオブジェクトを返すことがほぼ前提のため、他のコンパイラではエクスポートしない
#ifdef _MSC_VER
extern "C" BONAPI
#else
static
#endif
IBonDriver * CreateBonDriver(void)
{
    if (!g_this) {
        WCHAR exePath[MAX_PATH + 64] = {};
        {
            // "BonDriverLocalProxy.exe"を探す
            WCHAR path[MAX_PATH + 64];
            DWORD len = GetModuleFileName(nullptr, path, MAX_PATH);
            if (len && len < MAX_PATH && wcsrchr(path, L'\\')) {
                *wcsrchr(path, L'\\') = L'\0';
                if (wcsrchr(path, L'\\')) {
                    *wcsrchr(path, L'\\') = L'\0';
                    wcscat_s(path, L"\\BonDriverProxy\\BonDriverLocalProxy.exe");
                    DWORD attr = GetFileAttributes(path);
                    DWORD err = GetLastError();
                    if (attr != INVALID_FILE_ATTRIBUTES || (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND)) {
                        wcscpy_s(exePath, path);
                    }
                }
            }
            if (!exePath[0]) {
                len = GetEnvironmentVariable(L"SystemDrive", path, MAX_PATH);
                if (len && len < MAX_PATH) {
                    wcscat_s(path, L"\\BonDriverProxy\\BonDriverLocalProxy.exe");
                    DWORD attr = GetFileAttributes(path);
                    DWORD err = GetLastError();
                    if (attr != INVALID_FILE_ATTRIBUTES || (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND)) {
                        wcscpy_s(exePath, path);
                    }
                }
            }
        }

        WCHAR pathBuf[MAX_PATH];
        LPWSTR param = nullptr;
        LPWSTR origin = nullptr;
        {
            // DLLの名前から代理元のドライバ名とパラメータを抽出
            WCHAR path[MAX_PATH];
            DWORD len = GetModuleFileName(g_hModule, path, MAX_PATH);
            if (len && len < MAX_PATH) {
                len = GetLongPathName(path, pathBuf, MAX_PATH);
                if (len && len < MAX_PATH && wcsrchr(pathBuf, L'\\')) {
                    param = wcsrchr(pathBuf, L'\\') + 1;
                    if (wcsrchr(param, L'.')) {
                        *wcsrchr(param, L'.') = L'\0';
                    }
                    if (_wcsnicmp(param, L"BonDriver_Proxy", 15) == 0) {
                        param += 15;
                        origin = wcschr(param, L'_');
                        if (origin) {
                            *(origin++) = L'\0';
                        }
                    }
                    else if (_wcsnicmp(param, L"BonDriver_", 10) == 0) {
                        origin = param + 10;
                        param = nullptr;
                    }
                }
            }
        }

        if (exePath[0] && origin) {
            // 代理元プロセスに接続(タイムアウトは20秒)
            HANDLE hProcess = nullptr;
            for (int retry = 0; retry < 1000; ++retry) {
                WCHAR pipeName[MAX_PATH + 64];
                wcscpy_s(pipeName, L"\\\\.\\pipe\\BonDriverLocalProxy_");
                wcscat_s(pipeName, origin);
                HANDLE hPipe = CreateFile(pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hPipe != INVALID_HANDLE_VALUE) {
                    CProxyClient3 *down = new CProxyClient3(hPipe);
                    DWORD type = down->CreateBon(param);
                    if (type != 0xFFFFFFFF) {
                        if (type == 0) {
                            // 初期化に失敗
                            down->Release();
                        }
                        else if (type == 1) {
                            g_this = new CProxyClient(down);
                        }
                        else if (type == 2) {
                            g_this = new CProxyClient2(down);
                        }
                        else {
                            g_this = down;
                        }
                        break;
                    }
                    // 初期化中に切断
                    down->Release();
                }
                if (hProcess) {
                    if (WaitForSingleObject(hProcess, 0) == WAIT_OBJECT_0) {
                        CloseHandle(hProcess);
                        hProcess = nullptr;
                    }
                }
                if (!hProcess) {
                    WCHAR exeParam[MAX_PATH + 16];
                    wcscpy_s(exeParam, L" \"");
                    wcscat_s(exeParam, origin);
                    wcscat_s(exeParam, L"\"");
                    STARTUPINFO si = {};
                    si.cb = sizeof(si);
                    PROCESS_INFORMATION pi;
                    if (CreateProcess(exePath, exeParam, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
                        CloseHandle(pi.hThread);
                        hProcess = pi.hProcess;
                    }
                }
                Sleep(20);
            }
            if (hProcess) {
                CloseHandle(hProcess);
            }
        }
    }
    return g_this;
}

extern "C" BONAPI const STRUCT_IBONDRIVER * CreateBonStruct(void)
{
    if (CreateBonDriver()) {
        CProxyClient3 *cli3 = dynamic_cast<CProxyClient3*>(g_this);
        if (cli3) {
            return &cli3->GetBonStruct3().Initialize(cli3, nullptr);
        }
        CProxyClient2 *cli2 = dynamic_cast<CProxyClient2*>(g_this);
        if (cli2) {
            return &cli2->GetBonStruct2().Initialize(cli2, nullptr);
        }
        CProxyClient *cli = static_cast<CProxyClient*>(g_this);
        return &cli->GetBonStruct().Initialize(cli, nullptr);
    }
    return nullptr;
}
