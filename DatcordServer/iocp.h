#pragma once

#include <WinSock2.h>
#include <WS2tcpip.h>
#include "common.h"
#include "printer.h"
#include <vector>
constexpr auto port = "6969";
constexpr int buff_size = 4096;


class IocpServer
{
private:
	enum class ClientOperation { ClientRead, ClientWrite };
	union sockaddr_data
	{
		struct sockaddr_in6     s6;
		struct sockaddr			sa;
		struct sockaddr_storage ss;
	};

	class ClientContext
	{
	public:
		//IO context (first data member must be WSAOVERLAPPED)
		WSAOVERLAPPED Overlapped;
		char Buffer[buff_size];
		WSABUF wsabuf;
		int nTotalBytes;
		WSABUF remainingData;
		int nSentBytes;
		int recepients;
		ClientOperation IOOperation;
		SOCKET Socket;
		bool m_bGraceful;

	public:
		ClientContext(SOCKET sd, ClientOperation ClientIO, bool graceful);
		~ClientContext();
	};

private:
	HANDLE m_hIOCP;
	SOCKET m_sdTcpAccept;

	CRITICAL_SECTION m_criticalSection;

	std::vector<std::thread> m_threads;

	bool m_bServerReady;
	std::atomic<bool> m_bStopServer;

	std::vector<SOCKET> allClients; //TODO

private:
	bool UpdateCompletionPort(ClientContext* clientContext);

	bool EnterCritical(std::string err = "");
	bool LeaveCritical(std::string err = "");

	static void Worker(IocpServer* self);
	bool ClientRead(ClientContext* key, int tId);
	bool ClientWrite(ClientContext* key, int tId);

public:
	IocpServer();
	void Shutdown();

	bool Run();
};

