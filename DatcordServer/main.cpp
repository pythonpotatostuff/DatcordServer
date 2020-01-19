#include "iocp.h"

#pragma comment(lib, "ws2_32")
typedef unsigned long long ull;


int main(int argc, char* argv[])
{
	printer::startPrinter();
	IocpServer iocp;
	iocp.Run();
	printer::stopPrinter();
	return 0;
}

