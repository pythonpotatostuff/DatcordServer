#include "iocp.h"


IocpServer::ClientContext::ClientContext(SOCKET sd, ClientOperation ClientIO, bool graceful) :
	Socket{ sd },
	m_bGraceful{ graceful },
	IOOperation{ ClientIO },
	Buffer{},
	wsabuf{ buff_size, Buffer },
	nTotalBytes{ 0 },
	nSentBytes{ 0 },
	SocketAccept{},
	Overlapped{}

{
	/*IOOperation = ClientIO;
	wsabuf.buf = Buffer;
	wsabuf.len = sizeof(Buffer);*/
	SecureZeroMemory(wsabuf.buf, wsabuf.len);
}


IocpServer::ClientContext::~ClientContext()
{
	printer::queuePrintf(printer::color::RED, "CloseClient: Socket(%d) connection closing (graceful=%s)\n", Socket, m_bGraceful ? "TRUE" : "FALSE");
	if (!m_bGraceful)
	{

		//
		// force the subsequent closesocket to be abortative.
		//
		LINGER lingerStruct;

		lingerStruct.l_onoff = 1;
		lingerStruct.l_linger = 0;
		setsockopt(Socket, SOL_SOCKET, SO_LINGER,
			(char*)&lingerStruct, sizeof(lingerStruct)); // do not linger, closes even if there is unsent data present (loss of data)
	}
	closesocket(Socket); // why wrong
	Socket = INVALID_SOCKET;

	//CtxtListDeleteFrom(lpPerSocketContext);

	printer::queuePrintf(printer::color::RED, "CloseClient: lpPerSocketContext is NULL\n");
}

