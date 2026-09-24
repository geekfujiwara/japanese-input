#include "module.h"

#include "astelio/tip/guids.h"

#include <msctf.h>
#include <wrl/client.h>

#include <cwchar>
#include <iterator>

namespace astelio::tip {
namespace {

constexpr wchar_t kDescription[] = L"Astelio IME";

const GUID* const kCategories[] = {
    &GUID_TFCAT_TIP_KEYBOARD,
    &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
    &GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
};

bool ClsidKeyPath(wchar_t (&path)[64])
{
    wchar_t clsid[40] = {};
    if (StringFromGUID2(kTextServiceClsid, clsid, static_cast<int>(std::size(clsid))) == 0) {
        return false;
    }
    return wcscpy_s(path, std::size(path), L"CLSID\\") == 0 && wcscat_s(path, std::size(path), clsid) == 0;
}

HRESULT SetString(HKEY key, const wchar_t* name, const wchar_t* value)
{
    const auto bytes = static_cast<DWORD>((std::wcslen(value) + 1) * sizeof(wchar_t));
    return HRESULT_FROM_WIN32(RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value), bytes));
}

HRESULT CreateKey(HKEY parent, const wchar_t* path, HKEY* key)
{
    return HRESULT_FROM_WIN32(
        RegCreateKeyExW(parent, path, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, key, nullptr));
}

HRESULT RegisterComServer()
{
    wchar_t module_path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(ModuleHandle(), module_path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return E_FAIL;
    }
    wchar_t clsid_path[64] = {};
    if (!ClsidKeyPath(clsid_path)) {
        return E_FAIL;
    }

    HKEY clsid_key = nullptr;
    HRESULT hr = CreateKey(HKEY_CLASSES_ROOT, clsid_path, &clsid_key);
    if (FAILED(hr)) {
        return hr;
    }
    hr = SetString(clsid_key, nullptr, kDescription);

    HKEY server_key = nullptr;
    if (SUCCEEDED(hr)) {
        hr = CreateKey(clsid_key, L"InprocServer32", &server_key);
    }
    if (SUCCEEDED(hr)) {
        hr = SetString(server_key, nullptr, module_path);
    }
    if (SUCCEEDED(hr)) {
        hr = SetString(server_key, L"ThreadingModel", L"Apartment");
    }
    if (server_key != nullptr) {
        RegCloseKey(server_key);
    }
    RegCloseKey(clsid_key);
    return hr;
}

HRESULT UnregisterComServer()
{
    wchar_t clsid_path[64] = {};
    if (!ClsidKeyPath(clsid_path)) {
        return E_FAIL;
    }
    const LSTATUS status = RegDeleteTreeW(HKEY_CLASSES_ROOT, clsid_path);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(status);
}

HRESULT RegisterProfile()
{
    Microsoft::WRL::ComPtr<ITfInputProcessorProfileMgr> profiles;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles));
    if (FAILED(hr)) {
        return hr;
    }
    return profiles->RegisterProfile(kTextServiceClsid, kJapaneseLangId, kJapaneseProfileGuid, kDescription,
                                     static_cast<ULONG>(std::size(kDescription) - 1), nullptr, 0, 0, nullptr, 0,
                                     TRUE, 0);
}

HRESULT UnregisterProfile()
{
    Microsoft::WRL::ComPtr<ITfInputProcessorProfileMgr> profiles;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles));
    if (FAILED(hr)) {
        return hr;
    }
    return profiles->UnregisterProfile(kTextServiceClsid, kJapaneseLangId, kJapaneseProfileGuid, 0);
}

HRESULT RegisterCategories()
{
    Microsoft::WRL::ComPtr<ITfCategoryMgr> categories;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories));
    for (const GUID* category : kCategories) {
        if (FAILED(hr)) {
            break;
        }
        hr = categories->RegisterCategory(kTextServiceClsid, *category, kTextServiceClsid);
    }
    return hr;
}

HRESULT UnregisterCategories()
{
    Microsoft::WRL::ComPtr<ITfCategoryMgr> categories;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories));
    if (FAILED(hr)) {
        return hr;
    }
    for (const GUID* category : kCategories) {
        categories->UnregisterCategory(kTextServiceClsid, *category, kTextServiceClsid);
    }
    return S_OK;
}

} // namespace

HRESULT RegisterTextService()
{
    HRESULT hr = RegisterComServer();
    if (SUCCEEDED(hr)) {
        hr = RegisterProfile();
    }
    if (SUCCEEDED(hr)) {
        hr = RegisterCategories();
    }
    if (FAILED(hr)) {
        UnregisterTextService();
    }
    return hr;
}

HRESULT UnregisterTextService()
{
    UnregisterCategories();
    UnregisterProfile();
    return UnregisterComServer();
}

} // namespace astelio::tip
