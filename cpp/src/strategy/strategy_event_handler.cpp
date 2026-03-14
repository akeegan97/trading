#include "trading/strategy/strategy_event_handler.hpp"

#include <utility>

namespace trading::strategy {

StrategyEventHandler::StrategyEventHandler(IStrategy& strategy, IOrderIntentSink& intent_sink,
                                           StrategyRunnerConfig config)
    : runner_(strategy, intent_sink, config) {}

bool StrategyEventHandler::on_event(const internal::NormalizedEvent& event) {
    return runner_.on_event(event);
}

StrategyRunnerStats StrategyEventHandler::stats() const { return runner_.stats(); }

std::string StrategyEventHandler::last_error() const { return runner_.last_error(); }

} // namespace trading::strategy
