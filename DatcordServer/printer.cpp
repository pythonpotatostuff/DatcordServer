#include <Windows.h>
#include "printer.h"


std::thread printer::tPrinter;
std::atomic<bool> printer::bStopPrinter;
HANDLE printer::hOut;
std::queue<printer::PrintData> printer::qPrint;
va_list printer::arglist;


std::string printer::GetTimestamp()
{
	SYSTEMTIME lt = { 0 };
	GetLocalTime(&lt);
	char time[timeSize];
	sprintf_s<timeSize>(time, "[%04d-%02d-%02d|%02d:%02d:%02d]", lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);
	return std::string(time);
}


bool printer::startPrinter()
{
	bStopPrinter = false;
	hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	tPrinter = std::thread(printLoop);
	if (tPrinter.joinable()) return true;
	return false;
}


size_t printer::queuePrintf(color c, const char* lpFormat, ...)
{
	va_start(arglist, lpFormat);
	char cBuffer[1024];
	vsnprintf(cBuffer, 1024, lpFormat, arglist);
	std::string strBuff(cBuffer);
	strBuff.insert(0, GetTimestamp() + ": ");
	printer::qPrint.push({ c, strBuff });
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

