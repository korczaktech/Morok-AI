#include "morok/g2/tsukuyomi/model.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

// Os testes precisam continuar ativos também em builds Release (-DNDEBUG).
#undef assert
#define assert(condition) do { if (!(condition)) { std::fprintf(stderr, "Assertion failed at line %d: %s\n", __LINE__, #condition); std::abort(); } } while (false)

int main() {
    using namespace morok::g2::tsukuyomi;

    static_assert(kParameterCount == 10000);
    static_assert(kCheckpointFormatVersion == 8);
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

    // Verificação por diferenças finitas: gradiente analítico vs. derivada numérica.
    const std::array<std::uint8_t, 6> gradient_sample{'Q', 'u', 'e', 's', 't', 'o'};
    std::array<float, kParameterCount> analytic{};
    const auto check_gradient = [&](std::size_t target, std::size_t parameter) {
        assert(compute_target_gradient(model, gradient_sample, target, analytic));
        constexpr float epsilon = 1.0e-3F;
        Model plus = model;
        Model minus = model;
        plus.parameters[parameter] += epsilon;
        minus.parameters[parameter] -= epsilon;
        std::array<std::uint8_t, 6> one_target_mask{};
        one_target_mask[target] = 1;
        const double loss_plus = evaluate_loss(plus, gradient_sample, one_target_mask);
        const double loss_minus = evaluate_loss(minus, gradient_sample, one_target_mask);
        const double numerical = (loss_plus - loss_minus) / (2.0 * epsilon);
        const double error = std::abs(numerical - analytic[parameter]);
        assert(error <= 3.0e-2 + 3.0e-2 * std::abs(numerical));
    };
    check_gradient(0, 9984); // vetor inicial
    check_gradient(3, 0);    // embedding usado na saída
    check_gradient(3, 101 * kHiddenSize); // embedding de entrada compartilhado
    check_gradient(3, 0xC0 * kHiddenSize); // protótipo aprendido de letra maiúscula
    check_gradient(3, 0xFC * kHiddenSize); // protótipo aprendido de início de palavra
    check_gradient(3, 8192); // projeção oculta
    check_gradient(3, 9216); // projeção de contexto
    check_gradient(3, 9632); // bias oculto
    check_gradient(3, 9664); // bias de saída
    check_gradient(3, 9920); // escala
    check_gradient(3, 9952); // deslocamento

    // Varredura estratificada de diferenças finitas ao longo dos 10.000 offsets (passo 137), incluindo o caminho incremental.
    {
        const std::string broad_text = "U: ABCDEFGH\nIA:xy";
        const std::vector<std::uint8_t> broad_bytes(broad_text.begin(), broad_text.end());
        const std::size_t target = broad_bytes.size() - 1;
        std::vector<std::uint8_t> mask(broad_bytes.size(), 0);
        mask[target] = 1;
        assert(compute_target_gradient(model, broad_bytes, target, analytic));
        constexpr float epsilon = 1.0e-3F;
        for (std::size_t parameter = 0; parameter < kParameterCount; parameter += 137) {
            Model plus = model;
            Model minus = model;
            plus.parameters[parameter] += epsilon;
            minus.parameters[parameter] -= epsilon;
            const double numerical =
                (evaluate_loss(plus, broad_bytes, mask) -
                 evaluate_loss(minus, broad_bytes, mask)) / (2.0 * epsilon);
            const double error = std::abs(numerical - analytic[parameter]);
            assert(error <= 3.0e-2 + 3.0e-2 * std::abs(numerical));
        }
    }

    // O gradiente também deve refletir a pergunta inteira no protocolo U:/IA:.
    const std::array<std::uint8_t, 13> question_gradient_sample{
        'U', ':', 'A', 'B', 'C', 'D', 'E', 'F', 'G', '\n', 'I', 'A', ':'
    };
    assert(compute_target_gradient(model, question_gradient_sample, 12, analytic));
    {
        constexpr float epsilon = 1.0e-3F;
        Model plus = model;
        Model minus = model;
        plus.parameters[9216] += epsilon;
        minus.parameters[9216] -= epsilon;
        std::array<std::uint8_t, 13> one_target_mask{};
        one_target_mask[12] = 1;
        const double loss_plus = evaluate_loss(plus, question_gradient_sample, one_target_mask);
        const double loss_minus = evaluate_loss(minus, question_gradient_sample, one_target_mask);
        const double numerical = (loss_plus - loss_minus) / (2.0 * epsilon);
        assert(std::abs(numerical - analytic[9216]) <=
               3.0e-2 + 3.0e-2 * std::abs(numerical));
    }

    // O gradiente alcança os embeddings de todos os bytes da pergunta.
    assert(compute_target_gradient(model, question_gradient_sample, 12, analytic));
    {
        constexpr float epsilon = 1.0e-3F;
        Model plus = model;
        Model minus = model;
        const std::size_t question_embedding =
            static_cast<std::size_t>('A') * kHiddenSize;
        plus.parameters[question_embedding] += epsilon;
        minus.parameters[question_embedding] -= epsilon;
        std::array<std::uint8_t, 13> one_target_mask{};
        one_target_mask[12] = 1;
        const double loss_plus = evaluate_loss(plus, question_gradient_sample, one_target_mask);
        const double loss_minus = evaluate_loss(minus, question_gradient_sample, one_target_mask);
        const double numerical = (loss_plus - loss_minus) / (2.0 * epsilon);
        assert(std::abs(numerical - analytic[question_embedding]) <=
               3.0e-2 + 3.0e-2 * std::abs(numerical));
    }

    // Canais linguísticos: letras da mesma classe compartilham o protótipo
    // de categoria, enquanto maiúsculas e minúsculas usam protótipos distintos.
    {
        Model linguistic_model;
        linguistic_model.parameters.fill(0.0F);
        for (std::size_t h = 0; h < kHiddenSize; ++h) {
            linguistic_model.parameters[8192 + h * kHiddenSize + h] = 0.1F;
            linguistic_model.parameters[9920 + h] = 1.0F;
        }
        linguistic_model.parameters[0xC0 * kHiddenSize] = 0.7F; // maiúsculas
        linguistic_model.parameters[0xC1 * kHiddenSize] = -0.7F; // minúsculas
        const std::array<std::uint8_t, 1> upper_a{'A'}, upper_b{'B'}, lower_a{'a'};
        assert(forward(linguistic_model, upper_a).hidden ==
               forward(linguistic_model, upper_b).hidden);
        assert(forward(linguistic_model, upper_a).hidden !=
               forward(linguistic_model, lower_a).hidden);
    }

    // O canal de categoria também recebe gradiente pela representação da pergunta.
    {
        assert(compute_target_gradient(model, question_gradient_sample, 12, analytic));
        constexpr float epsilon = 1.0e-3F;
        Model plus = model;
        Model minus = model;
        const std::size_t feature_parameter = 0xC0 * kHiddenSize;
        plus.parameters[feature_parameter] += epsilon;
        minus.parameters[feature_parameter] -= epsilon;
        std::array<std::uint8_t, 13> one_target_mask{};
        one_target_mask[12] = 1;
        const double loss_plus = evaluate_loss(plus, question_gradient_sample, one_target_mask);
        const double loss_minus = evaluate_loss(minus, question_gradient_sample, one_target_mask);
        const double numerical = (loss_plus - loss_minus) / (2.0 * epsilon);
        assert(std::abs(numerical - analytic[feature_parameter]) <=
               3.0e-2 + 3.0e-2 * std::abs(numerical));
    }

    // As 13 faixas são regiões posicionais contíguas, não buckets cíclicos.
    {
        Model positional_model;
        positional_model.parameters.fill(0.0F);
        positional_model.parameters[static_cast<std::size_t>('A') * kHiddenSize] = 1.0F;
        positional_model.parameters[9216] = 1.0F; // primeira região, dimensão 0
        positional_model.parameters[9216 + 12 * kHiddenSize] = 2.0F; // última região
        positional_model.parameters[9920] = 1.0F;
        const std::string first_text = "U: A............\nIA:";
        const std::string last_text  = "U: ............A\nIA:";
        const std::vector<std::uint8_t> first_prompt(first_text.begin(), first_text.end());
        const std::vector<std::uint8_t> last_prompt(last_text.begin(), last_text.end());
        assert(forward(positional_model, first_prompt).hidden !=
               forward(positional_model, last_prompt).hidden);
    }

    // Perguntas longas que diferem no início não podem colapsar ao mesmo contexto.
    const std::string prompt_a = "U: A pergunta inteira tem contexto longo?\nIA:";
    const std::string prompt_b = "U: B pergunta inteira tem contexto longo?\nIA:";
    const std::vector<std::uint8_t> bytes_a(prompt_a.begin(), prompt_a.end());
    const std::vector<std::uint8_t> bytes_b(prompt_b.begin(), prompt_b.end());
    assert(forward(model, bytes_a).hidden != forward(model, bytes_b).hidden);

    // A resposta anterior precisa influenciar a previsão: pergunta e último
    // byte iguais, mas histórico de resposta diferente, devem gerar estados distintos.
    const std::string history_a = "U: Q\nIA:ab";
    const std::string history_b = "U: Q\nIA:zb";
    const std::vector<std::uint8_t> history_bytes_a(history_a.begin(), history_a.end());
    const std::vector<std::uint8_t> history_bytes_b(history_b.begin(), history_b.end());
    assert(forward(model, history_bytes_a).hidden !=
           forward(model, history_bytes_b).hidden);

    // Memória temporal: uma diferença antiga deve continuar influenciando
    // o estado após mais de uma janela TBPTT de bytes comuns.
    {
        Model memory_model;
        memory_model.parameters.fill(0.0F);
        for (std::size_t h = 0; h < kHiddenSize; ++h)
            memory_model.parameters[9920 + h] = 1.0F;
        memory_model.parameters[9632] = 0.2F;
        memory_model.parameters[8192] = 8.0F;
        memory_model.parameters[static_cast<std::size_t>('a') * kHiddenSize] = 0.8F;
        memory_model.parameters[static_cast<std::size_t>('z') * kHiddenSize] = -0.8F;
        std::string memory_a = "U: Q\nIA:a";
        std::string memory_b = "U: Q\nIA:z";
        memory_a.append(24, 'x');
        memory_b.append(24, 'x');
        const std::vector<std::uint8_t> memory_bytes_a(memory_a.begin(), memory_a.end());
        const std::vector<std::uint8_t> memory_bytes_b(memory_b.begin(), memory_b.end());
        assert(forward(memory_model, memory_bytes_a).hidden !=
               forward(memory_model, memory_bytes_b).hidden);
    }

    // Ablations causais: remover a projeção da pergunta elimina a influência
    // da pergunta; remover a matriz recorrente elimina a influência do histórico.
    {
        Model context_ablated = model;
        std::fill(context_ablated.parameters.begin() + 9216,
                  context_ablated.parameters.begin() + 9632, 0.0F);
        const std::string same_answer_a = "U: pergunta A\nIA:";
        const std::string same_answer_b = "U: pergunta B\nIA:";
        const std::vector<std::uint8_t> prompt_a(same_answer_a.begin(), same_answer_a.end());
        const std::vector<std::uint8_t> prompt_b(same_answer_b.begin(), same_answer_b.end());
        assert(forward(context_ablated, prompt_a).hidden ==
               forward(context_ablated, prompt_b).hidden);

        Model recurrence_ablated = model;
        std::fill(recurrence_ablated.parameters.begin() + 8192,
                  recurrence_ablated.parameters.begin() + 9216, 0.0F);
        assert(forward(recurrence_ablated, history_bytes_a).hidden ==
               forward(recurrence_ablated, history_bytes_b).hidden);
    }

    // A matriz recorrente compartilhada altera o estado após processar a sequência.
    Model recurrent_changed = model;
    recurrent_changed.parameters[8192] += 0.5F;
    assert(forward(model, history_bytes_a).hidden !=
           forward(recurrent_changed, history_bytes_a).hidden);

    // A ordem dos bytes anteriores importa: o estado é recorrente, não uma soma sem ordem.
    const std::string ordered_a = "U: Q\nIA:abc";
    const std::string ordered_b = "U: Q\nIA:acb";
    const std::vector<std::uint8_t> ordered_bytes_a(ordered_a.begin(), ordered_a.end());
    const std::vector<std::uint8_t> ordered_bytes_b(ordered_b.begin(), ordered_b.end());
    assert(forward(model, ordered_bytes_a).hidden !=
           forward(model, ordered_bytes_b).hidden);

    // O gradiente da projeção contextual inclui o byte anterior da resposta.
    const std::string history_target = "U: Q\nIA:abc";
    const std::vector<std::uint8_t> history_target_bytes(
        history_target.begin(), history_target.end());
    const std::size_t history_target_index = history_target_bytes.size() - 1;
    assert(compute_target_gradient(model, history_target_bytes,
                                   history_target_index, analytic));
    {
        constexpr float epsilon = 1.0e-3F;
        Model plus = model;
        Model minus = model;
        plus.parameters[9232] += epsilon;
        minus.parameters[9232] -= epsilon;
        std::vector<std::uint8_t> history_mask(history_target_bytes.size(), 0);
        history_mask[history_target_index] = 1;
        const double loss_plus = evaluate_loss(plus, history_target_bytes, history_mask);
        const double loss_minus = evaluate_loss(minus, history_target_bytes, history_mask);
        const double numerical = (loss_plus - loss_minus) / (2.0 * epsilon);
        assert(std::abs(numerical - analytic[9232]) <=
               3.0e-2 + 3.0e-2 * std::abs(numerical));
    }

    // A geração gulosa deve parar no primeiro terminador, sem produzir bytes extras.
    Model newline_model;
    newline_model.parameters[9664 + static_cast<std::size_t>('\n')] = 10.0F;
    const std::array<std::uint8_t, 3> generation_prompt{'I', 'A', ':'};
    const auto stopped = generate(newline_model, generation_prompt, 32);
    assert(stopped.size() == generation_prompt.size() + 1);
    assert(stopped.back() == static_cast<std::uint8_t>('\n'));

    // Decodificação configurável: penalidade reduz repetição sem alterar pesos.
    {
        Model decoding_model;
        decoding_model.parameters.fill(0.0F);
        decoding_model.parameters[9664 + static_cast<std::size_t>('a')] = 1.0F;
        decoding_model.parameters[9664 + static_cast<std::size_t>('b')] = 0.9F;
        const std::array<std::uint8_t, 3> prompt{'I', 'A', ':'};
        const auto greedy = generate(decoding_model, prompt, 2);
        assert(greedy.size() == prompt.size() + 2);
        assert(greedy[prompt.size()] == static_cast<std::uint8_t>('a'));
        assert(greedy[prompt.size() + 1] == static_cast<std::uint8_t>('a'));

        GenerationConfig penalty_config;
        penalty_config.repetition_penalty = 1.2;
        penalty_config.repetition_window = 8;
        const auto penalized = generate(decoding_model, prompt, 2, penalty_config);
        assert(penalized.size() == prompt.size() + 2);
        assert(penalized[prompt.size()] == static_cast<std::uint8_t>('a'));
        assert(penalized[prompt.size() + 1] == static_cast<std::uint8_t>('b'));

        GenerationConfig sampling_config;
        sampling_config.sample = true;
        sampling_config.temperature = 0.7;
        sampling_config.top_k = 2;
        sampling_config.top_p = 0.9;
        sampling_config.seed = 9876;
        assert(generate(decoding_model, prompt, 16, sampling_config) ==
               generate(decoding_model, prompt, 16, sampling_config));
        sampling_config.top_p = 0.0;
        assert(generate(decoding_model, prompt, 16, sampling_config).size() == prompt.size());
    }

    // Caminho com U:/IA: usa a representação da pergunta pré-calculada.
    const std::string cached_prompt_text =
        "U: Uma pergunta longa para exercitar o cache contextual da inferência?\nIA:";
    const std::vector<std::uint8_t> cached_prompt(
        cached_prompt_text.begin(), cached_prompt_text.end());
    const auto cached_stopped = generate(newline_model, cached_prompt, 32);
    assert(cached_stopped.size() == cached_prompt.size() + 1);
    assert(cached_stopped.back() == static_cast<std::uint8_t>('\n'));

    // Hiperparâmetros inválidos não podem alterar o modelo.
    Model invalid_config_model = model;
    TrainingConfig invalid_config;
    invalid_config.weight_decay = std::numeric_limits<double>::quiet_NaN();
    const auto invalid_report = train(invalid_config_model, sample, invalid_config);
    assert(invalid_report.epochs_completed == 0);
    assert(invalid_config_model.parameters == model.parameters);
    invalid_config.weight_decay = -0.01;
    const auto negative_decay_report = train(invalid_config_model, sample, invalid_config);
    assert(negative_decay_report.epochs_completed == 0);
    assert(invalid_config_model.parameters == model.parameters);
    invalid_config.weight_decay = 0.0;
    invalid_config.learning_rate = std::numeric_limits<double>::infinity();
    assert(train(invalid_config_model, sample, invalid_config).epochs_completed == 0);
    invalid_config.learning_rate = 0.001;
    invalid_config.gradient_clip = std::numeric_limits<double>::quiet_NaN();
    assert(train(invalid_config_model, sample, invalid_config).epochs_completed == 0);
    invalid_config.gradient_clip = 1.0;
    invalid_config.batch_size = 0;
    assert(train(invalid_config_model, sample, invalid_config).epochs_completed == 0);
    invalid_config.batch_size = 1;
    invalid_config.bptt_steps = kBpttTruncation + 1;
    assert(train(invalid_config_model, sample, invalid_config).epochs_completed == 0);
    invalid_config.bptt_steps = kBpttTruncation;
    const std::array<std::uint8_t, 1> invalid_mask{1};
    assert(train(invalid_config_model, sample, invalid_config, nullptr,
                 invalid_mask).epochs_completed == 0);
    const std::array<std::uint8_t, 6> empty_mask{};
    assert(train(invalid_config_model, sample, invalid_config, nullptr,
                 empty_mask).epochs_completed == 0);
    assert(invalid_config_model.parameters == model.parameters);

    // Weight decay altera matrizes, mas deixa biases e parâmetros afins intactos.
    Model without_decay;
    Model with_decay;
    initialize(without_decay, 777);
    initialize(with_decay, 777);
    TrainingConfig no_decay_config;
    no_decay_config.epochs = 1;
    no_decay_config.batch_size = sample.size();
    no_decay_config.weight_decay = 0.0;
    TrainingConfig selective_decay_config = no_decay_config;
    selective_decay_config.weight_decay = 0.5;
    const auto no_decay_report = train(without_decay, sample, no_decay_config);
    const auto selective_decay_report = train(with_decay, sample, selective_decay_config);
    assert(no_decay_report.epochs_completed == 1);
    assert(selective_decay_report.epochs_completed == 1);
    assert(with_decay.parameters[8192] != without_decay.parameters[8192]);
    for (std::size_t i = 9632; i < kParameterCount; ++i)
        assert(with_decay.parameters[i] == without_decay.parameters[i]);
    for (std::size_t i = 9216; i < 9632; ++i)
        assert(with_decay.parameters[i] != without_decay.parameters[i]);

    // Auditoria de Adam: primeiro passo, bias correction e clipping global.
    {
        Model adam_model;
        initialize(adam_model, 456);
        std::array<std::uint8_t, 6> adam_mask{};
        constexpr std::size_t target = 3;
        adam_mask[target] = 1;
        std::array<float, kParameterCount> raw_gradient{};
        assert(compute_target_gradient(adam_model, gradient_sample, target, raw_gradient));
        double norm_squared = 0.0;
        for (const float value : raw_gradient)
            norm_squared += static_cast<double>(value) * value;
        const double norm = std::sqrt(norm_squared);
        TrainingConfig adam_config;
        adam_config.epochs = 1;
        adam_config.batch_size = 1;
        adam_config.learning_rate = 0.001;
        adam_config.weight_decay = 0.0;
        adam_config.gradient_clip = 0.5;
        TrainingState adam_state;
        const std::size_t audited_parameter = 9664 + gradient_sample[target];
        const double clipped_gradient = raw_gradient[audited_parameter] *
            std::min(1.0, adam_config.gradient_clip / norm);
        const double expected_parameter =
            adam_model.parameters[audited_parameter] -
            adam_config.learning_rate * clipped_gradient /
                (std::abs(clipped_gradient) + 1.0e-8);
        const auto adam_report = train(
            adam_model, gradient_sample, adam_config, &adam_state, adam_mask);
        assert(adam_report.epochs_completed == 1);
        assert(adam_report.target_bytes == 1);
        assert(adam_state.step == 1);
        assert(std::abs(adam_model.parameters[audited_parameter] -
                        expected_parameter) < 1.0e-7);
        assert(std::abs(adam_state.first_moment[audited_parameter] -
                        0.1 * clipped_gradient) < 1.0e-7);
        assert(std::abs(adam_state.second_moment[audited_parameter] -
                        0.001 * clipped_gradient * clipped_gradient) < 1.0e-9);
    }

    const auto before = evaluate_loss(model, sample);
    assert(std::isfinite(before) && before > 0.0);

    const auto prediction = forward(model, sample);
    for (float value : prediction.logits)
        assert(std::isfinite(value));

    TrainingConfig config;
    config.epochs = 2;
    config.learning_rate = 0.002;
    config.batch_size = 1;
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

    // BPTT com janela 1 e janela completa devem propagar sinais diferentes.
    {
        const std::string temporal_text = "U: Q\nIA:abcdef";
        const std::vector<std::uint8_t> temporal_bytes(
            temporal_text.begin(), temporal_text.end());
        std::vector<std::uint8_t> final_byte_mask(temporal_bytes.size(), 0);
        final_byte_mask.back() = 1;
        Model truncated_one, truncated_full;
        initialize(truncated_one, 654);
        initialize(truncated_full, 654);
        TrainingConfig one_step_config;
        one_step_config.epochs = 1;
        one_step_config.batch_size = 1;
        one_step_config.bptt_steps = 1;
        one_step_config.weight_decay = 0.0;
        TrainingConfig full_window_config = one_step_config;
        full_window_config.bptt_steps = kBpttTruncation;
        const auto one_step_report = train(
            truncated_one, temporal_bytes, one_step_config, nullptr, final_byte_mask);
        const auto full_window_report = train(
            truncated_full, temporal_bytes, full_window_config, nullptr, final_byte_mask);
        assert(one_step_report.epochs_completed == 1);
        assert(full_window_report.epochs_completed == 1);
        assert(truncated_one.parameters != truncated_full.parameters);
    }

    // Com 32 bytes de resposta, BPTT=64 permite que a perda alcance o contexto da pergunta.
    {
        const std::string dialogue = "U: pergunta\nIA:" + std::string(32, 'a');
        const std::vector<std::uint8_t> bytes(dialogue.begin(), dialogue.end());
        assert(compute_target_gradient(model, bytes, bytes.size() - 1, analytic));
        bool question_gradient_present = false;
        for (std::size_t i = 9216; i < 9632; ++i)
            question_gradient_present = question_gradient_present ||
                std::abs(analytic[i]) > 1.0e-8F;
        assert(question_gradient_present);
    }

    // Quando o histórico excede 64 etapas, o gradiente não atravessa o limite TBPTT.
    {
        const std::string long_dialogue = "U: Q\nIA:" + std::string(80, 'a');
        const std::vector<std::uint8_t> long_bytes(
            long_dialogue.begin(), long_dialogue.end());
        assert(compute_target_gradient(
            model, long_bytes, long_bytes.size() - 1, analytic));
        for (std::size_t i = 9216; i < 9632; ++i)
            assert(std::abs(analytic[i]) < 1.0e-8F);
        for (std::size_t i = 9984; i < 10000; ++i)
            assert(std::abs(analytic[i]) < 1.0e-8F);
    }

    // O treinamento supervisionado atualiza somente os bytes marcados como resposta.
    const std::array<std::uint8_t, 9> dialogue{
        'U', ':', 'Q', '\n', 'I', 'A', ':', 'A', '\n'
    };
    const std::array<std::uint8_t, 9> answer_mask{
        0, 0, 0, 0, 0, 0, 0, 1, 1
    };
    Model supervised;
    initialize(supervised, 321);
    TrainingState supervised_state;
    TrainingConfig supervised_config;
    supervised_config.epochs = 2;
    supervised_config.batch_size = 1;
    const auto supervised_report = train(
        supervised, dialogue, supervised_config, &supervised_state, answer_mask);
    assert(supervised_report.epochs_completed == 2);
    assert(supervised_report.target_bytes == 4);
    assert(std::isfinite(supervised_report.initial_loss));
    assert(std::isfinite(supervised_report.final_loss));
    assert(std::isfinite(evaluate_loss(supervised, dialogue, answer_mask)));

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
