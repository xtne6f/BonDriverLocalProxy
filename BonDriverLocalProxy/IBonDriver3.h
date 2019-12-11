// IBonDriver3.h: IBonDriver3 クラスのインターフェイス
//
/////////////////////////////////////////////////////////////////////////////

#pragma once


#include "IBonDriver2.h"


/////////////////////////////////////////////////////////////////////////////
// Bonドライバインタフェース3
/////////////////////////////////////////////////////////////////////////////

class IBonDriver3 : public IBonDriver2
{
public:
// IBonDriver3
	virtual const DWORD GetTotalDeviceNum(void) = 0;
	virtual const DWORD GetActiveDeviceNum(void) = 0;
	virtual const BOOL SetLnbPower(const BOOL bEnable) = 0;
	
// IBonDriver
	virtual void Release(void) = 0;
};

// IBonDriver3->C互換構造体
struct STRUCT_IBONDRIVER3
{
	STRUCT_IBONDRIVER2 st2;
	DWORD (*pF17)(void *);
	DWORD (*pF18)(void *);
	BOOL (*pF19)(void *, BOOL);
	STRUCT_IBONDRIVER &Initialize(IBonDriver3 *pBon3, const void *pEnd) {
		pF17 = F17;
		pF18 = F18;
		pF19 = F19;
		return st2.Initialize(pBon3, pEnd ? pEnd : this + 1);
	}
	static DWORD F17(void *p) { return static_cast<IBonDriver3 *>(static_cast<IBonDriver *>(p))->GetTotalDeviceNum(); }
	static DWORD F18(void *p) { return static_cast<IBonDriver3 *>(static_cast<IBonDriver *>(p))->GetActiveDeviceNum(); }
	static BOOL F19(void *p, BOOL a0) { return static_cast<IBonDriver3 *>(static_cast<IBonDriver *>(p))->SetLnbPower(a0); }
};

#define DEFINE_BON_STRUCT3_ADAPTER(st, p) \
	const DWORD GetTotalDeviceNum() { return st.pF17(p); } \
	const DWORD GetActiveDeviceNum() { return st.pF18(p); } \
	const BOOL SetLnbPower(const BOOL bEnable) { return st.pF19(p, bEnable); }

// C互換構造体->IBonDriver3
class CBonStruct3Adapter : public IBonDriver3
{
public:
	bool Adapt(const STRUCT_IBONDRIVER &st) {
		if (static_cast<const char *>(st.pEnd) - reinterpret_cast<const char *>(&st) < static_cast<int>(sizeof(st3))) {
			return false;
		}
		st3 = reinterpret_cast<const STRUCT_IBONDRIVER3 &>(st);
		return true;
	}
	DEFINE_BON_STRUCT_ADAPTER(st3.st2.st, st3.st2.st.pCtx);
	DEFINE_BON_STRUCT2_ADAPTER(st3.st2, st3.st2.st.pCtx);
	DEFINE_BON_STRUCT3_ADAPTER(st3, st3.st2.st.pCtx);
protected:
	STRUCT_IBONDRIVER3 st3;
};
