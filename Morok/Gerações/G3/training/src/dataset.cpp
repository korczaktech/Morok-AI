#include "morok/g3/training/dataset.hpp"

#include <unicode/normalizer2.h>
#include <unicode/unistr.h>
#include <unicode/utf8.h>
#include <unicode/utf16.h>
#include <unicode/utypes.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace morok::g3::training {
namespace {

class JsonObject final {
public:
    explicit JsonObject(const std::string& line) : text_(line) {}

    std::string string(const std::string& key, bool required = true) const {
        const auto value = find_value(key);
        if (!value) {
            if (required) throw std::runtime_error("dataset field missing: " + key);
            return {};
        }
        if (*value >= text_.size() || text_[*value] != '"')
            throw std::runtime_error("dataset field must be a JSON string: " + key);
        std::size_t cursor = *value + 1U;
        std::string out;
        while (cursor < text_.size()) {
            const char ch = text_[cursor++];
            if (ch == '"') return out;
            if (ch != '\\') { out.push_back(ch); continue; }
            if (cursor >= text_.size()) throw std::runtime_error("truncated JSON escape");
            const char escaped = text_[cursor++];
            switch (escaped) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    const auto code = hex4(cursor);
                    cursor += 4U;
                    UChar32 cp = static_cast<UChar32>(code);
                    if (U16_IS_LEAD(cp)) {
                        if (cursor + 6U > text_.size() || text_.compare(cursor, 2U, "\\u") != 0)
                            throw std::runtime_error("unpaired JSON high surrogate");
                        cursor += 2U;
                        const auto low = hex4(cursor);
                        cursor += 4U;
                        if (low < 0xDC00U || low > 0xDFFFU)
                            throw std::runtime_error("invalid JSON low surrogate");
                        cp = U16_GET_SUPPLEMENTARY(cp, static_cast<UChar32>(low));
                    } else if (U16_IS_TRAIL(cp)) {
                        throw std::runtime_error("unpaired JSON low surrogate");
                    }
                    icu::UnicodeString unicode;
                    unicode.append(cp);
                    std::string utf8;
                    unicode.toUTF8String(utf8);
                    out += utf8;
                    break;
                }
                default: throw std::runtime_error("invalid JSON escape");
            }
        }
        throw std::runtime_error("unterminated JSON string");
    }

    bool boolean(const std::string& key, bool fallback = false) const {
        const auto value = find_value(key);
        if (!value) return fallback;
        if (text_.compare(*value, 4U, "true") == 0) return true;
        if (text_.compare(*value, 5U, "false") == 0) return false;
        throw std::runtime_error("dataset field must be boolean: " + key);
    }

    double number(const std::string& key, double fallback = 1.0) const {
        const auto value = find_value(key);
        if (!value) return fallback;
        std::size_t used = 0;
        const double result = std::stod(text_.substr(*value), &used);
        if (used == 0U || !std::isfinite(result))
            throw std::runtime_error("dataset field must be a finite number: " + key);
        return result;
    }

    std::vector<std::string> string_array(const std::string& key) const {
        const auto value = find_value(key);
        if (!value) return {};
        std::size_t cursor = *value;
        if (cursor >= text_.size() || text_[cursor++] != '[')
            throw std::runtime_error("dataset field must be an array: " + key);
        std::vector<std::string> result;
        while (cursor < text_.size()) {
            while (cursor < text_.size() && (text_[cursor] == ' ' || text_[cursor] == '\n' ||
                   text_[cursor] == '\r' || text_[cursor] == '\t' || text_[cursor] == ','))
                ++cursor;
            if (cursor < text_.size() && text_[cursor] == ']') return result;
            if (cursor >= text_.size() || text_[cursor] != '"')
                throw std::runtime_error("question_variants must contain strings");
            // Reuse the string decoder against a view beginning at this quote.
            JsonObject tail("{\"_value\":" + text_.substr(cursor));
            const auto decoded = tail.string("_value");
            result.push_back(decoded);
            std::size_t end = cursor + 1U;
            bool escaped = false;
            for (; end < text_.size(); ++end) {
                if (!escaped && text_[end] == '"') { ++end; break; }
                if (!escaped && text_[end] == '\\') escaped = true;
                else escaped = false;
            }
            cursor = end;
        }
        throw std::runtime_error("unterminated JSON array");
    }

private:
    std::optional<std::size_t> find_value(const std::string& key) const {
        const std::string needle = "\"" + key + "\"";
        auto position = text_.find(needle);
        if (position == std::string::npos) return std::nullopt;
        position = text_.find(':', position + needle.size());
        if (position == std::string::npos) throw std::runtime_error("invalid JSON object");
        ++position;
        while (position < text_.size() && (text_[position] == ' ' || text_[position] == '\t'))
            ++position;
        return position;
    }

    std::uint32_t hex4(std::size_t cursor) const {
        if (cursor + 4U > text_.size()) throw std::runtime_error("truncated unicode escape");
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < 4U; ++i) {
            const char ch = text_[cursor + i];
            value <<= 4U;
            if (ch >= '0' && ch <= '9') value |= static_cast<std::uint32_t>(ch - '0');
            else if (ch >= 'a' && ch <= 'f') value |= static_cast<std::uint32_t>(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F') value |= static_cast<std::uint32_t>(ch - 'A' + 10);
            else throw std::runtime_error("invalid unicode escape");
        }
        return value;
    }

    const std::string& text_;
};

std::string normalize_nfc(const std::string& input) {
    UErrorCode status = U_ZERO_ERROR;
    const auto* normalizer = icu::Normalizer2::getNFCInstance(status);
    if (U_FAILURE(status) || normalizer == nullptr)
        throw std::runtime_error("ICU NFC normalizer is unavailable");
    icu::UnicodeString source = icu::UnicodeString::fromUTF8(input);
    icu::UnicodeString normalized;
    normalizer->normalize(source, normalized, status);
    if (U_FAILURE(status)) throw std::runtime_error("NFC normalization failed");
    std::string output;
    normalized.toUTF8String(output);
    // ICU conversion substitutes malformed input; reject it instead.
    int32_t i = 0;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(input.data());
    const int32_t length = static_cast<int32_t>(input.size());
    while (i < length) {
        UChar32 cp;
        U8_NEXT(bytes, i, length, cp);
        if (cp < 0) throw std::runtime_error("dataset contains invalid UTF-8");
    }
    return output;
}

} // namespace

Dataset load_jsonl(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open dataset: " + path.string());
    Dataset dataset;
    std::unordered_set<std::string> ids;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (line.empty()) continue;
        try {
            JsonObject object(line);
            DatasetExample item;
            item.example_id = object.string("example_id");
            item.group_id = object.string("group_id");
            item.category = object.string("category");
            item.question = normalize_nfc(object.string("question"));
            item.answer = normalize_nfc(object.string("answer"));
            item.source = object.string("source");
            item.language = object.string("language");
            item.dataset_version = object.string("dataset_version");
            item.quality = object.number("quality", 1.0);
            item.sensitive = object.boolean("sensitive", false);
            item.question_variants = object.string_array("question_variants");
            if (item.example_id.empty() || item.group_id.empty() ||
                item.question.empty() || item.answer.empty() || item.source.empty() ||
                item.language.empty() || item.dataset_version.empty())
                throw std::runtime_error("required dataset fields cannot be empty");
            if (item.quality < 0.0 || item.quality > 1.0)
                throw std::runtime_error("quality must be in [0,1]");
            if (!ids.insert(item.example_id).second)
                throw std::runtime_error("duplicate example_id: " + item.example_id);
            for (auto& variant : item.question_variants)
                variant = normalize_nfc(variant);
            dataset.examples.push_back(std::move(item));
        } catch (const std::exception& error) {
            throw std::runtime_error("dataset line " + std::to_string(line_number) +
                                     ": " + error.what());
        }
    }
    if (dataset.examples.empty()) throw std::runtime_error("dataset is empty");
    // FNV-1a fingerprint is deterministic and intentionally labelled as such.
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto& item : dataset.examples) {
        std::string material = item.example_id;
        material.push_back(static_cast<char>(0)); material += item.group_id;
        material.push_back(static_cast<char>(0)); material += item.question;
        material.push_back(static_cast<char>(0)); material += item.answer;
        material.push_back(static_cast<char>(10));
        for (unsigned char byte : material) { hash ^= byte; hash *= 1099511628211ULL; }
    }
    std::ostringstream digest;
    digest << std::hex << hash;
    dataset.fingerprint = "fnv1a64:" + digest.str();
    return dataset;
}

DatasetSplits split_by_group_id(const Dataset& dataset, SplitConfig config) {
    if (!std::isfinite(config.train_fraction) ||
        !std::isfinite(config.validation_fraction) ||
        !std::isfinite(config.test_fraction) ||
        config.train_fraction <= 0.0 || config.validation_fraction <= 0.0 ||
        config.test_fraction <= 0.0 ||
        std::abs(config.train_fraction + config.validation_fraction +
                 config.test_fraction - 1.0) > 1.0e-9)
        throw std::invalid_argument("dataset split fractions must be positive and sum to one");
    std::vector<std::string> groups;
    std::unordered_map<std::string, std::vector<std::size_t>> members;
    for (std::size_t i = 0; i < dataset.examples.size(); ++i) {
        const auto& group = dataset.examples[i].group_id;
        if (!members.contains(group)) groups.push_back(group);
        members[group].push_back(i);
    }
    if (groups.size() < 3U)
        throw std::invalid_argument("at least three distinct group_id values are required");
    std::mt19937_64 random(config.seed);
    std::shuffle(groups.begin(), groups.end(), random);
    const auto total = groups.size();
    std::size_t train_groups = static_cast<std::size_t>(
        std::floor(config.train_fraction * static_cast<double>(total)));
    std::size_t valid_groups = static_cast<std::size_t>(
        std::floor(config.validation_fraction * static_cast<double>(total)));
    train_groups = std::max<std::size_t>(1U, train_groups);
    valid_groups = std::max<std::size_t>(1U, valid_groups);
    if (train_groups + valid_groups >= total) {
        train_groups = total - 2U;
        valid_groups = 1U;
    }
    DatasetSplits result;
    for (std::size_t g = 0; g < groups.size(); ++g) {
        auto* destination = g < train_groups ? &result.train :
            (g < train_groups + valid_groups ? &result.validation : &result.test);
        const auto& indices = members.at(groups[g]);
        destination->insert(destination->end(), indices.begin(), indices.end());
    }
    return result;
}

std::vector<DatasetExample> expand_controlled(const DatasetExample& example,
                                               std::size_t maximum_variants,
                                               std::uint64_t seed) {
    if (maximum_variants == 0U) throw std::invalid_argument("maximum_variants must be positive");
    std::vector<DatasetExample> candidates;
    candidates.push_back(example);
    std::unordered_set<std::string> seen{example.question};
    for (const auto& variant : example.question_variants) {
        if (variant.empty() || !seen.insert(variant).second) continue;
        auto copy = example;
        copy.question = variant;
        copy.example_id = example.example_id + "#variant-" +
                          std::to_string(candidates.size());
        candidates.push_back(std::move(copy));
    }
    if (candidates.size() <= maximum_variants) return candidates;
    std::vector<DatasetExample> selected;
    selected.push_back(candidates.front());
    std::vector<std::size_t> indices;
    for (std::size_t i = 1; i < candidates.size(); ++i) indices.push_back(i);
    std::mt19937_64 random(seed);
    std::shuffle(indices.begin(), indices.end(), random);
    for (std::size_t i = 0; selected.size() < maximum_variants; ++i)
        selected.push_back(candidates[indices[i]]);
    return selected;
}

} // namespace morok::g3::training
