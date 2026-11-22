#include "extera/exd_crash_log.h"

#include "core/version.h"
#include "settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#include <csignal>
#include <cstdio>
#include <exception>
#pragma comment(lib, "dbghelp.lib")
#endif

namespace Exd::CrashLog {
namespace {

#ifdef Q_OS_WIN

wchar_t FolderPath[MAX_PATH] = { 0 };
char Buffer[64 * 1024] = { 0 };
int BufferSize = 0;
volatile LONG Handling = 0;

void Append(const char *format, ...) {
	if (BufferSize >= int(sizeof(Buffer)) - 1) {
		return;
	}
	va_list args;
	va_start(args, format);
	const auto written = _vsnprintf_s(
		Buffer + BufferSize,
		sizeof(Buffer) - BufferSize,
		_TRUNCATE,
		format,
		args);
	va_end(args);
	BufferSize = (written < 0)
		? int(sizeof(Buffer)) - 1
		: (BufferSize + written);
}

void AppendAddress(DWORD64 address) {
	HMODULE module = nullptr;
	wchar_t path[MAX_PATH] = { 0 };
	if (GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
				| GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(address),
			&module)
		&& module
		&& GetModuleFileNameW(module, path, MAX_PATH)) {
		const wchar_t *name = path;
		for (auto i = path; *i; ++i) {
			if (*i == L'\\' || *i == L'/') {
				name = i + 1;
			}
		}
		Append(
			"%ls+0x%llx\n",
			name,
			address - reinterpret_cast<DWORD64>(module));
	} else {
		Append("0x%llx\n", address);
	}
}

void AppendHeader(const char *reason) {
	const auto base = reinterpret_cast<const char*>(GetModuleHandleW(nullptr));
	const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
		base + dos->e_lfanew);
	SYSTEMTIME time;
	GetLocalTime(&time);
	Append(
		"exDesktop %s (%d), image %08lx\n"
		"time %04d-%02d-%02d %02d:%02d:%02d, thread %lu\n"
		"reason %s\n\n",
		AppVersionStr,
		int(AppVersion),
		nt->FileHeader.TimeDateStamp,
		int(time.wYear),
		int(time.wMonth),
		int(time.wDay),
		int(time.wHour),
		int(time.wMinute),
		int(time.wSecond),
		GetCurrentThreadId(),
		reason);
}

void AppendWalk(CONTEXT context) {
	const auto process = GetCurrentProcess();
	const auto thread = GetCurrentThread();
	SymInitialize(process, nullptr, TRUE);
	auto frame = STACKFRAME64();
	frame.AddrPC.Offset = context.Rip;
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Offset = context.Rbp;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = context.Rsp;
	frame.AddrStack.Mode = AddrModeFlat;
	Append("stack:\n");
	for (auto i = 0; i != 96; ++i) {
		if (!StackWalk64(
				IMAGE_FILE_MACHINE_AMD64,
				process,
				thread,
				&frame,
				&context,
				nullptr,
				SymFunctionTableAccess64,
				SymGetModuleBase64,
				nullptr)
			|| !frame.AddrPC.Offset) {
			break;
		}
		AppendAddress(frame.AddrPC.Offset);
	}
}

void AppendCaptured() {
	void *frames[96] = { nullptr };
	const auto count = RtlCaptureStackBackTrace(0, 96, frames, nullptr);
	Append("stack:\n");
	for (auto i = 0; i != int(count); ++i) {
		AppendAddress(reinterpret_cast<DWORD64>(frames[i]));
	}
}

void Flush(EXCEPTION_POINTERS *pointers) {
	SYSTEMTIME time;
	GetLocalTime(&time);
	wchar_t name[MAX_PATH] = { 0 };
	CreateDirectoryW(FolderPath, nullptr);
	swprintf_s(
		name,
		L"%ls\\crash-%04d%02d%02d-%02d%02d%02d",
		FolderPath,
		int(time.wYear),
		int(time.wMonth),
		int(time.wDay),
		int(time.wHour),
		int(time.wMinute),
		int(time.wSecond));
	wchar_t text[MAX_PATH] = { 0 };
	swprintf_s(text, L"%ls.txt", name);
	const auto file = CreateFileW(
		text,
		GENERIC_WRITE,
		0,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (file != INVALID_HANDLE_VALUE) {
		auto written = DWORD();
		WriteFile(file, Buffer, DWORD(BufferSize), &written, nullptr);
		CloseHandle(file);
	}
	if (!pointers) {
		return;
	}
	wchar_t dumpPath[MAX_PATH] = { 0 };
	swprintf_s(dumpPath, L"%ls.dmp", name);
	const auto dump = CreateFileW(
		dumpPath,
		GENERIC_WRITE,
		0,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (dump != INVALID_HANDLE_VALUE) {
		auto info = MINIDUMP_EXCEPTION_INFORMATION();
		info.ThreadId = GetCurrentThreadId();
		info.ExceptionPointers = pointers;
		info.ClientPointers = FALSE;
		MiniDumpWriteDump(
			GetCurrentProcess(),
			GetCurrentProcessId(),
			dump,
			MINIDUMP_TYPE(MiniDumpWithIndirectlyReferencedMemory
				| MiniDumpScanMemory),
			&info,
			nullptr,
			nullptr);
		CloseHandle(dump);
	}
}

LONG WINAPI Filter(EXCEPTION_POINTERS *pointers) {
	if (InterlockedExchange(&Handling, 1)) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	char reason[64] = { 0 };
	sprintf_s(
		reason,
		"exception 0x%08lx",
		pointers->ExceptionRecord->ExceptionCode);
	AppendHeader(reason);
	Append("address ");
	AppendAddress(reinterpret_cast<DWORD64>(
		pointers->ExceptionRecord->ExceptionAddress));
	const auto code = pointers->ExceptionRecord->ExceptionCode;
	if (code == EXCEPTION_ACCESS_VIOLATION
		&& pointers->ExceptionRecord->NumberParameters >= 2) {
		Append(
			"%s 0x%llx\n",
			pointers->ExceptionRecord->ExceptionInformation[0]
				? "write"
				: "read",
			DWORD64(pointers->ExceptionRecord->ExceptionInformation[1]));
	}
	Append("\n");
	AppendWalk(*pointers->ContextRecord);
	Flush(pointers);
	return EXCEPTION_CONTINUE_SEARCH;
}

void Abort(const char *reason) {
	if (InterlockedExchange(&Handling, 1)) {
		return;
	}
	AppendHeader(reason);
	AppendCaptured();
	Flush(nullptr);
}

void SignalHandler(int) {
	Abort("abort");
}

void TerminateHandler() {
	Abort("terminate");
	TerminateProcess(GetCurrentProcess(), 3);
}

void PureCallHandler() {
	Abort("pure virtual call");
	TerminateProcess(GetCurrentProcess(), 3);
}

#endif // Q_OS_WIN

} // namespace

QString Folder() {
	return cWorkingDir() + u"exd_crashes"_q;
}

void Install() {
#ifdef Q_OS_WIN
	const auto folder = QDir::toNativeSeparators(Folder()).toStdWString();
	if (folder.size() >= MAX_PATH) {
		return;
	}
	wcscpy_s(FolderPath, folder.c_str());
	SetUnhandledExceptionFilter(Filter);
	signal(SIGABRT, SignalHandler);
	std::set_terminate(TerminateHandler);
	_set_purecall_handler(PureCallHandler);
#endif // Q_OS_WIN
}

QString TakeNewReport() {
	const auto dir = QDir(Folder());
	const auto list = dir.entryInfoList(
		{ u"crash-*.txt"_q },
		QDir::Files,
		QDir::Time);
	if (list.isEmpty()) {
		return QString();
	}
	const auto latest = list.front().absoluteFilePath();
	const auto marker = Folder() + u"/.seen"_q;
	auto seen = QFile(marker);
	if (seen.open(QIODevice::ReadOnly)) {
		if (QString::fromUtf8(seen.readAll()) == latest) {
			return QString();
		}
		seen.close();
	}
	if (seen.open(QIODevice::WriteOnly)) {
		seen.write(latest.toUtf8());
	}
	return latest;
}

} // namespace Exd::CrashLog
