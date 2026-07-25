#pragma once

class CMessagePumpThread
{
public:
	CMessagePumpThread(void);
	~CMessagePumpThread(void);

	// Thread function uses __cdecl (default) to match AfxBeginThread signature.
	// This ensures MFC properly tracks thread lifecycle and cleans up thread state,
	// preventing mtex.cpp:90 debug assertion during DLL detach.
	static UINT MessagePumpThread(void* thisptr);

protected:
	virtual void TakeMsg(UINT msg, WPARAM wParam, LPARAM lParam)	{ return; }
	void RunMessagePump();

	UINT m_threadID;
	HANDLE m_thread;
	HANDLE m_hEvt;

public:
	void Start();
	void Stop(); 
	void PostMsg(UINT msg, WPARAM wParam, LPARAM lParam);

	UINT getThreadID() const { return m_threadID; }
	uintptr_t getThread() const { return m_thread; }
};



