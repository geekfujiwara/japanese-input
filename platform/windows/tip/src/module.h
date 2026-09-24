#pragma once

#include <windows.h>

namespace astelio::tip {

HMODULE ModuleHandle();
void AddModuleRef();
void ReleaseModuleRef();
bool CanUnloadModule();

HRESULT RegisterTextService();
HRESULT UnregisterTextService();

// Key event counters for diagnosing routing in integration tests.
struct KeyDiagnostics {
    long test_key_down = 0;
    long key_down = 0;
    long null_context = 0;
    long eaten = 0;
};
KeyDiagnostics& Diagnostics();

} // namespace astelio::tip
