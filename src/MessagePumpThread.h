#pragma once

class CMessagePumpThread
{
public:
	CMessagePumpThread(void);
	~CMessagePumpThread(void);

	static UINT MessagePumpThread(void* thisptr);

protected:
	virtual void TakeMsg(UINT msg, WPARAM wParam, LPARAM lParam)	{ return; }
	void RunMessagePump();

	UINT m_threadID;
	HANDLE m_hEvt;

public:
	void Start();
	void Stop(); 
	void PostMsg(UINT msg, WPARAM wParam, LPARAM lParam);

	UINT getThreadID() const { return m_threadID; }
};



