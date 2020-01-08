#pragma once
#include <Windows.h>
#include <vector>


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
	short qwCurrentIndex;
	static constexpr DWORD extendSize = 8192;
	short maxIndex;
public:
	Log(char* lpFname);
	uint64_t log(char* lpStr, size_t size);
	uint64_t fileExtend();
	~Log();
};

