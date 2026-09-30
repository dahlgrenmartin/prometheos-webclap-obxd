#pragma once

// OB-Xd's program bank and its plugin-state format.
//
// Desktop OB-Xd stores state with JUCE's copyXmlToBinary(): the int32 magic
// 0x21324356, an int32 byte count, then a single-line XML document and a NUL.
// The document is <discoDSP currentProgram=".."><programs><program
// programName=".." voiceCount="32" Val_0=".." ... /></programs></discoDSP>.
// This module writes exactly that layout and reads it back, together with the
// single-program variant (<discoDSP Val_0=".." programName=".."/>) and the
// pre-voiceCount attribute naming, so state moves between the WCLAP and the
// desktop plugin unchanged.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace obxd {

constexpr int kProgramCount = 128;
constexpr int kEngineParamCount = 80; // ObxdParameters::PARAM_COUNT
constexpr int kStateVoiceCount = 32;  // Motherboard::MAX_VOICES, written as voiceCount

struct Program {
    std::string name;
    std::array<float, kEngineParamCount> values{};

    Program() { setDefaults(); }
    void setDefaults();
};

struct Bank {
    std::array<Program, kProgramCount> programs{};
    int current{0};

    Program &currentProgram() noexcept { return programs[static_cast<size_t>(current)]; }
    const Program &currentProgram() const noexcept {
        return programs[static_cast<size_t>(current)];
    }
};

std::vector<uint8_t> writeBankState(const Bank &bank);

// Replaces `bank` with the state in `bytes`; returns false (leaving `bank`
// untouched) when the bytes are not an OB-Xd state.
bool readBankState(const uint8_t *bytes, size_t size, Bank &bank);

} // namespace obxd
