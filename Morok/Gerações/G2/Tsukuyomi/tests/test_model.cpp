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

    const auto generated = generate(model, sample, 12);
    assert(generated.size() == sample.size() + 12);
    assert(std::equal(sample.begin(), sample.end(), generated.begin()));
    for (std::size_t i = sample.size(); i < generated.size(); ++i)
        assert(generated[i] <= 0x7F || generated[i] >= 0xC2);

    const auto path = std::filesystem::temp_directory_path() /
                      "morok-tsukuyomi-g2-v0.1.ckpt";
    assert(save_checkpoint(model, path));
    Model restored;
    assert(load_checkpoint(restored, path));
    assert(restored.parameters == model.parameters);
    Model rejected;
    const std::array<std::uint8_t, 1> invalid_utf8{0xC0};
    const auto unchanged = generate(model, invalid_utf8, 4);
    assert(unchanged.size() == invalid_utf8.size());
    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    return 0;
}
