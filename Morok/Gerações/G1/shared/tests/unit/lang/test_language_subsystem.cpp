#include "morok/common/utf8_text.hpp"
#include "morok/core/cancellation.hpp"
#include "morok/core/configuration.hpp"
#include "morok/core/request_context.hpp"
#include "morok/core/session_registry.hpp"
#include "morok/lang/language_subsystem.hpp"
#include "morok/model/parameters/g1_parameters.hpp"

#include <cassert>
#include <string>

int main() {
    using namespace morok;

    model::G1Parameters parameters;
    model::initialize_g1_parameters(parameters, 42);
    lang::LanguageSubsystem language(parameters);
    core::RuntimeConfig config;
    config.max_sessions = 2;
    config.resources.max_generation_bytes = 8;
    core::Runtime runtime(config);
    assert(runtime.initialize(
        {language.initializer()}, {language.shutdown_callback()}));
    assert(language.initialized());

    core::SessionRegistry sessions(2);
    auto first = language.create_session(sessions);
    auto second = language.create_session(sessions);
    assert(first && second);
    assert(language.active_sessions() == 2);
    assert(sessions.size() == 2);

    auto request = core::RequestContext::create(*first.value_if());
    auto other_request = core::RequestContext::create(*second.value_if());
    assert(request && other_request);

    auto prompt = common::Utf8Text::from_utf8("  Olá,\n   Morok!  ");
    assert(prompt);
    assert(language.observe(*request.value_if(), *prompt.value_if()));

    auto generated = language.generate(*request.value_if(), 4);
    assert(generated);
    assert(generated.value_if()->generated_bytes <= 4);
    assert(generated.value_if()->input_language == lang::DetectedLanguage::portuguese);
    assert(generated.value_if()->input_token_count == 4);
    assert(generated.value_if()->output_token_count <= generated.value_if()->generated_bytes);
    assert(generated.value_if()->text.size_bytes() ==
           generated.value_if()->generated_bytes);
    assert(common::Utf8Text::from_utf8(
        generated.value_if()->text.view()));

    auto english_prompt = common::Utf8Text::from_utf8("Is the sky blue?");
    assert(english_prompt);
    assert(language.observe(*other_request.value_if(), *english_prompt.value_if()));
    auto second_generated = language.generate(*other_request.value_if(), 4);
    assert(second_generated);
    assert(second_generated.value_if()->text.size_bytes() <= 4);
    assert(second_generated.value_if()->input_language == lang::DetectedLanguage::english);
    assert(second_generated.value_if()->input_token_count == 5);

    auto followup = common::Utf8Text::from_utf8("E a grama?");
    assert(followup);
    assert(language.observe(*request.value_if(), *followup.value_if()));
    auto followup_generated = language.generate(*request.value_if(), 4);
    assert(followup_generated);
    assert(followup_generated.value_if()->input_token_count == 4);

    auto oversized = language.generate(*request.value_if(), 9);
    assert(!oversized);
    assert(oversized.error_if()->code == common::ErrorCode::resource_limit);

    core::CancellationSource cancellation;
    assert(cancellation.cancel());
    auto cancelled_request = core::RequestContext::create(
        *first.value_if(), cancellation.token());
    assert(cancelled_request);
    auto cancelled = language.generate(*cancelled_request.value_if(), 1);
    assert(!cancelled);
    assert(cancelled.error_if()->code == common::ErrorCode::cancelled);

    assert(language.reset_session(*first.value_if()));
    assert(language.close_session(sessions, *first.value_if()));
    assert(!sessions.contains(*first.value_if()));
    assert(language.active_sessions() == 1);

    auto stale = language.generate(*request.value_if(), 1);
    assert(!stale);
    assert(stale.error_if()->code == common::ErrorCode::invalid_argument);

    auto third = language.create_session(sessions);
    assert(third);
    auto spanish_request = core::RequestContext::create(*third.value_if());
    assert(spanish_request);
    auto spanish_prompt = common::Utf8Text::from_utf8("¿El cielo es azul?");
    assert(spanish_prompt);
    assert(language.observe(*spanish_request.value_if(), *spanish_prompt.value_if()));
    auto spanish_generated = language.generate(*spanish_request.value_if(), 4);
    assert(spanish_generated);
    assert(spanish_generated.value_if()->input_language == lang::DetectedLanguage::spanish);
    assert(spanish_generated.value_if()->input_token_count == 6);
    assert(language.close_session(sessions, *third.value_if()));

    assert(language.close_session(sessions, *second.value_if()));
    assert(language.active_sessions() == 0);
    assert(sessions.size() == 0);
    assert(runtime.shutdown());
    assert(!language.initialized());

    auto unavailable = language.create_session(sessions);
    assert(!unavailable);
    assert(unavailable.error_if()->code == common::ErrorCode::model_not_loaded);
    return 0;
}
