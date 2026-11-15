#include "morok/g2/tsukuyomi/model.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

int main() {
    using namespace morok::g2::tsukuyomi;

    static_assert(kParameterCount == 10000);
    Model model;
    initialize(model, 1234);
    Model same_seed;
    initialize(same_seed, 1234);
    assert(model.parameters == same_seed.parameters);
    for (float value : model.parameters)
        assert(std::isfinite(value));

    const std::array<std::uint8_t, 8> sample{
        'M', 'o', 'r', 'o', 'k', ' ', 'A', 'I'
    };
    const auto before = evaluate_loss(model, sample);
    assert(std::isfinite(before) && before > 0.0);

    const auto prediction = forward(model, sample);
    for (float value : prediction.logits)
        assert(std::isfinite(value));

    TrainingConfig config;
    config.epochs = 2;
    config.learning_rate = 0.002;
    const auto report = train(model, sample, config);
    assert(report.epochs_completed == 2);
    assert(report.target_bytes == sample.size() * 2);
    assert(std::isfinite(report.final_loss));
    assert(std::isfinite(report.final_bpb));
    assert(report.final_loss < report.initial_loss);
    assert(report.final_bpb < report.initial_bpb);

    // Retomar em dois blocos deve equivaler ao mesmo número de épocas contínuas.
    Model uninterrupted;
    Model resumed;
    initialize(uninterrupted, 987);
    initialize(resumed, 987);
    TrainingState uninterrupted_state;
    TrainingState resumed_state;
    TrainingConfig one_epoch;
    one_epoch.epochs = 1;
    one_epoch.batch_size = 4;
    TrainingConfig two_epochs;
    two_epochs.epochs = 2;
    two_epochs.batch_size = 4;
    const auto full_report = train(uninterrupted, sample, two_epochs,
                                   &uninterrupted_state);
    const auto first_report = train(resumed, sample, one_epoch, &resumed_state);
    const auto second_report = train(resumed, sample, one_epoch, &resumed_state);
    assert(full_report.epochs_completed == 2);
    assert(first_report.epochs_completed == 1 && second_report.epochs_completed == 1);
    assert(uninterrupted.parameters == resumed.parameters);
    assert(uninterrupted_state.first_moment == resumed_state.first_moment);
    assert(uninterrupted_state.second_moment == resumed_state.second_moment);
    assert(uninterrupted_state.step == resumed_state.step);
    assert(resumed_state.step == 4);

    const auto optimizer_path = std::filesystem::temp_directory_path() /
                               "morok-tsukuyomi-g2-v0.1.adam";
    assert(save_training_state(resumed_state, optimizer_path));
    TrainingState restored_optimizer;
    assert(load_training_state(restored_optimizer, optimizer_path));
    assert(restored_optimizer.first_moment == resumed_state.first_moment);
    assert(restored_optimizer.second_moment == resumed_state.second_moment);
    assert(restored_optimizer.step == resumed_state.step);

    const auto generated = generate(model, sample, 12);
    assert(generated.size() == sample.size() + 12);
    assert(std::equal(sample.begin(), sample.end(), generated.begin()));
    for (std::size_t i = sample.size(); i < generated.size(); ++i)
        assert(generated[i] <= 0x7F || (generated[i] >= 0x80 && generated[i] <= 0xBF) || (generated[i] >= 0xC2 && generated[i] <= 0xF4));

    const auto path = std::filesystem::temp_directory_path() /
                      "morok-tsukuyomi-g2-v0.1.ckpt";
    assert(save_checkpoint(model, path));
    Model restored;
    assert(load_checkpoint(restored, path));
    assert(restored.parameters == model.parameters);
    const std::array<std::uint8_t, 1> invalid_utf8{0xC0};
    const auto unchanged = generate(model, invalid_utf8, 4);
    assert(unchanged.size() == invalid_utf8.size());
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(optimizer_path, ignored);

    return 0;
}
