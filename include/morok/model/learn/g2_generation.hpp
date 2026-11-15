#pragma once
#include "morok/common/utf8_text.hpp"
#include "morok/g2/engine/generation.hpp"
#include "morok/model/parameters/g2_parameters.hpp"
namespace morok::model {
struct G2GenerationReport {
    common::Utf8Text continuation;
    std::size_t candidate_evaluations{0},generated_bytes{0};
    bool reached_byte_limit{false},stopped_by_eos{false};
};
// Generate one continuation per expert, then select by cross-expert mean NLL.
[[nodiscard]] G2GenerationReport generate_g2(const G2Parameters&,const common::Utf8Text&,const G2ExpertGenerationConfig& = {});
}
