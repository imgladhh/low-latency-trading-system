#include "risk_engine.h"

namespace llt {

OrderDecision RiskEngine::evaluate(
    const PositionState& position,
    const OrderRequest& request) const noexcept {
    if (request.quantity <= 0) {
        return OrderDecision{false, RejectReason::InvalidQuantity};
    }

    const std::int64_t projected =
        position.net_qty + side_sign(request.side) * static_cast<std::int64_t>(request.quantity);
    if (projected <= max_abs_position_ && projected >= -max_abs_position_) {
        return OrderDecision{true, RejectReason::None};
    }

    return OrderDecision{false, RejectReason::MaxPosition};
}

}  // namespace llt
