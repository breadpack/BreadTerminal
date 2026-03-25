#pragma once
#include <string>

namespace bread {
int emitOsc(int argc, char* argv[]);
std::string buildOscSequence(const std::string& json_payload);
}  // namespace bread
