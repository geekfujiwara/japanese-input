#include "astelio/tip/guids.h"

#include <windows.h>

#include <msctf.h>
#include <olectl.h>
#include <wrl/client.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <cwctype>
#include <string>

namespace {

using Microsoft::WRL::ComPtr;

using RegisterFunction = HRESULT(STDAPICALLTYPE*)();

bool EnvironmentFlag(const char* name)
{
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return false;
    }
    const bool enabled = std::string(value) == "1";
    std::free(value);
    return enabled;
}

// Registering a TIP writes HKCR/HKLM, so these tests only run where that is allowed (CI runs as administrator).
bool IntegrationEnabled()
{
    return EnvironmentFlag("ASTELIO_TIP_INTEGRATION");
}

std::wstring TipPath()
{
    const std::string narrow = ASTELIO_TIP_PATH;
    std::wstring path(narrow.begin(), narrow.end());
    for (wchar_t& c : path) {
        if (c == L'/') {
            c = L'\\';
        }
    }
    return path;
}

std::wstring GuidString(const GUID& guid)
{
    wchar_t text[40] = {};
    StringFromGUID2(guid, text, 40);
    return text;
}

bool KeyExists(HKEY root, const std::wstring& path)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    RegCloseKey(key);
    return true;
}

std::wstring ReadString(HKEY root, const std::wstring& path, const wchar_t* name)
{
    wchar_t buffer[MAX_PATH * 2] = {};
    DWORD bytes = sizeof(buffer);
    if (RegGetValueW(root, path.c_str(), name, RRF_RT_REG_SZ, nullptr, buffer, &bytes) != ERROR_SUCCESS) {
        return {};
    }
    return buffer;
}

std::wstring Lower(std::wstring text)
{
    for (wchar_t& c : text) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return text;
}

class RegisteredTip {
public:
    RegisteredTip() : module_(LoadLibraryW(TipPath().c_str()))
    {
        if (module_ == nullptr) {
            return;
        }
        register_ = reinterpret_cast<RegisterFunction>(GetProcAddress(module_, "DllRegisterServer"));
        unregister_ = reinterpret_cast<RegisterFunction>(GetProcAddress(module_, "DllUnregisterServer"));
        if (register_ != nullptr && unregister_ != nullptr) {
            result_ = register_();
        }
    }

    ~RegisteredTip()
    {
        Unregister();
        if (module_ != nullptr) {
            FreeLibrary(module_);
        }
    }

    RegisteredTip(const RegisteredTip&) = delete;
    RegisteredTip& operator=(const RegisteredTip&) = delete;

    HRESULT result() const { return result_; }

    HRESULT Unregister()
    {
        if (FAILED(result_) || unregistered_) {
            return S_OK;
        }
        unregistered_ = true;
        return unregister_();
    }

private:
    HMODULE module_ = nullptr;
    RegisterFunction register_ = nullptr;
    RegisterFunction unregister_ = nullptr;
    HRESULT result_ = E_FAIL;
    bool unregistered_ = false;
};

class ComApartment {
public:
    ComApartment() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComApartment()
    {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;

    HRESULT result() const { return result_; }

private:
    HRESULT result_;
};

const std::wstring kClsidKey = L"CLSID\\" + GuidString(astelio::tip::kTextServiceClsid);
const std::wstring kTipKey = L"SOFTWARE\\Microsoft\\CTF\\TIP\\" + GuidString(astelio::tip::kTextServiceClsid);

TEST(TipRegistration, WritesComServerAndJapaneseProfileAndRemovesThem)
{
    if (!IntegrationEnabled()) {
        GTEST_SKIP() << "Set ASTELIO_TIP_INTEGRATION=1 to run (needs administrator rights)";
    }
    ComApartment apartment;
    ASSERT_HRESULT_SUCCEEDED(apartment.result());
    RegisteredTip tip;
    ASSERT_HRESULT_SUCCEEDED(tip.result());

    EXPECT_EQ(Lower(ReadString(HKEY_CLASSES_ROOT, kClsidKey + L"\\InprocServer32", nullptr)), Lower(TipPath()));
    EXPECT_EQ(ReadString(HKEY_CLASSES_ROOT, kClsidKey + L"\\InprocServer32", L"ThreadingModel"), L"Apartment");
    EXPECT_TRUE(KeyExists(HKEY_LOCAL_MACHINE,
                          kTipKey + L"\\LanguageProfile\\0x00000411\\" + GuidString(astelio::tip::kJapaneseProfileGuid)));
    EXPECT_TRUE(KeyExists(HKEY_LOCAL_MACHINE, kTipKey + L"\\Category\\Category\\" + GuidString(GUID_TFCAT_TIP_KEYBOARD)));

    ASSERT_HRESULT_SUCCEEDED(tip.Unregister());
    EXPECT_FALSE(KeyExists(HKEY_CLASSES_ROOT, kClsidKey));
    EXPECT_FALSE(KeyExists(HKEY_LOCAL_MACHINE,
                           kTipKey + L"\\LanguageProfile\\0x00000411\\" + GuidString(astelio::tip::kJapaneseProfileGuid)));
}

TEST(TipActivation, ActivateAdvisesAndDeactivateRemovesTheKeyEventSink)
{
    if (!IntegrationEnabled()) {
        GTEST_SKIP() << "Set ASTELIO_TIP_INTEGRATION=1 to run (needs administrator rights)";
    }
    ComApartment apartment;
    ASSERT_HRESULT_SUCCEEDED(apartment.result());
    RegisteredTip tip;
    ASSERT_HRESULT_SUCCEEDED(tip.result());

    ComPtr<ITfThreadMgr> thread_mgr;
    ASSERT_HRESULT_SUCCEEDED(
        CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&thread_mgr)));
    TfClientId client_id = TF_CLIENTID_NULL;
    ASSERT_HRESULT_SUCCEEDED(thread_mgr->Activate(&client_id));
    ComPtr<ITfKeystrokeMgr> keystrokes;
    ASSERT_HRESULT_SUCCEEDED(thread_mgr.As(&keystrokes));

    // TSF only accepts key event sinks from client ids issued to a text service.
    ComPtr<ITfClientId> client_ids;
    ASSERT_HRESULT_SUCCEEDED(thread_mgr.As(&client_ids));
    TfClientId tip_id = TF_CLIENTID_NULL;
    ASSERT_HRESULT_SUCCEEDED(client_ids->GetClientId(astelio::tip::kTextServiceClsid, &tip_id));

    ComPtr<ITfTextInputProcessorEx> service;
    ASSERT_HRESULT_SUCCEEDED(CoCreateInstance(astelio::tip::kTextServiceClsid, nullptr, CLSCTX_INPROC_SERVER,
                                              IID_PPV_ARGS(&service)));
    ComPtr<ITfKeyEventSink> service_sink;
    ASSERT_HRESULT_SUCCEEDED(service.As(&service_sink));

    ASSERT_HRESULT_SUCCEEDED(service->ActivateEx(thread_mgr.Get(), tip_id, 0));
    // The client id already owns a key event sink, so a second one is refused.
    EXPECT_EQ(keystrokes->AdviseKeyEventSink(tip_id, service_sink.Get(), TRUE), CONNECT_E_ADVISELIMIT);

    ASSERT_HRESULT_SUCCEEDED(service->Deactivate());
    EXPECT_EQ(keystrokes->UnadviseKeyEventSink(tip_id), CONNECT_E_NOCONNECTION);

    service_sink.Reset();
    service.Reset();
    client_ids.Reset();
    keystrokes.Reset();
    EXPECT_HRESULT_SUCCEEDED(thread_mgr->Deactivate());
}

// Switching to the ja-JP profile needs the Japanese input language on the machine, so it only runs on request.
TEST(TipProfile, JapaneseProfileBecomesTheActiveKeyboard)
{
    if (!IntegrationEnabled() || !EnvironmentFlag("ASTELIO_TIP_PROFILE_TEST")) {
        GTEST_SKIP() << "Set ASTELIO_TIP_INTEGRATION=1 and ASTELIO_TIP_PROFILE_TEST=1 (needs Japanese input language)";
    }
    ComApartment apartment;
    ASSERT_HRESULT_SUCCEEDED(apartment.result());
    RegisteredTip tip;
    ASSERT_HRESULT_SUCCEEDED(tip.result());

    ComPtr<ITfThreadMgr> thread_mgr;
    ASSERT_HRESULT_SUCCEEDED(
        CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&thread_mgr)));
    TfClientId client_id = TF_CLIENTID_NULL;
    ASSERT_HRESULT_SUCCEEDED(thread_mgr->Activate(&client_id));

    ComPtr<ITfInputProcessorProfileMgr> profiles;
    ASSERT_HRESULT_SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                              IID_PPV_ARGS(&profiles)));
    ASSERT_HRESULT_SUCCEEDED(profiles->ActivateProfile(
        TF_PROFILETYPE_INPUTPROCESSOR, astelio::tip::kJapaneseLangId, astelio::tip::kTextServiceClsid,
        astelio::tip::kJapaneseProfileGuid, nullptr, TF_IPPMF_FORPROCESS | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE));

    ComPtr<ITfKeystrokeMgr> keystrokes;
    ASSERT_HRESULT_SUCCEEDED(thread_mgr.As(&keystrokes));
    CLSID foreground{};
    ASSERT_HRESULT_SUCCEEDED(keystrokes->GetForeground(&foreground));
    EXPECT_TRUE(IsEqualCLSID(foreground, astelio::tip::kTextServiceClsid));

    keystrokes.Reset();
    profiles.Reset();
    EXPECT_HRESULT_SUCCEEDED(thread_mgr->Deactivate());
}

} // namespace
