#include "morok/lang/rlah/segmenter.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

int main() {
    using namespace morok::lang::rlah;

    const std::string source =
        "abracadabra abracadabra abracadabra abracadabra ";
    const std::vector<std::uint8_t> raw(source.begin(), source.end());
    Segmenter segmenter;
    const auto candidates = segmenter.evaluate(raw);
    assert(candidates.size() <= 256);

    const auto raw_encoded = encode(raw, candidates);
    std::vector<std::uint8_t> decoded;
    std::string error;
    if (!decode(raw_encoded.data, decoded, error)) {
        std::cerr << "RLAH raw decode failed: " << error << '\n';
        return 1;
    }
    assert(decoded == raw);

    std::string repetitive;
    for (int i = 0; i < 32; ++i) repetitive += "abracadabra";
    const std::vector<std::uint8_t> repetitive_bytes(
        repetitive.begin(), repetitive.end());
    Candidate dictionary_entry;
    dictionary_entry.bytes.assign(
        {'a', 'b', 'r', 'a', 'c', 'a', 'd', 'a', 'b', 'r', 'a'});
    dictionary_entry.active = true;
    dictionary_entry.utility = 1.0F;
    const auto compressed = encode(
        repetitive_bytes, std::span<const Candidate>(&dictionary_entry, 1));
    assert(compressed.compressed);
    if (!decode(compressed.data, decoded, error)) {
        std::cerr << "RLAH compressed decode failed: " << error << '\n';
        return 1;
    }
    assert(decoded == repetitive_bytes);

    auto corrupted = compressed.data;
    corrupted.back() ^= 1U;
    assert(!decode(corrupted, decoded, error));

    Scorer scorer;
    std::vector<Candidate> training_candidates(16);
    std::vector<float> labels(16, 1.0F);
    for (auto& candidate : training_candidates)
        candidate.bytes = {1, 2};
    assert(scorer.train(training_candidates, labels));
}
