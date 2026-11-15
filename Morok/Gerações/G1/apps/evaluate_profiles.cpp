#include "morok/model/learn/g1_checkpoint.hpp"
#include "morok/g1/tsukuyomi/profile.hpp"
#include "morok/g1/mokosh/profile.hpp"
#include "morok/g1/apollo/profile.hpp"
#include "morok/g1/tsukuyomi_max/profile.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
struct Question {
    const char* id;
    const char* prompt;
    const char* expected;
};

const std::vector<Question> questions{
    {"sky_is_blue", "U: O céu é azul?\nIA: ", "Sim, o céu é azul."},
    {"grass_color", "U: Qual é a cor da grama?\nIA: ", "A grama geralmente é verde."},
    {"mountains_high_or_low", "U: As montanhas são altas?\nIA: ", "Sim, muitas montanhas são altas."},
    {"greeting", "U: Olá!\nIA: ", "Olá!"},
    {"english_sky", "U: Is the sky blue?\nIA: ", "Yes, the sky is blue."},
    {"spanish_sky", "U: ¿El cielo es azul?\nIA: ", "Sí, el cielo es azul."},
    {"multi_turn", "U: O céu é azul?\nIA: Sim, o céu é azul.\nU: E a grama?\nIA: ",
     "A grama geralmente é verde."}
};

void replace_all(std::string& value, std::string_view from, std::string_view to) {
    std::size_t position = 0;
    while ((position = value.find(from, position)) != std::string::npos) {
        value.replace(position, from.size(), to);
        position += to.size();
    }
}

std::string normalize(std::string_view input) {
    std::string value(input);
    const std::pair<std::string_view, std::string_view> accents[] = {
        {"á","a"},{"à","a"},{"â","a"},{"ã","a"},{"ä","a"},
        {"Á","a"},{"À","a"},{"Â","a"},{"Ã","a"},{"Ä","a"},
        {"é","e"},{"è","e"},{"ê","e"},{"ë","e"},
        {"É","e"},{"È","e"},{"Ê","e"},{"Ë","e"},
        {"í","i"},{"ì","i"},{"î","i"},{"ï","i"},
        {"Í","i"},{"Ì","i"},{"Î","i"},{"Ï","i"},
        {"ó","o"},{"ò","o"},{"ô","o"},{"õ","o"},{"ö","o"},
        {"Ó","o"},{"Ò","o"},{"Ô","o"},{"Õ","o"},{"Ö","o"},
        {"ú","u"},{"ù","u"},{"û","u"},{"ü","u"},
        {"Ú","u"},{"Ù","u"},{"Û","u"},{"Ü","u"},
        {"ç","c"},{"Ç","c"},{"ñ","n"},{"Ñ","n"}
    };
    for (const auto& [from, to] : accents) replace_all(value, from, to);

    std::string output;
    output.reserve(value.size());
    bool pending_space = false;
    for (const unsigned char byte : value) {
        if (byte < 128U && std::isalnum(byte)) {
            if (pending_space && !output.empty()) output.push_back(' ');
            pending_space = false;
            output.push_back(static_cast<char>(std::tolower(byte)));
        } else if (byte >= 128U) {
            if (pending_space && !output.empty()) output.push_back(' ');
            pending_space = false;
            output.push_back(static_cast<char>(byte));
        } else {
            pending_space = true;
        }
    }
    return output;
}

bool has_any(const std::string& text, const std::vector<std::string_view>& terms) {
    const std::string padded = " " + text + " ";
    for (const auto term : terms) {
        const std::string needle = " " + std::string(term) + " ";
        if (padded.find(needle) != std::string::npos) return true;
    }
    return false;
}

bool starts_with_any(const std::string& text,
                     const std::vector<std::string_view>& terms) {
    for (const auto term : terms) {
        if (text == term || (text.size() > term.size() &&
            text.compare(0, term.size(), term) == 0 && text[term.size()] == ' '))
            return true;
    }
    return false;
}

bool has_negated_target(const std::string& text,
                        const std::vector<std::string_view>& targets) {
    for (const auto target : targets) {
        const std::string t(target);
        const std::vector<std::string> patterns{
            "nao " + t, "nao e " + t, "nao esta " + t,
            "not " + t, "is not " + t, "isnt " + t,
            "no es " + t, "no " + t
        };
        for (const auto& pattern : patterns) {
            if (text.find(pattern) != std::string::npos) return true;
        }
    }
    return false;
}

bool is_correct(const std::string& raw_answer, const Question& question) {
    const std::string answer = normalize(raw_answer);
    if (answer.empty()) return false;

    const std::vector<std::string_view> negative_openers{
        "nao", "no", "not", "never", "nunca"
    };
    if (starts_with_any(answer, negative_openers)) return false;

    if (question.id == std::string_view("greeting")) {
        return has_any(answer, {"ola", "oi", "hello", "hi", "hola", "buenas"});
    }

    if (question.id == std::string_view("sky_is_blue") ||
        question.id == std::string_view("english_sky") ||
        question.id == std::string_view("spanish_sky")) {
        const std::vector<std::string_view> target{"azul", "blue"};
        if (has_negated_target(answer, target)) return false;
        if (has_any(answer, {"vermelho", "red"})) return false;
        return has_any(answer, target);
    }

    if (question.id == std::string_view("grass_color") ||
        question.id == std::string_view("multi_turn")) {
        const std::vector<std::string_view> target{"verde", "green"};
        if (has_negated_target(answer, target)) return false;
        if (has_any(answer, {"azul", "blue", "vermelho", "red", "amarelo",
                             "yellow", "marrom", "brown", "preto", "black"}))
            return false;
        return has_any(answer, target);
    }

    if (question.id == std::string_view("mountains_high_or_low")) {
        const std::vector<std::string_view> target{
            "alta", "altas", "alto", "altos", "high", "tall", "elevadas", "elevados"
        };
        if (has_negated_target(answer, target)) return false;
        if (has_any(answer, {"baixa", "baixas", "baixo", "baixos", "low",
                             "short", "pequenas", "pequenos"}))
            return false;
        return has_any(answer, target);
    }
    return false;
}

std::string tsv_safe(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (const char c : value) {
        if (c == '\t' || c == '\n' || c == '\r') output.push_back(' ');
        else output.push_back(c);
    }
    return output;
}

bool run_self_tests() {
    struct Test { const char* id; const char* answer; bool expected; };
    const Test tests[] = {
        {"sky_is_blue", "Sim, o céu é azul.", true},
        {"sky_is_blue", "Sim, azul.", true},
        {"sky_is_blue", "Sim, é azul.", true},
        {"sky_is_blue", "Sim, o nosso céu é azul.", true},
        {"sky_is_blue", "The sky is blue.", true},
        {"sky_is_blue", "Não, o céu não é azul.", false},
        {"sky_is_blue", "O céu é vermelho.", false},
        {"grass_color", "A grama geralmente é verde.", true},
        {"grass_color", "Verde.", true},
        {"grass_color", "Sim, é verde.", true},
        {"grass_color", "A grama não é verde.", false},
        {"grass_color", "A grama é amarela.", false},
        {"mountains_high_or_low", "Sim, muitas montanhas são altas.", true},
        {"mountains_high_or_low", "Yes, they are tall.", true},
        {"mountains_high_or_low", "Não, são baixas.", false},
        {"greeting", "Olá!", true},
        {"greeting", "Hello there!", true},
        {"greeting", "Até amanhã.", false},
        {"multi_turn", "A grama é verde.", true},
        {"multi_turn", "A grama não é verde, é azul.", false}
    };
    for (const auto& test : tests) {
        const auto it = std::find_if(questions.begin(), questions.end(),
            [&](const Question& q) { return test.id == std::string_view(q.id); });
        if (it == questions.end() || is_correct(test.answer, *it) != test.expected) {
            std::cerr << "semantic evaluator self-test failed: " << test.id
                      << " / " << test.answer << '\n';
            return false;
        }
    }
    std::cout << "Semantic evaluator self-tests passed (" << std::size(tests)
              << " cases).\n";
    return true;
}

template<class Generate>
void run_profile(const char* name, const morok::model::G1Parameters& parameters,
                 std::ofstream& output, Generate generate,
                 const std::vector<std::string>& selected, std::size_t attempts) {
    for (const auto& question : questions) {
        if (std::find(selected.begin(), selected.end(), question.id) == selected.end())
            continue;
        auto prompt = morok::common::Utf8Text::from_utf8(question.prompt);
        if (!prompt) throw std::runtime_error("evaluation prompt is not valid UTF-8");
        for (std::size_t attempt = 1; attempt <= attempts; ++attempt) {
            const auto start = std::chrono::steady_clock::now();
            const auto result = generate(parameters, *prompt.value_if(), attempt);
            const auto stop = std::chrono::steady_clock::now();
            const double latency_ms = std::chrono::duration<double, std::milli>(
                stop - start).count();
            const std::string answer(result.continuation.view());
            const bool correct = is_correct(answer, question);
            output << name << '\t' << question.id << '\t' << attempt << '\t'
                   << (correct ? "correct" : "incorrect") << '\t'
                   << latency_ms << '\t' << tsv_safe(answer) << '\n';
            if (!output) throw std::runtime_error("failed writing evaluation attempt");
        }
    }
}

std::vector<std::string> split(const std::string& value) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin < value.size()) {
        const auto end = value.find(',', begin);
        auto item = value.substr(begin, end == std::string::npos
            ? end : end - begin);
        if (!item.empty()) result.push_back(std::move(item));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return result;
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test")
        return run_self_tests() ? 0 : 1;
    if (argc != 6) {
        std::cerr << "usage: morok-g1-profile-evaluate <checkpoint> <results.tsv> "
                     "<profile> <question-ids-csv> <attempts>\n"
                     "       morok-g1-profile-evaluate --self-test\n";
        return 2;
    }
    try {
        const std::string profile = argv[3];
        const auto selected = split(argv[4]);
        std::size_t consumed = 0;
        const auto parsed_attempts = std::stoull(argv[5], &consumed);
        if (consumed != std::string(argv[5]).size())
            throw std::invalid_argument("attempts must be an integer");
        const auto attempts = static_cast<std::size_t>(parsed_attempts);
        if (attempts < 1 || attempts > 1000)
            throw std::invalid_argument("attempts must be in 1..1000");
        if (selected.empty())
            throw std::invalid_argument("at least one question is required");
        if (profile != "Tsukuyomi" && profile != "Apollo" &&
            profile != "Mokosh" && profile != "TsukuyomiMax")
            throw std::invalid_argument("unknown profile");
        for (const auto& id : selected) {
            if (std::none_of(questions.begin(), questions.end(),
                [&](const auto& question) { return id == question.id; }))
                throw std::invalid_argument("unknown question id: " + id);
        }

        const auto checkpoint = morok::model::load_g1_checkpoint(argv[1]);
        const bool new_file = !std::filesystem::exists(argv[2]) ||
            std::filesystem::file_size(argv[2]) == 0;
        std::ofstream output(argv[2], std::ios::app);
        if (!output) throw std::runtime_error("cannot open results file");
        if (new_file)
            output << "profile\tquestion_id\tattempt\tverdict\tlatency_ms\tanswer\n";

        if (profile == "Tsukuyomi") {
            run_profile(profile.c_str(), checkpoint.parameters, output,
                [](const auto& p, const auto& q, std::size_t attempt) {
                    morok::g1::tsukuyomi::Policy policy{};
                    policy.seed += attempt - 1;
                    return morok::g1::tsukuyomi::generate(p, q, policy);
                }, selected, attempts);
        } else if (profile == "Apollo") {
            run_profile(profile.c_str(), checkpoint.parameters, output,
                [](const auto& p, const auto& q, std::size_t attempt) {
                    morok::g1::apollo::Policy policy{};
                    policy.seed += attempt - 1;
                    return morok::g1::apollo::generate(p, q, policy);
                }, selected, attempts);
        } else if (profile == "Mokosh") {
            run_profile(profile.c_str(), checkpoint.parameters, output,
                [](const auto& p, const auto& q, std::size_t attempt) {
                    morok::g1::mokosh::Policy policy{};
                    policy.seed += attempt - 1;
                    return morok::g1::mokosh::generate(p, q, policy);
                }, selected, attempts);
        } else {
            run_profile(profile.c_str(), checkpoint.parameters, output,
                [](const auto& p, const auto& q, std::size_t attempt) {
                    morok::g1::tsukuyomi_max::Policy policy{};
                    policy.seed += attempt - 1;
                    return morok::g1::tsukuyomi_max::generate(p, q, policy);
                }, selected, attempts);
        }
        output.flush();
        if (!output) throw std::runtime_error("failed flushing evaluation results");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "profile-evaluate: " << error.what() << '\n';
        return 1;
    }
}
