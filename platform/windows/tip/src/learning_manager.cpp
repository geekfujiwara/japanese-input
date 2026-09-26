#include "learning_manager.h"

#include "learning_store.h"
#include "module.h"

#include <windows.h>

#include <commctrl.h>

#include <chrono>
#include <ctime>
#include <cwchar>
#include <iterator>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace astelio::tip {
namespace {

constexpr wchar_t kClassName[] = L"AstelioLearningManager";
constexpr int kListId = 100;
constexpr int kDeleteId = 101;
constexpr int kClearId = 102;
constexpr int kCloseId = 103;
constexpr int kPeriodId = 104;
constexpr int kDeletePeriodId = 105;

// T-D05-2: 過去1時間 / 過去24時間 / 過去7日間 / 過去4週間
constexpr struct {
    const wchar_t* name;
    std::int64_t seconds;
} kPeriods[] = {
    {L"\u904E\u53BB1\u6642\u9593", 3600},
    {L"\u904E\u53BB24\u6642\u9593", 24 * 3600},
    {L"\u904E\u53BB7\u65E5\u9593", 7 * 24 * 3600},
    {L"\u904E\u53BB4\u9031\u9593", 28 * 24 * 3600},
};

struct State {
    LearningHistory history; // what the list shows, in the same order
    HWND note = nullptr;
    HWND list = nullptr;
    HWND delete_button = nullptr;
    HWND period = nullptr;
    HWND delete_period_button = nullptr;
    HWND clear_button = nullptr;
    HWND close_button = nullptr;
    HFONT font = nullptr;
};

HWND g_window = nullptr;

const wchar_t* Wide(const std::u16string& text)
{
    return reinterpret_cast<const wchar_t*>(text.c_str());
}

int Scale(HWND window, int value)
{
    return MulDiv(value, static_cast<int>(GetDpiForWindow(window)), 96);
}

const wchar_t* KindName(LearningHistory::Kind kind)
{
    switch (kind) {
    case LearningHistory::Kind::Prediction: return L"\u4E88\u6E2C";                          // 予測
    case LearningHistory::Kind::Pair: return L"\u7D44\u307F\u5408\u308F\u305B";              // 組み合わせ
    case LearningHistory::Kind::Segmentation: return L"\u6587\u7BC0\u306E\u533A\u5207\u308A"; // 文節の区切り
    case LearningHistory::Kind::Conversion: break;
    }
    return L"\u5909\u63DB"; // 変換
}

// Local date and time of the last use, or a dash for entries saved before times were kept.
std::wstring TimeText(std::int64_t time)
{
    if (time <= 0) {
        return L"\u2014";
    }
    const std::time_t seconds = static_cast<std::time_t>(time);
    std::tm local{};
    if (localtime_s(&local, &seconds) != 0) {
        return L"\u2014";
    }
    wchar_t text[32];
    wcsftime(text, std::size(text), L"%Y/%m/%d %H:%M", &local);
    return text;
}

std::int64_t Now()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void Fill(State& state)
{
    SendMessageW(state.list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(state.list);
    const std::vector<LearningHistory::Entry>& entries = state.history.Entries();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const LearningHistory::Entry& entry = entries[i];
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<wchar_t*>(Wide(entry.surface));
        item.lParam = static_cast<LPARAM>(i);
        const int row = ListView_InsertItem(state.list, &item);
        ListView_SetItemText(state.list, row, 1, const_cast<wchar_t*>(Wide(entry.reading)));
        ListView_SetItemText(state.list, row, 2, const_cast<wchar_t*>(Wide(entry.context)));
        ListView_SetItemText(state.list, row, 3, const_cast<wchar_t*>(KindName(entry.kind)));
        std::wstring count = std::to_wstring(entry.count);
        ListView_SetItemText(state.list, row, 4, count.data());
        std::wstring used = TimeText(entry.time);
        ListView_SetItemText(state.list, row, 5, used.data());
    }
    SendMessageW(state.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(state.list, nullptr, TRUE);
    EnableWindow(state.clear_button, !entries.empty());
    EnableWindow(state.delete_period_button, !entries.empty());
    EnableWindow(state.delete_button, FALSE);
}

// Deletes from the file as it is now (other apps may have added words since the list was filled).
void DeleteSelected(State& state)
{
    LearningHistory latest = LoadLearning();
    bool removed = false;
    for (int row = ListView_GetNextItem(state.list, -1, LVNI_SELECTED); row >= 0;
         row = ListView_GetNextItem(state.list, row, LVNI_SELECTED)) {
        LVITEMW item{};
        item.mask = LVIF_PARAM;
        item.iItem = row;
        if (!ListView_GetItem(state.list, &item)) {
            continue;
        }
        const auto index = static_cast<std::size_t>(item.lParam);
        if (index < state.history.Entries().size()) {
            const LearningHistory::Entry& entry = state.history.Entries()[index];
            removed = latest.Remove(entry.kind, entry.reading, entry.surface, entry.context) || removed;
        }
    }
    if (removed) {
        SaveLearning(latest);
    }
    state.history = std::move(latest);
    Fill(state);
}

void DeletePeriod(State& state)
{
    const LRESULT chosen = SendMessageW(state.period, CB_GETCURSEL, 0, 0);
    if (chosen < 0 || static_cast<std::size_t>(chosen) >= std::size(kPeriods)) {
        return;
    }
    LearningHistory latest = LoadLearning();
    if (latest.RemoveSince(Now() - kPeriods[chosen].seconds) > 0) {
        SaveLearning(latest);
    }
    state.history = std::move(latest);
    Fill(state);
}

void ClearAll(HWND window, State& state)
{
    if (MessageBoxW(window, L"\u5165\u529B\u5C65\u6B74\u3092\u3059\u3079\u3066\u524A\u9664\u3057\u307E\u3059\u304B\uFF1F",
                    L"Astelio IME", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
        return;
    }
    state.history = LearningHistory();
    SaveLearning(state.history);
    Fill(state);
}

void Layout(HWND window, State& state)
{
    RECT client{};
    GetClientRect(window, &client);
    const int margin = Scale(window, 12);
    const int button_height = Scale(window, 28);
    const int button_width = Scale(window, 140);
    const int note_height = Scale(window, 36);
    const int width = client.right - margin * 2;
    MoveWindow(state.note, margin, margin, width, note_height, TRUE);
    const int list_top = margin + note_height + Scale(window, 4);
    const int buttons_top = client.bottom - margin - button_height;
    MoveWindow(state.list, margin, list_top, width, buttons_top - margin - list_top, TRUE);
    int left = margin;
    MoveWindow(state.delete_button, left, buttons_top, button_width, button_height, TRUE);
    left += button_width + margin;
    MoveWindow(state.period, left, buttons_top + Scale(window, 2), Scale(window, 110), Scale(window, 200), TRUE);
    left += Scale(window, 110) + Scale(window, 4);
    MoveWindow(state.delete_period_button, left, buttons_top, Scale(window, 90), button_height, TRUE);
    left += Scale(window, 90) + margin;
    MoveWindow(state.clear_button, left, buttons_top, Scale(window, 90), button_height, TRUE);
    MoveWindow(state.close_button, client.right - margin - Scale(window, 80), buttons_top, Scale(window, 80),
               button_height, TRUE);
}

HWND Child(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style, int id)
{
    return CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), ModuleHandle(), nullptr);
}

bool Create(HWND window, State& state)
{
    const int point_size = 9;
    state.font = CreateFontW(-MulDiv(point_size, static_cast<int>(GetDpiForWindow(window)), 72), 0, 0, 0,
                             FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Yu Gothic UI");
    // 選んだ候補の履歴です。次の変換で先に表示されます。削除すると元の順番に戻ります。
    state.note = Child(window, L"STATIC",
                       L"\u9078\u3093\u3060\u5019\u88DC\u306E\u5C65\u6B74\u3067\u3059\u3002\u6B21\u306E\u5909\u63DB"
                       L"\u3067\u5148\u306B\u8868\u793A\u3055\u308C\u307E\u3059\u3002\u524A\u9664\u3059\u308B\u3068"
                       L"\u5143\u306E\u9806\u756A\u306B\u623B\u308A\u307E\u3059\u3002",
                       SS_LEFT, 0);
    state.list = Child(window, WC_LISTVIEWW, L"", WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS, kListId);
    state.delete_button = Child(window, L"BUTTON", L"\u9078\u3093\u3060\u9805\u76EE\u3092\u524A\u9664",
                                WS_TABSTOP | BS_PUSHBUTTON, kDeleteId);                     // 選んだ項目を削除
    state.period = Child(window, WC_COMBOBOXW, L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, kPeriodId);
    state.delete_period_button = Child(window, L"BUTTON", L"\u671F\u9593\u3092\u524A\u9664",
                                       WS_TABSTOP | BS_PUSHBUTTON, kDeletePeriodId);        // 期間を削除
    state.clear_button = Child(window, L"BUTTON", L"\u3059\u3079\u3066\u524A\u9664", WS_TABSTOP | BS_PUSHBUTTON,
                               kClearId);                                                  // すべて削除
    state.close_button = Child(window, L"BUTTON", L"\u9589\u3058\u308B", WS_TABSTOP | BS_PUSHBUTTON, kCloseId); // 閉じる
    if (state.note == nullptr || state.list == nullptr || state.delete_button == nullptr || state.period == nullptr ||
        state.delete_period_button == nullptr || state.clear_button == nullptr || state.close_button == nullptr) {
        return false;
    }
    for (HWND child : {state.note, state.list, state.delete_button, state.period, state.delete_period_button,
                       state.clear_button, state.close_button}) {
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE);
    }
    for (const auto& period : kPeriods) {
        SendMessageW(state.period, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(period.name));
    }
    SendMessageW(state.period, CB_SETCURSEL, 0, 0);
    ListView_SetExtendedListViewStyle(state.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    const struct {
        const wchar_t* title;
        int width;
    } columns[] = {
        {L"\u5019\u88DC", 150},             // 候補
        {L"\u8AAD\u307F", 150},             // 読み
        {L"\u524D\u306E\u8A9E", 90},        // 前の語
        {L"\u7A2E\u985E", 90},              // 種類
        {L"\u56DE\u6570", 50},              // 回数
        {L"\u6700\u5F8C\u306B\u4F7F\u3063\u305F\u65E5\u6642", 130}, // 最後に使った日時
    };
    for (int i = 0; i < static_cast<int>(std::size(columns)); ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = const_cast<wchar_t*>(columns[i].title);
        column.cx = Scale(window, columns[i].width);
        column.iSubItem = i;
        ListView_InsertColumn(state.list, i, &column);
    }
    state.history = LoadLearning();
    Fill(state);
    Layout(window, state);
    return true;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    try {
        switch (message) {
        case WM_NCCREATE: {
            auto* created = new (std::nothrow) State();
            if (created == nullptr) {
                return FALSE;
            }
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
            AddModuleRef(); // the DLL must stay loaded while the window exists
            break;
        }
        case WM_CREATE:
            return state != nullptr && Create(window, *state) ? 0 : -1;
        case WM_SIZE:
            if (state != nullptr && state->list != nullptr) {
                Layout(window, *state);
            }
            return 0;
        case WM_COMMAND:
            if (state == nullptr) {
                break;
            }
            switch (LOWORD(wparam)) {
            case kDeleteId: DeleteSelected(*state); return 0;
            case kDeletePeriodId: DeletePeriod(*state); return 0;
            case kClearId: ClearAll(window, *state); return 0;
            case kCloseId:
            case IDCANCEL: DestroyWindow(window); return 0;
            default: break;
            }
            break;
        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<const NMHDR*>(lparam);
            if (state == nullptr || header->idFrom != kListId) {
                break;
            }
            if (header->code == LVN_ITEMCHANGED) {
                EnableWindow(state->delete_button, ListView_GetSelectedCount(state->list) > 0);
            } else if (header->code == LVN_KEYDOWN) {
                const auto* key = reinterpret_cast<const NMLVKEYDOWN*>(lparam);
                if (key->wVKey == VK_DELETE) {
                    DeleteSelected(*state);
                } else if (key->wVKey == VK_ESCAPE) {
                    DestroyWindow(window);
                }
            }
            return 0;
        }
        case WM_DESTROY:
            g_window = nullptr;
            break;
        case WM_NCDESTROY:
            if (state != nullptr) {
                if (state->font != nullptr) {
                    DeleteObject(state->font);
                }
                delete state;
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                ReleaseModuleRef();
            }
            break;
        default:
            break;
        }
    } catch (...) {
        // Never let an exception cross into the host app's message loop.
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

void ShowLearningManager()
{
    if (g_window != nullptr) {
        ShowWindow(g_window, SW_RESTORE);
        SetForegroundWindow(g_window);
        return;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = WindowProc;
    window_class.hInstance = ModuleHandle();
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    window_class.lpszClassName = kClassName;
    RegisterClassExW(&window_class); // fails harmlessly when already registered

    const UINT dpi = GetDpiForSystem();
    const int width = MulDiv(780, static_cast<int>(dpi), 96);
    const int height = MulDiv(480, static_cast<int>(dpi), 96);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    // Astelio IME 入力履歴
    g_window = CreateWindowExW(0, kClassName, L"Astelio IME \u5165\u529B\u5C65\u6B74",
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX,
                               work.left + (work.right - work.left - width) / 2,
                               work.top + (work.bottom - work.top - height) / 2, width, height, nullptr, nullptr,
                               ModuleHandle(), nullptr);
    if (g_window != nullptr) {
        ShowWindow(g_window, SW_SHOWNORMAL);
        SetForegroundWindow(g_window);
    }
}

} // namespace astelio::tip
