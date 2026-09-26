#include "astelio/user_dictionary.h"
#include "astelio/user_dictionary_io.h"
#include "astelio/utf.h"

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace astelio {
namespace {

using Pos = UserDictionary::PartOfSpeech;
using Word = UserDictionary::Word;

std::string ReadSample(std::string_view name)
{
    std::ifstream in(std::string(ASTELIO_TEST_DATA_DIR) + "/user_dictionary/" + std::string(name), std::ios::binary);
    EXPECT_TRUE(in.good()) << name;
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::u16string Decode(std::string_view bytes)
{
    const auto text = DecodeText(bytes);
    EXPECT_TRUE(text.has_value());
    return text.value_or(u"");
}

// One line per word, so a mismatch shows which word differs.
std::string Describe(const std::vector<Word>& words)
{
    std::string out;
    for (const auto& word : words) {
        out += Utf16ToUtf8(word.reading) + " | " + Utf16ToUtf8(word.surface) + " | " +
               Utf16ToUtf8(UserDictionary::PartOfSpeechName(word.pos)) + " | " + Utf16ToUtf8(word.comment) + "\n";
    }
    return out;
}

// T-D03-1: every word of each format comes in, with the part of speech of the table in the test plan.
TEST(UserDictionaryIo, ImportsGoogleIme)
{
    const auto result = ImportUserDictionary(Decode(ReadSample("google_ime.txt")));
    EXPECT_EQ(result.format, UserDictionaryFormat::GoogleIme);
    EXPECT_EQ(Describe(result.words), Describe({
                                          {u"あすてりお", u"Astelio", Pos::ProperNoun, u"入力システムの名前"},
                                          {u"ふじわら", u"藤原", Pos::PersonName, u""},
                                          {u"はなこ", u"花子", Pos::PersonName, u""},
                                          {u"みなとみらい", u"みなとみらい", Pos::PlaceName, u""},
                                          {u"あすてりおぶ", u"Astelio部", Pos::Organization, u""},
                                          {u"おつ", u"お疲れさまです", Pos::Abbreviation, u"あいさつ"},
                                          {u"やじるし", u"→", Pos::Symbol, u""},
                                          {u"にこ", u"(^_^)", Pos::Symbol, u""},
                                          {u"かえる", u"蛙", Pos::Suppressed, u""},
                                          {u"ぎじゅつしょ", u"技術書", Pos::Noun, u""},
                                          {u"てすと", u"テスト", Pos::Noun, u""},
                                          {u"ぐぐる", u"ググる", Pos::Noun, u""},
                                          {u"えもい", u"エモい", Pos::Noun, u""},
                                      }));
    EXPECT_EQ(result.skipped, 0u);
}

TEST(UserDictionaryIo, ImportsMsIme)
{
    // MS-IME writes UTF-16LE with a BOM.
    const auto bytes = EncodeText(Decode(ReadSample("ms_ime.txt")), TextEncoding::Utf16Le);
    ASSERT_TRUE(bytes.has_value());
    EXPECT_EQ(DetectEncoding(*bytes), TextEncoding::Utf16Le);
    const auto result = ImportUserDictionary(Decode(*bytes));
    EXPECT_EQ(result.format, UserDictionaryFormat::MsIme);
    EXPECT_EQ(Describe(result.words), Describe({
                                          {u"あすてりお", u"Astelio", Pos::ProperNoun, u""},
                                          {u"ふじわら", u"藤原", Pos::PersonName, u""},
                                          {u"たろう", u"太郎", Pos::PersonName, u""},
                                          {u"あすてりお", u"アステリオ", Pos::PersonName, u"読みの確認用"},
                                          {u"よこはま", u"横浜", Pos::PlaceName, u""},
                                          {u"おつ", u"お疲れさまです", Pos::Abbreviation, u""},
                                          {u"にこにこ", u"(^_^)", Pos::Symbol, u""},
                                          {u"かえる", u"蛙", Pos::Suppressed, u""},
                                          {u"ぎじゅつしょ", u"技術書", Pos::Noun, u""},
                                          {u"てすと", u"テスト", Pos::Noun, u""},
                                          {u"しずか", u"静か", Pos::Noun, u""},
                                          {u"ぐぐる", u"ググる", Pos::Noun, u""},
                                      }));
    EXPECT_EQ(result.skipped, 0u);
}

TEST(UserDictionaryIo, ImportsAtok)
{
    const auto result = ImportUserDictionary(Decode(ReadSample("atok.txt")));
    EXPECT_EQ(result.format, UserDictionaryFormat::Atok);
    EXPECT_EQ(Describe(result.words), Describe({
                                          {u"あすてりお", u"Astelio", Pos::ProperNoun, u""},
                                          {u"ふじわら", u"藤原", Pos::PersonName, u""},
                                          {u"はなこ", u"花子", Pos::PersonName, u""},
                                          {u"あすてりお", u"アステリオ", Pos::PersonName, u""},
                                          {u"よこはま", u"横浜", Pos::PlaceName, u""},
                                          {u"あすてりおぶ", u"Astelio部", Pos::Organization, u""},
                                          {u"おつ", u"お疲れさまです", Pos::Abbreviation, u""},
                                          {u"にこにこ", u"(^_^)", Pos::Symbol, u""},
                                          {u"ぎじゅつしょ", u"技術書", Pos::Noun, u""},
                                          {u"てすと", u"テスト", Pos::Noun, u""},
                                          {u"ぐぐる", u"ググる", Pos::Noun, u""},
                                      }));
    EXPECT_EQ(result.skipped, 0u);
}

TEST(UserDictionaryIo, ImportsKotoeri)
{
    const auto result = ImportUserDictionary(Decode(ReadSample("kotoeri.txt")));
    EXPECT_EQ(result.format, UserDictionaryFormat::Kotoeri);
    EXPECT_EQ(Describe(result.words), Describe({
                                          {u"あすてりお", u"Astelio", Pos::ProperNoun, u""},
                                          {u"ふじわら", u"藤原", Pos::PersonName, u""},
                                          {u"よこはま", u"横浜", Pos::PlaceName, u""},
                                          {u"ぎじゅつしょ", u"技術書", Pos::Noun, u""},
                                          {u"てすと", u"テスト", Pos::Noun, u""},
                                          {u"しずか", u"静か", Pos::Noun, u""},
                                          {u"ぐぐる", u"ググる", Pos::Noun, u""},
                                          {u"かっこ", u"\"引用\",です", Pos::Noun, u""},
                                          {u"あいう", u"あいう", Pos::Noun, u""},
                                      }));
    EXPECT_EQ(result.skipped, 0u);
}

TEST(UserDictionaryIo, ImportsAstelioJson)
{
    const auto result = ImportUserDictionary(Decode(ReadSample("astelio.json")));
    EXPECT_EQ(result.format, UserDictionaryFormat::AstelioJson);
    EXPECT_EQ(Describe(result.words), Describe({
                                          {u"あすてりお", u"Astelio", Pos::ProperNoun, u"入力システムの名前"},
                                          {u"ふじわら", u"藤原", Pos::PersonName, u""},
                                          {u"よこはま", u"横浜", Pos::PlaceName, u""},
                                          {u"あすてりおぶ", u"Astelio部", Pos::Organization, u""},
                                          {u"おつ", u"お疲れさまです", Pos::Abbreviation, u"あいさつ"},
                                          {u"やじるし", u"→", Pos::Symbol, u""},
                                          {u"", u"鰐", Pos::Suppressed, u"どの読みでも出さない"},
                                          {u"かっこ", u"\"引用\"\\です", Pos::Noun, u""},
                                          {u"えもじ", u"\U0001F600", Pos::Symbol, u""},
                                      }));
    EXPECT_EQ(result.skipped, 0u);
}

TEST(UserDictionaryIo, KatakanaReadingsBecomeHiragana)
{
    const auto result = ImportUserDictionary(u"アステリオ\tAstelio\t固有名詞\nヴぁいおりん\tヴァイオリン\t名詞\n");
    EXPECT_EQ(Describe(result.words), Describe({
                                          {u"あすてりお", u"Astelio", Pos::ProperNoun, u""},
                                          {u"ゔぁいおりん", u"ヴァイオリン", Pos::Noun, u""},
                                      }));
}

TEST(UserDictionaryIo, UnrecognizedTextImportsNothing)
{
    for (const std::u16string_view text : {
             std::u16string_view(u""),
             std::u16string_view(u"\r\n\r\n"),
             std::u16string_view(u"hello world\n"),
             std::u16string_view(u"{\"version\": 1, \"words\": ["),
             std::u16string_view(u"{\"version\": 2, \"words\": []}"),
             std::u16string_view(u"{\"words\": []}"),
             std::u16string_view(u"[1, 2]"),
         }) {
        const auto result = ImportUserDictionary(text);
        EXPECT_FALSE(result.format.has_value()) << Utf16ToUtf8(text);
        EXPECT_TRUE(result.words.empty()) << Utf16ToUtf8(text);
    }
}

// T-D03-2: the encodings.
TEST(UserDictionaryIo, DetectsEncoding)
{
    using namespace std::string_view_literals;
    EXPECT_EQ(DetectEncoding(""sv), TextEncoding::Utf8);
    EXPECT_EQ(DetectEncoding("\xEF\xBB\xBF\xE3\x81\x82"sv), TextEncoding::Utf8);
    EXPECT_EQ(DetectEncoding("\xE3\x81\x82\t\x41"sv), TextEncoding::Utf8);
    EXPECT_EQ(DetectEncoding("\xFF\xFE\x42\x30\x09\x00"sv), TextEncoding::Utf16Le);
    EXPECT_EQ(DetectEncoding("\xFE\xFF\x30\x42\x00\x09"sv), TextEncoding::Utf16Be);
    EXPECT_EQ(DetectEncoding("\x42\x30\x09\x00\x41\x00"sv), TextEncoding::Utf16Le) << "no BOM";
    EXPECT_EQ(DetectEncoding("\x30\x42\x00\x09\x00\x41"sv), TextEncoding::Utf16Be) << "no BOM";
    // あいう<TAB>亜伊宇 in Shift_JIS.
    EXPECT_EQ(DetectEncoding("\x82\xA0\x82\xA2\x82\xA4\t\x88\x9F\x88\xC9\x89\x46"sv), TextEncoding::ShiftJis);
}

TEST(UserDictionaryIo, DecodesAndEncodesText)
{
    using namespace std::string_literals;
    using namespace std::string_view_literals;
    const std::u16string text = u"あ\tA\U0001F600";
    const std::string utf8 = "\xE3\x81\x82\x09\x41\xF0\x9F\x98\x80";
    const std::string utf16le = "\xFF\xFE\x42\x30\x09\x00\x41\x00\x3D\xD8\x00\xDE"s;
    const std::string utf16be = "\xFE\xFF\x30\x42\x00\x09\x00\x41\xD8\x3D\xDE\x00"s;

    EXPECT_EQ(DecodeText(utf8), text);
    EXPECT_EQ(DecodeText("\xEF\xBB\xBF" + utf8), text);
    EXPECT_EQ(DecodeText(utf16le), text);
    EXPECT_EQ(DecodeText(utf16be), text);
    EXPECT_EQ(DecodeText(utf16le.substr(2)), text) << "UTF-16LE without a BOM";

    EXPECT_EQ(EncodeText(text, TextEncoding::Utf8), utf8);
    EXPECT_EQ(EncodeText(text, TextEncoding::Utf16Le), utf16le);
    EXPECT_EQ(EncodeText(text, TextEncoding::Utf16Be), utf16be);
    EXPECT_FALSE(EncodeText(text, TextEncoding::ShiftJis).has_value());

    EXPECT_FALSE(DecodeText("\x82\xA0\x82\xA2\t\x88\x9F"sv).has_value()) << "Shift_JIS is left to the platform";
    EXPECT_FALSE(DecodeText("\xEF\xBB\xBF\xE3\x81"sv).has_value()) << "truncated UTF-8";
    EXPECT_FALSE(DecodeText("\xFF\xFE\x42\x30\x09"sv).has_value()) << "odd UTF-16 length";
    EXPECT_FALSE(DecodeText("\xFF\xFE\x3D\xD8\x41\x00"sv).has_value()) << "unpaired surrogate";
}

TEST(UserDictionaryIo, EveryEncodingReadsTheSameWords)
{
    for (const auto* name : {"google_ime.txt", "ms_ime.txt", "atok.txt", "kotoeri.txt", "astelio.json"}) {
        const auto text = Decode(ReadSample(name));
        const auto expected = ImportUserDictionary(text);
        ASSERT_FALSE(expected.words.empty()) << name;
        for (const auto encoding : {TextEncoding::Utf8, TextEncoding::Utf16Le, TextEncoding::Utf16Be}) {
            auto bytes = EncodeText(text, encoding);
            ASSERT_TRUE(bytes.has_value());
            if (encoding == TextEncoding::Utf8) {
                bytes->insert(0, "\xEF\xBB\xBF");
            }
            EXPECT_EQ(DetectEncoding(*bytes), encoding) << name;
            const auto result = ImportUserDictionary(Decode(*bytes));
            EXPECT_EQ(result.format, expected.format) << name;
            EXPECT_EQ(Describe(result.words), Describe(expected.words)) << name;
        }
    }
}

TEST(UserDictionaryIo, SkipsMalformedLines)
{
    const std::u16string too_long(UserDictionary::kMaxReadingLength + 1, u'あ');
    const auto google = ImportUserDictionary(u"# comment\n"
                                             u"あすてりお\tAstelio\t固有名詞\n"
                                             u"\n"
                                             u"たりない\t足りない\n"
                                             u"ひょうきなし\t\t名詞\n"
                                             u"\t読みなし\t名詞\n"
                                             u"なぞ\t謎\t謎の品詞\n"
                                             u"ひんしなし\t品詞なし\t\n" +
                                             too_long + u"\t長い\t名詞\n" + u"ぎじゅつしょ\t技術書\t名詞\r\n");
    EXPECT_EQ(google.format, UserDictionaryFormat::GoogleIme);
    EXPECT_EQ(Describe(google.words), Describe({
                                          {u"あすてりお", u"Astelio", Pos::ProperNoun, u""},
                                          {u"ぎじゅつしょ", u"技術書", Pos::Noun, u""},
                                      }));
    EXPECT_EQ(google.skipped, 6u);

    const auto msime = ImportUserDictionary(u"!Microsoft IME Dictionary Tool\r\n"
                                            u"!Format:WORDLIST\r\n"
                                            u"あすてりお\tAstelio\t固有名詞\r\n"
                                            u"ひとつだけ\r\n"
                                            u"なぞ\t謎\t謎の品詞\r\n");
    EXPECT_EQ(msime.format, UserDictionaryFormat::MsIme);
    EXPECT_EQ(msime.words.size(), 1u);
    EXPECT_EQ(msime.skipped, 2u);

    const auto atok = ImportUserDictionary(u"!!ATOK_TANGO_TEXT_HEADER_1\r\n"
                                           u"あすてりお\tAstelio\t固有一般\r\n"
                                           u"よみだけ\r\n"
                                           u"かえる\t蛙\t抑制単語\r\n");
    EXPECT_EQ(atok.format, UserDictionaryFormat::Atok);
    EXPECT_EQ(atok.words.size(), 2u);
    EXPECT_EQ(atok.skipped, 1u);

    // Old Mac text ends lines with CR only.
    const auto kotoeri = ImportUserDictionary(u"\"あすてりお\",\"Astelio\",\"固有名詞\"\r"
                                              u"\"とじない\",\"閉じない,\"普通名詞\"\r"
                                              u"\"ふたつ\",\"二つ\"\r"
                                              u"\"ぎじゅつしょ\",\"技術書\",\"普通名詞\"\r");
    EXPECT_EQ(kotoeri.format, UserDictionaryFormat::Kotoeri);
    EXPECT_EQ(Describe(kotoeri.words), Describe({
                                           {u"あすてりお", u"Astelio", Pos::ProperNoun, u""},
                                           {u"ぎじゅつしょ", u"技術書", Pos::Noun, u""},
                                       }));
    EXPECT_EQ(kotoeri.skipped, 2u);

    const auto json = ImportUserDictionary(uR"({"version": 1, "words": [
        {"reading": "あすてりお", "surface": "Astelio", "pos": "固有名詞"},
        "not a word",
        {"reading": "ひょうきなし", "pos": "名詞"},
        {"reading": "なぞ", "surface": "謎", "pos": "謎の品詞"},
        {"reading": "かず", "surface": 1, "pos": "名詞"},
        {"reading": "かいぎょう", "surface": "一行目\n二行目", "pos": "名詞"},
        {"reading": "ぎじゅつしょ", "surface": "技術書"}
    ]})");
    EXPECT_EQ(json.format, UserDictionaryFormat::AstelioJson);
    EXPECT_EQ(Describe(json.words), Describe({
                                        {u"あすてりお", u"Astelio", Pos::ProperNoun, u""},
                                        {u"ぎじゅつしょ", u"技術書", Pos::Noun, u""},
                                    }));
    EXPECT_EQ(json.skipped, 5u);
}

// Written words come back when read again; parts of speech a format cannot hold change as in the table.
std::vector<Word> AllKinds()
{
    return {
        {u"あすてりお", u"Astelio", Pos::ProperNoun, u"入力システム"},
        {u"ふじわら", u"藤原", Pos::PersonName, u""},
        {u"よこはま", u"横浜", Pos::PlaceName, u""},
        {u"あすてりおぶ", u"Astelio部", Pos::Organization, u""},
        {u"おつ", u"お疲れさまです", Pos::Abbreviation, u"あいさつ"},
        {u"やじるし", u"→", Pos::Symbol, u""},
        {u"かえる", u"蛙", Pos::Suppressed, u""},
        {u"", u"鰐", Pos::Suppressed, u""},
        {u"ぎじゅつしょ", u"技術書", Pos::Noun, u"本"},
        {u"かっこ", u"\"引用\",です", Pos::Noun, u""},
        {u"えすけーぷ", u"a\\b/\x01", Pos::Noun, u"\"x\""},
    };
}

std::vector<Word> RoundTrip(UserDictionaryFormat format)
{
    const auto bytes = EncodeText(ExportUserDictionary(AllKinds(), format), ExportEncoding(format));
    EXPECT_TRUE(bytes.has_value());
    const auto result = ImportUserDictionary(Decode(bytes.value_or("")));
    EXPECT_EQ(result.format, format);
    EXPECT_EQ(result.skipped, 0u);
    return result.words;
}

TEST(UserDictionaryIo, ExportEncodings)
{
    EXPECT_EQ(ExportEncoding(UserDictionaryFormat::GoogleIme), TextEncoding::Utf8);
    EXPECT_EQ(ExportEncoding(UserDictionaryFormat::MsIme), TextEncoding::Utf16Le);
    EXPECT_EQ(ExportEncoding(UserDictionaryFormat::Atok), TextEncoding::Utf16Le);
    EXPECT_EQ(ExportEncoding(UserDictionaryFormat::Kotoeri), TextEncoding::Utf8);
    EXPECT_EQ(ExportEncoding(UserDictionaryFormat::AstelioJson), TextEncoding::Utf8);
}

TEST(UserDictionaryIo, GoogleImeRoundTrip)
{
    EXPECT_EQ(Describe(RoundTrip(UserDictionaryFormat::GoogleIme)), Describe(AllKinds()));
}

TEST(UserDictionaryIo, AstelioJsonRoundTrip)
{
    EXPECT_EQ(Describe(RoundTrip(UserDictionaryFormat::AstelioJson)), Describe(AllKinds()));
}

TEST(UserDictionaryIo, MsImeRoundTrip)
{
    auto expected = AllKinds();
    expected[3].pos = Pos::ProperNoun; // no 組織 in MS-IME
    EXPECT_EQ(Describe(RoundTrip(UserDictionaryFormat::MsIme)), Describe(expected));
}

TEST(UserDictionaryIo, AtokRoundTrip)
{
    // No suppressed words and no comments in ATOK's text.
    EXPECT_EQ(Describe(RoundTrip(UserDictionaryFormat::Atok)), Describe({
                                                                   {u"あすてりお", u"Astelio", Pos::ProperNoun, u""},
                                                                   {u"ふじわら", u"藤原", Pos::PersonName, u""},
                                                                   {u"よこはま", u"横浜", Pos::PlaceName, u""},
                                                                   {u"あすてりおぶ", u"Astelio部", Pos::Organization, u""},
                                                                   {u"おつ", u"お疲れさまです", Pos::Abbreviation, u""},
                                                                   {u"やじるし", u"→", Pos::Symbol, u""},
                                                                   {u"ぎじゅつしょ", u"技術書", Pos::Noun, u""},
                                                                   {u"かっこ", u"\"引用\",です", Pos::Noun, u""},
                                                                   {u"えすけーぷ", u"a\\b/\x01", Pos::Noun, u""},
                                                               }));
}

TEST(UserDictionaryIo, KotoeriRoundTrip)
{
    EXPECT_EQ(Describe(RoundTrip(UserDictionaryFormat::Kotoeri)), Describe({
                                                                      {u"あすてりお", u"Astelio", Pos::ProperNoun, u""},
                                                                      {u"ふじわら", u"藤原", Pos::PersonName, u""},
                                                                      {u"よこはま", u"横浜", Pos::PlaceName, u""},
                                                                      {u"あすてりおぶ", u"Astelio部", Pos::ProperNoun, u""},
                                                                      {u"おつ", u"お疲れさまです", Pos::Noun, u""},
                                                                      {u"やじるし", u"→", Pos::Noun, u""},
                                                                      {u"ぎじゅつしょ", u"技術書", Pos::Noun, u""},
                                                                      {u"かっこ", u"\"引用\",です", Pos::Noun, u""},
                                                                      {u"えすけーぷ", u"a\\b/\x01", Pos::Noun, u""},
                                                                  }));
}

} // namespace
} // namespace astelio
