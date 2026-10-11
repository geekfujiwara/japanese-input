import Foundation

/// user_dictionary.tsv in the data folder, the file the engine reads (and the same format as the Windows version):
/// the header line, then reading, surface, part of speech and comment separated by tabs. The input method notices a
/// changed file by itself.
struct UserWord: Identifiable, Hashable {
    var reading: String
    var surface: String
    var partOfSpeech: String
    var comment: String
    var id: String { "\(reading)\t\(surface)\t\(partOfSpeech)" }
}

enum UserDictionaryStore {
    static let partsOfSpeech = ["名詞", "固有名詞", "人名", "地名", "組織", "短縮よみ", "記号", "抑制単語"]
    static let suppressed = "抑制単語"
    static let maxWords = 10000
    private static let header = "# astelio user dictionary 1"

    static var url: URL {
        AppState.dataDirectory().appendingPathComponent("user_dictionary.tsv")
    }

    static func load() -> [UserWord] {
        guard let data = try? Data(contentsOf: url), var text = String(data: data, encoding: .utf8) else {
            return []
        }
        if text.hasPrefix("\u{FEFF}") {
            text.removeFirst()
        }
        var words: [UserWord] = []
        for line in text.split(omittingEmptySubsequences: false, whereSeparator: { $0 == "\n" || $0 == "\r\n" }) {
            if line.isEmpty || line.hasPrefix("#") {
                continue
            }
            let fields = line.split(separator: "\t", maxSplits: 3, omittingEmptySubsequences: false).map(String.init)
            guard fields.count >= 3, partsOfSpeech.contains(fields[2]) else {
                continue
            }
            words.append(UserWord(reading: fields[0], surface: fields[1], partOfSpeech: fields[2],
                                  comment: fields.count > 3 ? fields[3] : ""))
        }
        return words
    }

    static func save(_ words: [UserWord]) throws {
        var text = header + "\n"
        for word in words {
            text += "\(word.reading)\t\(word.surface)\t\(word.partOfSpeech)\t\(word.comment)\n"
        }
        let folder = url.deletingLastPathComponent()
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        try text.write(to: url, atomically: true, encoding: .utf8)
    }

    /// Why `word` cannot be stored, or nil. Katakana readings are turned into hiragana by `normalized`.
    static func problem(with word: UserWord, in words: [UserWord]) -> String? {
        let fields = [word.reading, word.surface, word.comment]
        if fields.contains(where: { $0.contains("\t") || $0.contains("\n") || $0.contains("\r") }) {
            return "タブと改行は使えません。"
        }
        if word.surface.isEmpty {
            return "単語を入力してください。"
        }
        if word.reading.isEmpty && word.partOfSpeech != suppressed {
            return "読みを入力してください。"
        }
        if word.reading.utf16.count > 64 || word.surface.utf16.count > 128 || word.comment.utf16.count > 256 {
            return "長すぎます（読み 64、単語 128、コメント 256 文字まで）。"
        }
        if words.count >= maxWords && !words.contains(where: { $0.id == word.id }) {
            return "登録できるのは \(maxWords) 語までです。"
        }
        return nil
    }

    static func normalized(_ word: UserWord) -> UserWord {
        var word = word
        word.reading = word.reading.trimmingCharacters(in: .whitespaces)
            .applyingTransform(.hiraganaToKatakana, reverse: true) ?? word.reading
        word.surface = word.surface.trimmingCharacters(in: .whitespaces)
        word.comment = word.comment.trimmingCharacters(in: .whitespaces)
        return word
    }
}
