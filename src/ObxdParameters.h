#pragma once

// OB-Xd's engine parameters as CLAP parameters.
//
// The CLAP id of a parameter is its OB-Xd engine index (ObxdParameters in
// vendor/obxd/Source/Engine/ParamsEnum.h), so ids are stable and match the
// indices in OB-Xd's own program/state format. The list order follows the
// hardware panel, section by section, which is what hosts show as columns.
//
// Every engine value is OB-Xd's normalized 0..1 float. Most parameters expose
// that directly. Switches are stepped 0/1, and the three selectors are stepped
// in their natural units (voices 1..32, legato mode 0..3, coarse -2..+2
// octaves); plainToEngine/engineToPlain convert between the two domains.

#include <cstddef>
#include <cstdint>
#include <string>

namespace obxd {

enum class ParamKind : uint8_t {
    Continuous, // 0..1, value is the engine value
    Switch,     // stepped 0..1, engine sees 0 or 1
    Voices,     // stepped 1..32
    Legato,     // stepped 0..3
    Octave,     // stepped -2..+2
};

struct ParamSpec {
    int index;              // OB-Xd engine index == CLAP id
    const char *key;        // OB-Xd's parameter id (getEngineParameterId)
    const char *name;       // panel label
    const char *module;     // panel section
    ParamKind kind;
};

// The exposed parameters in panel order.
const ParamSpec *paramSpecs() noexcept;
size_t paramSpecCount() noexcept;
// The spec for an engine index, or nullptr when the index is not exposed.
const ParamSpec *paramSpecForIndex(int index) noexcept;

double plainMin(const ParamSpec &spec) noexcept;
double plainMax(const ParamSpec &spec) noexcept;
bool isStepped(const ParamSpec &spec) noexcept;

float plainToEngine(const ParamSpec &spec, double plain) noexcept;
double engineToPlain(const ParamSpec &spec, float engine) noexcept;

// OB-Xd's display text for a value, following getTrueParameterValueFromNormalizedRange.
std::string formatValue(const ParamSpec &spec, double plain);
bool parseValue(const ParamSpec &spec, const char *text, double *plain) noexcept;

} // namespace obxd
