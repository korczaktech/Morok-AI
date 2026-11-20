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
#include <string_view>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace morok::g3::training {
namespace {

class JsonObject final {
public:
    explicit JsonObject(std::string line) : text_(std::move(line)) {}

    std::string string(const std::string& key, bool required = true) const {
        const auto value = find_value(key);
        if (!value) {
            if (required) throw std::runtime_error("dataset field missing: " + key);
            return {};
        }
        std::size_t cursor = *value;
        return decode_string_at(cursor);
    }

    std::vector<std::vector<std::string>> string_matrix(const std::string& key) const {
        const auto value = find_value(key);
        if (!value) return {};
        std::size_t cursor = *value;
        if (cursor >= text_.size() || text_[cursor++] != '[')
            throw std::runtime_error("dataset field must be a matrix of strings: " + key);
        std::vector<std::vector<std::string>> result;
        skip_whitespace(cursor);
        if (cursor < text_.size() && text_[cursor] == ']') return result;
        while (cursor < text_.size()) {
            if (text_[cursor++] != '[')
                throw std::runtime_error("each expansion axis must be an array");
            std::vector<std::string> axis;
            skip_whitespace(cursor);
            if (cursor < text_.size() && text_[cursor] == ']')
                throw std::runtime_error("expansion axes cannot be empty");
            while (cursor < text_.size()) {
                axis.push_back(decode_string_at(cursor));
                skip_whitespace(cursor);
                if (cursor < text_.size() && text_[cursor] == ',') {
                    ++cursor;
                    skip_whitespace(cursor);
                    if (cursor < text_.size() && text_[cursor] == ']')
                        throw std::runtime_error("trailing comma in expansion axis");
                    continue;
                }
                if (cursor < text_.size() && text_[cursor] == ']') {
                    ++cursor;
                    break;
                }
                throw std::runtime_error("invalid expansion axis separator");
            }
            if (axis.empty()) throw std::runtime_error("expansion axes cannot be empty");
            result.push_back(std::move(axis));
            skip_whitespace(cursor);
            if (cursor < text_.size() && text_[cursor] == ',') {
                ++cursor;
                skip_whitespace(cursor);
                if (cursor < text_.size() && text_[cursor] == ']')
                    throw std::runtime_error("trailing comma in expansion_axes");
                continue;
            }
            if (cursor < text_.size() && text_[cursor] == ']') return result;
            throw std::runtime_error("invalid expansion_axes separator");
        }
        throw std::runtime_error("unterminated expansion_axes array");
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
        skip_whitespace(cursor);
        if (cursor < text_.size() && text_[cursor] == ']') return result;
        while (cursor < text_.size()) {
            result.push_back(decode_string_at(cursor));
            skip_whitespace(cursor);
            if (cursor < text_.size() && text_[cursor] == ',') {
                ++cursor;
                skip_whitespace(cursor);
                if (cursor < text_.size() && text_[cursor] == ']')
                    throw std::runtime_error("trailing comma in JSON string array");
                continue;
            }
            if (cursor < text_.size() && text_[cursor] == ']') return result;
            throw std::runtime_error("invalid JSON string array separator");
        }
        throw std::runtime_error("unterminated JSON array");
    }

private:
    std::string decode_string_at(std::size_t& cursor) const {
        if (cursor >= text_.size() || text_[cursor++] != '"')
            throw std::runtime_error("expected JSON string");
        std::string out;
        while (cursor < text_.size()) {
            const char ch = text_[cursor++];
            if (ch == '"') return out;
            if (static_cast<unsigned char>(ch) < 0x20U)
                throw std::runtime_error("unescaped control character in JSON string");
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
                        if (cursor + 6U > text_.size() ||
                            text_.compare(cursor, 2U, "\\u") != 0)
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

    void skip_whitespace(std::size_t& cursor) const {
        while (cursor < text_.size() &&
               (text_[cursor] == ' ' || text_[cursor] == '\n' ||
                text_[cursor] == '\r' || text_[cursor] == '\t')) {
            ++cursor;
        }
    }

    void skip_json_string(std::size_t& cursor) const {
        if (cursor >= text_.size() || text_[cursor] != '"')
            throw std::runtime_error("expected JSON string");
        ++cursor;
        while (cursor < text_.size()) {
            const char ch = text_[cursor++];
            if (ch == '"') return;
            if (static_cast<unsigned char>(ch) < 0x20U)
                throw std::runtime_error("unescaped control character in JSON string");
            if (ch != '\\') continue;
            if (cursor >= text_.size())
                throw std::runtime_error("truncated JSON escape");
            const char escaped = text_[cursor++];
            if (escaped == 'u') {
                (void)hex4(cursor);
                cursor += 4U;
            } else if (escaped != '"' && escaped != '\\' && escaped != '/' &&
                       escaped != 'b' && escaped != 'f' && escaped != 'n' &&
                       escaped != 'r' && escaped != 't') {
                throw std::runtime_error("invalid JSON escape");
            }
        }
        throw std::runtime_error("unterminated JSON string");
    }

    static bool valid_json_number(std::string_view token) noexcept {
        std::size_t i = 0U;
        if (i < token.size() && token[i] == '-') ++i;
        if (i >= token.size()) return false;
        if (token[i] == '0') {
            ++i;
            if (i < token.size() && token[i] >= '0' && token[i] <= '9')
                return false;
        } else {
            if (token[i] < '1' || token[i] > '9') return false;
            while (i < token.size() && token[i] >= '0' && token[i] <= '9') ++i;
        }
        if (i < token.size() && token[i] == '.') {
            ++i;
            const std::size_t fraction_start = i;
            while (i < token.size() && token[i] >= '0' && token[i] <= '9') ++i;
            if (i == fraction_start) return false;
        }
        if (i < token.size() && (token[i] == 'e' || token[i] == 'E')) {
            ++i;
            if (i < token.size() && (token[i] == '+' || token[i] == '-')) ++i;
            const std::size_t exponent_start = i;
            while (i < token.size() && token[i] >= '0' && token[i] <= '9') ++i;
            if (i == exponent_start) return false;
        }
        return i == token.size();
    }

    void skip_json_value(std::size_t& cursor) const {
        skip_whitespace(cursor);
        if (cursor >= text_.size())
            throw std::runtime_error("missing JSON value");
        if (text_[cursor] == '"') {
            skip_json_string(cursor);
            return;
        }
        if (text_[cursor] == '[') {
            ++cursor;
            skip_whitespace(cursor);
            if (cursor < text_.size() && text_[cursor] == ']') {
                ++cursor;
                return;
            }
            while (cursor < text_.size()) {
                skip_json_value(cursor);
                skip_whitespace(cursor);
                if (cursor < text_.size() && text_[cursor] == ',') {
                    ++cursor;
                    skip_whitespace(cursor);
                    if (cursor < text_.size() && text_[cursor] == ']')
                        throw std::runtime_error("trailing comma in JSON array");
                    continue;
                }
                if (cursor < text_.size() && text_[cursor] == ']') {
                    ++cursor;
                    return;
                }
                throw std::runtime_error("invalid JSON array separator");
            }
            throw std::runtime_error("unterminated JSON array");
        }
        if (text_[cursor] == '{') {
            ++cursor;
            skip_whitespace(cursor);
            if (cursor < text_.size() && text_[cursor] == '}') {
                ++cursor;
                return;
            }
            while (cursor < text_.size()) {
                if (text_[cursor] != '"')
                    throw std::runtime_error("JSON object key must be a string");
                skip_json_string(cursor);
                skip_whitespace(cursor);
                if (cursor >= text_.size() || text_[cursor++] != ':')
                    throw std::runtime_error("missing colon in JSON object");
                skip_json_value(cursor);
                skip_whitespace(cursor);
                if (cursor < text_.size() && text_[cursor] == ',') {
                    ++cursor;
                    skip_whitespace(cursor);
                    if (cursor < text_.size() && text_[cursor] == '}')
                        throw std::runtime_error("trailing comma in JSON object");
                    continue;
                }
                if (cursor < text_.size() && text_[cursor] == '}') {
                    ++cursor;
                    return;
                }
                throw std::runtime_error("invalid JSON object separator");
            }
            throw std::runtime_error("unterminated JSON object");
        }

        const std::size_t start = cursor;
        while (cursor < text_.size() && text_[cursor] != ',' &&
               text_[cursor] != ']' && text_[cursor] != '}' &&
               text_[cursor] != ' ' && text_[cursor] != '\n' &&
               text_[cursor] != '\r' && text_[cursor] != '\t') {
            ++cursor;
        }
        const std::string_view token(text_.data() + start, cursor - start);
        if (token != "true" && token != "false" && token != "null" &&
            !valid_json_number(token)) {
            throw std::runtime_error("invalid JSON primitive");
        }
    }

    std::optional<std::size_t> find_value(const std::string& key) const {
        std::size_t cursor = 0U;
        skip_whitespace(cursor);
        if (cursor >= text_.size() || text_[cursor++] != '{')
            throw std::runtime_error("dataset line must be a JSON object");

        std::optional<std::size_t> found;
        std::unordered_set<std::string> keys;
        bool closed = false;
        while (cursor < text_.size()) {
            skip_whitespace(cursor);
            if (cursor < text_.size() && text_[cursor] == '}') {
                ++cursor;
                closed = true;
                break;
            }
            if (cursor >= text_.size() || text_[cursor] != '"')
                throw std::runtime_error("JSON object key must be a string");

            const std::size_t key_start = cursor + 1U;
            skip_json_string(cursor);
            const std::size_t key_end = cursor - 1U;
            const std::string current_key =
                text_.substr(key_start, key_end - key_start);
            if (!keys.insert(current_key).second)
                throw std::runtime_error("duplicate JSON object key: " + current_key);

            skip_whitespace(cursor);
            if (cursor >= text_.size() || text_[cursor++] != ':')
                throw std::runtime_error("missing colon after JSON object key");
            skip_whitespace(cursor);
            const std::size_t value_position = cursor;
            skip_json_value(cursor);
            if (current_key == key) found = value_position;

            skip_whitespace(cursor);
            if (cursor < text_.size() && text_[cursor] == ',') {
                ++cursor;
                skip_whitespace(cursor);
                if (cursor < text_.size() && text_[cursor] == '}')
                    throw std::runtime_error("trailing comma in JSON object");
                continue;
            }
            if (cursor < text_.size() && text_[cursor] == '}') {
                ++cursor;
                closed = true;
                break;
            }
            throw std::runtime_error("invalid JSON object separator");
        }

        skip_whitespace(cursor);
        if (!closed) throw std::runtime_error("unterminated JSON object");
        if (cursor != text_.size())
            throw std::runtime_error("trailing data after JSON object");
        return found;
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

    std::string text_;
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
            item.expansion_axes = object.string_matrix("expansion_axes");
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
            for (auto& axis : item.expansion_axes) {
                for (auto& option : axis) {
                    option = normalize_nfc(option);
                    if (option.empty())
                        throw std::runtime_error("expansion axis options cannot be empty");
                }
            }
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
        std::string material;
        const auto append_field = [&material](const std::string& field) {
            material += field;
            material.push_back(static_cast<char>(0));
        };
        append_field(item.example_id); append_field(item.group_id);
        append_field(item.category); append_field(item.question);
        append_field(item.answer); append_field(item.source);
        append_field(item.language); append_field(item.dataset_version);
        append_field(std::to_string(item.quality));
        append_field(item.sensitive ? "true" : "false");
        for (const auto& variant : item.question_variants) append_field(variant);
        for (const auto& axis : item.expansion_axes) {
            append_field("[");
            for (const auto& option : axis) append_field(option);
            append_field("]");
        }
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
    if (maximum_variants == 0U)
        throw std::invalid_argument("maximum_variants must be positive");
    std::vector<std::string> questions;
    std::unordered_set<std::string> seen;
    auto add = [&](std::string question) {
        if (!question.empty() && seen.insert(question).second &&
            questions.size() < maximum_variants)
            questions.push_back(std::move(question));
    };

    if (!example.expansion_axes.empty()) {
        std::string canonical = example.question;
        for (std::size_t axis = 0; axis < example.expansion_axes.size(); ++axis) {
            const std::string slot = "{" + std::to_string(axis) + "}";
            if (canonical.find(slot) == std::string::npos)
                throw std::invalid_argument("question template is missing expansion slot " + slot);
            std::size_t position = canonical.find(slot);
            while (position != std::string::npos) {
                const auto& replacement = example.expansion_axes[axis].front();
                canonical.replace(position, slot.size(), replacement);
                position = canonical.find(slot, position + replacement.size());
            }
        }
        add(canonical);
        std::mt19937_64 random(seed);
        for (std::size_t attempt = 0;
             questions.size() < maximum_variants &&
             attempt < maximum_variants * 32U; ++attempt) {
            std::string combination = example.question;
            for (std::size_t axis = 0; axis < example.expansion_axes.size(); ++axis) {
                const auto& options = example.expansion_axes[axis];
                std::uniform_int_distribution<std::size_t> pick(0U, options.size() - 1U);
                const auto& replacement = options[pick(random)];
                const std::string slot = "{" + std::to_string(axis) + "}";
                std::size_t position = combination.find(slot);
                while (position != std::string::npos) {
                    combination.replace(position, slot.size(), replacement);
                    position = combination.find(slot, position + replacement.size());
                }
            }
            add(std::move(combination));
        }
    } else {
        add(example.question);
    }

    for (const auto& variant : example.question_variants) add(variant);
    if (questions.empty())
        throw std::runtime_error("controlled expansion produced no candidates");
    std::vector<DatasetExample> result;
    result.reserve(questions.size());
    for (std::size_t i = 0; i < questions.size(); ++i) {
        auto copy = example;
        copy.question = std::move(questions[i]);
        if (i != 0U)
            copy.example_id = example.example_id + "#variant-" + std::to_string(i);
        result.push_back(std::move(copy));
    }
    return result;
}

} // namespace morok::g3::training
