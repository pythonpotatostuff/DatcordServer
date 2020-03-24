#include <Windows.h>
#include "printer.h"
#include <algorithm>

std::thread printer::tPrinter;
std::atomic<bool> printer::bStopPrinter;
HANDLE printer::hOut;
std::queue<printer::PrintData> printer::qPrint;
va_list printer::arglist;
CRITICAL_SECTION printer::critSection;


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
	if (!InitializeCriticalSectionAndSpinCount(&critSection, 1024))
	{
		printer::queuePrintf(printer::color::RED, "[PRINTER] InitializeCriticalSectionAndSpinCount() failed: %d\n", GetLastError());
		return false;
	}
	tPrinter = std::thread(printLoop);
	if (tPrinter.joinable()) return true;
	return false;
}


size_t printer::queuePrintf(color c, const char* lpFormat, ...)
{
	try //Try to enter the critical section
	{
		EnterCriticalSection(&critSection);
	}
	catch (const std::exception& e)
	{
		return -1;
	}
	va_start(arglist, lpFormat);
	char cBuffer[1024]{};
	vsnprintf(cBuffer, 1024, lpFormat, arglist);
	std::string strBuff(cBuffer);
	strBuff.insert(0, GetTimestamp() + ": ");
	printer::qPrint.push({ c, strBuff });
	try //Try to leave the critical section
	{
		LeaveCriticalSection(&critSection);
	}
	catch (const std::exception& e)
	{
		return -1;
	}
	return strBuff.length();
}


bool printer::stopPrinter()
{
	bStopPrinter = true;
	tPrinter.join();
	SetConsoleTextAttribute(hOut, 0x07);
	return true;
}


void printer::printLoop()
{
	while (!bStopPrinter || !qPrint.empty()) {
		try //Try to enter the critical section
		{
			EnterCriticalSection(&critSection);
		}
		catch (const std::exception& e)
		{
		}
		if (!qPrint.empty()) {
			SetConsoleTextAttribute(hOut, static_cast<WORD>(qPrint.front().color));
			printf("%s", qPrint.front().text.c_str());
			qPrint.pop();
		}
		try //Try to enter the critical section
		{
			LeaveCriticalSection(&critSection);
		}
		catch (const std::exception& e)
		{
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}

