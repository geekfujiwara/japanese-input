#pragma once

#include "astelio/dictionary_builder.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace astelio::mac::testing {

// A small system dictionary written to a file, and a folder for the user's data, removed afterwards.
class TemporaryFiles {
public:
    TemporaryFiles()
    {
        std::random_device random;
        directory_ = std::filesystem::temp_directory_path() / ("astelio-mac-test-" + std::to_string(random()));
        std::filesystem::create_directories(directory_);
    }
    ~TemporaryFiles()
    {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    const std::filesystem::path& directory() const { return directory_; }
    std::filesystem::path data() const { return directory_ / "data"; }

    std::filesystem::path WriteDictionary() const
    {
        constexpr std::uint16_t kEdge = 0;
        constexpr std::uint16_t kNoun = 1;
        constexpr std::uint16_t kParticle = 2;
        constexpr std::uint16_t kIds = 3;
        ConnectionMatrix matrix;
        matrix.size = kIds;
        matrix.bos_id = kEdge;
        matrix.eos_id = kEdge;
        matrix.costs.assign(kIds * kIds, 100);
        matrix.word_types = {WordType::Edge, WordType::Content, WordType::Suffix};
        matrix.unknown_id = kNoun;
        matrix.unknown_cost = 5000;
        matrix.costs[kEdge * kIds + kNoun] = 0;
        matrix.costs[kNoun * kIds + kParticle] = 0;
        matrix.costs[kParticle * kIds + kEdge] = 0;
        DictionaryBuilder builder(std::move(matrix));
        builder.Add({u"わたし", u"私", kNoun, kNoun, 0, 300});
        builder.Add({u"わたし", u"渡し", kNoun, kNoun, 0, 900});
        builder.Add({u"は", u"は", kParticle, kParticle, 0, 50});
        builder.Add({u"にほんご", u"日本語", kNoun, kNoun, 0, 400});
        builder.Add({u"いぬ", u"犬", kNoun, kNoun, 0, 300});
        builder.Add({u"いぬ", u"🐶", kNoun, kNoun, 0, 2000});
        builder.Add({u"えがお", u"😀", kNoun, kNoun, 0, 2000});
        // 20 readings that start with かん: the predictions spread into columns too.
        const std::u16string tails = u"あいうえおかきくけこさしすせそたちつてと";
        for (std::size_t i = 0; i < tails.size(); ++i) {
            builder.Add({std::u16string(u"かん") + tails[i], std::u16string(u"予") + static_cast<char16_t>(u'Ａ' + i),
                         kNoun, kNoun, 0, static_cast<std::int16_t>(500 + i * 10)});
        }
        // A reading with 20 candidates: the list spreads into columns of 9.
        for (int i = 0; i < 20; ++i) {
            builder.Add({u"かんじ", std::u16string(u"感") + static_cast<char16_t>(u'Ａ' + i), kNoun, kNoun, 0,
                         static_cast<std::int16_t>(300 + i * 10)});
        }
        const std::vector<std::byte> bytes = builder.Build();
        const std::filesystem::path path = directory_ / "system.dic";
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return path;
    }

private:
    std::filesystem::path directory_;
};

} // namespace astelio::mac::testing
