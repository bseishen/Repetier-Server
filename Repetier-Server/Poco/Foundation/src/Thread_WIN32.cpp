//
// Thread_WIN32.h
//
// $Id: //poco/1.4/Foundation/src/Thread_WIN32.cpp#5 $
//
// Library: Foundation
// Package: Threading
// Module:  Thread
//
// Copyright (c) 2004-2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
// 
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//


#include "Poco/Thread_WIN32.h"
#include "Poco/Exception.h"
#include "Poco/ErrorHandler.h"
#include <process.h>


#if defined(_DEBUG) && defined(POCO_WIN32_DEBUGGER_THREAD_NAMES)


namespace
{
	/// See <http://msdn.microsoft.com/en-us/library/xcb2z8hs.aspx> 
	/// and <http://blogs.msdn.com/b/stevejs/archive/2005/12/19/505815.aspx> for
	/// more information on the code below.

	const DWORD MS_VC_EXCEPTION = 0x406D1388;
	
	#pragma pack(push,8)
	typedef struct tagTHREADNAME_INFO
	{
		DWORD dwType;     // Must be 0x1000.
		LPCSTR szName;    // Pointer to name (in user addr space).
		DWORD dwThreadID; // Thread ID (-1=caller thread).
		DWORD dwFlags;    // Reserved for future use, must be zero.
	} THREADNAME_INFO;
	#pragma pack(pop)
	
	void setThreadName(DWORD dwThreadID, const char* threadName)
	{
		if (IsDebuggerPresent())
		{
			THREADNAME_INFO info;
			info.dwType     = 0x1000;
			info.szName     = threadName;
			info.dwThreadID = dwThreadID;
			info.dwFlags    = 0;
		
			__try
			{
				RaiseException(MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(ULONG_PTR), (ULONG_PTR*)&info);
			}
			__except (EXCEPTION_CONTINUE_EXECUTION)
			{
			}
		}
	}
}


#endif


namespace Poco {


ThreadImpl::CurrentThreadHolder ThreadImpl::_currentThreadHolder;


ThreadImpl::ThreadImpl():
	_pRunnableTarget(0),
	_thread(0),
	_threadId(0),
	_prio(PRIO_NORMAL_IMPL),
	_stackSize(POCO_THREAD_STACK_SIZE)
{
}

			
ThreadImpl::~ThreadImpl()
{
	if (_thread) CloseHandle(_thread);
}


void ThreadImpl::setPriorityImpl(int prio)
{
	if (prio != _prio)
	{
		_prio = prio;
		if (_thread)
		{
			if (SetThreadPriority(_thread, _prio) == 0)
				throw SystemException("cannot set thread priority");
		}
	}
}


void ThreadImpl::setOSPriorityImpl(int prio, int /* policy */)
{
	setPriorityImpl(prio);
}


void ThreadImpl::startImpl(Runnable& target)
{
	if (isRunningImpl())
		throw SystemException("thread already running");

	_pRunnableTarget = &target;

	createImpl(runnableEntry, this);
}


void ThreadImpl::startImpl(Callable target, void* pData)
{
	if (isRunningImpl())
		throw SystemException("thread already running");

	threadCleanup();
	_callbackTarget.callback = target;
	_callbackTarget.pData = pData;

	createImpl(callableEntry, this);
}


void ThreadImpl::createImpl(Entry ent, void* pData)
{
#if defined(_DLL)
	_thread = CreateThread(NULL, _stackSize, ent, pData, 0, &_threadId);
#else
	unsigned threadId;
	_thread = (HANDLE) _beginthreadex(NULL, _stackSize, ent, this, 0, &threadId);
	_threadId = static_cast<DWORD>(threadId);
#endif
	if (!_thread)
		throw SystemException("cannot create thread");
	if (_prio != PRIO_NORMAL_IMPL && !SetThreadPriority(_thread, _prio))
		throw SystemException("cannot set thread priority");
}


void ThreadImpl::joinImpl()
{
	if (!_thread) return;

	switch (WaitForSingleObject(_thread, INFINITE))
	{
	case WAIT_OBJECT_0:
		threadCleanup();
		return;
	default:
		throw SystemException("cannot join thread");
	}
}


bool ThreadImpl::joinImpl(long milliseconds)
{
	if (!_thread) return true;

	switch (WaitForSingleObject(_thread, milliseconds + 1))
	{
	case WAIT_TIMEOUT:
		return false;
	case WAIT_OBJECT_0:
		threadCleanup();
		return true;
	default:
		throw SystemException("cannot join thread");
	}
}


bool ThreadImpl::isRunningImpl() const
{
	if (_thread)
	{
		DWORD ec = 0;
		return GetExitCodeThread(_thread, &ec) && ec == STILL_ACTIVE;
	}
	return false;
}


void ThreadImpl::threadCleanup()
{
	if (!_thread) return;
	if (CloseHandle(_thread)) _thread = 0;
}


ThreadImpl* ThreadImpl::currentImpl()
{
	return _currentThreadHolder.get();
}


ThreadImpl::TIDImpl ThreadImpl::currentTidImpl()
{
    return GetCurrentThreadId();
}


#if defined(_DLL)
DWORD WINAPI ThreadImpl::runnableEntry(LPVOID pThread)
#else
unsigned __stdcall ThreadImpl::runnableEntry(void* pThread)
#endif
{
	_currentThreadHolder.set(reinterpret_cast<ThreadImpl*>(pThread));
#if defined(_DEBUG) && defined(POCO_WIN32_DEBUGGER_THREAD_NAMES)
	setThreadName(-1, reinterpret_cast<Thread*>(pThread)->getName().c_str());
#endif
	try
	{
		reinterpret_cast<ThreadImpl*>(pThread)->_pRunnableTarget->run();
	}
	catch (Exception& exc)
	{
		ErrorHandler::handle(exc);
	}
	catch (std::exception& exc)
	{
		ErrorHandler::handle(exc);
	}
	catch (...)
	{
		ErrorHandler::handle();
	}
	return 0;
}


#if defined(_DLL)
DWORD WINAPI ThreadImpl::callableEntry(LPVOID pThread)
#else
unsigned __stdcall ThreadImpl::callableEntry(void* pThread)
#endif
{
	_currentThreadHolder.set(reinterpret_cast<ThreadImpl*>(pThread));
#if defined(_DEBUG) && defined(POCO_WIN32_DEBUGGER_THREAD_NAMES)
	setThreadName(-1, reinterpret_cast<Thread*>(pThread)->getName().c_str());
#endif
	try
	{
		ThreadImpl* pTI = reinterpret_cast<ThreadImpl*>(pThread);
		pTI->_callbackTarget.callback(pTI->_callbackTarget.pData);
	}
	catch (Exception& exc)
	{
		ErrorHandler::handle(exc);
	}
	catch (std::exception& exc)
	{
		ErrorHandler::handle(exc);
	}
	catch (...)
	{
		ErrorHandler::handle();
	}
	return 0;
}


} // namespace Poco
