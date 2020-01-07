#pragma once

#include <thread>
#include <string>
#include <atomic>
#include <queue>


namespace printer
{
	enum class color { WHITE, GREEN, RED, BLUE, YELLOW };
	extern std::thread tPrinter;
	extern std::atomic<bool> bStopPrinter;
	extern HANDLE hOut;
	extern std::queue<std::pair<std::string, color>> qPrint;
	extern va_list arglist;
	bool startPrinter();
	int queuePrintf(color, const char* lpFormat, ...);
	bool stopPrinter();
	void printLoop();
}

