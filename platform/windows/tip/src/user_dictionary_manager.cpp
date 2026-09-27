#include "user_dictionary_manager.h"

#include "dialog_keys.h"
#include "module.h"
#include "user_dictionary_store.h"

#include <windows.h>

#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>
#include <iterator>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace astelio::tip {
namespace {

using Word = UserDictionary::Word;

constexpr wchar_t kClassName[] = L"AstelioUserDictionaryManager";
enum : int {
    kSearchId = kUserDictionarySearchId,
    kListId = kUserDictionaryListId,
    kReadingId = kUserDictionaryReadingId,
    kSurfaceId = kUserDictionarySurfaceId,
    kPosId = kUserDictionaryPosId,
    kCommentId = kUserDictionaryCommentId,
    kAddId,
    kUpdateId,
    kDeleteId,
    kImportId,
    kExportId,
    kCloseId,
};

// The file types of the export dialog, in this order.
constexpr struct {
    UserDictionaryFormat format;
    const wchar_t* name;
    const wchar_t* pattern;
} kExportFormats[] = {
    {UserDictionaryFormat::GoogleIme, L"Google 日本語入力 (*.txt)", L"*.txt"},
    {UserDictionaryFormat::MsIme, L"Microsoft IME (*.txt)", L"*.txt"},
    {UserDictionaryFormat::Atok, L"ATOK (*.txt)", L"*.txt"},
    {UserDictionaryFormat::Kotoeri, L"ことえり (*.txt)", L"*.txt"},
    {UserDictionaryFormat::AstelioJson, L"Astelio IME (*.json)", L"*.json"},
};

struct State {
    UserDictionary dictionary; // the file as last read
    std::vector<Word> shown;   // what the list shows, in the same order
    HWND note = nullptr;
    HWND search_label = nullptr;
    HWND search = nullptr;
    HWND count = nullptr;
    HWND list = nullptr;
    HWND labels[4] = {};
    HWND reading = nullptr;
    HWND surface = nullptr;
    HWND pos = nullptr;
    HWND comment = nullptr;
    HWND add_button = nullptr;
    HWND update_button = nullptr;
    HWND delete_button = nullptr;
    HWND import_button = nullptr;
    HWND export_button = nullptr;
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

std::u16string TextOf(HWND control)
{
    const int length = GetWindowTextLengthW(control);
    std::u16string text(static_cast<std::size_t>(length) + 1, u'\0');
    const int copied = GetWindowTextW(control, reinterpret_cast<wchar_t*>(text.data()), length + 1);
    text.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0);
    return text;
}

void Tell(HWND window, const std::wstring& text, UINT icon = MB_ICONINFORMATION)
{
    MessageBoxW(window, text.c_str(), L"Astelio IME", MB_OK | icon);
}

void Fill(State& state)
{
    state.shown = state.dictionary.Search(TextOf(state.search));
    SendMessageW(state.list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(state.list);
    for (std::size_t i = 0; i < state.shown.size(); ++i) {
        const Word& word = state.shown[i];
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<wchar_t*>(Wide(word.reading));
        item.lParam = static_cast<LPARAM>(i);
        const int row = ListView_InsertItem(state.list, &item);
        ListView_SetItemText(state.list, row, 1, const_cast<wchar_t*>(Wide(word.surface)));
        const std::u16string pos(UserDictionary::PartOfSpeechName(word.pos));
        ListView_SetItemText(state.list, row, 2, const_cast<wchar_t*>(Wide(pos)));
        ListView_SetItemText(state.list, row, 3, const_cast<wchar_t*>(Wide(word.comment)));
    }
    SendMessageW(state.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(state.list, nullptr, TRUE);

    const std::size_t total = state.dictionary.Words().size();
    std::wstring count = std::to_wstring(total) + L"語";
    if (state.shown.size() != total) {
        count = std::to_wstring(state.shown.size()) + L" / " + count;
    }
    SetWindowTextW(state.count, count.c_str());
    EnableWindow(state.update_button, FALSE);
    EnableWindow(state.delete_button, FALSE);
    EnableWindow(state.export_button, total > 0);
}

// Saves `latest` and shows it; on failure shows the file as it still is.
bool Commit(HWND window, State& state, UserDictionary latest)
{
    const bool saved = SaveUserDictionary(latest);
    if (saved) {
        state.dictionary = std::move(latest);
    } else {
        Tell(window, L"ユーザー辞書を保存できませんでした。", MB_ICONERROR);
        state.dictionary = LoadUserDictionary();
    }
    Fill(state);
    return saved;
}

Word WordFromFields(const State& state)
{
    Word word;
    word.reading = TextOf(state.reading);
    // Readings are matched against the hiragana typed; katakana entered here would never match.
    for (char16_t& c : word.reading) {
        if ((c >= u'\u30A1' && c <= u'\u30F6') || c == u'\u30FD' || c == u'\u30FE') {
            c = static_cast<char16_t>(c - 0x60);
        }
    }
    word.surface = TextOf(state.surface);
    const LRESULT pos = SendMessageW(state.pos, CB_GETCURSEL, 0, 0);
    if (pos >= 0 && static_cast<std::size_t>(pos) < UserDictionary::kPartOfSpeechNames.size()) {
        word.pos = static_cast<UserDictionary::PartOfSpeech>(pos);
    }
    word.comment = TextOf(state.comment);
    return word;
}

void ShowInFields(const State& state, const Word& word)
{
    SetWindowTextW(state.reading, Wide(word.reading));
    SetWindowTextW(state.surface, Wide(word.surface));
    SendMessageW(state.pos, CB_SETCURSEL, static_cast<WPARAM>(word.pos), 0);
    SetWindowTextW(state.comment, Wide(word.comment));
}

bool CheckWord(HWND window, const Word& word)
{
    if (UserDictionary::Valid(word)) {
        return true;
    }
    Tell(window,
         L"読みと表記を入力してください（抑制単語は読みを空にできます）。タブと改行は使えません。"
         L"読みは64文字、表記は128文字、コメントは256文字までです。",
         MB_ICONWARNING);
    return false;
}

// The rows chosen in the list, as indexes into `shown`.
std::vector<std::size_t> SelectedRows(const State& state)
{
    std::vector<std::size_t> rows;
    for (int row = ListView_GetNextItem(state.list, -1, LVNI_SELECTED); row >= 0;
         row = ListView_GetNextItem(state.list, row, LVNI_SELECTED)) {
        LVITEMW item{};
        item.mask = LVIF_PARAM;
        item.iItem = row;
        if (ListView_GetItem(state.list, &item) && static_cast<std::size_t>(item.lParam) < state.shown.size()) {
            rows.push_back(static_cast<std::size_t>(item.lParam));
        }
    }
    return rows;
}

// Every change starts from the file as it is now (other apps may have changed it since the list was filled).
void AddWord(HWND window, State& state)
{
    const Word word = WordFromFields(state);
    if (!CheckWord(window, word)) {
        return;
    }
    UserDictionary latest = LoadUserDictionary();
    if (!latest.Add(word)) {
        Tell(window, L"ユーザー辞書に登録できるのは10000語までです。", MB_ICONWARNING);
        return;
    }
    Commit(window, state, std::move(latest));
    SetWindowTextW(state.reading, L"");
    SetWindowTextW(state.surface, L"");
    SetWindowTextW(state.comment, L"");
    SetFocus(state.reading);
}

void UpdateWord(HWND window, State& state)
{
    const std::vector<std::size_t> rows = SelectedRows(state);
    if (rows.size() != 1) {
        return;
    }
    const Word word = WordFromFields(state);
    if (!CheckWord(window, word)) {
        return;
    }
    UserDictionary latest = LoadUserDictionary();
    if (!latest.Update(state.shown[rows.front()], word)) {
        Tell(window, L"この語はほかのアプリで変更されました。一覧を読み直します。", MB_ICONWARNING);
        state.dictionary = std::move(latest);
        Fill(state);
        return;
    }
    Commit(window, state, std::move(latest));
}

void DeleteSelected(HWND window, State& state)
{
    UserDictionary latest = LoadUserDictionary();
    bool removed = false;
    for (const std::size_t row : SelectedRows(state)) {
        removed = latest.Remove(state.shown[row]) || removed;
    }
    if (removed) {
        Commit(window, state, std::move(latest));
    } else {
        state.dictionary = std::move(latest);
        Fill(state);
    }
}

void Import(HWND window, State& state)
{
    // 辞書ファイル (*.txt; *.tsv; *.json) / すべてのファイル (*.*)
    std::wstring filter = L"辞書ファイル (*.txt; *.tsv; *.json)";
    filter += L'\0';
    filter += L"*.txt;*.tsv;*.json";
    filter += L'\0';
    filter += L"すべてのファイル (*.*)";
    filter += L'\0';
    filter += L"*.*";
    filter += L'\0';
    wchar_t path[MAX_PATH * 4] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&dialog)) {
        return;
    }
    UserDictionary latest = LoadUserDictionary();
    const UserDictionaryFileImport result = ImportUserDictionaryFile(path, latest);
    if (!result.read) {
        Tell(window, L"ファイルを読めませんでした。ユーザー辞書は変わっていません。", MB_ICONWARNING);
        return;
    }
    if (!result.format) {
        Tell(window, L"ファイルの形式が分かりませんでした。ユーザー辞書は変わっていません。", MB_ICONWARNING);
        return;
    }
    if (result.added > 0 && !Commit(window, state, std::move(latest))) {
        return;
    }
    Tell(window, std::to_wstring(result.added) + L"語を取り込みました。" + std::to_wstring(result.skipped) +
                     L"行を飛ばしました。");
}

void Export(HWND window, State& state)
{
    std::wstring filter;
    for (const auto& type : kExportFormats) {
        filter += type.name;
        filter += L'\0';
        filter += type.pattern;
        filter += L'\0';
    }
    wchar_t path[MAX_PATH * 4] = L"astelio_user_dictionary";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrFilter = filter.c_str();
    dialog.nFilterIndex = 1;
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.lpstrDefExt = L"txt";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&dialog) || dialog.nFilterIndex < 1 || dialog.nFilterIndex > std::size(kExportFormats)) {
        return;
    }
    state.dictionary = LoadUserDictionary();
    Fill(state);
    const std::vector<Word>& words = state.dictionary.Words();
    const UserDictionaryFormat format = kExportFormats[dialog.nFilterIndex - 1].format;
    // ATOK and Kotoeri have no suppressed words, so those are left out of the file.
    const bool drops_suppressed = format == UserDictionaryFormat::Atok || format == UserDictionaryFormat::Kotoeri;
    const auto written = static_cast<std::size_t>(std::count_if(words.begin(), words.end(), [&](const Word& word) {
        return !drops_suppressed || word.pos != UserDictionary::PartOfSpeech::Suppressed;
    }));
    switch (ExportUserDictionaryFile(path, words, format)) {
    case UserDictionaryExport::Written:
        Tell(window, std::to_wstring(written) + L"語を書き出しました。");
        break;
    case UserDictionaryExport::NotEncodable:
        Tell(window,
             L"この形式の文字コード（Shift_JIS）で表せない文字があるため、書き出せませんでした。"
             L"ほかの形式を選んでください。",
             MB_ICONWARNING);
        break;
    case UserDictionaryExport::NotWritten:
        Tell(window, L"ファイルに書き出せませんでした。", MB_ICONERROR);
        break;
    }
}

void OnSelectionChanged(State& state)
{
    const std::vector<std::size_t> rows = SelectedRows(state);
    if (rows.size() == 1) {
        ShowInFields(state, state.shown[rows.front()]);
    }
    EnableWindow(state.update_button, rows.size() == 1);
    EnableWindow(state.delete_button, !rows.empty());
}

void Layout(HWND window, State& state)
{
    RECT client{};
    GetClientRect(window, &client);
    const int margin = Scale(window, 12);
    const int gap = Scale(window, 8);
    const int line = Scale(window, 20);
    const int edit_height = Scale(window, 24);
    const int button_height = Scale(window, 28);
    const int width = client.right - margin * 2;

    int top = margin;
    MoveWindow(state.note, margin, top, width, line * 2, TRUE);
    top += line * 2 + gap / 2;
    const int label_width = Scale(window, 40);
    const int count_width = Scale(window, 120);
    MoveWindow(state.search_label, margin, top + Scale(window, 3), label_width, line, TRUE);
    MoveWindow(state.search, margin + label_width, top, Scale(window, 300), edit_height, TRUE);
    MoveWindow(state.count, client.right - margin - count_width, top + Scale(window, 3), count_width, line, TRUE);
    top += edit_height + gap;

    const int buttons_top = client.bottom - margin - button_height;
    const int fields_top = buttons_top - gap - edit_height;
    const int labels_top = fields_top - line;
    MoveWindow(state.list, margin, top, width, labels_top - gap - top, TRUE);

    const int reading_width = Scale(window, 170);
    const int surface_width = Scale(window, 170);
    const int pos_width = Scale(window, 110);
    const int comment_width = width - reading_width - surface_width - pos_width - gap * 3;
    const struct {
        HWND field;
        int width;
    } fields[] = {{state.reading, reading_width},
                  {state.surface, surface_width},
                  {state.pos, pos_width},
                  {state.comment, comment_width}};
    int left = margin;
    for (std::size_t i = 0; i < std::size(fields); ++i) {
        MoveWindow(state.labels[i], left, labels_top, fields[i].width, line, TRUE);
        const int height = fields[i].field == state.pos ? Scale(window, 240) : edit_height;
        MoveWindow(fields[i].field, left, fields_top, fields[i].width, height, TRUE);
        left += fields[i].width + gap;
    }

    const int short_width = Scale(window, 80);
    const int long_width = Scale(window, 120);
    left = margin;
    for (HWND button : {state.add_button, state.update_button, state.delete_button}) {
        MoveWindow(button, left, buttons_top, short_width, button_height, TRUE);
        left += short_width + gap;
    }
    left += gap * 2;
    for (HWND button : {state.import_button, state.export_button}) {
        MoveWindow(button, left, buttons_top, long_width, button_height, TRUE);
        left += long_width + gap;
    }
    MoveWindow(state.close_button, client.right - margin - short_width, buttons_top, short_width, button_height,
               TRUE);
}

HWND Child(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style, int id, DWORD extended = 0)
{
    return CreateWindowExW(extended, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), ModuleHandle(), nullptr);
}

HWND CreateEdit(HWND parent, int id, std::size_t limit)
{
    HWND edit = Child(parent, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, id, WS_EX_CLIENTEDGE);
    if (edit != nullptr) {
        SendMessageW(edit, EM_SETLIMITTEXT, static_cast<WPARAM>(limit), 0);
    }
    return edit;
}

bool Create(HWND window, State& state)
{
    const int point_size = 9;
    state.font = CreateFontW(-MulDiv(point_size, static_cast<int>(GetDpiForWindow(window)), 72), 0, 0, 0,
                             FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Yu Gothic UI");
    state.note = Child(window, L"STATIC",
                       L"登録した語は、どのアプリでも次の入力から変換の先頭に出ます。品詞を「抑制単語」にすると、"
                       L"その表記を候補に出しません（読みを空にすると、どの読みでも出しません）。",
                       SS_LEFT, 0);
    state.search_label = Child(window, L"STATIC", L"検索", SS_LEFT, 0);
    state.search = CreateEdit(window, kSearchId, UserDictionary::kMaxCommentLength);
    state.count = Child(window, L"STATIC", L"", SS_RIGHT, 0);
    state.list = Child(window, WC_LISTVIEWW, L"", WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS, kListId);
    const wchar_t* const titles[] = {L"読み", L"表記", L"品詞", L"コメント"};
    for (std::size_t i = 0; i < std::size(titles); ++i) {
        state.labels[i] = Child(window, L"STATIC", titles[i], SS_LEFT, 0);
    }
    state.reading = CreateEdit(window, kReadingId, UserDictionary::kMaxReadingLength);
    state.surface = CreateEdit(window, kSurfaceId, UserDictionary::kMaxSurfaceLength);
    state.pos = Child(window, WC_COMBOBOXW, L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, kPosId);
    state.comment = CreateEdit(window, kCommentId, UserDictionary::kMaxCommentLength);
    state.add_button = Child(window, L"BUTTON", L"追加", WS_TABSTOP | BS_PUSHBUTTON, kAddId);
    state.update_button = Child(window, L"BUTTON", L"変更", WS_TABSTOP | BS_PUSHBUTTON, kUpdateId);
    state.delete_button = Child(window, L"BUTTON", L"削除", WS_TABSTOP | BS_PUSHBUTTON, kDeleteId);
    state.import_button = Child(window, L"BUTTON", L"インポート...", WS_TABSTOP | BS_PUSHBUTTON, kImportId);
    state.export_button = Child(window, L"BUTTON", L"エクスポート...", WS_TABSTOP | BS_PUSHBUTTON, kExportId);
    state.close_button = Child(window, L"BUTTON", L"閉じる", WS_TABSTOP | BS_PUSHBUTTON, kCloseId);
    const HWND children[] = {state.note,          state.search_label,  state.search,        state.count,
                             state.list,          state.labels[0],     state.labels[1],     state.labels[2],
                             state.labels[3],     state.reading,       state.surface,       state.pos,
                             state.comment,       state.add_button,    state.update_button, state.delete_button,
                             state.import_button, state.export_button, state.close_button};
    for (HWND child : children) {
        if (child == nullptr) {
            return false;
        }
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE);
    }
    for (const std::u16string_view name : UserDictionary::kPartOfSpeechNames) {
        const std::u16string text(name);
        SendMessageW(state.pos, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Wide(text)));
    }
    SendMessageW(state.pos, CB_SETCURSEL, 0, 0);
    ListView_SetExtendedListViewStyle(state.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    const int widths[] = {170, 170, 90, 250};
    for (int i = 0; i < static_cast<int>(std::size(titles)); ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = const_cast<wchar_t*>(titles[i]);
        column.cx = Scale(window, widths[i]);
        column.iSubItem = i;
        ListView_InsertColumn(state.list, i, &column);
    }
    state.dictionary = LoadUserDictionary();
    Fill(state);
    Layout(window, state);
    UseDialogKeys(window);
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
        case WM_COMMAND: {
            if (state == nullptr) {
                break;
            }
            const int id = LOWORD(wparam);
            const int code = HIWORD(wparam);
            if (id == kSearchId && code == EN_CHANGE) {
                Fill(*state);
                return 0;
            }
            if (code != BN_CLICKED) {
                break;
            }
            switch (id) {
            case kAddId: AddWord(window, *state); return 0;
            case kUpdateId: UpdateWord(window, *state); return 0;
            case kDeleteId: DeleteSelected(window, *state); return 0;
            case kImportId: Import(window, *state); return 0;
            case kExportId: Export(window, *state); return 0;
            case IDOK: {
                // Enter in the fields adds the word, or changes the one selected.
                const HWND focus = GetFocus();
                if (focus != state->search && focus != state->list) {
                    if (IsWindowEnabled(state->update_button)) {
                        UpdateWord(window, *state);
                    } else {
                        AddWord(window, *state);
                    }
                }
                return 0;
            }
            case kCloseId:
            case IDCANCEL: DestroyWindow(window); return 0;
            default: break;
            }
            break;
        }
        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<const NMHDR*>(lparam);
            if (state == nullptr || header->idFrom != kListId) {
                break;
            }
            if (header->code == LVN_ITEMCHANGED) {
                OnSelectionChanged(*state);
            } else if (header->code == LVN_KEYDOWN) {
                const auto* key = reinterpret_cast<const NMLVKEYDOWN*>(lparam);
                if (key->wVKey == VK_DELETE) {
                    DeleteSelected(window, *state);
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

HWND UserDictionaryManagerWindow()
{
    return g_window;
}

void ShowUserDictionaryManager()
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
    const int width = MulDiv(820, static_cast<int>(dpi), 96);
    const int height = MulDiv(560, static_cast<int>(dpi), 96);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    g_window = CreateWindowExW(0, kClassName, L"Astelio IME ユーザー辞書",
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
