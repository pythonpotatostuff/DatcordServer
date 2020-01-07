#include <Windows.h>
#include "printer.h"


std::thread printer::tPrinter;
std::atomic<bool> printer::bStopPrinter;
HANDLE printer::hOut;
std::queue<std::pair<std::string, printer::color>> printer::qPrint;
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
	printer::qPrint.push(make_pair(strBuff, c));
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
			switch (qPrint.front().second)
			{
			case color::WHITE: SetConsoleTextAttribute(hOut, 0x07); break;
			case color::GREEN: SetConsoleTextAttribute(hOut, 0x0A); break;
			case color::RED: SetConsoleTextAttribute(hOut, 0x0C); break;
			case color::BLUE: SetConsoleTextAttribute(hOut, 0x0B); break;
			case color::YELLOW: SetConsoleTextAttribute(hOut, 0x0E); break;
			}
			printf("%s", qPrint.front().first.c_str());
			qPrint.pop();
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}

