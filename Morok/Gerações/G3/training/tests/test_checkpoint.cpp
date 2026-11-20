#include "morok/g3/training/checkpoint.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace morok::g3::training;

namespace {

G3TrainingCheckpoint fixture() {
    G3TrainingCheckpoint c;
    c.compatibility.code_version = "g3-test-commit";
    c.compatibility.dataset_fingerprint = "dataset-sha256-fixture";
    c.compatibility.optimizer_id = "AdamW";
    c.compatibility.configuration_fingerprint = "config-fixture";
    c.run_id = "run-stage7-test";
    c.configuration_snapshot = "{\"epochs\":4,\"seed\":42}";
    c.model_weights.assign(kG3CheckpointParameterCount, 0.125F);
    c.optimizer_first_moment.assign(kG3CheckpointParameterCount, 0.01F);
    c.optimizer_second_moment.assign(kG3CheckpointParameterCount, 0.02F);
    c.optimizer_step = 123U;
    c.scheduler_step = 123U;
    c.scheduler_learning_rate = 0.00042;
    c.completed_epochs = 2U;
    c.next_batch = 17U;
    c.sampler_position = 1U;
    c.sampler_order = {2U, 0U, 1U};
    c.train_split = {0U, 1U, 2U};
    c.validation_split = {3U};
    c.test_split = {4U};
    c.cpp_rng_state = "mt19937_64-state";
    c.python_rng_state = "python-random-state";
    c.numpy_rng_state = "numpy-bit-generator-state";
    c.backend_rng_state = "backend-rng-state";
    c.rl_category_baselines = {{"general", 0.25}, {"code", -0.10}};
    c.rl_rng_state = "rl-mt19937-state";
    c.rl_optimizer_first_moment.assign(kG3CheckpointParameterCount, 0.03F);
    c.rl_optimizer_second_moment.assign(kG3CheckpointParameterCount, 0.04F);
    c.rl_optimizer_step = 55U;
    c.kl_beta = 0.02;
    c.frozen_sft_reference = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    c.replay_buffer_state = {std::byte{0x04}, std::byte{0x05}};
    c.training_history = {std::byte{0x06}};
    return c;
}

void test_round_trip_and_exact_resume_state() {
    const auto original = fixture();
    const auto encoded = serialize_training_checkpoint(original);
    assert(encoded);
    assert(encoded.value_if()->size() > kTrainingCheckpointHeaderBytes);
    const auto decoded = deserialize_training_checkpoint(*encoded.value_if());
    assert(decoded);
    const auto& restored = *decoded.value_if();
    assert(restored.compatibility.dataset_fingerprint == original.compatibility.dataset_fingerprint);
    assert(restored.compatibility.configuration_fingerprint == original.compatibility.configuration_fingerprint);
    assert(restored.model_weights == original.model_weights);
    assert(restored.optimizer_first_moment == original.optimizer_first_moment);
    assert(restored.optimizer_second_moment == original.optimizer_second_moment);
    assert(restored.optimizer_step == original.optimizer_step);
    assert(restored.scheduler_step == original.scheduler_step);
    assert(restored.scheduler_learning_rate == original.scheduler_learning_rate);
    assert(restored.completed_epochs == original.completed_epochs);
    assert(restored.next_batch == original.next_batch);
    assert(restored.sampler_order == original.sampler_order);
    assert(restored.cpp_rng_state == original.cpp_rng_state);
    assert(restored.python_rng_state == original.python_rng_state);
    assert(restored.numpy_rng_state == original.numpy_rng_state);
    assert(restored.backend_rng_state == original.backend_rng_state);
    assert(restored.rl_category_baselines == original.rl_category_baselines);
    assert(restored.rl_rng_state == original.rl_rng_state);
    assert(restored.rl_optimizer_first_moment == original.rl_optimizer_first_moment);
    assert(restored.rl_optimizer_second_moment == original.rl_optimizer_second_moment);
    assert(restored.rl_optimizer_step == original.rl_optimizer_step);
    assert(restored.kl_beta == original.kl_beta);
    assert(restored.frozen_sft_reference == original.frozen_sft_reference);
    assert(restored.replay_buffer_state == original.replay_buffer_state);
    assert(restored.training_history == original.training_history);
}

void test_independent_rl_checkpoint_and_optimizer_consistency() {
    auto checkpoint = fixture();
    checkpoint.kl_beta = 0.0;
    checkpoint.frozen_sft_reference.clear();
    assert(validate_training_checkpoint(checkpoint).has_value());

    checkpoint.optimizer_first_moment.clear();
    checkpoint.optimizer_second_moment.clear();
    assert(!validate_training_checkpoint(checkpoint).has_value());

    checkpoint = fixture();
    checkpoint.rl_optimizer_first_moment.clear();
    checkpoint.rl_optimizer_second_moment.clear();
    assert(!validate_training_checkpoint(checkpoint).has_value());

    checkpoint = fixture();
    checkpoint.kl_beta = 0.02;
    checkpoint.frozen_sft_reference.clear();
    assert(!validate_training_checkpoint(checkpoint).has_value());

    checkpoint = fixture();
    checkpoint.rl_rng_state.clear();
    assert(!validate_training_checkpoint(checkpoint).has_value());
}

void test_checkpoint_rejects_invalid_splits_and_sampler() {
    auto checkpoint = fixture();
    checkpoint.sampler_order = {0U, 0U, 2U};
    assert(!validate_training_checkpoint(checkpoint).has_value());

    checkpoint = fixture();
    checkpoint.validation_split = {1U};
    assert(!validate_training_checkpoint(checkpoint).has_value());

    checkpoint = fixture();
    checkpoint.optimizer_second_moment[10] = -0.1F;
    assert(!validate_training_checkpoint(checkpoint).has_value());

    checkpoint = fixture();
    checkpoint.rl_optimizer_second_moment[10] = -0.1F;
    assert(!validate_training_checkpoint(checkpoint).has_value());
}

void test_model_weights_restore_for_resume() {
    morok::g3::neural::G3Backend source(
        morok::g3::neural::G3Config{71U, 1.0e-5F});
    auto checkpoint = fixture();
    checkpoint.model_weights = capture_g3_model_weights(source);
    const auto encoded = serialize_training_checkpoint(checkpoint);
    assert(encoded);
    const auto decoded = deserialize_training_checkpoint(*encoded.value_if());
    assert(decoded);

    morok::g3::neural::G3Backend resumed(
        morok::g3::neural::G3Config{999U, 1.0e-5F});
    const auto restored = restore_g3_model_weights(
        decoded.value_if()->model_weights, resumed);
    assert(restored);
    assert(capture_g3_model_weights(resumed) == checkpoint.model_weights);
    assert(decoded.value_if()->optimizer_first_moment == checkpoint.optimizer_first_moment);
    assert(decoded.value_if()->optimizer_second_moment == checkpoint.optimizer_second_moment);
    assert(decoded.value_if()->optimizer_step == checkpoint.optimizer_step);
    assert(decoded.value_if()->cpp_rng_state == checkpoint.cpp_rng_state);

    auto expected = checkpoint.compatibility;
    expected.dataset_fingerprint = "different-dataset";
    assert(!validate_training_checkpoint(*decoded.value_if(), &expected));
}

void test_checksum_version_and_truncation_rejection() {
    const auto encoded = serialize_training_checkpoint(fixture());
    assert(encoded);
    auto corrupted = *encoded.value_if();
    corrupted.back() ^= std::byte{0x40};
    assert(!deserialize_training_checkpoint(corrupted));
    auto bad_version = *encoded.value_if();
    bad_version[8] = std::byte{0xff};
    assert(!deserialize_training_checkpoint(bad_version));
    auto truncated = *encoded.value_if();
    truncated.pop_back();
    assert(!deserialize_training_checkpoint(truncated));
}

void test_strict_compatibility_validation() {
    auto expected = fixture().compatibility;
    auto encoded = serialize_training_checkpoint(fixture());
    assert(encoded);
    assert(deserialize_training_checkpoint(*encoded.value_if(), &expected));
    expected.dataset_fingerprint = "different-dataset";
    assert(!deserialize_training_checkpoint(*encoded.value_if(), &expected));
    expected = fixture().compatibility;
    expected.configuration_fingerprint = "different-config";
    assert(!deserialize_training_checkpoint(*encoded.value_if(), &expected));
    expected = fixture().compatibility;
    expected.code_version = "different-code";
    assert(!deserialize_training_checkpoint(*encoded.value_if(), &expected));
    expected = fixture().compatibility;
    expected.optimizer_id = "SGD";
    assert(!deserialize_training_checkpoint(*encoded.value_if(), &expected));
}

void test_invalid_optimizer_and_nonfinite_weights() {
    auto invalid = fixture();
    invalid.optimizer_second_moment.clear();
    assert(!validate_training_checkpoint(invalid));
    invalid = fixture();
    invalid.model_weights[10] = std::numeric_limits<float>::quiet_NaN();
    assert(!validate_training_checkpoint(invalid));
    invalid = fixture();
    invalid.rl_category_baselines["general"] = 2.0;
    assert(!validate_training_checkpoint(invalid));
}

void test_atomic_file_save_and_existing_checkpoint_preservation() {
    const auto path = std::filesystem::temp_directory_path() /
        "morok-g3-stage7-checkpoint.morokckpt";
    std::error_code ec;
    std::filesystem::remove(path, ec);
    const auto original = fixture();
    assert(save_training_checkpoint_atomic(path, original));
    const auto loaded = read_training_checkpoint(path, &original.compatibility);
    assert(loaded);
    assert(loaded.value_if()->model_weights == original.model_weights);

    auto invalid = fixture();
    invalid.model_weights.clear();
    assert(!save_training_checkpoint_atomic(path, invalid));
    const auto preserved = read_training_checkpoint(path, &original.compatibility);
    assert(preserved);
    assert(preserved.value_if()->optimizer_step == original.optimizer_step);
    std::filesystem::remove(path, ec);
}

void test_real_g3_weight_capture_and_restore() {
    morok::g3::neural::G3Backend source(
        morok::g3::neural::G3Config{17U, 1.0e-5F});
    morok::g3::neural::G3Backend destination(
        morok::g3::neural::G3Config{99U, 1.0e-5F});
    const auto snapshot = capture_g3_model_weights(source);
    assert(snapshot.size() == kG3CheckpointParameterCount);
    assert(snapshot != capture_g3_model_weights(destination));
    assert(restore_g3_model_weights(snapshot, destination));
    assert(snapshot == capture_g3_model_weights(destination));
    const std::vector<float> invalid(10U, 0.0F);
    assert(!restore_g3_model_weights(invalid, destination));
}

} // namespace

int main() {
    test_round_trip_and_exact_resume_state();
    test_independent_rl_checkpoint_and_optimizer_consistency();
    test_checkpoint_rejects_invalid_splits_and_sampler();
    test_model_weights_restore_for_resume();
    test_checksum_version_and_truncation_rejection();
    test_strict_compatibility_validation();
    test_invalid_optimizer_and_nonfinite_weights();
    test_atomic_file_save_and_existing_checkpoint_preservation();
    test_real_g3_weight_capture_and_restore();
    return 0;
}
