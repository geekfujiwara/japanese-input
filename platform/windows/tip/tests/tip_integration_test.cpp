#include "astelio/tip/guids.h"

#include "astelio/dictionary_builder.h"
#include "test_text_store.h"

#include <windows.h>

#include <msctf.h>
#include <oleauto.h>
#include <wrl/client.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <cwctype>
#include <fstream>
#include <functional>
#include <ios>
#include <iostream>
#include <iterator>
#include <memory>
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

// {39C14AD5-B069-4C7D-8F50-19E3E0001609}: test-only profile under the machine's current input language,
// because TSF will not switch to ja-JP where Japanese is not installed (such as CI runners).
constexpr GUID kTestProfileGuid = {0x39c14ad5, 0xb069, 0x4c7d, {0x8f, 0x50, 0x19, 0xe3, 0xe0, 0x00, 0x16, 0x09}};

TEST(TipActivation, TsfActivatesTheTextServiceAsForegroundKeySink)
{
    if (!IntegrationEnabled()) {
        GTEST_SKIP() << "Set ASTELIO_TIP_INTEGRATION=1 to run (needs administrator rights)";
    }
    ComApartment apartment;
    ASSERT_HRESULT_SUCCEEDED(apartment.result());
    RegisteredTip tip;
    ASSERT_HRESULT_SUCCEEDED(tip.result());

    ComPtr<ITfInputProcessorProfileMgr> profiles;
    ASSERT_HRESULT_SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                              IID_PPV_ARGS(&profiles)));
    const LANGID language = LOWORD(reinterpret_cast<UINT_PTR>(GetKeyboardLayout(0)));
    constexpr wchar_t kName[] = L"Astelio IME test";
    ASSERT_HRESULT_SUCCEEDED(profiles->RegisterProfile(astelio::tip::kTextServiceClsid, language, kTestProfileGuid,
                                                       kName, static_cast<ULONG>(std::size(kName) - 1), nullptr, 0,
                                                       0, nullptr, 0, TRUE, 0));

    ComPtr<ITfThreadMgr> thread_mgr;
    ASSERT_HRESULT_SUCCEEDED(
        CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&thread_mgr)));
    TfClientId client_id = TF_CLIENTID_NULL;
    ASSERT_HRESULT_SUCCEEDED(thread_mgr->Activate(&client_id));

    const HRESULT activated = profiles->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, language,
                                                        astelio::tip::kTextServiceClsid, kTestProfileGuid, nullptr,
                                                        TF_IPPMF_FORPROCESS);
    EXPECT_HRESULT_SUCCEEDED(activated) << "language 0x" << std::hex << language;

    ComPtr<ITfKeystrokeMgr> keystrokes;
    ASSERT_HRESULT_SUCCEEDED(thread_mgr.As(&keystrokes));
    CLSID foreground{};
    EXPECT_HRESULT_SUCCEEDED(keystrokes->GetForeground(&foreground));
    EXPECT_TRUE(IsEqualCLSID(foreground, astelio::tip::kTextServiceClsid));

    keystrokes.Reset();
    EXPECT_HRESULT_SUCCEEDED(thread_mgr->Deactivate());
    thread_mgr.Reset();
    EXPECT_HRESULT_SUCCEEDED(
        profiles->UnregisterProfile(astelio::tip::kTextServiceClsid, language, kTestProfileGuid, 0));
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

// Writes a small dictionary (わたし: 私 / 渡し, は) and returns its path.
std::wstring WriteTestDictionary()
{
    constexpr std::uint16_t kIds = 4; // 0 edge, 1 noun, 2 particle, 3 verb
    astelio::ConnectionMatrix matrix;
    matrix.size = kIds;
    matrix.costs.assign(kIds * kIds, 100);
    matrix.word_types = {astelio::WordType::Edge, astelio::WordType::Content, astelio::WordType::Suffix,
                         astelio::WordType::Content};
    matrix.unknown_id = 1;
    matrix.unknown_cost = 5000;
    matrix.costs[0 * kIds + 1] = 0;
    matrix.costs[1 * kIds + 2] = 0;
    matrix.costs[2 * kIds + 0] = 0;
    astelio::DictionaryBuilder builder(std::move(matrix));
    builder.Add({u"わたし", u"私", 1, 1, 0, 300});
    builder.Add({u"わたし", u"渡し", 3, 3, 0, 900});
    builder.Add({u"は", u"は", 2, 2, 0, 50});
    builder.Add({u"にほんご", u"日本語", 1, 1, 0, 400});
    builder.Add({u"えもじ", u"絵文字", 1, 1, 0, 400});
    builder.Add({u"いぬ", u"🐶", 1, 1, 0, 900});
    builder.Add({u"ねこ", u"🐱", 1, 1, 0, 900});
    const std::vector<std::byte> bytes = builder.Build();

    wchar_t directory[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, directory);
    const std::wstring path = std::wstring(directory) + L"astelio_tip_test.dic";
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return file ? path : std::wstring();
}

// Runs `body` inside a read-only edit session of the test document.
class ReadSession final : public ITfEditSession {
public:
    explicit ReadSession(std::function<void(TfEditCookie)> body) : body_(std::move(body)) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** object) override
    {
        if (riid == IID_IUnknown || riid == IID_ITfEditSession) {
            *object = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_count_; }
    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG count = --ref_count_;
        if (count == 0) {
            delete this;
        }
        return count;
    }
    STDMETHODIMP DoEditSession(TfEditCookie cookie) override
    {
        body_(cookie);
        return S_OK;
    }

private:
    ULONG ref_count_ = 1;
    std::function<void(TfEditCookie)> body_;
};

// Types into an in-memory document through real TSF with Astelio active.
class TypingTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        if (!IntegrationEnabled()) {
            GTEST_SKIP() << "Set ASTELIO_TIP_INTEGRATION=1 to run (needs administrator rights)";
        }
        SetModifierState(false, false);
        ASSERT_HRESULT_SUCCEEDED(apartment_.result());
        tip_ = std::make_unique<RegisteredTip>();
        ASSERT_HRESULT_SUCCEEDED(tip_->result());

        ASSERT_HRESULT_SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                                  IID_PPV_ARGS(&profiles_)));
        language_ = LOWORD(reinterpret_cast<UINT_PTR>(GetKeyboardLayout(0)));
        constexpr wchar_t kName[] = L"Astelio IME test";
        ASSERT_HRESULT_SUCCEEDED(profiles_->RegisterProfile(astelio::tip::kTextServiceClsid, language_,
                                                            kTestProfileGuid, kName,
                                                            static_cast<ULONG>(std::size(kName) - 1), nullptr, 0, 0,
                                                            nullptr, 0, TRUE, 0));
        profile_registered_ = true;

        ASSERT_HRESULT_SUCCEEDED(
            CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&thread_mgr_)));
        ASSERT_HRESULT_SUCCEEDED(thread_mgr_->Activate(&client_id_));
        thread_mgr_active_ = true;

        ASSERT_HRESULT_SUCCEEDED(thread_mgr_->CreateDocumentMgr(&document_));
        store_.Attach(new astelio::tip::testing::TestTextStore());
        TfEditCookie cookie = TF_INVALID_EDIT_COOKIE;
        ASSERT_HRESULT_SUCCEEDED(
            document_->CreateContext(client_id_, 0, static_cast<ITextStoreACP*>(store_.Get()), &context_, &cookie));
        ASSERT_HRESULT_SUCCEEDED(document_->Push(context_.Get()));

        // TSF sends keys only to a thread that has keyboard focus, so the document gets a real focused window.
        window_ = CreateFocusedWindow();
        ASSERT_NE(window_, nullptr);
        ComPtr<ITfDocumentMgr> previous;
        ASSERT_HRESULT_SUCCEEDED(thread_mgr_->AssociateFocus(window_, document_.Get(), &previous));
        ASSERT_HRESULT_SUCCEEDED(thread_mgr_->SetFocus(document_.Get()));
        PumpMessages();

        ASSERT_HRESULT_SUCCEEDED(profiles_->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, language_,
                                                            astelio::tip::kTextServiceClsid, kTestProfileGuid,
                                                            nullptr, TF_IPPMF_FORPROCESS));
        ASSERT_HRESULT_SUCCEEDED(thread_mgr_.As(&keystrokes_));
        // With keyboard focus TSF carries the open state over from earlier tests, so start from Japanese.
        SetOpenClose(1);
        ASSERT_EQ(OpenCloseValue(), 1);

        key_ = reinterpret_cast<TestKeyFunction>(
            GetProcAddress(GetModuleHandleW(TipPath().c_str()), "AstelioTipTestKey"));
        ASSERT_NE(key_, nullptr);
        use_dictionary_ = reinterpret_cast<UseDictionaryFunction>(
            GetProcAddress(GetModuleHandleW(TipPath().c_str()), "AstelioTipTestUseDictionary"));
        ASSERT_NE(use_dictionary_, nullptr);
        ASSERT_HRESULT_SUCCEEDED(thread_mgr_->IsThreadFocus(&thread_focus_));
    }

    void TearDown() override
    {
        SetModifierState(false, false);
        if (use_dictionary_ != nullptr && thread_mgr_active_) {
            use_dictionary_(nullptr);
        }
        if (thread_mgr_active_) {
            SetOpenClose(1);
        }
        keystrokes_.Reset();
        if (document_) {
            document_->Pop(TF_POPF_ALL);
        }
        context_.Reset();
        document_.Reset();
        if (thread_mgr_active_) {
            thread_mgr_->Deactivate();
        }
        thread_mgr_.Reset();
        if (window_ != nullptr) {
            DestroyWindow(window_);
            window_ = nullptr;
        }
        if (profile_registered_) {
            profiles_->UnregisterProfile(astelio::tip::kTextServiceClsid, language_, kTestProfileGuid, 0);
        }
        profiles_.Reset();
        store_.Reset();
        tip_.reset();
    }

    static HWND CreateFocusedWindow()
    {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = DefWindowProcW;
        window_class.hInstance = instance;
        window_class.lpszClassName = L"AstelioTipTestWindow";
        RegisterClassW(&window_class);
        HWND window = CreateWindowExW(WS_EX_TOPMOST, window_class.lpszClassName, L"Astelio TIP test",
                                      WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, nullptr, nullptr, instance, nullptr);
        if (window == nullptr) {
            return nullptr;
        }
        ShowWindow(window, SW_SHOW);
        // Windows only lets a process take the foreground right after input, so tap Alt before each attempt.
        for (int attempt = 0; attempt < 20 && GetForegroundWindow() != window; ++attempt) {
            INPUT alt[2] = {};
            alt[0].type = alt[1].type = INPUT_KEYBOARD;
            alt[0].ki.wVk = alt[1].ki.wVk = VK_MENU;
            alt[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, alt, sizeof(INPUT));
            SetForegroundWindow(window);
            PumpMessages();
            Sleep(50);
        }
        SetActiveWindow(window);
        SetFocus(window);
        PumpMessages();
        return window;
    }

    static void PumpMessages()
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    static void SetModifierState(bool shift, bool control, bool alt = false)
    {
        BYTE state[256] = {};
        state[VK_SHIFT] = state[VK_LSHIFT] = shift ? 0x80 : 0;
        state[VK_CONTROL] = state[VK_LCONTROL] = control ? 0x80 : 0;
        state[VK_MENU] = state[VK_LMENU] = alt ? 0x80 : 0;
        SetKeyboardState(state);
    }

    // Sends one key event the way TSF does (test call, then the real call). Returns whether it was eaten.
    bool SendKey(UINT virtual_key, BYTE scan_code, bool key_up, bool extended = false)
    {
        LPARAM lparam = 1 | (static_cast<LPARAM>(scan_code) << 16) | (extended ? (1 << 24) : 0);
        if (key_up) {
            lparam |= (1 << 30) | (1u << 31);
        }
        BOOL eaten = FALSE;
        EXPECT_HRESULT_SUCCEEDED(key_(context_.Get(), virtual_key, lparam, key_up ? TRUE : FALSE, &eaten));
        return eaten != FALSE;
    }

    // Returns whether the IME consumed the key down. Keys go to the TSF-activated text service directly,
    // because CI machines do not reliably grant the keyboard focus TSF needs to route real keystrokes.
    bool Press(UINT virtual_key, BYTE scan_code, bool shift = false, bool extended = false)
    {
        SetModifierState(shift, false);
        const bool eaten = SendKey(virtual_key, scan_code, false, extended);
        SetModifierState(false, false);
        return eaten;
    }

    // Alt as delivered by WM_SYSKEYDOWN: VK_MENU, with the extended bit for the right key.
    bool AltDown(bool right) { return SendKey(VK_MENU, 0x38, false, right); }
    bool AltUp(bool right) { return SendKey(VK_MENU, 0x38, true, right); }
    // Returns whether the release was eaten (so the app never sees a lone Alt and opens no menu).
    bool TapAlt(bool right)
    {
        EXPECT_FALSE(AltDown(right)) << "Alt down must reach the app";
        return AltUp(right);
    }

    void TypeLetters(const char* letters)
    {
        static constexpr BYTE kScanCodes[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
                                                0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
                                                0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
        for (const char* c = letters; *c != '\0'; ++c) {
            const bool upper = *c >= 'A' && *c <= 'Z';
            const int index = upper ? *c - 'A' : *c - 'a';
            Press(static_cast<UINT>('A' + index), kScanCodes[index], upper);
        }
    }

    int CompositionCount()
    {
        ComPtr<ITfContextComposition> compositions;
        if (FAILED(context_.As(&compositions))) {
            return -1;
        }
        ComPtr<IEnumITfCompositionView> views;
        if (FAILED(compositions->EnumCompositions(&views)) || !views) {
            return -1;
        }
        int count = 0;
        ComPtr<ITfCompositionView> view;
        ULONG fetched = 0;
        while (views->Next(1, view.ReleaseAndGetAddressOf(), &fetched) == S_OK && fetched == 1) {
            ++count;
        }
        return count;
    }

    const std::wstring& Text() const { return store_->Text(); }

    // The display attribute GUID of the character at `position`, or GUID_NULL.
    GUID AttributeAt(LONG position)
    {
        GUID result = GUID_NULL;
        ComPtr<ITfCategoryMgr> categories;
        EXPECT_HRESULT_SUCCEEDED(
            CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories)));
        auto* session = new ReadSession([&](TfEditCookie cookie) {
            ComPtr<ITfProperty> property;
            ComPtr<ITfRange> range;
            LONG shifted = 0;
            if (FAILED(context_->GetProperty(GUID_PROP_ATTRIBUTE, &property)) ||
                FAILED(context_->GetStart(cookie, &range)) ||
                FAILED(range->ShiftEnd(cookie, position + 1, &shifted, nullptr)) ||
                FAILED(range->ShiftStart(cookie, position, &shifted, nullptr))) {
                return;
            }
            VARIANT value;
            VariantInit(&value);
            if (SUCCEEDED(property->GetValue(cookie, range.Get(), &value)) && value.vt == VT_I4 && categories) {
                categories->GetGUID(static_cast<TfGuidAtom>(value.lVal), &result);
            }
            VariantClear(&value);
        });
        HRESULT session_result = S_OK;
        EXPECT_HRESULT_SUCCEEDED(
            context_->RequestEditSession(client_id_, session, TF_ES_SYNC | TF_ES_READ, &session_result));
        session->Release();
        return result;
    }

    ComPtr<ITfCompartment> OpenClose()
    {
        ComPtr<ITfCompartmentMgr> compartments;
        ComPtr<ITfCompartment> compartment;
        EXPECT_HRESULT_SUCCEEDED(thread_mgr_.As(&compartments));
        if (compartments) {
            EXPECT_HRESULT_SUCCEEDED(compartments->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &compartment));
        }
        return compartment;
    }

    // -1 when the value is missing.
    long OpenCloseValue()
    {
        const ComPtr<ITfCompartment> compartment = OpenClose();
        VARIANT value;
        VariantInit(&value);
        if (!compartment || FAILED(compartment->GetValue(&value)) || value.vt != VT_I4) {
            VariantClear(&value);
            return -1;
        }
        return value.lVal;
    }

    void SetOpenClose(long open)
    {
        const ComPtr<ITfCompartment> compartment = OpenClose();
        ASSERT_TRUE(compartment);
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_I4;
        value.lVal = open;
        ASSERT_HRESULT_SUCCEEDED(compartment->SetValue(client_id_, &value));
    }

    ComPtr<ITfLangBarItemButton> ModeButton()
    {
        ComPtr<ITfLangBarItemMgr> items;
        ComPtr<ITfLangBarItem> item;
        ComPtr<ITfLangBarItemButton> button;
        EXPECT_HRESULT_SUCCEEDED(thread_mgr_.As(&items));
        if (items && SUCCEEDED(items->GetItem(astelio::tip::kLangBarInputModeGuid, &item)) && item) {
            item.As(&button);
        }
        return button;
    }

    std::wstring ModeButtonText()
    {
        const ComPtr<ITfLangBarItemButton> button = ModeButton();
        BSTR text = nullptr;
        if (!button || FAILED(button->GetText(&text)) || text == nullptr) {
            return L"(none)";
        }
        std::wstring result(text, SysStringLen(text));
        SysFreeString(text);
        return result;
    }

    ComApartment apartment_;
    std::unique_ptr<RegisteredTip> tip_;
    ComPtr<ITfInputProcessorProfileMgr> profiles_;
    LANGID language_ = 0;
    bool profile_registered_ = false;
    ComPtr<ITfThreadMgr> thread_mgr_;
    TfClientId client_id_ = TF_CLIENTID_NULL;
    bool thread_mgr_active_ = false;
    HWND window_ = nullptr;
    ComPtr<ITfDocumentMgr> document_;
    ComPtr<ITfContext> context_;
    ComPtr<astelio::tip::testing::TestTextStore> store_;
    ComPtr<ITfKeystrokeMgr> keystrokes_;
    using TestKeyFunction = HRESULT(WINAPI*)(ITfContext*, WPARAM, LPARAM, BOOL, BOOL*);
    TestKeyFunction key_ = nullptr;
    using UseDictionaryFunction = HRESULT(WINAPI*)(const wchar_t*);
    UseDictionaryFunction use_dictionary_ = nullptr;
    BOOL thread_focus_ = FALSE;
};

// Full OS -> TSF -> TIP key route; needs keyboard focus, which CI machines grant only intermittently.
TEST_F(TypingTest, KeyRouteReachesTheTextService)
{
    if (!thread_focus_) {
        GTEST_SKIP() << "The OS did not give this thread keyboard focus (foreground window denied)";
    }
    CLSID foreground{};
    EXPECT_HRESULT_SUCCEEDED(keystrokes_->GetForeground(&foreground));
    EXPECT_TRUE(IsEqualCLSID(foreground, astelio::tip::kTextServiceClsid)) << "Astelio is not the foreground TIP";

    ComPtr<ITfDocumentMgr> focused;
    EXPECT_HRESULT_SUCCEEDED(thread_mgr_->GetFocus(&focused));
    EXPECT_EQ(focused.Get(), document_.Get()) << "test document is not focused";

    SetModifierState(false, false);
    const LPARAM down = 1 | (0x1E << 16);
    BOOL test_eaten = FALSE;
    const HRESULT test_hr = keystrokes_->TestKeyDown('A', down, &test_eaten);
    EXPECT_HRESULT_SUCCEEDED(test_hr);
    EXPECT_TRUE(test_eaten) << "TestKeyDown('A') was not eaten";

    BOOL eaten = FALSE;
    const HRESULT key_hr = keystrokes_->KeyDown('A', down, &eaten);
    EXPECT_HRESULT_SUCCEEDED(key_hr) << "KeyDown hr=0x" << std::hex << static_cast<unsigned long>(key_hr);
    EXPECT_TRUE(eaten) << "KeyDown('A') was not eaten";
    EXPECT_EQ(Text(), L"\u3042");

    using DiagnosticsFunction = void(WINAPI*)(long*, int);
    const auto diagnostics = reinterpret_cast<DiagnosticsFunction>(
        GetProcAddress(GetModuleHandleW(TipPath().c_str()), "AstelioTipKeyDiagnostics"));
    ASSERT_NE(diagnostics, nullptr);
    long counters[4] = {};
    diagnostics(counters, 4);
    std::cout << "[diagnostics] test_key_down=" << counters[0] << " key_down=" << counters[1]
              << " null_context=" << counters[2] << " eaten=" << counters[3] << std::endl;
}

// T-R01-1, T-B01-1: romaji becomes uncommitted hiragana; Enter commits (T-B06-1).
TEST_F(TypingTest, RomajiIsComposedAndEnterCommits)
{
    TypeLetters("ka");
    EXPECT_EQ(Text(), L"\u304B");
    EXPECT_EQ(CompositionCount(), 1);
    EXPECT_EQ(store_->SelectionEnd(), 1);

    EXPECT_TRUE(Press(VK_RETURN, 0x1C));
    EXPECT_EQ(Text(), L"\u304B");
    EXPECT_EQ(CompositionCount(), 0);
}

// T-B06-1: Esc cancels the uncommitted text.
TEST_F(TypingTest, EscapeCancelsTheComposition)
{
    TypeLetters("kya");
    EXPECT_EQ(Text(), L"\u304D\u3083");
    EXPECT_TRUE(Press(VK_ESCAPE, 0x01));
    EXPECT_EQ(Text(), L"");
    EXPECT_EQ(CompositionCount(), 0);
}

// T-R01-2, REG-03, REG-06: Shift+letter gives half-width letters that stay uncommitted.
TEST_F(TypingTest, ShiftLetterStaysUncommittedHalfWidth)
{
    TypeLetters("GitHub");
    EXPECT_EQ(Text(), L"GitHub");
    EXPECT_EQ(CompositionCount(), 1);
}

// T-R02-1, REG-04: Shift symbols are typed.
TEST_F(TypingTest, ShiftSymbolsAreTyped)
{
    Press(VK_OEM_PLUS, 0x0D, true);
    Press('9', 0x0A, true);
    Press('0', 0x0B, true);
    EXPECT_EQ(Text(), L"+()");
}

TEST_F(TypingTest, BackspaceEditsAndEditingKeysPassThroughWhenEmpty)
{
    EXPECT_FALSE(Press(VK_BACK, 0x0E));
    TypeLetters("aiu");
    EXPECT_TRUE(Press(VK_BACK, 0x0E));
    EXPECT_EQ(Text(), L"\u3042\u3044");
    EXPECT_EQ(CompositionCount(), 1);
}

// C-03: Space outside a composition inserts a half-width space.
TEST_F(TypingTest, SpaceOutsideACompositionInsertsASpace)
{
    EXPECT_TRUE(Press(VK_SPACE, 0x39));
    EXPECT_EQ(Text(), L" ");
    EXPECT_EQ(CompositionCount(), 0);
}

TEST_F(TypingTest, ControlShortcutsAreNotConsumed)
{
    SetModifierState(false, true);
    const bool eaten = SendKey('C', 0x2E, false);
    SetModifierState(false, false);
    EXPECT_FALSE(eaten);
    EXPECT_EQ(Text(), L"");
}

// T-R03-3, REG-01: a left Alt tap in Japanese commits the uncommitted text and switches to English;
// the release is eaten so the app never sees a lone Alt (no menu).
TEST_F(TypingTest, LeftAltTapCommitsAndSwitchesToEnglish)
{
    TypeLetters("ka");
    EXPECT_TRUE(TapAlt(false));
    EXPECT_EQ(Text(), L"\u304B");
    EXPECT_EQ(CompositionCount(), 0);
    EXPECT_FALSE(Press('K', 0x25)) << "English mode must pass letters to the app";
    EXPECT_EQ(Text(), L"\u304B");
}

// T-R03-1: a left Alt tap in English stays English.
TEST_F(TypingTest, LeftAltTapInEnglishStaysEnglish)
{
    EXPECT_TRUE(TapAlt(false));
    EXPECT_TRUE(TapAlt(false));
    EXPECT_FALSE(Press('A', 0x1E));
    EXPECT_EQ(Text(), L"");
}

// T-R03-2: a right Alt tap in English switches to Japanese.
TEST_F(TypingTest, RightAltTapSwitchesToJapanese)
{
    EXPECT_TRUE(TapAlt(false));
    EXPECT_TRUE(TapAlt(true));
    TypeLetters("a");
    EXPECT_EQ(Text(), L"\u3042");
    EXPECT_EQ(CompositionCount(), 1);
}

// REG-05: after a right Alt tap every letter stays uncommitted Japanese.
TEST_F(TypingTest, RightAltTapThenAllLettersStayJapanese)
{
    EXPECT_TRUE(TapAlt(false));
    EXPECT_TRUE(TapAlt(true));
    TypeLetters("abcdefghijklmnopqrstuvwxyz");
    EXPECT_EQ(CompositionCount(), 1);
    EXPECT_FALSE(Text().empty());
    EXPECT_EQ(Text().find_first_of(L"aeiou"), std::wstring::npos) << "vowels must be converted to kana";
}

// Supplement to T-R03: a tap for the current mode keeps the mode and the uncommitted text.
TEST_F(TypingTest, RightAltTapInJapaneseKeepsJapanese)
{
    TypeLetters("ka");
    EXPECT_TRUE(TapAlt(true));
    EXPECT_EQ(CompositionCount(), 1) << "switching to the current mode must not commit";
    TypeLetters("a");
    EXPECT_EQ(Text(), L"\u304B\u3042");
}

// T-R04-1, REG-05: Alt+key shortcuts reach the app and do not switch the mode.
TEST_F(TypingTest, AltShortcutIsNotATap)
{
    SetModifierState(false, false, true);
    EXPECT_FALSE(AltDown(false));
    EXPECT_FALSE(SendKey('F', 0x21, false)) << "Alt+F must reach the app";
    EXPECT_FALSE(SendKey('F', 0x21, true));
    EXPECT_FALSE(AltUp(false)) << "Alt release after a shortcut must reach the app";
    SetModifierState(false, false);
    TypeLetters("a");
    EXPECT_EQ(Text(), L"\u3042") << "still Japanese";
}

// T-R04-4: a lone Alt after an Alt shortcut is a tap again.
TEST_F(TypingTest, AltTapAfterShortcutIsATap)
{
    SetModifierState(false, false, true);
    EXPECT_FALSE(AltDown(false));
    EXPECT_FALSE(SendKey('F', 0x21, false));
    EXPECT_FALSE(AltUp(false));
    SetModifierState(false, false);
    EXPECT_TRUE(TapAlt(false));
    EXPECT_FALSE(Press('A', 0x1E)) << "now English";
}

// T-R04-2: holding Alt longer than the limit (1 s) is not a tap.
TEST_F(TypingTest, LongAltHoldIsNotATap)
{
    EXPECT_FALSE(AltDown(false));
    Sleep(1100);
    EXPECT_FALSE(AltUp(false));
    TypeLetters("a");
    EXPECT_EQ(Text(), L"\u3042");
}

// T-R04-3: pressing the other Alt while one is held is not a tap for either.
TEST_F(TypingTest, BothAltKeysTogetherAreNotATap)
{
    EXPECT_FALSE(AltDown(false));
    EXPECT_FALSE(AltDown(true));
    EXPECT_FALSE(AltUp(true));
    EXPECT_FALSE(AltUp(false));
    TypeLetters("a");
    EXPECT_EQ(Text(), L"\u3042");
}

// B-12: the taskbar mode indicator and the keyboard open state follow the mode.
TEST_F(TypingTest, ModeIndicatorFollowsTheMode)
{
    ASSERT_TRUE(ModeButton()) << "input mode button (GUID_LBI_INPUTMODE) is not in the language bar";
    TF_LANGBARITEMINFO info{};
    ASSERT_HRESULT_SUCCEEDED(ModeButton()->GetInfo(&info));
    EXPECT_TRUE(IsEqualCLSID(info.clsidService, astelio::tip::kTextServiceClsid));
    EXPECT_EQ(ModeButtonText(), L"\u3042");
    EXPECT_EQ(OpenCloseValue(), 1);

    HICON icon = nullptr;
    EXPECT_HRESULT_SUCCEEDED(ModeButton()->GetIcon(&icon));
    EXPECT_NE(icon, nullptr);
    if (icon != nullptr) {
        DestroyIcon(icon);
    }

    EXPECT_TRUE(TapAlt(false));
    EXPECT_EQ(ModeButtonText(), L"A");
    EXPECT_EQ(OpenCloseValue(), 0);

    EXPECT_TRUE(TapAlt(true));
    EXPECT_EQ(ModeButtonText(), L"\u3042");
    EXPECT_EQ(OpenCloseValue(), 1);
}

// B-12: clicking the indicator toggles the mode and commits the uncommitted text.
TEST_F(TypingTest, ModeButtonClickTogglesTheMode)
{
    TypeLetters("ka");
    const ComPtr<ITfLangBarItemButton> button = ModeButton();
    ASSERT_TRUE(button);
    const RECT area{};
    ASSERT_HRESULT_SUCCEEDED(button->OnClick(TF_LBI_CLK_LEFT, POINT{}, &area));
    EXPECT_EQ(Text(), L"\u304B");
    EXPECT_EQ(CompositionCount(), 0);
    EXPECT_EQ(ModeButtonText(), L"A");
    EXPECT_FALSE(Press('A', 0x1E));

    ASSERT_HRESULT_SUCCEEDED(button->OnClick(TF_LBI_CLK_LEFT, POINT{}, &area));
    TypeLetters("a");
    EXPECT_EQ(Text(), L"\u304B\u3042");
}

// Closing the keyboard from outside (taskbar, app, IME on/off key) commits and switches to English.
TEST_F(TypingTest, KeyboardCloseFromOutsideSwitchesToEnglish)
{
    TypeLetters("ka");
    SetOpenClose(0);
    EXPECT_EQ(Text(), L"\u304B");
    EXPECT_EQ(CompositionCount(), 0);
    EXPECT_EQ(ModeButtonText(), L"A");
    EXPECT_FALSE(Press('A', 0x1E));

    SetOpenClose(1);
    EXPECT_TRUE(Press('A', 0x1E));
    EXPECT_EQ(Text(), L"\u304B\u3042");
}

// T-B02-1, T-B06-1 (TIP): Space converts in the document, Space picks the next candidate,
// Esc returns to the kana, Enter commits.
TEST_F(TypingTest, SpaceConvertsWithTheDictionary)
{
    const std::wstring path = WriteTestDictionary();
    ASSERT_FALSE(path.empty());
    ASSERT_HRESULT_SUCCEEDED(use_dictionary_(path.c_str()));

    TypeLetters("watasiha");
    EXPECT_TRUE(Press(VK_SPACE, 0x39));
    EXPECT_EQ(Text(), L"\u79C1\u306F");
    EXPECT_EQ(CompositionCount(), 1) << "the conversion stays uncommitted";
    EXPECT_TRUE(Press(VK_SPACE, 0x39));
    EXPECT_EQ(Text(), L"\u6E21\u3057\u306F");

    // T-B03-1: the second Space opens the candidate window.
    using CandidateWindowFunction = HWND(WINAPI*)();
    const auto candidate_window = reinterpret_cast<CandidateWindowFunction>(
        GetProcAddress(GetModuleHandleW(TipPath().c_str()), "AstelioTipTestCandidateWindow"));
    ASSERT_NE(candidate_window, nullptr);
    const HWND window = candidate_window();
    ASSERT_NE(window, nullptr);
    EXPECT_TRUE(IsWindowVisible(window));
    RECT bounds{};
    GetWindowRect(window, &bounds);
    EXPECT_GT(bounds.right - bounds.left, 0);
    EXPECT_GT(bounds.bottom - bounds.top, 0);
    UpdateWindow(window); // paints with Direct2D
    EXPECT_TRUE(Press(VK_DOWN, 0x50, false, true));
    EXPECT_TRUE(IsWindowVisible(window));

    EXPECT_TRUE(Press(VK_ESCAPE, 0x01));
    EXPECT_TRUE(IsWindowVisible(window)) << "back in the kana, the window shows predictions again";
    EXPECT_EQ(Text(), L"\u308F\u305F\u3057\u306F");
    EXPECT_EQ(CompositionCount(), 1);

    EXPECT_TRUE(Press(VK_SPACE, 0x39));
    EXPECT_TRUE(Press(VK_RETURN, 0x1C));
    EXPECT_EQ(Text(), L"\u79C1\u306F");
    EXPECT_EQ(CompositionCount(), 0);
    EXPECT_EQ(store_->SelectionEnd(), 2);
}

// T-B04-1 (TIP): predictions are shown while typing; Tab selects them.
TEST_F(TypingTest, PredictionsShowWhileTypingAndTabSelects)
{
    const std::wstring path = WriteTestDictionary();
    ASSERT_FALSE(path.empty());
    ASSERT_HRESULT_SUCCEEDED(use_dictionary_(path.c_str()));
    using CandidateWindowFunction = HWND(WINAPI*)();
    const auto candidate_window = reinterpret_cast<CandidateWindowFunction>(
        GetProcAddress(GetModuleHandleW(TipPath().c_str()), "AstelioTipTestCandidateWindow"));
    ASSERT_NE(candidate_window, nullptr);

    TypeLetters("wata");
    EXPECT_EQ(Text(), L"\u308F\u305F");
    const HWND window = candidate_window();
    ASSERT_NE(window, nullptr);
    EXPECT_TRUE(IsWindowVisible(window)) << "predictions are shown while typing";
    UpdateWindow(window);

    EXPECT_TRUE(Press(VK_TAB, 0x0F));
    EXPECT_EQ(Text(), L"\u79C1");
    EXPECT_TRUE(IsWindowVisible(window));
    EXPECT_TRUE(Press(VK_RETURN, 0x1C));
    EXPECT_EQ(Text(), L"\u79C1");
    EXPECT_EQ(CompositionCount(), 0);
    EXPECT_FALSE(IsWindowVisible(window));
}

// B-02: typed kana are dotted; converted segments are underlined and the focused one is bold.
TEST_F(TypingTest, DisplayAttributesMarkTheFocusedSegment)
{
    const std::wstring path = WriteTestDictionary();
    ASSERT_FALSE(path.empty());
    ASSERT_HRESULT_SUCCEEDED(use_dictionary_(path.c_str()));

    TypeLetters("wata");
    EXPECT_TRUE(IsEqualGUID(AttributeAt(0), astelio::tip::kInputAttributeGuid));
    TypeLetters("sihanihongo");
    EXPECT_TRUE(Press(VK_SPACE, 0x39));
    ASSERT_EQ(Text(), L"\u79C1\u306F\u65E5\u672C\u8A9E");
    EXPECT_TRUE(IsEqualGUID(AttributeAt(0), astelio::tip::kFocusedAttributeGuid));
    EXPECT_TRUE(IsEqualGUID(AttributeAt(2), astelio::tip::kConvertedAttributeGuid));

    EXPECT_TRUE(Press(VK_RIGHT, 0x4D, false, true));
    EXPECT_TRUE(IsEqualGUID(AttributeAt(0), astelio::tip::kConvertedAttributeGuid));
    EXPECT_TRUE(IsEqualGUID(AttributeAt(2), astelio::tip::kFocusedAttributeGuid));

    EXPECT_TRUE(Press(VK_RETURN, 0x1C));
    EXPECT_TRUE(IsEqualGUID(AttributeAt(0), GUID_NULL)) << "committed text has no attribute";
}

// T-B13-5 (TIP): えもじ shows the emoji palette, Space still converts, and Tab + search + Enter insert an emoji
// and save it in the history.
TEST_F(TypingTest, EmojiPaletteInsertsAnEmoji)
{
    const std::wstring path = WriteTestDictionary();
    ASSERT_FALSE(path.empty());
    ASSERT_HRESULT_SUCCEEDED(use_dictionary_(path.c_str()));
    const HMODULE tip = GetModuleHandleW(TipPath().c_str());
    using WindowFunction = HWND(WINAPI*)();
    using UseHistoryFunction = void(WINAPI*)(const wchar_t*);
    const auto emoji_window = reinterpret_cast<WindowFunction>(GetProcAddress(tip, "AstelioTipTestEmojiWindow"));
    const auto candidate_window = reinterpret_cast<WindowFunction>(GetProcAddress(tip, "AstelioTipTestCandidateWindow"));
    const auto use_history = reinterpret_cast<UseHistoryFunction>(GetProcAddress(tip, "AstelioTipTestUseEmojiHistory"));
    ASSERT_NE(emoji_window, nullptr);
    ASSERT_NE(candidate_window, nullptr);
    ASSERT_NE(use_history, nullptr);
    wchar_t directory[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, directory);
    const std::wstring history = std::wstring(directory) + L"astelio_tip_test_emoji.txt";
    DeleteFileW(history.c_str());
    use_history(history.c_str());

    TypeLetters("emoji");
    EXPECT_EQ(Text(), L"\u3048\u3082\u3058");
    const HWND window = emoji_window();
    ASSERT_NE(window, nullptr);
    EXPECT_TRUE(IsWindowVisible(window)) << "the palette is offered";
    if (const HWND candidates = candidate_window()) {
        EXPECT_FALSE(IsWindowVisible(candidates)) << "one popup at a time";
    }
    UpdateWindow(window);

    EXPECT_TRUE(Press(VK_SPACE, 0x39));
    EXPECT_EQ(Text(), L"\u7D75\u6587\u5B57") << "Space still converts";
    EXPECT_FALSE(IsWindowVisible(window));
    EXPECT_TRUE(Press(VK_ESCAPE, 0x01));
    EXPECT_EQ(Text(), L"\u3048\u3082\u3058");
    EXPECT_TRUE(IsWindowVisible(window));

    EXPECT_TRUE(Press(VK_TAB, 0x0F));
    TypeLetters("inu");
    EXPECT_EQ(Text(), L"\u3048\u3082\u3058") << "the search stays in the palette";
    UpdateWindow(window);
    EXPECT_TRUE(Press(VK_RETURN, 0x1C));
    EXPECT_EQ(Text(), L"\U0001F436");
    EXPECT_EQ(CompositionCount(), 0);
    EXPECT_FALSE(IsWindowVisible(window));

    std::ifstream saved(history, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "\xF0\x9F\x90\xB6\n") << "the history keeps the emoji";
    saved.close();
    use_history(nullptr);
    DeleteFileW(history.c_str());
}

// Without an installed dictionary typing still works and Space keeps the kana.
TEST_F(TypingTest, SpaceWithoutADictionaryKeepsTheKana)
{
    ASSERT_HRESULT_SUCCEEDED(use_dictionary_(nullptr));
    TypeLetters("ka");
    EXPECT_TRUE(Press(VK_SPACE, 0x39));
    EXPECT_EQ(Text(), L"\u304B");
    EXPECT_EQ(CompositionCount(), 1);
}

} // namespace
