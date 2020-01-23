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

#pragma warning(disable : 4127)

#ifdef _IA64_
#pragma warning(disable : 4267)
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif


IocpServer::IocpServer()
{
	int bRet;
	DWORD dwThreadCount;
	WSADATA wsaData;
	SYSTEM_INFO systemInfo;

	printer::queuePrintf(printer::color::BLUE, "[STARTUP] Initilizing server...\n");

	if (!InitializeCriticalSectionAndSpinCount(&m_criticalSection, 1024)) //Initilize the critical section
	{
		printer::queuePrintf(printer::color::RED, "[STARTUP] InitializeCriticalSectionAndSpinCount() failed: %d\n", GetLastError());
		Shutdown();
		return;
	}

	bRet = WSAStartup(MAKEWORD(2, 2), &wsaData); //Startup WSA V2.2
	if (bRet != 0)
	{
		printer::queuePrintf(printer::color::RED, "[STARTUP] WSAStartup() failed: %d\n", bRet);
		Shutdown();
		return;
	}

	GetSystemInfo(&systemInfo);
	dwThreadCount = systemInfo.dwNumberOfProcessors * 2; //Make number of total threads twice number of logical cores

	//Create new io completion port with number of logical cores as number of concurrent threads
	m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, systemInfo.dwNumberOfProcessors);
	if (m_hIOCP == NULL)
	{
		printer::queuePrintf(printer::color::RED, "[STARTUP] CreateIoCompletionPort() failed: %d\n", GetLastError());
		Shutdown();
		return;
	}

	for (DWORD i = 0; i < dwThreadCount; i++) //Startup worker threads and pass the IOCP handle to them
	{
		m_threads.emplace_back(Worker, this);
		if (!m_threads.at(i).joinable())
		{
			printer::queuePrintf(printer::color::RED, "[STARTUP] Failed to create worker thread: %d\n", GetLastError());
			Shutdown();
			return;
		}
	}

	//Create TCP listen socket
	m_sdTcpAccept = WSASocketW(AF_INET6, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	//MIGHT NEED A SECOND LISTEN SOCKET TO ACCEPT UDP CONNECTIONS TOO
	if (m_sdTcpAccept == INVALID_SOCKET)
	{
		printer::queuePrintf(printer::color::RED, "[STARTUP] WSASocketW() failed: %d\n", WSAGetLastError());
		Shutdown();
		return;
	}

	const char flagVal = 1;
	bRet = setsockopt(m_sdTcpAccept, SOL_SOCKET, SO_REUSEADDR, &flagVal, sizeof(flagVal)); //Allow reuse of the local address
	if (bRet == -1)
	{
		printer::queuePrintf(printer::color::RED, "[STARTUP] setsockopt() failed: %d\n", WSAGetLastError());
		Shutdown();
		return;
	}

	sockaddr_data addr;
	SecureZeroMemory(&addr, sizeof(addr));
	addr.s6.sin6_family = AF_INET6;
	addr.s6.sin6_addr = in6addr_any;
	addr.s6.sin6_port = htons(atoi(port));

	bRet = bind(m_sdTcpAccept, &addr.sa, sizeof(addr)); //Bind listen socket
	if (bRet == SOCKET_ERROR)
	{
		printer::queuePrintf(printer::color::RED, "[STARTUP] bind() failed: %d\n", WSAGetLastError());
		Shutdown();
		return;
	}

	bRet = listen(m_sdTcpAccept, SOMAXCONN); //Set listen socket to the listening state
	if (bRet == SOCKET_ERROR)
	{
		printer::queuePrintf(printer::color::RED, "[STARTUP] listen() failed: %d\n", WSAGetLastError());
		Shutdown();
		return;
	}

	printer::queuePrintf(printer::color::GREEN, "[STARTUP] Initilization successful");
	m_bServerReady = true; //Set the ready state to true
}


void IocpServer::Shutdown()
{
	m_bServerReady = false;
	if (shutdown(m_sdTcpAccept, SD_BOTH) == SOCKET_ERROR) //Shutdown tcp listening socket
	{
		printer::queuePrintf(printer::color::RED, "[SHUTDOWN] shutdown() failed: %d\n", WSAGetLastError());
	}
	if (closesocket(m_sdTcpAccept) == SOCKET_ERROR) //Close tcp listening socket
	{
		printer::queuePrintf(printer::color::RED, "[SHUTDOWN] closesocket() failed: %d\n", WSAGetLastError());
	}

	m_bStopServer = true; //Signal threads to stop
	for (auto& thread : m_threads) //Wait for each thread to exit
	{
		thread.join();
	}

	if (m_hIOCP) //Close the IOCP handle
	{
		if (CloseHandle(m_hIOCP) == 0)
		{
			printer::queuePrintf(printer::color::RED, "[SHUTDOWN] CloseHandle() failed: %d\n", GetLastError());
		}
	}

	if (WSACleanup() == SOCKET_ERROR) //Cleanup WSA
	{
		printer::queuePrintf(printer::color::RED, "[SHUTDOWN] WSACleanup() failed: %d\n", WSAGetLastError());
	}

	try //Delete critical section
	{
		DeleteCriticalSection(&m_criticalSection);
	}
	catch (...)
	{
		printer::queuePrintf(printer::color::RED, "[SHUTDOWN] DeleteCriticalSection() failed: %d\n", GetLastError());
	}
}


bool IocpServer::EnterCritical(std::string err)
{
	try //Try to enter the critical section
	{
		EnterCriticalSection(&m_criticalSection);
	}
	catch (const std::exception & e)
	{
		err.insert(0, " [ENTER CRIT] EnterCriticalSection() failed:");
		printer::queuePrintf(printer::color::WHITE, err.c_str(), e.what());
		return false;
	}
	return true;
}


bool IocpServer::LeaveCritical(std::string err)
{
	try //Try to leave the critical section
	{
		LeaveCriticalSection(&m_criticalSection);
	}
	catch (const std::exception & e)
	{
		err.insert(0, " [LEAVE CRIT] LeaveCriticalSection() failed: ");
		printer::queuePrintf(printer::color::WHITE, err.c_str(), e.what());
		return false;
	}
	return true;
}


bool IocpServer::Run()
{
	DWORD dwRecvNumBytes = 0;
	DWORD dwFlags = 0;
	int bRet = 0;

	sockaddr_data clientInfo;
	int addrLen = sizeof(sockaddr_data);
	char ip[INET6_ADDRSTRLEN];

	if (!m_bServerReady) return false;

	//accept incoming connetions and post initial read for them
	while (true)
	{
		SecureZeroMemory(&clientInfo, sizeof(clientInfo));
		SOCKET m_sdTcpClient = WSAAccept(m_sdTcpAccept, &clientInfo.sa, &addrLen, NULL, 0);

		if (m_sdTcpClient == SOCKET_ERROR)
		{
			printer::queuePrintf(printer::color::RED, "[RUN] WSAAccept() failed: %d\n", WSAGetLastError());
			continue;
		}

		allClients.push_back(m_sdTcpClient); //TEMP

		SecureZeroMemory(ip, INET6_ADDRSTRLEN);
		if (inet_ntop(AF_INET6, &clientInfo.s6.sin6_addr, ip, INET6_ADDRSTRLEN) != NULL)
		{
			printer::queuePrintf(printer::color::GREEN, "[RUN] WSAAccept() got a connection %s\n", ip);
		}
		else
		{
			printer::queuePrintf(printer::color::RED, "[RUN] inet_ntop() failed\n");
		}

		IocpServer::ClientContext* clientContext = new IocpServer::ClientContext(m_sdTcpClient, ClientOperation::ClientRead, true);
		if (!UpdateCompletionPort(clientContext))
		{
			printer::queuePrintf(printer::color::RED, "[RUN] UpdateCompletionPort() failed: %d\n", GetLastError());
			delete clientContext;
			continue;
		}

		//Post initial receive on this socket
		bRet = WSARecv(m_sdTcpClient,
			&(clientContext->wsabuf),
			1,
			&dwRecvNumBytes,
			&dwFlags,
			&(clientContext->Overlapped),
			NULL);
		if (bRet == SOCKET_ERROR && (ERROR_IO_PENDING != WSAGetLastError()))
		{
			printer::queuePrintf(printer::color::RED, "[RUN] WSARecv() failed: %d\n", WSAGetLastError());
			delete clientContext;
			continue;
		}
	}
	return true;
}


void IocpServer::Worker(IocpServer* self)
{
	DWORD tId = GetCurrentThreadId();
	ClientContext* key = nullptr;
	OVERLAPPED* overlapped = NULL;
	DWORD numBytes = 0;
	bool complete = false;
	bool bRet = false;

	while (!self->m_bStopServer)
	{
		complete = GetQueuedCompletionStatus(
								self->m_hIOCP, 
								&numBytes, 
								reinterpret_cast<PULONG_PTR>(&key),
								&overlapped, 
								INFINITE);

		if (key == nullptr) continue;

		if (complete)
		{
			switch (key->IOOperation)
			{
			case ClientOperation::ClientRead: //Read data
				key->nTotalBytes = numBytes;
				key->wsabuf.len = numBytes;
				key->nSentBytes = 0;
				bRet = self->ClientRead(key, tId);
				break;
			case ClientOperation::ClientWrite: //Write data
				key->nSentBytes += numBytes;
				bRet = self->ClientWrite(key, tId);
				break;
			}

			if (!bRet)
			{
				printer::queuePrintf(
					printer::color::RED,
					"[WORKER] [THREAD %d] %s() failed: ",
					tId,
					key->IOOperation == ClientOperation::ClientRead ? "ClientRead" : "ClientWrite");
				continue;
			}
		}
		else
		{
			printer::queuePrintf(printer::color::RED, "[WORKER] [THREAD %d] GetQueuedCompletionStatus() failed: %d", tId, GetLastError());
			continue;
		}
	}
}


bool IocpServer::ClientRead(ClientContext* key, int tId)
{
	//Because a recv just completed, start a write of the data
	printer::queuePrintf(printer::color::GREEN, "[WORKER] [THREAD %d] [READ] Recieved %d bytes. Posted a write operation", tId, key->nTotalBytes);
	key->IOOperation = ClientOperation::ClientWrite;
	if (WSASend(key->Socket, &key->wsabuf, 1, NULL, 0, &(key->Overlapped), NULL) == SOCKET_ERROR)
	{
		printer::queuePrintf(printer::color::GREEN, "[WORKER] [THREAD %d] [READ] WSASend() failed: %", tId, WSAGetLastError());
		return false;
	}
	return true;
}


bool IocpServer::ClientWrite(ClientContext* key, int tId)
{
	//Because a write just completed, if the write did not write all the data do another write, otherwise post a read
	if (key->nSentBytes < key->nTotalBytes)
	{ //NONE OF THIS SHIT WORKS BY THE WAY SO SURE AS HELL DONT USE IT. THIS IS GONNA TAKE YEARS TO FIGURE OUT
		key->remainingData.buf = key->wsabuf.buf + key->nSentBytes;
		key->remainingData.len = key->wsabuf.len - key->nSentBytes;
		if (WSASend(key->Socket, &key->wsabuf, 1, NULL, 0, &(key->Overlapped), NULL) == SOCKET_ERROR)
		{

		}
	}
	return true;
}


bool IocpServer::UpdateCompletionPort(ClientContext* clientContext)
{
	m_hIOCP = CreateIoCompletionPort((HANDLE)clientContext->Socket, m_hIOCP, (DWORD_PTR)clientContext, 0); // first member of this struct is required to be WSAOVERLAPPED 
	if (m_hIOCP == NULL)
	{
		printer::queuePrintf(printer::color::RED, "[UPDATE COMPLETION PORT] CreateIoCompletionPort() failed: %d\n", GetLastError());
		return false;
	}

	printer::queuePrintf(printer::color::BLUE, "[UPDATE COMPLETION PORT] Socket(%d) added to IOCP\n", clientContext->Socket);

	return true;
}

