#include "astelio/tip/guids.h"

#include <windows.h>

#include <msctf.h>
#include <wrl/client.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

namespace {

using Microsoft::WRL::ComPtr;

using RegisterFunction = HRESULT(STDAPICALLTYPE*)();

// Registering a TIP writes HKCR/HKLM, so the test only runs where it is allowed (CI runs as administrator).
bool IntegrationEnabled()
{
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, "ASTELIO_TIP_INTEGRATION") != 0 || value == nullptr) {
        return false;
    }
    const bool enabled = std::string(value) == "1";
    std::free(value);
    return enabled;
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
        if (SUCCEEDED(result_)) {
            unregister_();
        }
        if (module_ != nullptr) {
            FreeLibrary(module_);
        }
    }

    RegisteredTip(const RegisteredTip&) = delete;
    RegisteredTip& operator=(const RegisteredTip&) = delete;

    HRESULT result() const { return result_; }

private:
    HMODULE module_ = nullptr;
    RegisterFunction register_ = nullptr;
    RegisterFunction unregister_ = nullptr;
    HRESULT result_ = E_FAIL;
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

TEST(TipActivation, JapaneseProfileActivatesTheTextService)
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

    ComPtr<ITfInputProcessorProfileMgr> profiles;
    ASSERT_HRESULT_SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                              IID_PPV_ARGS(&profiles)));

    TF_INPUTPROCESSORPROFILE registered{};
    ASSERT_HRESULT_SUCCEEDED(profiles->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR, astelio::tip::kJapaneseLangId,
                                                  astelio::tip::kTextServiceClsid,
                                                  astelio::tip::kJapaneseProfileGuid, nullptr, &registered));
    EXPECT_NE(registered.dwFlags & TF_IPP_FLAG_ENABLED, 0u);

    ASSERT_HRESULT_SUCCEEDED(profiles->ActivateProfile(
        TF_PROFILETYPE_INPUTPROCESSOR, astelio::tip::kJapaneseLangId, astelio::tip::kTextServiceClsid,
        astelio::tip::kJapaneseProfileGuid, nullptr, TF_IPPMF_FORPROCESS | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE));

    TF_INPUTPROCESSORPROFILE active{};
    ASSERT_HRESULT_SUCCEEDED(profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &active));
    EXPECT_TRUE(IsEqualCLSID(active.clsid, astelio::tip::kTextServiceClsid));
    EXPECT_TRUE(IsEqualGUID(active.guidProfile, astelio::tip::kJapaneseProfileGuid));

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
