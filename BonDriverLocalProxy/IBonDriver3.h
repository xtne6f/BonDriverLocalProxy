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
