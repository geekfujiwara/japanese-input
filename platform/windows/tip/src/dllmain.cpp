#include "module.h"
#include "text_service.h"

#include "astelio/tip/guids.h"

#include <atomic>

namespace astelio::tip {
namespace {

HMODULE g_module = nullptr;
std::atomic<long> g_module_refs{0};

class ClassFactory final : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** object) override
    {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *object = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    // The factory is a static object; its references keep the module loaded.
    STDMETHODIMP_(ULONG) AddRef() override
    {
        AddModuleRef();
        return 2;
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        ReleaseModuleRef();
        return 1;
    }

    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** object) override
    {
        if (object == nullptr) {
            return E_POINTER;
        }
        *object = nullptr;
        if (outer != nullptr) {
            return CLASS_E_NOAGGREGATION;
        }
        return TextService::Create(riid, object);
    }

    STDMETHODIMP LockServer(BOOL lock) override
    {
        if (lock) {
            AddModuleRef();
        } else {
            ReleaseModuleRef();
        }
        return S_OK;
    }
};

ClassFactory g_class_factory;

} // namespace

HMODULE ModuleHandle()
{
    return g_module;
}

void AddModuleRef()
{
    ++g_module_refs;
}

void ReleaseModuleRef()
{
    --g_module_refs;
}

bool CanUnloadModule()
{
    return g_module_refs.load() == 0;
}

KeyDiagnostics& Diagnostics()
{
    static KeyDiagnostics diagnostics;
    return diagnostics;
}

} // namespace astelio::tip

extern "C" void WINAPI AstelioTipKeyDiagnostics(long* counters, int count)
{
    if (counters == nullptr || count < 4) {
        return;
    }
    const astelio::tip::KeyDiagnostics& diagnostics = astelio::tip::Diagnostics();
    counters[0] = diagnostics.test_key_down;
    counters[1] = diagnostics.key_down;
    counters[2] = diagnostics.null_context;
    counters[3] = diagnostics.eaten;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH) {
        astelio::tip::g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** object)
{
    if (object == nullptr) {
        return E_POINTER;
    }
    *object = nullptr;
    if (clsid != astelio::tip::kTextServiceClsid) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    return astelio::tip::g_class_factory.QueryInterface(riid, object);
}

STDAPI DllCanUnloadNow()
{
    return astelio::tip::CanUnloadModule() ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer()
{
    return astelio::tip::RegisterTextService();
}

STDAPI DllUnregisterServer()
{
    return astelio::tip::UnregisterTextService();
}
