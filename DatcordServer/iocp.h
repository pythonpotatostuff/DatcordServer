#pragma once

#include <WinSock2.h>
#include "common.h"
#include "printer.h"
#include <list>
constexpr auto port = "6969";
constexpr int buff_size = 1048;


typedef enum class ClientOperation {
	ClientAccept,
	ClientRead,
	ClientWrite
} *pClientOperation;

typedef struct IoContext {
	WSAOVERLAPPED	Overlapped;
	char			Buffer[buff_size];
	WSABUF			wsabuf;
	int				nTotalBytes;
	int				nSentBytes;
	ClientOperation	IOOperation;
	SOCKET			SocketAccept;
	IoContext* pIOContextForward;
} *pIoContext;

typedef struct SocketContext {
	SOCKET         Socket;
	IoContext* pIOContext;
	SocketContext* pCtxtBack;
	SocketContext* pCtxtForward;
} *pSocketContext;


bool StartServer();

bool WINAPI CtrlHandler(DWORD dwEvent);

bool CreateListenSocket();

DWORD WINAPI WorkerThread(LPVOID WorkContext);

pSocketContext UpdateCompletionPort(SOCKET s, ClientOperation ClientIo, BOOL bAddToList);

// bAddToList is FALSE for listening socket, and TRUE for connection sockets.
// As we maintain the context for listening socket in a global structure, we
// don't need to add it to the list.
void CloseClient(pSocketContext lpPerSocketContext, BOOL bGraceful);

pSocketContext CtxtAllocate(SOCKET s, ClientOperation ClientIO);

void CtxtListFree();

void CtxtListAddTo(pSocketContext lpPerSocketContext);

void CtxtListDeleteFrom(pSocketContext lpPerSocketContext);

// =================================================================================

template<class T>
class LinkedList : public std::list<T>
{
	CRITICAL_SECTION CriticalSection;


};

class IocpServer
{
private:
	HANDLE m_hIOCP;
	SOCKET m_sdTcpAccept;
	LinkedList<pSocketContext> m_contextList; //this might be TEMP
	pSocketContext m_pCtxtList; //this might be TEMP
	std::vector<HANDLE> m_threadHandles;
	CRITICAL_SECTION m_criticalSection; //Make segments of code thread safe
	bool m_serverReady;
public:
	IocpServer();
	void Shutdown();

	bool EnterCritical(char* const lpStr);
	bool LeaveCritical(char* const lpStr);

	void ContextAdd(pSocketContext lpPerSocketContext);
	void ContextDelete();

	bool Run();

	static void Worker(IocpServer* self);
};

