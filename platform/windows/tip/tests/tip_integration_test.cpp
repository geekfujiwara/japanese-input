#include "astelio/tip/guids.h"

#include "test_text_store.h"

#include <windows.h>

#include <msctf.h>
#include <wrl/client.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <cwctype>
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
        TfClientId client_id = TF_CLIENTID_NULL;
        ASSERT_HRESULT_SUCCEEDED(thread_mgr_->Activate(&client_id));
        thread_mgr_active_ = true;

        ASSERT_HRESULT_SUCCEEDED(thread_mgr_->CreateDocumentMgr(&document_));
        store_.Attach(new astelio::tip::testing::TestTextStore());
        TfEditCookie cookie = TF_INVALID_EDIT_COOKIE;
        ASSERT_HRESULT_SUCCEEDED(
            document_->CreateContext(client_id, 0, static_cast<ITextStoreACP*>(store_.Get()), &context_, &cookie));
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

        BOOL thread_focus = FALSE;
        ASSERT_HRESULT_SUCCEEDED(thread_mgr_->IsThreadFocus(&thread_focus));
        if (!thread_focus) {
            // Some CI machines (the Windows ARM64 runner) refuse foreground to test windows; TSF then routes no keys.
            GTEST_SKIP() << "The OS did not give this thread keyboard focus (foreground window denied)";
        }
    }

    void TearDown() override
    {
        SetModifierState(false, false);
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
        SetForegroundWindow(window);
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

    static void SetModifierState(bool shift, bool control)
    {
        BYTE state[256] = {};
        state[VK_SHIFT] = state[VK_LSHIFT] = shift ? 0x80 : 0;
        state[VK_CONTROL] = state[VK_LCONTROL] = control ? 0x80 : 0;
        SetKeyboardState(state);
    }

    // Returns whether the IME consumed the key down.
    bool Press(UINT virtual_key, BYTE scan_code, bool shift = false, bool extended = false)
    {
        SetModifierState(shift, false);
        const LPARAM down = 1 | (static_cast<LPARAM>(scan_code) << 16) | (extended ? (1 << 24) : 0);
        BOOL eaten = FALSE;
        EXPECT_HRESULT_SUCCEEDED(keystrokes_->KeyDown(virtual_key, down, &eaten));
        BOOL up_eaten = FALSE;
        keystrokes_->KeyUp(virtual_key, down | (1 << 30) | (1u << 31), &up_eaten);
        SetModifierState(false, false);
        return eaten != FALSE;
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

    ComApartment apartment_;
    std::unique_ptr<RegisteredTip> tip_;
    ComPtr<ITfInputProcessorProfileMgr> profiles_;
    LANGID language_ = 0;
    bool profile_registered_ = false;
    ComPtr<ITfThreadMgr> thread_mgr_;
    bool thread_mgr_active_ = false;
    HWND window_ = nullptr;
    ComPtr<ITfDocumentMgr> document_;
    ComPtr<ITfContext> context_;
    ComPtr<astelio::tip::testing::TestTextStore> store_;
    ComPtr<ITfKeystrokeMgr> keystrokes_;
};

// Reports each step of the key route so routing problems show up clearly in CI logs.
TEST_F(TypingTest, KeyRouteReachesTheTextService)
{
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
    const LPARAM down = 1 | (0x2E << 16);
    BOOL eaten = TRUE;
    EXPECT_HRESULT_SUCCEEDED(keystrokes_->KeyDown('C', down, &eaten));
    SetModifierState(false, false);
    EXPECT_FALSE(eaten);
    EXPECT_EQ(Text(), L"");
}

} // namespace
