namespace Astelio.Settings.Tour;

/// <summary>One moment of a demo: what the app shows, the key just pressed and what it means.</summary>
internal sealed record DemoFrame
{
    public string Committed { get; init; } = "";
    public string Composition { get; init; } = "";
    public int CaretInComposition { get; init; } = -1;
    public string[]? Segments { get; init; }
    public int Focus { get; init; }
    public string? Key { get; init; }
    public string Caption { get; init; } = "";
    public int Milliseconds { get; init; } = 260;
    public string? Mode { get; init; }
    public bool Flash { get; init; }
    public DemoCandidates? Candidates { get; init; }
    public string[]? Menu { get; init; }
    public int MenuSelection { get; init; } = -1;
}

internal sealed record DemoCandidates(string[] Items, int Selected = -1, string[]? Emoji = null, int EmojiSelected = -1,
    int Typo = -1, string? Note = null);

internal sealed record Demo(string Title, string Description, DemoFrame[] Frames);

/// <summary>The demos of the tour (U-03), the same scenes as the guide page (platform/windows/installer/guide).</summary>
internal static class Demos
{
    public static IReadOnlyList<Demo> All(DateTime today)
    {
        string iso = today.ToString("yyyy-MM-dd");
        string slash = today.ToString("yyyy/MM/dd");
        string shortDate = $"{today.Month}/{today.Day}";
        string[] dog = ["犬", "戌", "イヌ", "いぬ", "狗"];
        string[] dogEmoji = ["🐶", "🐕", "🐩", "🦮"];
        string[] thanks = ["ありがとう", "有り難い", "ありがとうございます"];
        string[] settings = ["全般", "入力補助", "変換", "入力の履歴", "ユーザー辞書", "アプリごとの設定", "使い方"];
        return
        [
            new("Alt キーで日本語と英語",
                "右 Alt を1回押すと日本語、左 Alt を1回押すと英語。押した側で決まるので、今どちらかを気にせず打てます。Alt でアプリのメニューが開くこともありません。",
                [
                    new() { Committed = "Hello ", Mode = "A", Caption = "英語で入力中", Milliseconds = 1200 },
                    new() { Committed = "Hello ", Mode = "あ", Flash = true, Key = "右 Alt", Caption = "右 Alt を1回 → 日本語", Milliseconds = 1600 },
                    .. Typing("Hello ", [("k", "k"), ("o", "こ"), ("n", "こn"), ("n", "こん")], "そのまま日本語で"),
                    new() { Committed = "Hello こん", Mode = "A", Flash = true, Key = "左 Alt", Caption = "左 Alt を1回 → 確定して英語", Milliseconds = 1800 },
                ]),
            new("ローマ字で打って Space で変換",
                "打った文字は下線付きの未確定の文字に。Space で文節ごとに変換し、Enter で確定します。← → で文節を移り、Shift+← → で区切りを直せます。",
                [
                    .. Typing("", [("n", "n"), ("i", "に"), ("h", "にh"), ("o", "にほ"), ("n", "にほn"), ("g", "にほんg"), ("o", "にほんご"),
                                   ("w", "にほんごw"), ("o", "にほんごを"), ("k", "にほんごをk"), ("a", "にほんごをか"), ("k", "にほんごをかk"), ("u", "にほんごをかく")],
                              "ローマ字で打つと未確定のかなに"),
                    new() { Segments = ["日本語を", "書く"], Key = "Space", Caption = "Space で変換（文節ごとに下線）", Milliseconds = 1800 },
                    new() { Segments = ["日本語を", "書く"], Focus = 1, Key = "→", Caption = "→ で次の文節へ", Milliseconds = 1400 },
                    new() { Committed = "日本語を書く", Key = "Enter", Caption = "Enter で確定", Milliseconds = 2000 },
                ]),
            new("英単語もそのまま",
                "Shift を押しながら打った英字は、半角のまま未確定の文字に入ります。かなの後に英単語を続けて、一緒に変換・確定できます。",
                [
                    .. Typing("", [("a", "あ"), ("t", "あt"), ("a", "あた"), ("r", "あたr"), ("a", "あたら"), ("s", "あたらs"), ("i", "あたらし"),
                                   ("i", "あたらしい"), ("Shift+W", "あたらしいW"), ("i", "あたらしいWi"), ("n", "あたらしいWin"),
                                   ("d", "あたらしいWind"), ("o", "あたらしいWindo"), ("w", "あたらしいWindow"), ("s", "あたらしいWindows")],
                              "Shift+英字から後は半角の英字"),
                    new() { Segments = ["新しい", "Windows"], Key = "Space", Caption = "かなと一緒に変換", Milliseconds = 2000 },
                    new() { Committed = "新しいWindows", Key = "Enter", Caption = "英語に切り替えずに確定", Milliseconds = 2000 },
                ]),
            new("候補と絵文字",
                "もう一度 Space で候補の一覧。↓ ↑ や数字キーで選びます。候補が多いときは3列に広がり、右端には読みに合う絵文字が並びます。",
                [
                    .. Typing("", [("i", "い"), ("n", "いn"), ("u", "いぬ")], "「いぬ」と打って"),
                    new() { Segments = ["犬"], Key = "Space", Caption = "Space で変換", Milliseconds = 1100 },
                    new() { Segments = ["犬"], Key = "Space", Caption = "もう一度 Space で候補の一覧", Candidates = new(dog, 0, dogEmoji), Milliseconds = 1400 },
                    new() { Segments = ["戌"], Key = "↓", Caption = "↓ で次の候補", Candidates = new(dog, 1, dogEmoji), Milliseconds = 1200 },
                    new() { Segments = ["戌"], Key = "→", Caption = "→ で絵文字の列へ", Candidates = new(dog, -1, dogEmoji, 1), Milliseconds = 1400 },
                    new() { Segments = ["🐶"], Key = "↑", Caption = "↑ で選んで", Candidates = new(dog, -1, dogEmoji, 0), Milliseconds = 1200 },
                    new() { Committed = "🐶", Key = "Enter", Caption = "Enter で確定", Milliseconds = 1800 },
                ]),
            new("予測ともしかして",
                "2文字ほど打つと、入力中から候補が出ます。打ち間違えても「もしかして」で正しい語を出します。一度選んだ語は、次から先に出ます。",
                [
                    .. Typing("", [("a", "あ"), ("r", "あr"), ("i", "あり")], "2文字から予測"),
                    new() { Composition = "あり", Caption = "入力中から候補が出る", Candidates = new(thanks), Milliseconds = 1400 },
                    new() { Segments = ["ありがとう"], Key = "Tab", Caption = "Tab で選ぶ", Candidates = new(thanks, 0), Milliseconds = 1400 },
                    new() { Committed = "ありがとう", Key = "Enter", Caption = "Enter で確定", Milliseconds = 1200 },
                    .. Typing("ありがとう", [("y", "y"), ("u", "ゆ"), ("-", "ゆー"), ("a", "ゆーあ"), ("-", "ゆーあー")], "打ち間違えても…"),
                    new() { Committed = "ありがとう", Composition = "ゆーあー", Caption = "「もしかして」で正しい語を", Candidates = new(["ゆーあー", "ユーザー"], Typo: 1), Milliseconds = 2400 },
                ]),
            new("日付、箇条書き、かっこ",
                "「きょう」「あした」は日付に。文の頭の「1.」は箇条書きの番号のまま。かっこは閉じかっこと一緒に入り、カーソルは中に入ります。",
                [
                    .. Typing("", [("k", "k"), ("y", "ky"), ("o", "きょ"), ("u", "きょう")], "「きょう」を変換すると"),
                    new() { Segments = ["今日"], Key = "Space", Caption = "日付の候補", Candidates = new(["今日", slash, iso, shortDate, "本日"], 2), Milliseconds = 2000 },
                    new() { Committed = iso + " ", Key = "Enter", Caption = "好きな表記で確定", Milliseconds = 1200 },
                    new() { Committed = iso + " ", Composition = "1. ", Key = "1 .", Caption = "文の頭の「1.」は「1. 」に", Milliseconds = 1600 },
                    new() { Committed = iso + " 1. ", Composition = "「」", CaretInComposition = 1, Key = "[", Caption = "[ で「」が入り、カーソルは中に", Milliseconds = 2000 },
                ]),
            new("設定",
                "タスクバーの「あ / A」を右クリックして「設定...」。機能ごとの画面で、すぐに切り替えられます。アプリごとに使わない設定もできます。",
                [
                    new() { Mode = "あ", Key = "右クリック", Caption = "タスクバーの「あ」を右クリック", Milliseconds = 1200 },
                    new() { Menu = ["設定..."], MenuSelection = 0, Caption = "「設定...」を選ぶと", Milliseconds = 1600 },
                    new() { Menu = settings, MenuSelection = 1, Caption = "機能ごとの画面ですぐに切り替え", Milliseconds = 2400 },
                    new() { Menu = settings, MenuSelection = 4, Caption = "自分の言葉はユーザー辞書に", Milliseconds = 2000 },
                ]),
        ];
    }

    private static IEnumerable<DemoFrame> Typing(string committed, (string Key, string Composition)[] keys, string caption) =>
        keys.Select(key => new DemoFrame { Committed = committed, Composition = key.Composition, Key = key.Key, Caption = caption });
}
