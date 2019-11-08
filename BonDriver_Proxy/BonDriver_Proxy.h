#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define BONSDK_IMPLEMENT
#include "IBonDriver3.h"

class CProxyClient3 : public IBonDriver3
{
public:
    CProxyClient3(HANDLE hPipe);
    DWORD CreateBon(LPCWSTR param);
    // IBonDriver3
    const DWORD GetTotalDeviceNum();
    const DWORD GetActiveDeviceNum();
    const BOOL SetLnbPower(const BOOL bEnable);
    // IBonDriver2
    LPCWSTR GetTunerName();
    const BOOL IsTunerOpening();
    LPCWSTR EnumTuningSpace(const DWORD dwSpace);
    LPCWSTR EnumChannelName(const DWORD dwSpace, const DWORD dwChannel);
    const BOOL SetChannel(const DWORD dwSpace, const DWORD dwChannel);
    const DWORD GetCurSpace();
    const DWORD GetCurChannel();
    // IBonDriver
    const BOOL OpenTuner();
    void CloseTuner();
    const BOOL SetChannel(const BYTE bCh);
    const float GetSignalLevel();
    const DWORD WaitTsStream(const DWORD dwTimeOut);
    const DWORD GetReadyCount();
    const BOOL GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain);
    const BOOL GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain);
    void PurgeTsStream();
    void Release();
private:
    bool WriteAndRead4(void *buf, const char (&cmd)[5], const void *param1 = nullptr, const void *param2 = nullptr);
    bool WriteAndReadString(WCHAR (&buf)[256], const char (&cmd)[5], const void *param1 = nullptr, const void *param2 = nullptr);
    bool Write(const char (&cmd)[5], const void *param1 = nullptr, const void *param2 = nullptr);
    bool ReadAll(void *buf, DWORD len);
    CRITICAL_SECTION m_cs;
    HANDLE m_hPipe;
    BYTE m_tsBuf[48128];
    WCHAR m_tunerName[256];
    WCHAR m_tuningSpace[256];
    WCHAR m_channelName[256];
};

class CProxyClient2 : public IBonDriver2
{
public:
    CProxyClient2(CProxyClient3 *down) : m_down(down) {}
    // IBonDriver2
    LPCWSTR GetTunerName() { return m_down->GetTunerName(); }
    const BOOL IsTunerOpening() { return m_down->IsTunerOpening(); }
    LPCWSTR EnumTuningSpace(const DWORD dwSpace) { return m_down->EnumTuningSpace(dwSpace); }
    LPCWSTR EnumChannelName(const DWORD dwSpace, const DWORD dwChannel) { return m_down->EnumChannelName(dwSpace, dwChannel); }
    const BOOL SetChannel(const DWORD dwSpace, const DWORD dwChannel) { return m_down->SetChannel(dwSpace, dwChannel); }
    const DWORD GetCurSpace() { return m_down->GetCurSpace(); }
    const DWORD GetCurChannel() { return m_down->GetCurChannel(); }
    // IBonDriver
    const BOOL OpenTuner() { return m_down->OpenTuner(); }
    void CloseTuner() { m_down->CloseTuner(); }
    const BOOL SetChannel(const BYTE bCh) { return m_down->SetChannel(bCh); }
    const float GetSignalLevel() { return m_down->GetSignalLevel(); }
    const DWORD WaitTsStream(const DWORD dwTimeOut) { return m_down->WaitTsStream(dwTimeOut); }
    const DWORD GetReadyCount() { return m_down->GetReadyCount(); }
    const BOOL GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain) { return m_down->GetTsStream(pDst, pdwSize, pdwRemain); }
    const BOOL GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain) { return m_down->GetTsStream(ppDst, pdwSize, pdwRemain); }
    void PurgeTsStream() { return m_down->PurgeTsStream(); }
    void Release() { m_down->Release(); delete this; }
private:
    CProxyClient3 *const m_down;
};

class CProxyClient : public IBonDriver
{
public:
    CProxyClient(CProxyClient3 *down) : m_down(down) {}
    // IBonDriver
    const BOOL OpenTuner() { return m_down->OpenTuner(); }
    void CloseTuner() { m_down->CloseTuner(); }
    const BOOL SetChannel(const BYTE bCh) { return m_down->SetChannel(bCh); }
    const float GetSignalLevel() { return m_down->GetSignalLevel(); }
    const DWORD WaitTsStream(const DWORD dwTimeOut) { return m_down->WaitTsStream(dwTimeOut); }
    const DWORD GetReadyCount() { return m_down->GetReadyCount(); }
    const BOOL GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain) { return m_down->GetTsStream(pDst, pdwSize, pdwRemain); }
    const BOOL GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain) { return m_down->GetTsStream(ppDst, pdwSize, pdwRemain); }
    void PurgeTsStream() { return m_down->PurgeTsStream(); }
    void Release() { m_down->Release(); delete this; }
private:
    CProxyClient3 *const m_down;
};

class CBlockLock
{
public:
    CBlockLock(CRITICAL_SECTION *cs) : m_cs(cs) { EnterCriticalSection(m_cs); }
    ~CBlockLock() { LeaveCriticalSection(m_cs); }
private:
    CBlockLock(const CBlockLock&);
    CBlockLock &operator=(const CBlockLock&);
    CRITICAL_SECTION *m_cs;
};
