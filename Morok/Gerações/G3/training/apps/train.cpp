#include "morok/g3/training/trainer.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc < 3 || argc > 5) {
        std::cerr << "Uso: morok-g3-train <dataset.jsonl> <metrics.jsonl> [epocas] [seed]\n";
        return 2;
    }
    try {
        auto dataset = morok::g3::training::load_jsonl(argv[1]);
        morok::g3::neural::G3Backend backend;
        morok::g3::training::TrainingConfig config;
        config.metrics_jsonl = argv[2];
        if (argc >= 4) config.epochs = static_cast<std::size_t>(std::stoull(argv[3]));
        if (argc >= 5) config.seed = std::stoull(argv[4]);
        const auto report = morok::g3::training::train(backend, dataset, config);
        for (const auto& epoch : report.epochs) {
            std::cout << "{\"epoch\":" << epoch.epoch
                      << ",\"training_loss\":" << epoch.training_loss
                      << ",\"validation_loss\":" << epoch.validation_loss
                      << ",\"optimizer_steps\":" << epoch.optimizer_steps << "}\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Falha no treinamento G3: " << error.what() << '\n';
        return 1;
    }
}
