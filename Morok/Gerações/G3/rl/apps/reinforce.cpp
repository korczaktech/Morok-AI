#include "morok/g3/rl/g3_policy.hpp"
#include "morok/g3/rl/reference_evaluator.hpp"
#include "morok/g3/training/dataset.hpp"
#include "morok/g3/training/system_prompt.hpp"
#include "morok/g3/training/evaluation.hpp"
#include "morok/g3/inference/model_artifact.hpp"
#include "morok/lang/byte_tokenizer.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using morok::lang::TokenId;

std::vector<TokenId> encode_prompt(const std::string& question) {
    const auto encoded = morok::lang::ByteTokenizer::encode_text(question);
    if (!encoded) throw std::runtime_error("Could not encode RL question");
    std::vector<TokenId> tokens{morok::lang::kBosToken};
    morok::g3::training::append_master_system_prompt(tokens);
    tokens.push_back(morok::lang::kUsrToken);
    tokens.insert(tokens.end(), encoded.value_if()->byte_tokens.begin(),
                  encoded.value_if()->byte_tokens.end());
    tokens.push_back(morok::lang::kBotToken);
    return tokens;
}

std::vector<TokenId> encode_answer(const std::string& answer) {
    const auto encoded = morok::lang::ByteTokenizer::encode_text(answer);
    if (!encoded) throw std::runtime_error("Could not encode RL reference answer");
    return encoded.value_if()->byte_tokens;
}

morok::g3::rl::Prompt make_prompt(
    const morok::g3::training::DatasetExample& example) {
    return {example.example_id, example.category, encode_prompt(example.question)};
}

void append_fp32(const std::vector<float>& tensor,
                 std::vector<std::byte>& payload) {
    for (const float value : tensor) {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        for (unsigned shift = 0; shift < 32U; shift += 8U)
            payload.push_back(static_cast<std::byte>((bits >> shift) & 0xffU));
    }
}

void save_fp32(const std::filesystem::path& path,
               const morok::g3::neural::G3Backend& backend) {
    using namespace morok::g3;
    inference::ModelArtifact artifact;
    artifact.metadata.generation_id = 3U;
    artifact.metadata.architecture_id = neural::kArchitectureId;
    artifact.metadata.embedding_dim = static_cast<std::uint16_t>(neural::kEmbeddingDim);
    artifact.metadata.hidden_dim = static_cast<std::uint16_t>(neural::kLayer1HiddenDim);
    artifact.metadata.hidden_dim_second = static_cast<std::uint16_t>(neural::kLayer2HiddenDim);
    artifact.metadata.recurrent_layers = static_cast<std::uint16_t>(neural::kRecurrentLayers);
    artifact.metadata.vocabulary_size = static_cast<std::uint32_t>(neural::kVocabularySize);
    artifact.metadata.state_size_bytes = backend.descriptor().state_size_bytes;
    artifact.metadata.parameter_count = backend.parameter_count();
    artifact.metadata.precision = inference::PrecisionMode::fp32;
    artifact.metadata.scale_count = 0U;

    const auto& weights = backend.weights();
    artifact.payload.reserve(static_cast<std::size_t>(backend.parameter_count()) *
                             sizeof(float));
    append_fp32(weights.embedding, artifact.payload);
    append_fp32(weights.layer1_input, artifact.payload);
    append_fp32(weights.layer1_recurrent, artifact.payload);
    append_fp32(weights.layer1_bias, artifact.payload);
    append_fp32(weights.layer2_input, artifact.payload);
    append_fp32(weights.layer2_recurrent, artifact.payload);
    append_fp32(weights.layer2_bias, artifact.payload);
    append_fp32(weights.rms_scale, artifact.payload);
    append_fp32(weights.projection, artifact.payload);
    append_fp32(weights.projection_bias, artifact.payload);
    append_fp32(weights.output_bias, artifact.payload);

    const auto saved = inference::save_model_artifact_atomic(path, artifact);
    if (!saved) throw std::runtime_error("Could not save independent RL model artifact");
}

void write_report(const std::filesystem::path& path,
                  const morok::g3::rl::EvaluationReport& evaluation,
                  const morok::g3::rl::TrainingReport& training,
                  std::size_t epochs, std::uint64_t seed,
                  const std::string& dataset_fingerprint,
                  std::uint64_t parameter_count) {
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    if (!out) throw std::runtime_error("Could not create G3 RL report");
    out << std::setprecision(12)
        << "{\n  \"schema\": \"morok-g3-rl-independent-report-v1\",\n"
        << "  \"training_mode\": \"independent_random_initialization\",\n"
        << "  \"seed\": " << seed << ",\n"
        << "  \"epochs\": " << epochs << ",\n"
        << "  \"dataset_fingerprint\": \"" << dataset_fingerprint << "\",\n"
        << "  \"parameters\": " << parameter_count << ",\n"
        << "  \"candidates\": " << training.candidates << ",\n"
        << "  \"optimizer_updates\": " << training.optimizer_updates << ",\n"
        << "  \"mean_training_reward\": " << training.mean_reward << ",\n"
        << "  \"mean_kl\": " << training.mean_kl << ",\n"
        << "  \"reward_hacking_suspicions\": " << training.reward_hacking_suspicions << ",\n"
        << "  \"evaluation\": {\n"
        << "    \"examples\": " << evaluation.examples << ",\n"
        << "    \"mean_reward\": " << evaluation.mean_reward << ",\n"
        << "    \"mean_response_tokens\": " << evaluation.mean_response_tokens << ",\n"
        << "    \"empty_rate\": " << evaluation.empty_response_rate << ",\n"
        << "    \"repeated_rate\": " << evaluation.repeated_response_rate << "\n"
        << "  }\n}\n";
    if (!out) throw std::runtime_error("Could not finish G3 RL report");
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 4 || argc > 6) {
        std::cerr << "Uso: morok-g3-reinforce <rl-dataset.jsonl> "
                     "<rl-output.morok> <report.json> [epochs] [seed]\n";
        return 2;
    }
    try {
        const std::size_t epochs = argc >= 5
            ? static_cast<std::size_t>(std::stoull(argv[4])) : 1U;
        const std::uint64_t seed = argc == 6
            ? std::stoull(argv[5]) : 42U;
        if (epochs == 0U || epochs > 100U)
            throw std::invalid_argument("epochs must be in [1,100]");

        const auto dataset = morok::g3::training::load_jsonl(argv[1]);
        const auto splits = morok::g3::training::split_by_group_id(
            dataset, morok::g3::training::SplitConfig{0.85, 0.10, 0.05, seed});

        std::map<std::vector<TokenId>, std::vector<TokenId>> unique_references;
        for (const auto& example : dataset.examples) {
            if (example.sensitive || example.quality < 1.0) continue;
            auto prompt = encode_prompt(example.question);
            auto answer = encode_answer(example.answer);
            const auto [it, inserted] = unique_references.emplace(
                std::move(prompt), std::move(answer));
            if (!inserted && it->second != encode_answer(example.answer))
                throw std::runtime_error(
                    "Conflicting reference answers for duplicate RL prompts");
        }
        std::vector<morok::g3::rl::ReferenceAnswer> references;
        references.reserve(unique_references.size());
        for (auto& [prompt, answer] : unique_references)
            references.push_back({prompt, answer});
        morok::g3::rl::ReferenceRewardEvaluator evaluator(std::move(references));

        std::vector<morok::g3::rl::Prompt> train_prompts;
        std::vector<morok::g3::rl::Prompt> test_prompts;
        const morok::g3::training::DatasetExample* audit_example = nullptr;
        for (const auto index : splits.train) {
            const auto& example = dataset.examples[index];
            if (example.sensitive || example.quality < 1.0) continue;
            train_prompts.push_back(make_prompt(example));
            if (audit_example == nullptr) audit_example = &example;
        }
        for (const auto index : splits.test) {
            const auto& example = dataset.examples[index];
            if (!example.sensitive && example.quality >= 1.0)
                test_prompts.push_back(make_prompt(example));
        }
        if (train_prompts.empty() || test_prompts.empty() || audit_example == nullptr)
            throw std::runtime_error(
                "Independent RL needs non-sensitive train and test examples");

        const auto audit_prompt = encode_prompt(audit_example->question);
        const auto audit_answer = encode_answer(audit_example->answer);
        const std::vector<morok::g3::rl::RewardAuditCase> audit{
            {"reference-exact", audit_prompt, audit_answer, 0.90, 1.0},
            {"empty-answer", audit_prompt, {}, -1.0, -0.50}};

        // RL starts from its own seeded initialization. No SFT model, weights,
        // checkpoint, optimizer state or frozen reference is loaded.
        morok::g3::neural::G3Backend rl_backend(
            morok::g3::neural::G3Config{seed, 1.0e-5F});
        morok::g3::rl::G3PolicyAdapter rl_policy(
            rl_backend, "g3-independent-rl-policy");
        morok::g3::rl::Config config;
        config.candidates_per_prompt = 4U;
        config.max_response_tokens = 128U;
        config.top_k = 10U;
        config.temperature = 0.7;
        config.gamma = 1.0;
        config.kl_beta = 0.0;
        config.seed = seed;
        morok::g3::rl::ReinforceTrainer trainer(config);

        morok::g3::rl::TrainingReport latest;
        for (std::size_t epoch = 0; epoch < epochs; ++epoch) {
            const auto result = trainer.train_batch(
                train_prompts, rl_policy, evaluator, audit);
            if (!result)
                throw std::runtime_error("G3 independent REINFORCE batch failed");
            latest = *result.value_if();
        }

        const auto evaluation = morok::g3::rl::evaluate_policy(
            rl_policy, evaluator, test_prompts, 128U);
        if (!evaluation)
            throw std::runtime_error("G3 independent RL evaluation failed");

        save_fp32(argv[2], rl_backend);
        write_report(argv[3], *evaluation.value_if(), latest, epochs, seed,
                     dataset.fingerprint, rl_backend.parameter_count());
        std::cout << "G3 independent RL complete. Parameters: "
                  << rl_backend.parameter_count()
                  << ". Test reward: " << evaluation.value_if()->mean_reward
                  << ". Model: " << argv[2]
                  << ". Report: " << argv[3] << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Falha no RL G3: " << error.what() << "\n";
        return 1;
    }
}
