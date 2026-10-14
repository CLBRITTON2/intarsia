// The command the process tests capture to hand its stdout to a process outside the command's job: it copies its
// stdout handle into the process the argument names, then exits, so that process alone keeps the pipe open.
//
//   pipe_holder.exe <process id>

#include <windows.h>

#include <cstdlib>
#include <cwchar>

int wmain(int argc, wchar_t* argv[]) { // NOLINT(misc-use-internal-linkage)
    if (argc != 2) {
        return 2;
    }
    const DWORD id     = std::wcstoul(argv[1], nullptr, 10);
    HANDLE      holder = OpenProcess(PROCESS_DUP_HANDLE, FALSE, id);
    if (holder == nullptr) {
        return 3;
    }
    HANDLE     copy   = nullptr;
    const BOOL copied = DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_OUTPUT_HANDLE), holder, &copy, 0, FALSE, DUPLICATE_SAME_ACCESS);
    CloseHandle(holder);
    if (!copied) {
        return 4;
    }
    return 0;
}
