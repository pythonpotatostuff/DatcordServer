#pragma once
#include <Windows.h>
#include <string>


class Log
{
private:
	union QWORD { uint64_t qword; DWORD high; DWORD low; };
	CRITICAL_SECTION cSection;
	HANDLE hFile;
	HANDLE hMap;
	char* data;
	QWORD qwSize;
	QWORD qwViewSize;
	DWORD dwGran;
	uint64_t qwCurrentIndex;
	uint64_t size;
	static constexpr DWORD extendSize = 8192;
	static constexpr DWORD timeSize = 1024;
	char time[timeSize];
	uint64_t maxIndex;

	void GetTimestamp();
	

public:
	Log(const char* lpFname);
	uint64_t log(const char* lpStr);
	uint64_t fileExtend();
	~Log();
};

