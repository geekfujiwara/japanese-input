#pragma once

#include <windows.h>

namespace astelio::tip {

HMODULE ModuleHandle();
void AddModuleRef();
void ReleaseModuleRef();
bool CanUnloadModule();

HRESULT RegisterTextService();
HRESULT UnregisterTextService();

} // namespace astelio::tip
