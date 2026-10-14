// Crash report: on a host access violation the runtime does not recover from,
// log the host call stack with symbol names. Recompiled functions are named
// after their guest address (sub_8xxxxxxx), so the stack reads as the guest
// call chain. Needs a build with debug info (build.bat relwithdebinfo).
//
// Registered last (AddVectoredExceptionHandler(0)), so the runtime's own
// handlers (MMIO, write watches, guest physical callbacks) see every fault
// first; this one only logs and lets the fault continue to the crash.
#if defined(_WIN32)
#include <windows.h>

#include <dbghelp.h>

#include <atomic>
#include <string>

#include <rex/logging.h>

namespace {

std::atomic<bool> g_reported{false};

std::string Describe(HANDLE process, DWORD64 address) {
  alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
  auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = MAX_SYM_NAME;
  DWORD64 displacement = 0;
  std::string out = "?";
  if (SymFromAddr(process, address, &displacement, symbol)) {
    out = std::string(symbol->Name);
  }
  // The generated .cpp line carries the original PPC instruction as a comment
  // right above it, so file:line pins the guest instruction.
  IMAGEHLP_LINE64 line = {};
  line.SizeOfStruct = sizeof(line);
  DWORD line_displacement = 0;
  if (SymGetLineFromAddr64(process, address, &line_displacement, &line)) {
    std::string file = line.FileName;
    out += " (" + file.substr(file.find_last_of("\\/") + 1) + ":" + std::to_string(line.LineNumber) +
           ")";
  }
  return out;
}

LONG CALLBACK OnException(EXCEPTION_POINTERS* info) {
  const DWORD code = info->ExceptionRecord->ExceptionCode;
  if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  if (g_reported.exchange(true)) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  HANDLE process = GetCurrentProcess();
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  SymInitialize(process, nullptr, TRUE);

  CONTEXT context = *info->ContextRecord;
  REXLOG_ERROR("crash: exception 0x{:08X} at {} (rip 0x{:X}), fault address 0x{:X}", code,
               Describe(process, context.Rip), context.Rip,
               info->ExceptionRecord->NumberParameters > 1
                   ? info->ExceptionRecord->ExceptionInformation[1]
                   : 0);

  STACKFRAME64 frame = {};
  frame.AddrPC.Offset = context.Rip;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Offset = context.Rbp;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Offset = context.Rsp;
  frame.AddrStack.Mode = AddrModeFlat;
  for (int depth = 0; depth < 40; ++depth) {
    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context,
                     nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
        !frame.AddrPC.Offset) {
      break;
    }
    REXLOG_ERROR("crash:   #{:02} {}", depth, Describe(process, frame.AddrPC.Offset));
  }
  rex::FlushLogging();
  return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

namespace forza2 {

void InstallCrashReporter() { AddVectoredExceptionHandler(0, OnException); }

}  // namespace forza2

#else

namespace forza2 {
void InstallCrashReporter() {}
}  // namespace forza2

#endif
