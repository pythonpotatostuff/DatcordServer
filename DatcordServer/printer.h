#pragma once

#include <thread>
#include <string>
#include <atomic>
#include <queue>


namespace printer
{
	enum class color { WHITE = 0x07, GREEN = 0x0A, RED = 0x0C, BLUE = 0x0B, YELLOW = 0x0E };
	struct PrintData { color color; std::string text; };
	extern std::thread tPrinter;
	extern std::atomic<bool> bStopPrinter;
	extern HANDLE hOut;
	extern std::queue<PrintData> qPrint;
	extern va_list arglist;
	constexpr DWORD timeSize = 1024;
	bool startPrinter();
	size_t queuePrintf(color, const char* lpFormat, ...);
	bool stopPrinter();
	void printLoop();
	std::string GetTimestamp();
}

