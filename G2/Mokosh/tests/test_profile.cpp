#include "morok/g2/mokosh/profile.hpp"
#include "morok/model/parameters/g2_parameters.hpp"
#include <cassert>
int main(){morok::model::G2Parameters p{};morok::model::initialize_g2_parameters(p,31);auto prompt=morok::common::Utf8Text::from_utf8("A");assert(prompt);morok::g2::mokosh::Policy policy{};policy.max_new_bytes=1;auto report=morok::g2::mokosh::generate(p,*prompt.value_if(),policy);assert(report.generated_bytes<=1);}
