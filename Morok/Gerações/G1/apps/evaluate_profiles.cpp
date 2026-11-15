#include "morok/model/learn/g1_checkpoint.hpp"
#include "morok/g1/tsukuyomi/profile.hpp"
#include "morok/g1/mokosh/profile.hpp"
#include "morok/g1/apollo/profile.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <utility>
#include <stdexcept>
#include <string>
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

std::string normalize(std::string_view input) {
    std::string output;
    output.reserve(input.size());
    bool pending_space = false;
    for (const unsigned char byte : input) {
        if (byte < 128U && std::isspace(byte)) {
            pending_space = !output.empty();
            continue;
        }
        if (byte < 128U && std::ispunct(byte)) continue;
        if (pending_space) output.push_back(' ');
        pending_space = false;
        output.push_back(static_cast<char>(byte < 128U
            ? std::tolower(byte) : byte));
    }
    return output;
}


struct Aggregate {
    std::size_t attempts{0};
    std::size_t correct{0};
    double total_latency_ms{0.0};
};

template<class Generate>
void run_profile(const char* name, const morok::model::G1Parameters& parameters,
                 std::ofstream& output, Generate generate,
                 const std::vector<std::string>& selected, std::size_t attempts) {
    for (const auto& question : questions) {
        if (std::find(selected.begin(), selected.end(), question.id) == selected.end())
            continue;

        auto prompt = morok::common::Utf8Text::from_utf8(question.prompt);
        if (!prompt) throw std::runtime_error("evaluation prompt is not valid UTF-8");

        Aggregate aggregate;
        const auto expected = normalize(question.expected);
        for (std::size_t attempt = 1; attempt <= attempts; ++attempt) {
            const auto start = std::chrono::steady_clock::now();
            const auto result = generate(parameters, *prompt.value_if(), attempt);
            const auto stop = std::chrono::steady_clock::now();
            aggregate.total_latency_ms += std::chrono::duration<double, std::milli>(
                stop - start).count();
            const auto answer = normalize(result.continuation.view());
            if (!expected.empty() && answer.find(expected) != std::string::npos)
                ++aggregate.correct;
            ++aggregate.attempts;
        }

        const double accuracy = aggregate.attempts == 0 ? 0.0
            : static_cast<double>(aggregate.correct) /
              static_cast<double>(aggregate.attempts);
        const double mean_latency = aggregate.attempts == 0 ? 0.0
            : aggregate.total_latency_ms / static_cast<double>(aggregate.attempts);
        output << name << '\t' << question.id << '\t' << aggregate.attempts
               << '\t' << accuracy << '\t' << mean_latency << '\t'
               << (aggregate.correct == aggregate.attempts ? "pass" : "fail") << '\n';
        if (!output) throw std::runtime_error("failed writing evaluation aggregate");
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
    if (argc != 6) {
        std::cerr << "usage: morok-g1-profile-evaluate <checkpoint> <results.tsv> "
                     "<profile> <question-ids-csv> <attempts>\n";
        return 2;
    }
    try {
        const std::string profile = argv[3];
        const auto selected = split(argv[4]);
        const auto attempts = static_cast<std::size_t>(std::stoull(argv[5]));
        if (attempts < 1 || attempts > 1000)
            throw std::invalid_argument("attempts must be in 1..1000");
        if (selected.empty())
            throw std::invalid_argument("at least one question is required");
        if (profile != "Tsukuyomi" && profile != "Apollo" && profile != "Mokosh")
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
            output << "profile\tquestion_id\tattempts\taccuracy\tmean_latency_ms\tverdict\n";

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
        } else {
            run_profile(profile.c_str(), checkpoint.parameters, output,
                [](const auto& p, const auto& q, std::size_t attempt) {
                    morok::g1::mokosh::Policy policy{};
                    policy.seed += attempt - 1;
                    return morok::g1::mokosh::generate(p, q, policy);
                }, selected, attempts);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "profile-evaluate: " << error.what() << '\n';
        return 1;
    }
}
