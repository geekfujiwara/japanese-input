import SwiftUI

private let ink = Color(red: 0.125, green: 0.145, blue: 0.129) // #202521, the header of the Windows settings
private let accent = Color(red: 0.31, green: 0.62, blue: 0.43)

struct SettingsView: View {
    @StateObject private var setup = SetupModel()

    var body: some View {
        VStack(spacing: 0) {
            Header(status: setup.status)
            TabView {
                SetupTab(setup: setup)
                    .tabItem { Label("はじめに", systemImage: "checklist") }
                TypingTab()
                    .tabItem { Label("入力", systemImage: "keyboard") }
                HistoryTab()
                    .tabItem { Label("履歴", systemImage: "clock.arrow.circlepath") }
                DictionaryTab()
                    .tabItem { Label("ユーザー辞書", systemImage: "character.book.closed") }
                AboutTab(version: setup.currentVersion)
                    .tabItem { Label("情報", systemImage: "info.circle") }
            }
            .padding(.horizontal, 8)
            .padding(.bottom, 8)
        }
    }
}

// MARK: - Header

private struct Header: View {
    let status: InputMethodInstaller.Status

    var body: some View {
        HStack(spacing: 14) {
            Image(nsImage: NSApp.applicationIconImage)
                .resizable()
                .frame(width: 52, height: 52)
            VStack(alignment: .leading, spacing: 4) {
                Text("Astelio IME")
                    .font(.system(size: 24, weight: .semibold))
                    .foregroundStyle(.white)
                Text("日本語入力。変換も履歴も、すべて Mac の中だけで動きます。")
                    .font(.system(size: 12))
                    .foregroundStyle(Color(white: 0.8))
            }
            Spacer()
            Badge(status: status)
        }
        .padding(.horizontal, 28)
        .padding(.vertical, 20)
        .background(ink)
    }
}

private struct Badge: View {
    let status: InputMethodInstaller.Status

    var body: some View {
        let (text, color): (String, Color) = switch status {
        case .ready: ("使えます", Color(red: 0.55, green: 0.85, blue: 0.62))
        case .needsEnabling: ("キーボードへの追加が必要", .orange)
        case .needsUpdate: ("更新が必要", .orange)
        case .notInstalled: ("未インストール", Color(white: 0.75))
        }
        Text(text)
            .font(.system(size: 12, weight: .semibold))
            .foregroundStyle(color)
            .padding(.horizontal, 12)
            .padding(.vertical, 6)
            .background(Capsule().fill(color.opacity(0.18)))
    }
}

// MARK: - Setup

private struct SetupTab: View {
    @ObservedObject var setup: SetupModel
    @State private var trial = ""
    @State private var confirmRemove = false

    private var installed: Bool { setup.status != .notInstalled && setup.status != .needsUpdate }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                Step(number: 1, title: "入力メソッドをインストール", done: installed,
                     detail: "Astelio IME 本体を、このユーザーの「~/Library/Input Methods」に入れます（管理者のパスワードは要りません）。") {
                    HStack {
                        Button(setup.status == .needsUpdate ? "更新する" : installed ? "入れ直す" : "インストール") {
                            setup.install()
                        }
                        .buttonStyle(.borderedProminent)
                        .tint(accent)
                        if let installedVersion = setup.installedVersion {
                            Text("入っている版: \(installedVersion)")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }
                }

                Step(number: 2, title: "キーボードに追加", done: setup.status == .ready,
                     detail: "システム設定 →［キーボード］→［テキスト入力］の［編集…］→［＋］→［日本語］→「Astelio IME」を選んで［追加］。") {
                    Button("システム設定を開く") { setup.openKeyboardSettings() }
                        .disabled(!installed)
                }

                Step(number: 3, title: "使ってみる", done: false,
                     detail: "メニューバーの入力メニューで「Astelio IME」を選び、下の欄に入力してみます。ローマ字で打って Space で変換、Return で確定。右⌘で日本語、左⌘で英語です。") {
                    TextField("ここに入力して試す", text: $trial, axis: .vertical)
                        .textFieldStyle(.roundedBorder)
                        .lineLimit(2...4)
                }

                if let message = setup.message {
                    Text(message)
                        .font(.callout)
                        .foregroundStyle(Color(red: 0.47, green: 0.19, blue: 0.05))
                        .padding(12)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(RoundedRectangle(cornerRadius: 6).fill(Color.orange.opacity(0.15)))
                }

                if setup.status != .notInstalled {
                    HStack {
                        Spacer()
                        Button("入力メソッドを取り除く…", role: .destructive) { confirmRemove = true }
                            .buttonStyle(.link)
                            .confirmationDialog("Astelio IME を取り除きますか？", isPresented: $confirmRemove) {
                                Button("取り除く", role: .destructive) { setup.uninstall() }
                            } message: {
                                Text("入力の履歴とユーザー辞書は残ります。この設定アプリも消すと、すべてなくなります。")
                            }
                    }
                }
            }
            .padding(24)
        }
    }
}

private struct Step<Content: View>: View {
    let number: Int
    let title: String
    let done: Bool
    let detail: String
    @ViewBuilder var content: Content

    var body: some View {
        HStack(alignment: .top, spacing: 14) {
            ZStack {
                Circle().fill(done ? accent : Color.secondary.opacity(0.25))
                if done {
                    Image(systemName: "checkmark").font(.system(size: 13, weight: .bold)).foregroundStyle(.white)
                } else {
                    Text("\(number)").font(.system(size: 14, weight: .semibold)).foregroundStyle(.primary)
                }
            }
            .frame(width: 30, height: 30)
            VStack(alignment: .leading, spacing: 8) {
                Text(title).font(.system(size: 15, weight: .semibold))
                Text(detail).font(.callout).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                content
            }
            Spacer(minLength: 0)
        }
        .padding(16)
        .background(RoundedRectangle(cornerRadius: 10).fill(Color(nsColor: .controlBackgroundColor)))
        .overlay(RoundedRectangle(cornerRadius: 10).stroke(Color.secondary.opacity(0.2)))
    }
}

// MARK: - Typing

private struct TypingTab: View {
    @AppStorage("TapModifier") private var tapModifier = "command"
    @AppStorage("TypoSuggestions") private var typoSuggestions = true
    @AppStorage("AutoCloseBrackets") private var autoCloseBrackets = true
    @AppStorage("ListNumberPeriod") private var listNumberPeriod = true
    @AppStorage("DateFormat") private var dateFormat = 0

    var body: some View {
        Form {
            Section("入力モードの切り替え") {
                Picker("単押しするキー", selection: $tapModifier) {
                    Text("⌘ Command").tag("command")
                    Text("⌥ Option").tag("option")
                }
                .pickerStyle(.segmented)
                Text("左のキーを単押しで英語入力、右のキーを単押しで日本語入力に切り替えます。⌘C などのショートカットでは切り替わりません。")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Section("入力の補助") {
                Toggle("もしかして（打ち間違いの候補）を出す", isOn: $typoSuggestions)
                Toggle("「（」を打つと「）」も入れる", isOn: $autoCloseBrackets)
                Toggle("文の頭の数字の後のピリオドを「. 」にする", isOn: $listNumberPeriod)
            }
            Section("日付") {
                Picker("「きょう」「にちじ」で出す日付", selection: $dateFormat) {
                    ForEach(Array(DateSample.titles.enumerated()), id: \.offset) { index, title in
                        Text(title).tag(index)
                    }
                }
            }
            Section {
                Text("変更はすぐ保存され、次に文字を入力する欄から反映されます。")
                    .font(.caption).foregroundStyle(.secondary)
            }
        }
        .formStyle(.grouped)
    }
}

private enum DateSample {
    static var titles: [String] {
        let now = Date()
        let patterns = ["yyyy/MM/dd", "yyyy年M月d日", "M月d日", "M月d日(E)", "GGGGy年M月d日", "yyyy-MM-dd", "yyyyMMdd", "M/d"]
        return patterns.map { pattern in
            let formatter = DateFormatter()
            formatter.locale = Locale(identifier: "ja_JP")
            formatter.calendar = Calendar(identifier: pattern.hasPrefix("GGGG") ? .japanese : .gregorian)
            formatter.dateFormat = pattern
            return formatter.string(from: now)
        }
    }
}

// MARK: - History

private struct HistoryTab: View {
    @AppStorage("LearningEnabled") private var learningEnabled = true
    @AppStorage("LearningPaused") private var learningPaused = false
    @State private var confirmClear = false
    @State private var cleared = false

    var body: some View {
        Form {
            Section("入力の履歴（学習）") {
                Toggle("入力の履歴を使って変換と予測を良くする", isOn: $learningEnabled)
                Toggle("履歴の記録を一時停止する（シークレットモード）", isOn: $learningPaused)
                    .disabled(!learningEnabled)
                Text("履歴はこの Mac の中だけに保存され、外部には送りません。パスワード欄では記録しません。")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Section("データ") {
                HStack {
                    Button("履歴をすべて削除…", role: .destructive) { confirmClear = true }
                        .confirmationDialog("入力の履歴をすべて削除しますか？", isPresented: $confirmClear) {
                            Button("削除する（元に戻せません）", role: .destructive) {
                                HistoryActions.clear()
                                cleared = true
                            }
                        }
                    if cleared {
                        Text("削除しました").font(.caption).foregroundStyle(.secondary)
                    }
                    Spacer()
                    Button("データのフォルダーを開く") { HistoryActions.openDataFolder() }
                }
            }
        }
        .formStyle(.grouped)
    }
}

enum HistoryActions {
    static func clear() {
        let folder = AppState.dataDirectory()
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        // The file first, then the running input method (which holds the history in memory).
        _ = ASTEngine(dictionaryPath: nil, dataDirectory: folder.path).clearLearning()
        DistributedNotificationCenter.default().postNotificationName(
            AppMode.clearLearningNotification, object: nil, userInfo: nil, deliverImmediately: true)
    }

    static func openDataFolder() {
        let folder = AppState.dataDirectory()
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        NSWorkspace.shared.open(folder)
    }
}

// MARK: - User dictionary

private struct DictionaryTab: View {
    @State private var words: [UserWord] = UserDictionaryStore.load()
    @State private var reading = ""
    @State private var surface = ""
    @State private var partOfSpeech = "名詞"
    @State private var comment = ""
    @State private var selection: Set<String> = []
    @State private var error: String?
    @State private var filter = ""

    private var shown: [UserWord] {
        filter.isEmpty ? words : words.filter { [$0.reading, $0.surface, $0.comment].contains { $0.localizedCaseInsensitiveContains(filter) } }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            GroupBox("単語を登録") {
                VStack(alignment: .leading, spacing: 8) {
                    HStack {
                        TextField("読み（ひらがな）", text: $reading)
                        TextField("単語", text: $surface)
                    }
                    HStack {
                        Picker("品詞", selection: $partOfSpeech) {
                            ForEach(UserDictionaryStore.partsOfSpeech, id: \.self) { Text($0).tag($0) }
                        }
                        .frame(maxWidth: 200)
                        TextField("コメント（任意）", text: $comment)
                        Button("追加", action: add)
                            .buttonStyle(.borderedProminent)
                            .tint(accent)
                            .keyboardShortcut(.defaultAction)
                    }
                    if partOfSpeech == UserDictionaryStore.suppressed {
                        Text("抑制単語: 読みの変換候補にこの単語を出しません（読みが空なら、どの読みでも出しません）。")
                            .font(.caption).foregroundStyle(.secondary)
                    }
                    if let error {
                        Text(error).font(.caption).foregroundStyle(.red)
                    }
                }
                .textFieldStyle(.roundedBorder)
                .padding(4)
            }

            HStack {
                Text("\(words.count) 語").font(.callout).foregroundStyle(.secondary)
                Spacer()
                TextField("検索", text: $filter).textFieldStyle(.roundedBorder).frame(maxWidth: 180)
                Button("選んだ単語を削除", role: .destructive, action: remove)
                    .disabled(selection.isEmpty)
            }

            Table(shown, selection: $selection) {
                TableColumn("読み", value: \.reading)
                TableColumn("単語", value: \.surface)
                TableColumn("品詞", value: \.partOfSpeech)
                TableColumn("コメント", value: \.comment)
            }
        }
        .padding(16)
        .onAppear { words = UserDictionaryStore.load() }
    }

    private func add() {
        let word = UserDictionaryStore.normalized(UserWord(reading: reading, surface: surface,
                                                            partOfSpeech: partOfSpeech, comment: comment))
        if let problem = UserDictionaryStore.problem(with: word, in: words) {
            error = problem
            return
        }
        var updated = words.filter { $0.id != word.id }
        updated.append(word)
        do {
            try UserDictionaryStore.save(updated)
            words = updated
            error = nil
            reading = ""
            surface = ""
            comment = ""
        } catch {
            self.error = "保存できませんでした: \(error.localizedDescription)"
        }
    }

    private func remove() {
        let updated = words.filter { !selection.contains($0.id) }
        do {
            try UserDictionaryStore.save(updated)
            words = updated
            selection = []
            error = nil
        } catch {
            self.error = "保存できませんでした: \(error.localizedDescription)"
        }
    }
}

// MARK: - About

private struct AboutTab: View {
    let version: String

    var body: some View {
        Form {
            Section("Astelio IME") {
                LabeledContent("版", value: version)
                LabeledContent("作者", value: "Geek Fujiwara")
                LabeledContent("ライセンス", value: "MIT License")
                Text("変換エンジンは Windows 版と同じ C++ Core です。辞書は azooKey_dictionary_storage を使っています（THIRD_PARTY_NOTICES）。外部とは通信しません。")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Section("データの場所") {
                LabeledContent("履歴・ユーザー辞書", value: AppState.dataDirectory().path)
                    .textSelection(.enabled)
                Button("フォルダーを開く") { HistoryActions.openDataFolder() }
            }
        }
        .formStyle(.grouped)
    }
}
