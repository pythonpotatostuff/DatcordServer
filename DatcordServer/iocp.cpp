#include <winsock2.h>
#include <Ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <strsafe.h>
#include <thread>
#include <array>
#include <queue>
#include "common.h"
#include "iocp.h"

#pragma warning (disable:4127)

#ifdef _IA64_
#pragma warning(disable:4267)
#endif 

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif


bool g_bEndServer = false;
bool g_bRestart = true;
bool g_bVerbose = true;
DWORD g_dwThreadCount = 0; //worker thread count
HANDLE g_hIOCP = INVALID_HANDLE_VALUE;
SOCKET g_sdListen = INVALID_SOCKET;
std::queue<pSocketContext> g_ContextList;
pSocketContext g_pCtxtList = NULL;	// linked list of context info structures
std::vector<std::thread> g_Threads;
std::vector<HANDLE> g_ThreadHandles;
CRITICAL_SECTION g_CriticalSection;	//guard access to the global context list


bool StartServer()
{
	printer::queuePrintf(printer::color::BLUE, "[STRT] Starting server...\n");

	SYSTEM_INFO systemInfo;
	WSADATA wsaData;
	SOCKET sdAccept = INVALID_SOCKET;
	pSocketContext lpPerSocketContext = NULL;
	DWORD dwRecvNumBytes = 0;
	DWORD dwFlags = 0;
	int nRet = 0;

	nRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (nRet != 0)
	{
		printer::queuePrintf(printer::color::RED, "[STRT] WSAStartup() failed: %d\n", nRet);
		//CtrlHandler : needs to close and exit
		return false;
	}

	try
	{
		InitializeCriticalSection(&g_CriticalSection);
	}
	catch (...)
	{
		printer::queuePrintf(printer::color::RED, "[STRT] InitializeCriticalSection() failed: %d\n", GetLastError());
		return false;
	}

	while (g_bRestart) {
		g_bRestart = false;
		g_bEndServer = true;

		try {
			g_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
			if (g_hIOCP == NULL) {
				printer::queuePrintf(printer::color::RED, "[STRT] CreateIoCompletionPort() failed: %d\n", GetLastError());
			}
			GetSystemInfo(&systemInfo);
			g_dwThreadCount = systemInfo.dwNumberOfProcessors / 4 * 3;
			for (DWORD dwThread = 0; dwThread < g_dwThreadCount; dwThread++) {
				g_Threads.emplace_back(WorkerThread, g_hIOCP);
				g_ThreadHandles.push_back(g_Threads.at(dwThread).native_handle());

				if (!g_Threads.at(dwThread).joinable()) {
					printer::queuePrintf(printer::color::RED, "[STRT] Failed to create worker thread: %d\n", GetLastError());
				}
			}

			if (!CreateListenSocket()) return false;

			while (true) {

				//
				// Loop forever accepting connections from clients until console shuts down.
				//
				sdAccept = WSAAccept(g_sdListen, NULL, NULL, NULL, 0);
				if (sdAccept == SOCKET_ERROR) {

					//
					// If user hits Ctrl+C or Ctrl+Brk or console window is closed, the control
					// handler will close the g_sdListen socket. The above WSAAccept call will 
					// fail and we thus break out the loop,
					//
					printer::queuePrintf(printer::color::RED, "WSAAccept() failed: %d\n", WSAGetLastError());

				}

				//
				// we add the just returned socket descriptor to the IOCP along with its
				// associated key data.  Also the global list of context structures
				// (the key data) gets added to a global list.
				//
				lpPerSocketContext = UpdateCompletionPort(sdAccept, ClientOperation::ClientRead, TRUE);
				if (lpPerSocketContext == NULL)


					//
					// if a CTRL-C was pressed "after" WSAAccept returns, the CTRL-C handler
					// will have set this flag and we can break out of the loop here before
					// we go ahead and post another read (but after we have added it to the 
					// list of sockets to close).
					//
					if (g_bEndServer) break;

				//
				// post initial receive on this socket
				//
				nRet = WSARecv(sdAccept,
					&(lpPerSocketContext->pIOContext->wsabuf),
					1,
					&dwRecvNumBytes,
					&dwFlags,
					&(lpPerSocketContext->pIOContext->Overlapped),
					NULL);
				if (nRet == SOCKET_ERROR && (ERROR_IO_PENDING != WSAGetLastError())) {
					printer::queuePrintf(printer::color::RED, "WSARecv() failed: %d\n", WSAGetLastError());
					CloseClient(lpPerSocketContext, false);
				}
			} //while
		}

		catch (const std::exception & e)
		{
			printer::queuePrintf(printer::color::RED, "Caught exception: %s", e.what());
		}

		g_bEndServer = true;

		//
		// Cause worker threads to exit
		//
		if (g_hIOCP) {
			for (DWORD i = 0; i < g_dwThreadCount; i++) {
				PostQueuedCompletionStatus(g_hIOCP, 0, 0, NULL);
			}
		}

		if (WaitForMultipleObjects(g_dwThreadCount, g_ThreadHandles.data(), TRUE, 1000) != WAIT_OBJECT_0)
			printer::queuePrintf(printer::color::RED, "WaitForMultipleObjects() failed: %d\n", GetLastError());
		else
			for (DWORD i = 0; i < g_dwThreadCount; i++) {
				g_ThreadHandles.at(i) = INVALID_HANDLE_VALUE;
			}

		CtxtListFree();

		if (g_hIOCP) {
			CloseHandle(g_hIOCP);
			g_hIOCP = NULL;
		}

		if (g_sdListen != INVALID_SOCKET) {
			closesocket(g_sdListen);
			g_sdListen = INVALID_SOCKET;
		}

		if (sdAccept != INVALID_SOCKET) {
			closesocket(sdAccept);
			sdAccept = INVALID_SOCKET;
		}

		if (g_bRestart) {
			printer::queuePrintf(printer::color::YELLOW, "\niocpserver is restarting...\n");
		}
		else {
			printer::queuePrintf(printer::color::YELLOW, "\niocpserver is exiting...\n");
		}

	} //while (g_bRestart)
}


//
//  Create a listening socket.
//
bool CreateListenSocket(void)
{
	int nRet = 0;
	int nZero = 0;
	struct addrinfo hints = { 0 };
	struct addrinfo* addrlocal = NULL;

	//
	// Resolve the interface
	//
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_IP;

	if (getaddrinfo(NULL, port, &hints, &addrlocal) != 0) {
		printer::queuePrintf(printer::color::RED, "getaddrinfo() failed with error %d\n", WSAGetLastError());
		return(FALSE);
	}

	if (addrlocal == NULL) {
		printer::queuePrintf(printer::color::RED, "getaddrinfo() failed to resolve/convert the interface\n");
		return(FALSE);
	}

	g_sdListen = WSASocketW(addrlocal->ai_family, addrlocal->ai_socktype, addrlocal->ai_protocol,
		NULL, 0, WSA_FLAG_OVERLAPPED);
	if (g_sdListen == INVALID_SOCKET) {
		printer::queuePrintf(printer::color::RED, "WSASocket(g_sdListen) failed: %d\n", WSAGetLastError());
		return(FALSE);
	}

	nRet = bind(g_sdListen, addrlocal->ai_addr, (int)addrlocal->ai_addrlen);
	if (nRet == SOCKET_ERROR) {
		printer::queuePrintf(printer::color::RED, "bind() failed: %d\n", WSAGetLastError());
		return(FALSE);
	}

	nRet = listen(g_sdListen, 5);
	if (nRet == SOCKET_ERROR) {
		printer::queuePrintf(printer::color::RED, "listen() failed: %d\n", WSAGetLastError());
		return(FALSE);
	}

	//
	// Disable send buffering on the socket.  Setting SO_SNDBUF
	// to 0 causes winsock to stop buffering sends and perform
	// sends directly from our buffers, thereby reducing CPU usage.
	//
	// However, this does prevent the socket from ever filling the
	// send pipeline. This can lead to packets being sent that are
	// not full (i.e. the overhead of the IP and TCP headers is 
	// great compared to the amount of data being carried).
	//
	// Disabling the send buffer has less serious repercussions 
	// than disabling the receive buffer.
	//
	nZero = 0;
	nRet = setsockopt(g_sdListen, SOL_SOCKET, SO_SNDBUF, (char*)&nZero, sizeof(nZero));
	if (nRet == SOCKET_ERROR) {
		printer::queuePrintf(printer::color::RED, "setsockopt(SNDBUF) failed: %d\n", WSAGetLastError());
		return(FALSE);
	}

	//
	// Don't disable receive buffering. This will cause poor network
	// performance since if no receive is posted and no receive buffers,
	// the TCP stack will set the window size to zero and the peer will
	// no longer be allowed to send data.
	//

	// 
	// Do not set a linger value...especially don't set it to an abortive
	// close. If you set abortive close and there happens to be a bit of
	// data remaining to be transfered (or data that has not been 
	// acknowledged by the peer), the connection will be forcefully reset
	// and will lead to a loss of data (i.e. the peer won't get the last
	// bit of data). This is BAD. If you are worried about malicious
	// clients connecting and then not sending or receiving, the server
	// should maintain a timer on each connection. If after some point,
	// the server deems a connection is "stale" it can then set linger
	// to be abortive and close the connection.
	//

	/*
	LINGER lingerStruct;

	lingerStruct.l_onoff = 1;
	lingerStruct.l_linger = 0;

	nRet = setsockopt(g_sdListen, SOL_SOCKET, SO_LINGER,
					  (char *)&lingerStruct, sizeof(lingerStruct) );
	if( nRet == SOCKET_ERROR ) {
		printer::queuePrintf(printer::color::WHITE, "setsockopt(SO_LINGER) failed: %d\n", WSAGetLastError());
		return(FALSE);
	}
	*/

	freeaddrinfo(addrlocal);

	return(TRUE);
}

//
//  Close down a connection with a client.  This involves closing the socket (when 
//  initiated as a result of a CTRL-C the socket closure is not graceful).  Additionally, 
//  any context data associated with that socket is free'd.
//
void CloseClient(pSocketContext lpPerSocketContext, BOOL bGraceful)
{
	try
	{
		EnterCriticalSection(&g_CriticalSection);
	}
	catch (const std::exception & e)
	{
		printer::queuePrintf(printer::color::RED, "Caught exception: %s", e.what());
		return;
	}

	if (lpPerSocketContext) {
		if (g_bVerbose)
			printer::queuePrintf(printer::color::RED, "CloseClient: Socket(%d) connection closing (graceful=%s)\n",
				lpPerSocketContext->Socket, (bGraceful ? "TRUE" : "FALSE"));
		if (!bGraceful) {

			//
			// force the subsequent closesocket to be abortative.
			//
			LINGER  lingerStruct;

			lingerStruct.l_onoff = 1;
			lingerStruct.l_linger = 0;
			setsockopt(lpPerSocketContext->Socket, SOL_SOCKET, SO_LINGER,
				(char*)&lingerStruct, sizeof(lingerStruct)); // do not linger, closes even if there is unsent data present (loss of data)
		}
		closesocket(lpPerSocketContext->Socket);
		lpPerSocketContext->Socket = INVALID_SOCKET;
		CtxtListDeleteFrom(lpPerSocketContext);
		lpPerSocketContext = NULL;
	}
	else {
		printer::queuePrintf(printer::color::RED, "CloseClient: lpPerSocketContext is NULL\n");
	}

	LeaveCriticalSection(&g_CriticalSection);

	return;
}


//
// Worker thread that handles all I/O requests on any socket handle added to the IOCP.
//
DWORD WINAPI WorkerThread(LPVOID WorkThreadContext) {

	HANDLE hIOCP = (HANDLE)WorkThreadContext;
	BOOL bSuccess = FALSE;
	int nRet = 0;
	LPWSAOVERLAPPED lpOverlapped = NULL;
	pSocketContext lpPerSocketContext = NULL;
	pIoContext lpIOContext = NULL;
	WSABUF buffRecv;
	WSABUF buffSend;
	DWORD dwRecvNumBytes = 0;
	DWORD dwSendNumBytes = 0;
	DWORD dwFlags = 0;
	DWORD dwIoSize = 0;

	while (TRUE) {

		//
		// continually loop to service io completion packets
		//
		bSuccess = GetQueuedCompletionStatus(hIOCP, &dwIoSize,
			(PDWORD_PTR)&lpPerSocketContext,
			(LPOVERLAPPED*)&lpOverlapped,
			INFINITE);
		if (!bSuccess)
			printer::queuePrintf(printer::color::RED, "GetQueuedCompletionStatus() failed: %d\n", GetLastError());

		if (lpPerSocketContext == NULL) {

			//
			// CTRL-C handler used PostQueuedCompletionStatus to post an I/O packet with
			// a NULL CompletionKey (or if we get one for any reason).  It is time to exit.
			//
			return(0);
		}

		if (g_bEndServer) {

			//
			// main thread will do all cleanup needed - see finally block
			//
			return(0);
		}

		if (!bSuccess || (bSuccess && (dwIoSize == 0))) {

			//
			// client connection dropped, continue to service remaining (and possibly 
			// new) client connections
			//
			CloseClient(lpPerSocketContext, FALSE);
			continue;
		}

		//
		// determine what type of IO packet has completed by checking the PER_IO_CONTEXT 
		// associated with this socket.  This will determine what action to take.
		//
		lpIOContext = (pIoContext)lpOverlapped;
		switch (lpIOContext->IOOperation) {
		case ClientOperation::ClientRead:
			
			//
			// a read operation has completed, post a write operation to echo the
			// data back to the client using the same data buffer.
			//
			lpIOContext->nTotalBytes = dwIoSize;
			lpIOContext->nSentBytes = 0;
			lpIOContext->wsabuf.len = dwIoSize;
			dwFlags = 0;
			nRet = WSASend(lpPerSocketContext->Socket, &lpIOContext->wsabuf, 1,
				&dwSendNumBytes, dwFlags, &(lpIOContext->Overlapped), NULL);
			if (nRet == SOCKET_ERROR && (ERROR_IO_PENDING != WSAGetLastError())) {
				printer::queuePrintf(printer::color::RED, "WSASend() failed: %d\n", WSAGetLastError());
				CloseClient(lpPerSocketContext, FALSE);
			}
			else if (g_bVerbose) {
				printer::queuePrintf(printer::color::RED, "WorkerThread %d: Socket(%d) Recv completed (%d bytes), Send posted\n",
					GetCurrentThreadId(), lpPerSocketContext->Socket, dwIoSize);
			}
			break;

		case ClientOperation::ClientWrite:

			//
			// a write operation has completed, determine if all the data intended to be
			// sent actually was sent.
			//
			lpIOContext->nSentBytes += dwIoSize;
			dwFlags = 0;
			if (lpIOContext->nSentBytes < lpIOContext->nTotalBytes) {

				//
				// the previous write operation didn't send all the data,
				// post another send to complete the operation
				//
				buffSend.buf = lpIOContext->Buffer + lpIOContext->nSentBytes;
				buffSend.len = lpIOContext->nTotalBytes - lpIOContext->nSentBytes;
				nRet = WSASend(lpPerSocketContext->Socket, &buffSend, 1,
					&dwSendNumBytes, dwFlags, &(lpIOContext->Overlapped), NULL);
				if (nRet == SOCKET_ERROR && (ERROR_IO_PENDING != WSAGetLastError())) {
					printer::queuePrintf(printer::color::RED, "WSASend() failed: %d\n", WSAGetLastError());
					CloseClient(lpPerSocketContext, FALSE);
				}
				else if (g_bVerbose) {
					printer::queuePrintf(printer::color::RED, "WorkerThread %d: Socket(%d) Send partially completed (%d bytes), Recv posted\n",
						GetCurrentThreadId(), lpPerSocketContext->Socket, dwIoSize);
				}
			}
			else {
				//
				// previous write operation completed for this socket, post another recv
				//
				lpIOContext->IOOperation = ClientOperation::ClientRead;
				dwRecvNumBytes = 0;
				dwFlags = 0;
				buffRecv.buf = lpIOContext->Buffer,
					buffRecv.len = buff_size;
				nRet = WSARecv(lpPerSocketContext->Socket, &buffRecv, 1,
					&dwRecvNumBytes, &dwFlags, &lpIOContext->Overlapped, NULL);
				if (nRet == SOCKET_ERROR && (ERROR_IO_PENDING != WSAGetLastError())) {
					printer::queuePrintf(printer::color::RED, "WSARecv() failed: %d\n", WSAGetLastError());
					CloseClient(lpPerSocketContext, FALSE);
				}
				else if (g_bVerbose) {
					printer::queuePrintf(printer::color::RED, "WorkerThread %d: Socket(%d) Send completed (%d bytes), Recv posted\n",
						GetCurrentThreadId(), lpPerSocketContext->Socket, dwIoSize);
				}
			}
			break;

		} //switch
	} //while
	return(0);
}



pSocketContext UpdateCompletionPort(SOCKET sd, ClientOperation ClientIo, BOOL bAddToList)
{

	pSocketContext lpPerSocketContext;

	lpPerSocketContext = CtxtAllocate(sd, ClientIo);
	if (lpPerSocketContext == NULL)
		return NULL;

	g_hIOCP = CreateIoCompletionPort((HANDLE)sd, g_hIOCP, (DWORD_PTR)lpPerSocketContext, 0);
	if (g_hIOCP == NULL) {
		printer::queuePrintf(printer::color::RED, "CreateIoCompletionPort() failed: %d\n", GetLastError());
		if (lpPerSocketContext->pIOContext)
			free(lpPerSocketContext->pIOContext);
		free(lpPerSocketContext);
		return NULL;
	}

	//
	//The listening socket context (bAddToList is FALSE) is not added to the list.
	//All other socket contexts are added to the list.
	//
	if (bAddToList) CtxtListAddTo(lpPerSocketContext);

	if (g_bVerbose)
		printer::queuePrintf(printer::color::BLUE, "UpdateCompletionPort: Socket(%d) added to IOCP\n", lpPerSocketContext->Socket);

	return lpPerSocketContext;
}


//
// Allocate a socket context for the new connection.  
//
pSocketContext CtxtAllocate(SOCKET sd, ClientOperation ClientIO) {

	pSocketContext lpPerSocketContext;

	try
	{
		EnterCriticalSection(&g_CriticalSection);
	}
	catch (const std::exception & e)
	{
		printer::queuePrintf(printer::color::RED, "Caught exception: %s", e.what());
		return NULL;
	}

	lpPerSocketContext = (pSocketContext)malloc(sizeof(SocketContext));
	if (lpPerSocketContext) {
		lpPerSocketContext->pIOContext = (pIoContext)malloc(sizeof(IoContext));
		if (lpPerSocketContext->pIOContext) {
			lpPerSocketContext->Socket = sd;
			lpPerSocketContext->pCtxtBack = NULL;
			lpPerSocketContext->pCtxtForward = NULL;

			lpPerSocketContext->pIOContext->Overlapped.Internal = 0;
			lpPerSocketContext->pIOContext->Overlapped.InternalHigh = 0;
			lpPerSocketContext->pIOContext->Overlapped.Offset = 0;
			lpPerSocketContext->pIOContext->Overlapped.OffsetHigh = 0;
			lpPerSocketContext->pIOContext->Overlapped.hEvent = NULL;
			lpPerSocketContext->pIOContext->IOOperation = ClientIO;
			lpPerSocketContext->pIOContext->pIOContextForward = NULL;
			lpPerSocketContext->pIOContext->nTotalBytes = 0;
			lpPerSocketContext->pIOContext->nSentBytes = 0;
			lpPerSocketContext->pIOContext->wsabuf.buf = lpPerSocketContext->pIOContext->Buffer;
			lpPerSocketContext->pIOContext->wsabuf.len = sizeof(lpPerSocketContext->pIOContext->Buffer);

			ZeroMemory(lpPerSocketContext->pIOContext->wsabuf.buf, lpPerSocketContext->pIOContext->wsabuf.len);
		}
		else {
			free(lpPerSocketContext);
			printer::queuePrintf(printer::color::RED, "HeapAlloc() PER_IO_CONTEXT failed: %d\n", GetLastError());
		}

	}
	else {
		printer::queuePrintf(printer::color::RED, "HeapAlloc() SocketContext failed: %d\n", GetLastError());
	}

	LeaveCriticalSection(&g_CriticalSection);

	return lpPerSocketContext;
}

void CtxtListAddTo(pSocketContext lpPerSocketContext) {

	pSocketContext     pTemp;

	try
	{
		EnterCriticalSection(&g_CriticalSection);
	}
	catch (const std::exception & e)
	{
		printer::queuePrintf(printer::color::RED, "Caught exception: %s", e.what());
		return;
	}

	if (g_pCtxtList == NULL) {

		//
		// add the first node to the linked list
		//
		lpPerSocketContext->pCtxtBack = NULL;
		lpPerSocketContext->pCtxtForward = NULL;
		g_pCtxtList = lpPerSocketContext;
	}
	else {

		//
		// add node to head of list
		//
		pTemp = g_pCtxtList;

		g_pCtxtList = lpPerSocketContext;
		lpPerSocketContext->pCtxtBack = pTemp;
		lpPerSocketContext->pCtxtForward = NULL;

		pTemp->pCtxtForward = lpPerSocketContext;
	}

	LeaveCriticalSection(&g_CriticalSection);
	return;
}

//
//  Remove a client context structure from the global list of context structures.
//
void CtxtListDeleteFrom(pSocketContext lpPerSocketContext) {
	pSocketContext pBack;
	pSocketContext pForward;
	pIoContext     pNextIO = NULL;
	pIoContext     pTempIO = NULL;

	try
	{
		EnterCriticalSection(&g_CriticalSection);
	}
	catch (const std::exception & e)
	{
		printer::queuePrintf(printer::color::WHITE, "Caught exception: %s", e.what());
		return;
	}

	if (lpPerSocketContext) {
		pBack = lpPerSocketContext->pCtxtBack;
		pForward = lpPerSocketContext->pCtxtForward;


		if ((pBack == NULL) && (pForward == NULL)) {

			//
			// This is the only node in the list to delete
			//
			g_pCtxtList = NULL;
		}
		else if ((pBack == NULL) && (pForward != NULL)) {

			//
			// This is the start node in the list to delete
			//
			pForward->pCtxtBack = NULL;
			g_pCtxtList = pForward;
		}
		else if ((pBack != NULL) && (pForward == NULL)) {

			//
			// This is the end node in the list to delete
			//
			pBack->pCtxtForward = NULL;
		}
		else if (pBack && pForward) {

			//
			// Neither start node nor end node in the list
			//
			pBack->pCtxtForward = pForward;
			pForward->pCtxtBack = pBack;
		}

		//
		// Free all i/o context structures per socket
		//
		pTempIO = lpPerSocketContext->pIOContext;
		do {
			pNextIO = pTempIO->pIOContextForward;
			if (pTempIO) {

				//
				//The overlapped structure is safe to free when only the posted i/o has
				//completed. Here we only need to test those posted but not yet received 
				//by PQCS in the shutdown process.
				//
				if (g_bEndServer)
					while (!HasOverlappedIoCompleted((LPOVERLAPPED)pTempIO)) Sleep(0); // With C++11, you have a better platform independent way to do that: std::this_thread::yield()
				free(pTempIO);
				pTempIO = NULL;
			}
			pTempIO = pNextIO;
		} while (pNextIO);

		free(lpPerSocketContext);
		lpPerSocketContext = NULL;

	}
	else {
		printer::queuePrintf(printer::color::WHITE, "CtxtListDeleteFrom: lpPerSocketContext is NULL\n");
	}

	LeaveCriticalSection(&g_CriticalSection);
	return;
}

//
//  Free all context structure in the global list of context structures.
//
void CtxtListFree() {
	pSocketContext     pTemp1, pTemp2;

	try
	{
		EnterCriticalSection(&g_CriticalSection);
	}
	catch (const std::exception & e)
	{
		printer::queuePrintf(printer::color::WHITE, "Caught exception: %s", e.what());
		return;
	}

	pTemp1 = g_pCtxtList;
	while (pTemp1) {
		pTemp2 = pTemp1->pCtxtBack;
		CloseClient(pTemp1, FALSE);
		pTemp1 = pTemp2;
	}

	LeaveCriticalSection(&g_CriticalSection);
	return;
}

