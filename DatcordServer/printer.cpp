#include <Windows.h>
#include "printer.h"


std::thread printer::tPrinter;
std::atomic<bool> printer::bStopPrinter;
HANDLE printer::hOut;
std::queue<printer::PrintData> printer::qPrint;
va_list printer::arglist;


bool printer::startPrinter()
{
	bStopPrinter = false;
	hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	tPrinter = std::thread(printLoop);
	if (tPrinter.joinable()) return true;
	return false;
}


int printer::queuePrintf(color c, const char* lpFormat, ...)
{
	va_start(arglist, lpFormat);
	char cBuffer[1024];
	vsnprintf(cBuffer, 1024, lpFormat, arglist);
	std::string strBuff(cBuffer);
	printer::qPrint.push({ strBuff, c });
	return strBuff.length();
}


bool printer::stopPrinter()
{
	bStopPrinter = true;
	while (WaitForSingleObject(tPrinter.native_handle(), 100) != WAIT_OBJECT_0) {}
	SetConsoleTextAttribute(hOut, 0x07);
	return true;
}


void printer::printLoop()
{
	while (!bStopPrinter) {
		if (!qPrint.empty()) {
			SetConsoleTextAttribute(hOut, static_cast<WORD>(qPrint.front().color));
			printf("%s", qPrint.front().text.c_str());
			qPrint.pop();
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}

