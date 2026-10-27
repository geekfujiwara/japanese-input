import SwiftUI

private let ink = Color(red: 0.125, green: 0.145, blue: 0.129)
private let accent = Color(red: 0.31, green: 0.62, blue: 0.43)

/// The first-run wizard: one page per thing the user does, each with an animation of what happens and why it is
/// needed. The installer opens the app after installing, and the app shows this until the user finishes it once;
/// the "はじめに" tab opens it again.
struct SetupWizard: View {
    @ObservedObject var setup: SetupModel
    var onFinish: () -> Void
    @State private var page: Int
    @State private var forward = true

    init(setup: SetupModel, page: Int = 0, onFinish: @escaping () -> Void) {
        self.setup = setup
        self.onFinish = onFinish
        _page = State(initialValue: page)
    }

    private let titles = ["ようこそ", "キーボードに追加", "⌘ で切り替え", "使ってみる", "完了"]

    var body: some View {
        HStack(spacing: 0) {
            Sidebar(titles: titles, page: page, done: doneFlags)
            VStack(spacing: 0) {
                ZStack {
                    content
                        .id(page)
                        .transition(.asymmetric(
                            insertion: .move(edge: forward ? .trailing : .leading).combined(with: .opacity),
                            removal: .move(edge: forward ? .leading : .trailing).combined(with: .opacity)))
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .clipped()
                Divider()
                footer
            }
            .background(Color(nsColor: .windowBackgroundColor))
        }
        .frame(minWidth: 780, minHeight: 560)
    }

    private var doneFlags: [Bool] {
        [page > 0, setup.status == .ready, setup.switcherRegistered && setup.inputMonitoringAllowed, page > 3, false]
    }

    @ViewBuilder
    private var content: some View {
        switch page {
        case 0: WelcomePage()
        case 1: KeyboardPage(setup: setup)
        case 2: SwitcherPage(setup: setup)
        case 3: TryPage()
        default: DonePage(setup: setup)
        }
    }

    private var footer: some View {
        HStack {
            if page > 0 && page < titles.count - 1 {
                Button("戻る") { go(page - 1) }
                    .keyboardShortcut(.cancelAction)
            }
            Spacer()
            ProgressDots(count: titles.count, page: page)
            Spacer()
            if page < titles.count - 1 {
                Button(nextTitle) { go(page + 1) }
                    .buttonStyle(.borderedProminent)
                    .tint(accent)
                    .keyboardShortcut(.defaultAction)
            } else {
                Button("設定を開いて終わる") { onFinish() }
                    .buttonStyle(.borderedProminent)
                    .tint(accent)
                    .keyboardShortcut(.defaultAction)
            }
        }
        .controlSize(.large)
        .padding(.horizontal, 24)
        .padding(.vertical, 14)
    }

    private var nextTitle: String {
        switch page {
        case 0: "はじめる"
        case 1 where setup.status != .ready: "あとで追加する"
        case 2 where !setup.switcherRegistered: "使わずに次へ"
        default: "次へ"
        }
    }

    private func go(_ target: Int) {
        forward = target > page
        withAnimation(.spring(response: 0.45, dampingFraction: 0.86)) { page = target }
    }
}

// MARK: - Frame

private struct Sidebar: View {
    let titles: [String]
    let page: Int
    let done: [Bool]

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 10) {
                Image(nsImage: NSApp.applicationIconImage).resizable().frame(width: 36, height: 36)
                Text("Astelio").font(.system(size: 20, weight: .semibold)).foregroundStyle(.white)
            }
            .padding(.bottom, 22)
            ForEach(Array(titles.enumerated()), id: \.offset) { index, title in
                HStack(spacing: 10) {
                    ZStack {
                        Circle()
                            .fill(index == page ? accent : done[index] ? accent.opacity(0.55) : Color.white.opacity(0.12))
                        if done[index] && index != page {
                            Image(systemName: "checkmark").font(.system(size: 10, weight: .bold))
                                .transition(.scale.combined(with: .opacity))
                        } else {
                            Text("\(index + 1)").font(.system(size: 11, weight: .semibold))
                        }
                    }
                    .frame(width: 22, height: 22)
                    .foregroundStyle(.white)
                    Text(title)
                        .font(.system(size: 13, weight: index == page ? .semibold : .regular))
                        .foregroundStyle(index == page ? .white : Color(white: 0.7))
                }
                .padding(.vertical, 7)
                .padding(.horizontal, 10)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(RoundedRectangle(cornerRadius: 8).fill(index == page ? Color.white.opacity(0.08) : .clear))
                .animation(.easeInOut(duration: 0.25), value: page)
                .animation(.spring(response: 0.4, dampingFraction: 0.6), value: done[index])
            }
            Spacer()
            Text("変換も履歴も、この Mac の中だけで動きます。")
                .font(.caption).foregroundStyle(Color(white: 0.6))
                .fixedSize(horizontal: false, vertical: true)
        }
        .padding(20)
        .frame(width: 210)
        .frame(maxHeight: .infinity)
        .background(ink)
    }
}

private struct ProgressDots: View {
    let count: Int
    let page: Int

    var body: some View {
        HStack(spacing: 6) {
            ForEach(0..<count, id: \.self) { index in
                Capsule()
                    .fill(index == page ? accent : Color.secondary.opacity(0.3))
                    .frame(width: index == page ? 22 : 7, height: 7)
            }
        }
        .animation(.spring(response: 0.35, dampingFraction: 0.7), value: page)
    }
}

/// Title, the animation, what to do, and a box saying why.
private struct PageLayout<Hero: View, Actions: View>: View {
    let title: String
    let lead: String
    let why: String
    @ViewBuilder var hero: Hero
    @ViewBuilder var actions: Actions
    @State private var shown = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                hero
                    .frame(maxWidth: .infinity)
                    .frame(height: 170)
                    .background(RoundedRectangle(cornerRadius: 14).fill(ink.opacity(0.92)))
                Text(title).font(.system(size: 22, weight: .semibold))
                Text(lead).font(.body).fixedSize(horizontal: false, vertical: true)
                actions
                HStack(alignment: .top, spacing: 10) {
                    Image(systemName: "lightbulb.fill").foregroundStyle(.yellow)
                    VStack(alignment: .leading, spacing: 4) {
                        Text("なぜ必要？").font(.callout.weight(.semibold))
                        Text(why).font(.callout).foregroundStyle(.secondary)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
                .padding(12)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(RoundedRectangle(cornerRadius: 10).fill(Color.yellow.opacity(0.09)))
            }
            .padding(28)
            .opacity(shown ? 1 : 0)
            .offset(y: shown ? 0 : 12)
            .onAppear { withAnimation(.easeOut(duration: 0.4).delay(0.1)) { shown = true } }
        }
    }
}

private struct StatusLine: View {
    let ok: Bool
    let okText: String
    let waitingText: String

    var body: some View {
        HStack(spacing: 8) {
            Image(systemName: ok ? "checkmark.circle.fill" : "circle.dotted")
                .foregroundStyle(ok ? accent : .secondary)
                .symbolEffect(.pulse, options: .repeating, isActive: !ok)
                .contentTransition(.symbolEffect(.replace))
            Text(ok ? okText : waitingText).foregroundStyle(ok ? .primary : .secondary)
        }
        .font(.callout)
        .animation(.spring(response: 0.4, dampingFraction: 0.6), value: ok)
    }
}

/// A key cap that lights up while `pressed`.
private struct KeyCap: View {
    let label: String
    var width: CGFloat = 54
    var pressed = false

    var body: some View {
        Text(label)
            .font(.system(size: 14, weight: .medium, design: .rounded))
            .lineLimit(1)
            .minimumScaleFactor(0.7)
            .foregroundStyle(pressed ? ink : .white)
            .frame(width: width, height: 40)
            .background(RoundedRectangle(cornerRadius: 7).fill(pressed ? accent : Color.white.opacity(0.14)))
            .overlay(RoundedRectangle(cornerRadius: 7).stroke(Color.white.opacity(0.25)))
            .scaleEffect(pressed ? 0.92 : 1)
            .shadow(color: pressed ? accent.opacity(0.7) : .clear, radius: 10)
    }
}

// MARK: - Pages

private struct WelcomePage: View {
    var body: some View {
        PageLayout(
            title: "Astelio へようこそ",
            lead: "ローマ字で打って Space で変換、Return で確定する日本語入力です。このウィザードで、使い始めるまでの設定を順番に済ませます（2〜3 分）。",
            why: "macOS では、日本語入力は「入力ソース」として OS に登録し、利用者が自分でオンにする仕組みです。インストーラーはアプリを置くところまでしかできないので、残りの数ステップをここで一緒に行います。"
        ) {
            ConversionDemo()
        } actions: {
            EmptyView()
        }
    }
}

/// Types a reading, turns it into kana, then shows the candidates and the converted word, over and over.
private struct ConversionDemo: View {
    private let frames: [(String, [String])] = [
        ("n", []), ("ni", []), ("にh", []), ("にほ", []), ("にほn", []), ("にほんg", []), ("にほんご", []),
        ("にほんご", ["日本語", "にほんご", "ニホンゴ"]), ("日本語", []),
    ]

    var body: some View {
        TimelineView(.periodic(from: .now, by: 0.42)) { context in
            let step = Int(context.date.timeIntervalSinceReferenceDate / 0.42) % (frames.count + 3)
            let frame = frames[min(step, frames.count - 1)]
            VStack(spacing: 10) {
                HStack(spacing: 2) {
                    Text(frame.0)
                        .font(.system(size: 34, weight: .medium))
                        .foregroundStyle(.white)
                        .underline(step < frames.count - 1, color: accent)
                        .contentTransition(.interpolate)
                    Rectangle().fill(accent).frame(width: 2, height: 34)
                        .opacity(step % 2 == 0 ? 1 : 0.2)
                }
                HStack(spacing: 8) {
                    ForEach(Array(frame.1.enumerated()), id: \.offset) { index, word in
                        Text("\(index + 1) \(word)")
                            .font(.system(size: 14))
                            .foregroundStyle(index == 0 ? ink : .white)
                            .padding(.horizontal, 10).padding(.vertical, 5)
                            .background(RoundedRectangle(cornerRadius: 6).fill(index == 0 ? accent : Color.white.opacity(0.12)))
                            .transition(.scale(scale: 0.6).combined(with: .opacity))
                    }
                }
                .frame(height: 30)
                Text(step >= frames.count - 1 ? "Return で確定" : step >= 7 ? "Space で変換" : "ローマ字で入力")
                    .font(.caption).foregroundStyle(Color(white: 0.7))
            }
            .animation(.spring(response: 0.3, dampingFraction: 0.75), value: step)
        }
    }
}

private struct KeyboardPage: View {
    @ObservedObject var setup: SetupModel

    var body: some View {
        PageLayout(
            title: "キーボードに Astelio を追加",
            lead: "まず［自動で追加］を押してください。macOS が許さないときは、システム設定が開くので、上のアニメーションの順に「Astelio」を追加します。追加できると、下の表示が緑のチェックに変わります。",
            why: "どの入力ソースを使うかは、利用者が決めることになっています。そのため macOS は、入力ソースのオン・オフをシステム設定の［キーボード］で管理します。ここで追加すると、メニューバーの入力メニューに「Astelio」が出て、選べるようになります。"
        ) {
            SettingsPathDemo()
        } actions: {
            VStack(alignment: .leading, spacing: 10) {
                HStack {
                    Button("自動で追加") { setup.turnOn() }
                        .buttonStyle(.borderedProminent).tint(accent)
                        .disabled(setup.status == .ready)
                    Button("システム設定を開く") { setup.openKeyboardSettings() }
                }
                StatusLine(ok: setup.status == .ready,
                           okText: "追加されました。入力メニューで「Astelio」を選べます。",
                           waitingText: "まだ追加されていません（追加されると自動で気づきます）")
                if let message = setup.message {
                    Text(message).font(.caption).foregroundStyle(.orange)
                }
            }
        }
    }
}

/// The clicks in System Settings, lit one after another.
private struct SettingsPathDemo: View {
    private let steps = ["キーボード", "テキスト入力 の［編集…］", "［＋］", "日本語", "Astelio", "［追加］"]

    var body: some View {
        TimelineView(.periodic(from: .now, by: 0.8)) { context in
            let lit = Int(context.date.timeIntervalSinceReferenceDate / 0.8) % (steps.count + 2)
            VStack(spacing: 14) {
                HStack(spacing: 6) {
                    Image(systemName: "gearshape.fill").foregroundStyle(Color(white: 0.75))
                    Text("システム設定").font(.callout).foregroundStyle(Color(white: 0.75))
                }
                FlowRow(spacing: 6) {
                    ForEach(Array(steps.enumerated()), id: \.offset) { index, step in
                        HStack(spacing: 6) {
                            Text(step)
                                .font(.system(size: 13, weight: index == lit ? .semibold : .regular))
                                .foregroundStyle(index == lit ? ink : index < lit ? .white : Color(white: 0.55))
                                .padding(.horizontal, 10).padding(.vertical, 6)
                                .background(RoundedRectangle(cornerRadius: 6)
                                    .fill(index == lit ? accent : index < lit ? accent.opacity(0.3) : Color.white.opacity(0.08)))
                                .scaleEffect(index == lit ? 1.08 : 1)
                            if index < steps.count - 1 {
                                Image(systemName: "chevron.right").font(.caption2).foregroundStyle(Color(white: 0.5))
                            }
                        }
                    }
                }
                .padding(.horizontal, 20)
            }
            .animation(.spring(response: 0.45, dampingFraction: 0.7), value: lit)
        }
    }
}

/// Lays children left to right, wrapping to the next line.
private struct FlowRow: Layout {
    var spacing: CGFloat = 6

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let width = proposal.width ?? 600
        var x: CGFloat = 0, y: CGFloat = 0, line: CGFloat = 0, widest: CGFloat = 0
        for subview in subviews {
            let size = subview.sizeThatFits(.unspecified)
            if x > 0 && x + size.width > width {
                y += line + spacing
                x = 0
                line = 0
            }
            x += size.width + spacing
            widest = max(widest, x - spacing)
            line = max(line, size.height)
        }
        return CGSize(width: widest, height: y + line)
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        var x = bounds.minX, y = bounds.minY, line: CGFloat = 0
        for subview in subviews {
            let size = subview.sizeThatFits(.unspecified)
            if x > bounds.minX && x + size.width > bounds.maxX {
                y += line + spacing
                x = bounds.minX
                line = 0
            }
            subview.place(at: CGPoint(x: x, y: y), proposal: ProposedViewSize(size))
            x += size.width + spacing
            line = max(line, size.height)
        }
    }
}

private struct SwitcherPage: View {
    @ObservedObject var setup: SetupModel

    var body: some View {
        PageLayout(
            title: "左右の ⌘ で、英語と日本語を切り替える",
            lead: "左の ⌘ を 1 回押して離すと英語（ABC）、右の ⌘ を 1 回押して離すと Astelio になります。⌘C などのショートカットでは切り替わりません。［有効にする］を押し、macOS の確認で「入力監視」を許可してください。",
            why: "macOS は ⌘ や Shift だけの押し離しを入力メソッドに伝えません。そこで、ログイン時に小さな常駐処理を起動し、修飾キーが単独で押されたかだけを見ます。これに「入力監視」の許可が要ります。打った文字を記録したり、送信したりはしません。使わない場合は飛ばして構いません（メニューバーからいつでも切り替えられます）。"
        ) {
            CommandTapDemo()
        } actions: {
            VStack(alignment: .leading, spacing: 10) {
                HStack {
                    if setup.switcherRegistered {
                        Button("止める") { setup.disableSwitcher() }
                    } else {
                        Button("有効にする") { setup.enableSwitcher() }
                            .buttonStyle(.borderedProminent).tint(accent)
                    }
                    Button("入力監視の設定を開く") { setup.openInputMonitoringSettings() }
                }
                StatusLine(ok: setup.switcherRegistered, okText: "常駐処理: 動いています",
                           waitingText: "常駐処理: まだ有効になっていません")
                StatusLine(ok: setup.inputMonitoringAllowed, okText: "入力監視: 許可されています",
                           waitingText: "入力監視: 未許可（［プライバシーとセキュリティ］→［入力監視］で Astelio をオン）")
                if setup.switcherNeedsApproval {
                    Text("［ログイン項目］で Astelio の許可が必要です。").font(.caption).foregroundStyle(.orange)
                }
            }
        }
    }
}

/// Left ⌘ then right ⌘, with the input menu in a small menu bar following along.
private struct CommandTapDemo: View {
    var body: some View {
        TimelineView(.periodic(from: .now, by: 0.9)) { context in
            let phase = Int(context.date.timeIntervalSinceReferenceDate / 0.9) % 4
            let leftDown = phase == 0
            let rightDown = phase == 2
            let japanese = phase >= 2
            VStack(spacing: 16) {
                HStack(spacing: 8) {
                    Spacer()
                    Text(japanese ? "あ" : "A")
                        .font(.system(size: 13, weight: .bold))
                        .foregroundStyle(ink)
                        .frame(width: 22, height: 18)
                        .background(RoundedRectangle(cornerRadius: 4).fill(.white))
                        .contentTransition(.numericText())
                    Text(japanese ? "Astelio" : "ABC")
                        .font(.system(size: 12)).foregroundStyle(.white)
                        .contentTransition(.numericText())
                        .frame(width: 72, alignment: .leading)
                }
                .padding(.horizontal, 14).padding(.vertical, 5)
                .background(Color.white.opacity(0.08))
                HStack(spacing: 8) {
                    KeyCap(label: "option", width: 62)
                    KeyCap(label: "⌘", width: 64, pressed: leftDown)
                    KeyCap(label: "", width: 160)
                    KeyCap(label: "⌘", width: 64, pressed: rightDown)
                    KeyCap(label: "option", width: 62)
                }
                Text(leftDown || phase == 1 ? "左 ⌘ をポン → 英語" : "右 ⌘ をポン → 日本語")
                    .font(.caption).foregroundStyle(Color(white: 0.75))
                    .contentTransition(.opacity)
            }
            .animation(.spring(response: 0.3, dampingFraction: 0.6), value: phase)
        }
    }
}

private struct TryPage: View {
    @State private var text = ""

    var body: some View {
        PageLayout(
            title: "使ってみる",
            lead: "メニューバーの入力メニューで「Astelio」を選ぶか、右 ⌘ を押してから、下の欄に入力してみてください。",
            why: "設定がすべて効いているかを、この場で確かめられます。うまく変換されないときは、入力メニューで「Astelio」が選ばれているか確認してください。"
        ) {
            KeyTips()
        } actions: {
            TextField("ここに入力して試す（例: kyouhaiitenkidesune）", text: $text, axis: .vertical)
                .textFieldStyle(.roundedBorder)
                .font(.system(size: 16))
                .lineLimit(2...4)
        }
    }
}

private struct KeyTips: View {
    private let tips: [(String, String)] = [
        ("Space", "変換・次の候補"), ("Return", "確定"), ("Shift + 英字", "英字を混ぜる"),
        ("Shift", "かなに戻る"), ("えもじ", "絵文字パレット"),
    ]

    var body: some View {
        TimelineView(.periodic(from: .now, by: 1.2)) { context in
            let lit = Int(context.date.timeIntervalSinceReferenceDate / 1.2) % tips.count
            HStack(spacing: 8) {
                ForEach(Array(tips.enumerated()), id: \.offset) { index, tip in
                    VStack(spacing: 8) {
                        KeyCap(label: tip.0, width: 88, pressed: index == lit)
                        Text(tip.1).font(.caption)
                            .foregroundStyle(index == lit ? .white : Color(white: 0.6))
                    }
                }
            }
            .animation(.spring(response: 0.35, dampingFraction: 0.6), value: lit)
        }
    }
}

private struct DonePage: View {
    @ObservedObject var setup: SetupModel
    @State private var burst = false

    var body: some View {
        PageLayout(
            title: setup.status == .ready ? "準備ができました" : "あと少しです",
            lead: setup.status == .ready
                ? "これで Astelio を使えます。細かい設定（入力の補助、日付の形、履歴、ユーザー辞書）は設定ウインドウで変えられます。"
                : "キーボードへの追加がまだです。設定ウインドウの［はじめに］から、いつでも続きを行えます。",
            why: "設定ウインドウは、入力メニューの「Astelio の設定を開く…」や、アプリを開き直すことで、いつでも表示できます。"
        ) {
            ZStack {
                ForEach(0..<12, id: \.self) { index in
                    Circle()
                        .fill(index % 2 == 0 ? accent : Color.yellow)
                        .frame(width: 8, height: 8)
                        .offset(x: burst ? 90 : 0)
                        .rotationEffect(.degrees(Double(index) * 30))
                        .opacity(burst ? 0 : 1)
                }
                Image(systemName: setup.status == .ready ? "checkmark.circle.fill" : "hourglass.circle.fill")
                    .font(.system(size: 76))
                    .foregroundStyle(setup.status == .ready ? accent : .orange)
                    .scaleEffect(burst ? 1 : 0.3)
            }
            .onAppear {
                withAnimation(.spring(response: 0.6, dampingFraction: 0.55)) { burst = true }
            }
        } actions: {
            EmptyView()
        }
    }
}
