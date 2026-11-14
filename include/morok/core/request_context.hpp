#pragma once

#include "morok/common/error.hpp"
#include "morok/common/ids.hpp"
#include "morok/common/result.hpp"
#include "morok/common/timestamp.hpp"
#include "morok/core/cancellation.hpp"

#include <utility>

namespace morok::core {

class RequestContext final {
public:
    [[nodiscard]] static common::Result<RequestContext> create(
        common::SessionId session_id,
        CancellationToken cancellation = {}) {
        auto generated = common::RequestId::generate();
        if (!generated) {
            return common::Result<RequestContext>::failure(*generated.error_if());
        }
        return common::Result<RequestContext>::success(RequestContext(
            std::move(session_id), std::move(*generated.value_if()),
            common::Timestamp::now(), std::move(cancellation)));
    }

    [[nodiscard]] const common::SessionId& session_id() const noexcept {
        return session_id_;
    }

    [[nodiscard]] const common::RequestId& request_id() const noexcept {
        return request_id_;
    }

    [[nodiscard]] common::Timestamp created_at() const noexcept {
        return created_at_;
    }

    [[nodiscard]] bool is_cancelled() const noexcept {
        return cancellation_.is_cancelled();
    }

private:
    RequestContext(
        common::SessionId session_id,
        common::RequestId request_id,
        common::Timestamp created_at,
        CancellationToken cancellation)
        : session_id_(std::move(session_id)),
          request_id_(std::move(request_id)),
          created_at_(created_at),
          cancellation_(std::move(cancellation)) {}

    common::SessionId session_id_;
    common::RequestId request_id_;
    common::Timestamp created_at_;
    CancellationToken cancellation_;
};

} // namespace morok::core
