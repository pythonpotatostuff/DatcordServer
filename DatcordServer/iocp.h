#pragma once

#include <WinSock2.h>
#include "common.h"
#include "printer.h"

constexpr auto port = "6969";
constexpr int buff_size = 1048;


typedef enum _IO_OPERATION {
	ClientIoAccept,
	ClientIoRead,
	ClientIoWrite
} IO_OPERATION, *PIO_OPERATION;


typedef struct _PER_IO_CONTEXT {
	WSAOVERLAPPED  Overlapped;
	char           Buffer[buff_size];
	WSABUF         wsabuf;
	int            nTotalBytes;
	int            nSentBytes;
	IO_OPERATION   IOOperation;
	SOCKET         SocketAccept;

	struct _PER_IO_CONTEXT* pIOContextForward;
} PER_IO_CONTEXT, *PPER_IO_CONTEXT;


typedef struct _PER_SOCKET_CONTEXT {
	SOCKET                      Socket;
	//linked list for all outstanding i/o on the socket
	PPER_IO_CONTEXT             pIOContext;
	struct _PER_SOCKET_CONTEXT* pCtxtBack;
	struct _PER_SOCKET_CONTEXT* pCtxtForward;
} PER_SOCKET_CONTEXT, *PPER_SOCKET_CONTEXT;


bool StartServer();

BOOL WINAPI CtrlHandler(DWORD dwEvent);


BOOL CreateListenSocket();


DWORD WINAPI WorkerThread(LPVOID WorkContext);


PPER_SOCKET_CONTEXT UpdateCompletionPort(SOCKET s, IO_OPERATION ClientIo, BOOL bAddToList);


// bAddToList is FALSE for listening socket, and TRUE for connection sockets.
// As we maintain the context for listening socket in a global structure, we
// don't need to add it to the list.

VOID CloseClient(PPER_SOCKET_CONTEXT lpPerSocketContext, BOOL bGraceful);


PPER_SOCKET_CONTEXT CtxtAllocate(SOCKET s, IO_OPERATION ClientIO);


VOID CtxtListFree();


VOID CtxtListAddTo(PPER_SOCKET_CONTEXT lpPerSocketContext);


VOID CtxtListDeleteFrom(PPER_SOCKET_CONTEXT lpPerSocketContext);

