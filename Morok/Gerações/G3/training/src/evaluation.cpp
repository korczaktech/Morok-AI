#include "morok/g3/training/evaluation.hpp"

#include "morok/g3/inference/model_artifact.hpp"

#include <unicode/normalizer2.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>

namespace morok::g3::training {
namespace {

using icu::UnicodeString;
constexpr std::uint16_t kBos = 257U;
constexpr std::uint16_t kEos = 258U;
constexpr std::uint16_t kUsr = 259U;
constexpr std::uint16_t kBot = 260U;

std::string to_utf8(const UnicodeString& value) {
    std::string result;
    value.toUTF8String(result);
    return result;
}

std::vector<UChar32> codepoints(const UnicodeString& text) {
    std::vector<UChar32> result;
    for (int32_t i = 0; i < text.length();) {
        const UChar32 cp = text.char32At(i);
        result.push_back(cp);
        i += U16_LENGTH(cp);
    }
    return result;
}

UnicodeString from_codepoints(const std::vector<UChar32>& values) {
    UnicodeString result;
    for (const UChar32 cp : values) result.append(cp);
    return result;
}

std::size_t count_groups(const Dataset& dataset,
                         const std::vector<std::size_t>& indices) {
    std::set<std::string> groups;
    for (const auto index : indices) {
        if (index >= dataset.examples.size())
            throw std::invalid_argument("G3 evaluation split index is invalid");
        groups.insert(dataset.examples[index].group_id);
    }
    return groups.size();
}

struct OneExample {
    double loss{0.0};
    std::size_t correct{0};
    std::size_t targets{0};
};

OneExample score_example(const neural::G3Backend& backend,
                          const DatasetExample& example) {
    const auto descriptor = backend.descriptor();
    std::vector<std::byte> state(descriptor.state_size_bytes);
    if (!backend.reset_state(state))
        throw std::runtime_error("G3 evaluator could not reset recurrent state");
    auto consume = [&](std::uint16_t token) {
        if (!backend.consume_token(state, token))
            throw std::runtime_error("G3 evaluator could not consume an input token");
    };
    consume(kBos);
    consume(kUsr);
    for (const unsigned char byte : example.question) consume(byte);
    consume(kBot);

    std::vector<std::uint16_t> targets;
    targets.reserve(example.answer.size() + 1U);
    for (const unsigned char byte : example.answer) targets.push_back(byte);
    targets.push_back(kEos);

    std::vector<float> log_probabilities(neural::kVocabularySize);
    OneExample result;
    for (const auto target : targets) {
        if (!backend.next_token_log_probabilities(state, log_probabilities))
            throw std::runtime_error("G3 evaluator could not obtain next-token probabilities");
        if (target >= log_probabilities.size() ||
            !std::isfinite(log_probabilities[target]))
            throw std::runtime_error("G3 evaluator received an invalid target probability");
        const auto predicted = static_cast<std::uint16_t>(
            std::distance(log_probabilities.begin(),
                std::max_element(log_probabilities.begin(), log_probabilities.end())));
        result.loss -= static_cast<double>(log_probabilities[target]);
        if (predicted == target) ++result.correct;
        ++result.targets;
        if (target != kEos) consume(target);
    }
    result.loss /= static_cast<double>(result.targets);
    return result;
}

struct Aggregate {
    double loss{0.0};
    std::size_t correct{0};
    std::size_t targets{0};
    std::size_t examples{0};
};

Aggregate score_split(const neural::G3Backend& backend, const Dataset& dataset,
                      const std::vector<std::size_t>& indices,
                      RobustnessCondition condition) {
    Aggregate aggregate;
    for (const auto index : indices) {
        if (index >= dataset.examples.size())
            throw std::invalid_argument("G3 evaluation split index is invalid");
        auto example = dataset.examples[index];
        example.question = make_robustness_variant(example.question, condition);
        const auto scored = score_example(backend, example);
        aggregate.loss += scored.loss;
        aggregate.correct += scored.correct;
        aggregate.targets += scored.targets;
        ++aggregate.examples;
    }
    if (aggregate.examples == 0U || aggregate.targets == 0U)
        throw std::invalid_argument("G3 evaluation split is empty");
    aggregate.loss /= static_cast<double>(aggregate.examples);
    return aggregate;
}

} // namespace

std::string robustness_condition_name(RobustnessCondition condition) {
    switch (condition) {
        case RobustnessCondition::clean: return "clean";
        case RobustnessCondition::typographical_error: return "typographical_error";
        case RobustnessCondition::missing_accents: return "missing_accents";
        case RobustnessCondition::punctuation: return "punctuation";
        case RobustnessCondition::irregular_spacing: return "irregular_spacing";
        case RobustnessCondition::unicode_and_emoji: return "unicode_and_emoji";
    }
    throw std::invalid_argument("unknown G3 robustness condition");
}

std::string make_robustness_variant(const std::string& question,
                                    RobustnessCondition condition) {
    UnicodeString text = UnicodeString::fromUTF8(question);
    auto points = codepoints(text);
    switch (condition) {
        case RobustnessCondition::clean:
            return question;
        case RobustnessCondition::typographical_error: {
            std::vector<std::size_t> letters;
            for (std::size_t i = 0; i < points.size(); ++i)
                if (u_isalpha(points[i])) letters.push_back(i);
            if (letters.size() >= 2U)
                std::swap(points[letters[0]], points[letters[1]]);
            else if (!points.empty())
                points.insert(points.begin() + 1, points.front());
            text = from_codepoints(points);
            break;
        }
        case RobustnessCondition::missing_accents: {
            UErrorCode status = U_ZERO_ERROR;
            const auto* normalizer = icu::Normalizer2::getNFDInstance(status);
            if (U_FAILURE(status) || normalizer == nullptr)
                throw std::runtime_error("ICU NFD normalizer is unavailable");
            UnicodeString decomposed;
            normalizer->normalize(text, decomposed, status);
            if (U_FAILURE(status))
                throw std::runtime_error("ICU could not decompose evaluation text");
            std::vector<UChar32> stripped;
            for (const UChar32 cp : codepoints(decomposed)) {
                const auto category = u_charType(cp);
                if (category != U_NON_SPACING_MARK &&
                    category != U_COMBINING_SPACING_MARK &&
                    category != U_ENCLOSING_MARK)
                    stripped.push_back(cp);
            }
            text = from_codepoints(stripped);
            break;
        }
        case RobustnessCondition::punctuation: {
            std::vector<UChar32> stripped;
            for (const UChar32 cp : points)
                if (!u_ispunct(cp)) stripped.push_back(cp);
            stripped.push_back(static_cast<UChar32>('?'));
            text = from_codepoints(stripped);
            break;
        }
        case RobustnessCondition::irregular_spacing: {
            std::vector<UChar32> spaced{
                static_cast<UChar32>(' '), static_cast<UChar32>(' ')};
            for (const UChar32 cp : points) {
                spaced.push_back(cp);
                if (u_isUWhiteSpace(cp)) spaced.push_back(cp);
            }
            spaced.push_back(static_cast<UChar32>(' '));
            text = from_codepoints(spaced);
            break;
        }
        case RobustnessCondition::unicode_and_emoji:
            text.append(UnicodeString::fromUTF8(" — ação 東京 😀🚀"));
            break;
    }
    return to_utf8(text);
}

bool load_fp32_model_weights(const std::filesystem::path& model_path,
                             neural::G3Backend& backend) {
    const auto artifact = inference::read_model_artifact(model_path);
    if (!artifact)
        throw std::runtime_error("G3 model artifact failed validation");
    if (artifact.value_if()->metadata.precision != inference::PrecisionMode::fp32 ||
        artifact.value_if()->metadata.parameter_count != neural::kTargetParameterCount ||
        backend.parameter_count() != neural::kTargetParameterCount)
        throw std::runtime_error("G3 evaluator requires a 100k FP32 architecture 0x0302 artifact");

    const auto payload = std::span<const std::byte>(artifact.value_if()->payload);
    std::size_t offset = 0U;
    auto spans = backend.mutable_weights();
    const std::array<std::span<float>, 11> tensors{
        spans.embedding, spans.layer1_input, spans.layer1_recurrent,
        spans.layer1_bias, spans.layer2_input, spans.layer2_recurrent,
        spans.layer2_bias, spans.rms_scale, spans.projection,
        spans.projection_bias, spans.output_bias};
    for (const auto tensor : tensors) {
        for (float& value : tensor) {
            const std::uint32_t bits =
                std::to_integer<std::uint32_t>(payload[offset]) |
                (std::to_integer<std::uint32_t>(payload[offset + 1U]) << 8U) |
                (std::to_integer<std::uint32_t>(payload[offset + 2U]) << 16U) |
                (std::to_integer<std::uint32_t>(payload[offset + 3U]) << 24U);
            value = std::bit_cast<float>(bits);
            if (!std::isfinite(value))
                throw std::runtime_error("G3 model artifact contains non-finite FP32 weights");
            offset += sizeof(float);
        }
    }
    if (offset != payload.size())
        throw std::runtime_error("G3 model artifact payload has an unexpected tensor layout");
    return true;
}

GeneralizationReport evaluate_generalization(
    const neural::G3Backend& backend, const Dataset& dataset,
    const DatasetSplits& splits, const EvaluationConfig& config) {
    if (backend.parameter_count() != neural::kTargetParameterCount)
        throw std::invalid_argument("G3 evaluation requires exactly 100000 parameters");
    if (splits.train.empty() || splits.validation.empty() || splits.test.empty())
        throw std::invalid_argument("G3 evaluation requires non-empty train, validation and test splits");
    if (!std::isfinite(config.overfit_warning_relative_gap) ||
        config.overfit_warning_relative_gap < 0.0)
        throw std::invalid_argument("invalid G3 overfit warning threshold");

    const auto train = score_split(backend, dataset, splits.train,
                                   RobustnessCondition::clean);
    const auto validation = score_split(backend, dataset, splits.validation,
                                        RobustnessCondition::clean);
    const auto test = score_split(backend, dataset, splits.test,
                                  RobustnessCondition::clean);
    constexpr std::array conditions{
        RobustnessCondition::clean,
        RobustnessCondition::typographical_error,
        RobustnessCondition::missing_accents,
        RobustnessCondition::punctuation,
        RobustnessCondition::irregular_spacing,
        RobustnessCondition::unicode_and_emoji};
    std::vector<Aggregate> results;
    results.reserve(conditions.size());
    for (const auto condition : conditions)
        results.push_back(score_split(backend, dataset, splits.test, condition));

    GeneralizationReport report;
    report.dataset_fingerprint = dataset.fingerprint;
    report.split_seed = config.split_seed;
    report.parameter_count = static_cast<std::size_t>(backend.parameter_count());
    report.training_examples = splits.train.size();
    report.validation_examples = splits.validation.size();
    report.test_examples = splits.test.size();
    report.training_groups = count_groups(dataset, splits.train);
    report.validation_groups = count_groups(dataset, splits.validation);
    report.test_groups = count_groups(dataset, splits.test);
    report.training_loss = train.loss;
    report.validation_loss = validation.loss;
    report.clean_test_loss = test.loss;
    report.validation_minus_training_loss = validation.loss - train.loss;
    report.test_minus_training_loss = test.loss - train.loss;
    report.test_to_training_loss_ratio = test.loss / std::max(train.loss, 1.0e-12);
    const double threshold = config.overfit_warning_relative_gap *
                             std::max(train.loss, 1.0e-12);
    report.overfit_warning =
        report.validation_minus_training_loss > threshold ||
        report.test_minus_training_loss > threshold;

    const double clean_accuracy = static_cast<double>(results.front().correct) /
                                  static_cast<double>(results.front().targets);
    for (std::size_t i = 0; i < conditions.size(); ++i) {
        const auto& aggregate = results[i];
        RobustnessMetrics metrics;
        metrics.condition = robustness_condition_name(conditions[i]);
        metrics.examples = aggregate.examples;
        metrics.target_tokens = aggregate.targets;
        metrics.mean_loss = aggregate.loss;
        metrics.token_accuracy = static_cast<double>(aggregate.correct) /
                                 static_cast<double>(aggregate.targets);
        metrics.loss_delta_vs_clean = metrics.mean_loss - test.loss;
        metrics.accuracy_delta_vs_clean = metrics.token_accuracy - clean_accuracy;
        report.robustness.push_back(std::move(metrics));
    }
    return report;
}

void write_generalization_report(const std::filesystem::path& path,
                                 const GeneralizationReport& report) {
    if (path.empty()) throw std::invalid_argument("G3 report path is empty");
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    if (!out) throw std::runtime_error("cannot open G3 generalization report");
    out << std::setprecision(12)
        << "{\n"
        << "  \"schema\": \"morok-g3-generalization-v1\",\n"
        << "  \"dataset_fingerprint\": \"" << report.dataset_fingerprint << "\",\n"
        << "  \"split_seed\": " << report.split_seed << ",\n"
        << "  \"parameter_count\": " << report.parameter_count << ",\n"
        << "  \"splits\": {\n"
        << "    \"training_examples\": " << report.training_examples << ",\n"
        << "    \"validation_examples\": " << report.validation_examples << ",\n"
        << "    \"test_examples\": " << report.test_examples << ",\n"
        << "    \"training_groups\": " << report.training_groups << ",\n"
        << "    \"validation_groups\": " << report.validation_groups << ",\n"
        << "    \"test_groups\": " << report.test_groups << "\n"
        << "  },\n"
        << "  \"loss\": {\n"
        << "    \"training\": " << report.training_loss << ",\n"
        << "    \"validation\": " << report.validation_loss << ",\n"
        << "    \"clean_test\": " << report.clean_test_loss << ",\n"
        << "    \"validation_minus_training\": " << report.validation_minus_training_loss << ",\n"
        << "    \"test_minus_training\": " << report.test_minus_training_loss << ",\n"
        << "    \"test_to_training_ratio\": " << report.test_to_training_loss_ratio << "\n"
        << "  },\n"
        << "  \"overfit_warning\": " << (report.overfit_warning ? "true" : "false") << ",\n"
        << "  \"robustness\": [\n";
    for (std::size_t i = 0; i < report.robustness.size(); ++i) {
        const auto& metric = report.robustness[i];
        out << "    {\n"
            << "      \"condition\": \"" << metric.condition << "\",\n"
            << "      \"examples\": " << metric.examples << ",\n"
            << "      \"target_tokens\": " << metric.target_tokens << ",\n"
            << "      \"mean_loss\": " << metric.mean_loss << ",\n"
            << "      \"token_accuracy\": " << metric.token_accuracy << ",\n"
            << "      \"loss_delta_vs_clean\": " << metric.loss_delta_vs_clean << ",\n"
            << "      \"accuracy_delta_vs_clean\": " << metric.accuracy_delta_vs_clean << "\n"
            << "    }" << (i + 1U == report.robustness.size() ? "\n" : ",\n");
    }
    out << "  ]\n}\n";
    if (!out) throw std::runtime_error("failed writing G3 generalization report");
}

} // namespace morok::g3::training
