#include "Log.h"
#include "printer.h"


void Log::GetTimestamp()
{
	SYSTEMTIME lt = { 0 };
	GetLocalTime(&lt);
	sprintf_s<timeSize>(time, "[%04d-%02d-%02d|%02d:%02d:%02d]", lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);
}


Log::Log(const char* lpFname) : hFile{ INVALID_HANDLE_VALUE },
	hMap{ INVALID_HANDLE_VALUE },
	data{}, qwSize{}, qwViewSize{},
	dwGran{}, qwCurrentIndex{},
	size{}, time{}, maxIndex{}
{
	if (!InitializeCriticalSectionAndSpinCount(&cSection, 1024)) { //Initilize the critical section with a spin count of 1024
		printer::queuePrintf(printer::color::RED, "[LOG] InitializeCriticalSectionAndSpinCount() failed: %d", GetLastError());
		return;
	}

	hFile = CreateFileA(lpFname, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) { //Open existing file or create a new one
		try {
			DeleteCriticalSection(&cSection);
		}
		catch (const std::exception & e) {
			printer::queuePrintf(printer::color::RED, "[LOG] DeleteCriticalSection() failed: %s", e.what());
		}
		printer::queuePrintf(printer::color::RED, "[LOG] CreateFileA() failed: %d", GetLastError());
		return;
	}

	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	dwGran = sysInfo.dwAllocationGranularity; //Get system granularity because view of file must be at page edge

	if (fileExtend() == -1) { //Extend the file at add room for logging
		printer::queuePrintf(printer::color::RED, "[LOG] fileExtend failed");
		return;
	}
}


uint64_t Log::log(const char* lpStr)
{
	try { //Enter the critical section
		EnterCriticalSection(&cSection);
	}
	catch (const std::exception & e) {
		printer::queuePrintf(printer::color::RED, "[LOG] EnterCriticalSection() failed: %s", e.what());
		return -1;
	}

	ZeroMemory(time, timeSize);
	GetTimestamp();
	strcat_s(time, lpStr); //Append lpStr to buff
	size = strlen(time);
	for (size_t i = 0; i <= size; i++) { //Append buff to the end of the file
		data[qwCurrentIndex] = time[i];
		qwCurrentIndex++;
		if (qwCurrentIndex % 600 == 0) { //Every 600th character appended, flush the file, saving the changes to disk
			if (!FlushViewOfFile(data, 0)) {
				printer::queuePrintf(printer::color::RED, "[LOG] FlushViewOfFile() failed: %d", GetLastError());
				try {
					LeaveCriticalSection(&cSection);
				}
				catch (const std::exception & e) {
					printer::queuePrintf(printer::color::RED, "[LOG] LeaveCriticalSection() failed: %s", e.what());
					return -1;
				}
				return -1;
			}
		}
		if (qwCurrentIndex == maxIndex) { //Once the end of the file is reached extend the file
			if (fileExtend() == -1) {
				printer::queuePrintf(printer::color::RED, "[LOG] fileExtend() failed: %d", GetLastError());
				try {
					LeaveCriticalSection(&cSection);
				}
				catch (const std::exception & e) {
					printer::queuePrintf(printer::color::RED, "[LOG] LeaveCriticalSection() failed: %s", e.what());
					return -1;
				}
				return -1;
			}
		}
	}

	try { //Leave the critical section
		LeaveCriticalSection(&cSection);
	}
	catch (const std::exception & e) {
		printer::queuePrintf(printer::color::RED, "[LOG] LeaveCriticalSection() failed: %s", e.what());
		return -1;
	}

	return size;
}


uint64_t Log::fileExtend()
{
	if (hMap != 0 && hMap != INVALID_HANDLE_VALUE) {
		if (!UnmapViewOfFile(hMap)) { //Unmap the view of the file
			printer::queuePrintf(printer::color::RED, "[LOG] UnmapViewOfFile() failed: %d", GetLastError());
			return -1;
		}
		if (!CloseHandle(hMap)) { //Close the handle to the file mapping
			printer::queuePrintf(printer::color::RED, "[LOG] CloseHandle() failed: %d", GetLastError());
			return -1;
		}
	}

	qwSize.low = GetFileSize(hFile, &qwSize.high);
	qwSize.qword += extendSize;

	//Open the file mapping with the new extended size, the file resizes itself when this is done
	hMap = CreateFileMappingA(hFile, NULL, PAGE_READWRITE, qwSize.high, qwSize.low, NULL);
	if (hMap == INVALID_HANDLE_VALUE || hMap == 0) {
		if (!CloseHandle(hFile)) {
			printer::queuePrintf(printer::color::RED, "[LOG] CloseHandle() failed: %d", GetLastError());
		}
		try {
			DeleteCriticalSection(&cSection);
		}
		catch (const std::exception & e) {
			printer::queuePrintf(printer::color::RED, "[LOG] DeleteCriticalSection() failed: %s", e.what());
		}
		printer::queuePrintf(printer::color::RED, "[LOG] CreateFileMappingA() failed: %d", GetLastError());
		return -1;
	}

	qwViewSize.low = GetFileSize(hFile, &qwViewSize.high);
	qwViewSize.qword = qwViewSize.qword / dwGran * dwGran; //Determine where the last page of the file starts
	qwCurrentIndex = qwSize.qword - qwViewSize.qword; //Determine where to start logging from
	maxIndex = extendSize + qwCurrentIndex - 1; //Determine where the end of the file is, leaving 1 character at the end for the EOF

	data = (char*)MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, qwSize.high, qwSize.low, 0);
	if (!data) { //Map the view of the file
		printer::queuePrintf(printer::color::RED, "[LOG] MapViewOfFile() failed: %d", GetLastError());
		return -1;
	}

	return qwSize.qword;
}


Log::~Log()
{
	if (!FlushViewOfFile(data, 0)) { //Flush view of file to save it to disk
		printer::queuePrintf(printer::color::RED, "[LOG] FlushViewOfFile() failed: %d", GetLastError());
	}
	if (!UnmapViewOfFile(hMap)) { //Unmap the view of the file
		printer::queuePrintf(printer::color::RED, "[LOG] UnmapViewOfFile() failed: %d", GetLastError());
	}
	if (!CloseHandle(hMap)) { //Close the handle to the file mapping
		printer::queuePrintf(printer::color::RED, "[LOG] CloseHandle() failed: %d", GetLastError());
	}
	if (!CloseHandle(hFile)) { //Close the handle to the file
		printer::queuePrintf(printer::color::RED, "[LOG] CloseHandle() failed: %d", GetLastError());
	}
	try { //Delete the critical section
		DeleteCriticalSection(&cSection);
	}
	catch (const std::exception & e) {
		printer::queuePrintf(printer::color::WHITE, "[LOG] DeleteCriticalSection() failed: %s", e.what());
		return;
	}
}

