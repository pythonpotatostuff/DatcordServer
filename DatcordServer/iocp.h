#pragma once

#include <WinSock2.h>
#include "common.h"
#include "printer.h"

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
	IoContext*		pIOContextForward;
} *pIoContext;

typedef struct SocketContext {
	SOCKET         Socket;
	IoContext*     pIOContext;
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

