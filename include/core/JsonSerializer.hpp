#pragma once

#include "core/Commands.hpp"
#include "core/State.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace biomus {

// Serializes FullState into a compact JSON snapshot matching web/index.html's `state` schema
std::string serializeStateJson(const FullState& state);

// Parses incoming WebSocket JSON command (e.g. {"cmd":"preset","name":"WALL"})
// into one or more RT-safe Command structs. Returns true if valid.
bool parseJsonCommand(std::string_view json, std::vector<Command>& outCommands);

} // namespace biomus
