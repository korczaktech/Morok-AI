#include "morok/g3/training/evaluation.hpp"

#include "morok/g3/training/dataset.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc < 4 || argc > 5) {
        std::cerr << "Uso: morok-g3-evaluate <dataset.jsonl> <modelo.morok> "
                     "<relatorio.json> [seed]\n";
        return 2;
    }
    try {
        const std::uint64_t seed = argc == 5
            ? std::stoull(argv[4]) : 42U;
        const auto dataset = morok::g3::training::load_jsonl(argv[1]);
        const auto splits = morok::g3::training::split_by_group_id(
            dataset, morok::g3::training::SplitConfig{0.85, 0.10, 0.05, seed});
        morok::g3::neural::G3Backend backend(
            morok::g3::neural::G3Config{seed, 1.0e-5F});
        (void)morok::g3::training::load_fp32_model_weights(argv[2], backend);
        const auto report = morok::g3::training::evaluate_generalization(
            backend, dataset, splits,
            morok::g3::training::EvaluationConfig{seed, 0.25});
        morok::g3::training::write_generalization_report(argv[3], report);
        std::cout << "Relatorio G3 salvo: " << argv[3] << '\n'
                  << "Parametros: " << report.parameter_count << '\n'
                  << "Loss treino/validacao/teste: " << report.training_loss << " / "
                  << report.validation_loss << " / " << report.clean_test_loss << '\n'
                  << "Gap teste-treino: " << report.test_minus_training_loss << '\n'
                  << "Alerta de overfitting: "
                  << (report.overfit_warning ? "sim" : "nao") << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Falha na avaliacao G3: " << error.what() << '\n';
        return 1;
    }
}
