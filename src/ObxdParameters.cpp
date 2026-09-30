#include "ObxdParameters.h"

#include <ObxdJuceShim.h>

#include "Engine/SynthEngine.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace obxd {
namespace {

using K = ParamKind;

// Section names follow the OB-Xd panel. The last section holds OB-Xd 2.x
// engine parameters that the 2.10 skins do not place on the panel.
constexpr const char *kMaster = "Master";
constexpr const char *kGlobal = "Global";
constexpr const char *kOsc = "Oscillators";
constexpr const char *kMixer = "Mixer";
constexpr const char *kControl = "Control";
constexpr const char *kFilter = "Filter";
constexpr const char *kMod = "Modulation";
constexpr const char *kFenv = "Filter Envelope";
constexpr const char *kAenv = "Amplifier Envelope";
constexpr const char *kVoice = "Voice Variation";
constexpr const char *kExtra = "Extended";

constexpr ParamSpec kSpecs[] = {
    {VOLUME, "Volume", "Volume", kMaster, K::Continuous},
    {TUNE, "Tune", "Fine", kMaster, K::Continuous},
    {OCTAVE, "Octave", "Coarse", kMaster, K::Octave},

    {UDET, "VoiceDetune", "Spread", kGlobal, K::Continuous},
    {UNISON, "Unison", "Unison", kGlobal, K::Switch},
    {PORTAMENTO, "Portamento", "Glide", kGlobal, K::Continuous},
    {LEGATOMODE, "LegatoMode", "Legato Mode", kGlobal, K::Legato},
    {VOICE_COUNT, "VoiceCount", "Voices", kGlobal, K::Voices},
    {ASPLAYEDALLOCATION, "AsPlayedAllocation", "VAM", kGlobal, K::Switch},

    {OSC1P, "Osc1Pitch", "Osc1 Pitch", kOsc, K::Continuous},
    {PW, "PulseWidth", "PW", kOsc, K::Continuous},
    {OSC2P, "Osc2Pitch", "Osc2 Pitch", kOsc, K::Continuous},
    {OSC1Saw, "Osc1Saw", "Osc1 Saw", kOsc, K::Switch},
    {OSC1Pul, "Osc1Pulse", "Osc1 Pulse", kOsc, K::Switch},
    {OSC2_DET, "Oscillator2detune", "Detune", kOsc, K::Continuous},
    {OSC2Saw, "Osc2Saw", "Osc2 Saw", kOsc, K::Switch},
    {OSC2Pul, "Osc2Pulse", "Osc2 Pulse", kOsc, K::Switch},
    {OSC2HS, "Osc2HardSync", "Sync", kOsc, K::Switch},
    {XMOD, "Xmod", "Xmod", kOsc, K::Continuous},
    {OSCQuantize, "PitchQuant", "Step", kOsc, K::Switch},
    {BRIGHTNESS, "Brightness", "Bright Amt", kOsc, K::Continuous},
    {ENVPITCH, "EnvelopeToPitch", "Pitch Env Amt", kOsc, K::Continuous},

    {OSC1MIX, "Osc1Mix", "Osc1 Level", kMixer, K::Continuous},
    {OSC2MIX, "Osc2Mix", "Osc2 Level", kMixer, K::Continuous},
    {NOISEMIX, "NoiseMix", "Noise", kMixer, K::Continuous},

    {BENDRANGE, "BendRange", "Bend Octave", kControl, K::Switch},
    {BENDOSC2, "BendOsc2Only", "Bend Osc2", kControl, K::Switch},
    {BENDLFORATE, "VibratoRate", "Vibrato Rate", kControl, K::Continuous},
    {VFLTENV, "VFltFactor", "Flt Env Velocity", kControl, K::Continuous},
    {VAMPENV, "VAmpFactor", "Amp Env Velocity", kControl, K::Continuous},

    {CUTOFF, "Cutoff", "Cutoff", kFilter, K::Continuous},
    {RESONANCE, "Resonance", "Resonance", kFilter, K::Continuous},
    {ENVELOPE_AMT, "FilterEnvAmount", "Env Amt", kFilter, K::Continuous},
    {FLT_KF, "FilterKeyFollow", "Key Follow", kFilter, K::Continuous},
    {FILTER_WARM, "Filter_Warm", "HQ", kFilter, K::Switch},
    {MULTIMODE, "Multimode", "Multimode", kFilter, K::Continuous},
    {BANDPASS, "BandpassBlend", "BP", kFilter, K::Switch},
    {FOURPOLE, "FourPole", "24dB", kFilter, K::Switch},

    {LFOFREQ, "LfoFrequency", "LFO Rate", kMod, K::Continuous},
    {LFO1AMT, "LfoAmount1", "LFO Freq Amt", kMod, K::Continuous},
    {LFO2AMT, "LfoAmount2", "LFO PW Amt", kMod, K::Continuous},
    {LFOSINWAVE, "LfoSineWave", "LFO Sine", kMod, K::Switch},
    {LFOSQUAREWAVE, "LfoSquareWave", "LFO Square", kMod, K::Switch},
    {LFOSHWAVE, "LfoSampleHoldWave", "LFO S&H", kMod, K::Switch},
    {LFOOSC1, "LfoOsc1", "LFO Freq Osc1", kMod, K::Switch},
    {LFOOSC2, "LfoOsc2", "LFO Freq Osc2", kMod, K::Switch},
    {LFOFILTER, "LfoFilter", "LFO Freq Filter", kMod, K::Switch},
    {LFOPW1, "LfoPw1", "LFO PW Osc1", kMod, K::Switch},
    {LFOPW2, "LfoPw2", "LFO PW Osc2", kMod, K::Switch},
    {LFO_SYNC, "LfoSync", "LFO Sync", kMod, K::Switch},

    {FATK, "FilterAttack", "Filter Attack", kFenv, K::Continuous},
    {FDEC, "FilterDecay", "Filter Decay", kFenv, K::Continuous},
    {FSUS, "FilterSustain", "Filter Sustain", kFenv, K::Continuous},
    {FREL, "FilterRelease", "Filter Release", kFenv, K::Continuous},

    {LATK, "Attack", "Amp Attack", kAenv, K::Continuous},
    {LDEC, "Decay", "Amp Decay", kAenv, K::Continuous},
    {LSUS, "Sustain", "Amp Sustain", kAenv, K::Continuous},
    {LREL, "Release", "Amp Release", kAenv, K::Continuous},

    {FILTERDER, "FilterDetune", "Flt Slop", kVoice, K::Continuous},
    {PORTADER, "PortamentoDetune", "Gld Slop", kVoice, K::Continuous},
    {ENVDER, "EnvelopeDetune", "Env Slop", kVoice, K::Continuous},
    {LEVEL_DIF, "LevelDif", "Level Slop", kVoice, K::Continuous},
    {PAN1, "Pan1", "V1 Pan", kVoice, K::Continuous},
    {PAN2, "Pan2", "V2 Pan", kVoice, K::Continuous},
    {PAN3, "Pan3", "V3 Pan", kVoice, K::Continuous},
    {PAN4, "Pan4", "V4 Pan", kVoice, K::Continuous},
    {PAN5, "Pan5", "V5 Pan", kVoice, K::Continuous},
    {PAN6, "Pan6", "V6 Pan", kVoice, K::Continuous},
    {PAN7, "Pan7", "V7 Pan", kVoice, K::Continuous},
    {PAN8, "Pan8", "V8 Pan", kVoice, K::Continuous},

    {PW_ENV, "PwEnv", "PW Env Amt", kExtra, K::Continuous},
    {PW_ENV_BOTH, "PwEnvBoth", "PW Env Both", kExtra, K::Switch},
    {PW_OSC2_OFS, "PwOfs", "PW Osc2 Offset", kExtra, K::Continuous},
    {ENV_PITCH_BOTH, "EnvPitchBoth", "Pitch Env Both", kExtra, K::Switch},
    {FENV_INVERT, "FenvInvert", "Filter Env Invert", kExtra, K::Switch},
    {SELF_OSC_PUSH, "SelfOscPush", "Self-Osc Push", kExtra, K::Switch},
    {ECONOMY_MODE, "EconomyMode", "Economy Mode", kExtra, K::Switch},
};

constexpr size_t kSpecCount = sizeof(kSpecs) / sizeof(kSpecs[0]);
static_assert(kSpecCount == PARAM_COUNT - 3,
              "every engine parameter except UNDEFINED, MIDILEARN and UNLEARN is exposed");

constexpr std::array<const char *, 4> kLegatoNames{
    "Keep All", "Keep Filter Envelope", "Keep Amplitude Envelope", "Retrig"};

const std::array<const ParamSpec *, PARAM_COUNT> kByIndex = [] {
    std::array<const ParamSpec *, PARAM_COUNT> table{};
    for (const auto &spec : kSpecs) {
        table[static_cast<size_t>(spec.index)] = &spec;
    }
    return table;
}();

float logsc(float param, float min, float max, float rolloff = 19.0f) {
    return ((std::exp(param * std::log(rolloff + 1)) - 1.0f) / rolloff) * (max - min) + min;
}

std::string fixed(double value, int decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return buffer;
}

std::string decibels(double gain) {
    if (gain <= 0.0) {
        return "-Inf";
    }
    const double db = 20.0 * std::log10(gain);
    return db < -80.0 ? "-Inf" : fixed(db, 2) + " dB";
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool leadingNumber(const char *text, double *out) {
    char *end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text || !std::isfinite(value)) {
        return false;
    }
    *out = value;
    return true;
}

double clampPlain(const ParamSpec &spec, double value) {
    const double lo = plainMin(spec);
    const double hi = plainMax(spec);
    value = value < lo ? lo : (value > hi ? hi : value);
    return isStepped(spec) ? std::round(value) : value;
}

} // namespace

const ParamSpec *paramSpecs() noexcept { return kSpecs; }
size_t paramSpecCount() noexcept { return kSpecCount; }

const ParamSpec *paramSpecForIndex(int index) noexcept {
    if (index < 0 || index >= PARAM_COUNT) {
        return nullptr;
    }
    return kByIndex[static_cast<size_t>(index)];
}

double plainMin(const ParamSpec &spec) noexcept {
    switch (spec.kind) {
    case K::Voices:
        return 1.0;
    case K::Octave:
        return -2.0;
    default:
        return 0.0;
    }
}

double plainMax(const ParamSpec &spec) noexcept {
    switch (spec.kind) {
    case K::Voices:
        return 32.0;
    case K::Legato:
        return 3.0;
    case K::Octave:
        return 2.0;
    default:
        return 1.0;
    }
}

bool isStepped(const ParamSpec &spec) noexcept { return spec.kind != K::Continuous; }

float plainToEngine(const ParamSpec &spec, double plain) noexcept {
    const double v = clampPlain(spec, plain);
    switch (spec.kind) {
    case K::Continuous:
    case K::Switch:
        return static_cast<float>(v);
    case K::Voices:
        return static_cast<float>((v - 1.0) / 31.0);
    case K::Legato:
        return static_cast<float>(v / 3.0);
    case K::Octave:
        return static_cast<float>((v + 2.0) / 4.0);
    }
    return 0.0f;
}

double engineToPlain(const ParamSpec &spec, float engine) noexcept {
    const double e = engine < 0.0f ? 0.0 : (engine > 1.0f ? 1.0 : engine);
    switch (spec.kind) {
    case K::Continuous:
        return e;
    case K::Switch:
        return e > 0.5 ? 1.0 : 0.0;
    case K::Voices:
        return std::round(e * 31.0) + 1.0;
    case K::Legato:
        return std::round(e * 3.0);
    case K::Octave:
        return std::round(e * 4.0) - 2.0;
    }
    return 0.0;
}

std::string formatValue(const ParamSpec &spec, double plain) {
    const double v = clampPlain(spec, plain);
    const auto f = static_cast<float>(v);
    switch (spec.kind) {
    case K::Switch:
        if (spec.index == BENDRANGE) {
            return v > 0.5 ? "12 Semitones" : "2 Semitones";
        }
        return v > 0.5 ? "On" : "Off";
    case K::Voices:
        return fixed(v, 0) + (v == 1.0 ? " Voice" : " Voices");
    case K::Legato:
        return kLegatoNames[static_cast<size_t>(v)];
    case K::Octave:
        return fixed(v * 12.0, 0) + " Semitones";
    case K::Continuous:
        break;
    }

    switch (spec.index) {
    case BENDLFORATE:
        return fixed(logsc(f, 3, 10), 2) + " Hz";
    case TUNE:
        return fixed(v * 200.0 - 100.0, 1) + " Cents";
    case NOISEMIX:
        return decibels(logsc(f, 0, 1, 35));
    case OSC1MIX:
    case OSC2MIX:
        return decibels(v);
    case LFOFREQ:
        return fixed(logsc(f, 0, 50, 120), 2) + " Hz";
    case PAN1:
    case PAN2:
    case PAN3:
    case PAN4:
    case PAN5:
    case PAN6:
    case PAN7:
    case PAN8: {
        const double pan = v - 0.5;
        const char *side = pan < 0.0 ? " (Left)" : (pan > 0.0 ? " (Right)" : " (Center)");
        return fixed(pan, 2) + side;
    }
    case OSC1P:
    case OSC2P:
        return fixed((v * 4.0 - 2.0) * 12.0, 1) + " Semitones";
    default:
        // OB-Xd shows every other knob on a 0..127 scale.
        return fixed(std::trunc(v * 127.0), 0);
    }
}

bool parseValue(const ParamSpec &spec, const char *text, double *plain) noexcept {
    if (!text || !plain) {
        return false;
    }
    while (std::isspace(static_cast<unsigned char>(*text))) {
        ++text;
    }
    double number = 0.0;
    switch (spec.kind) {
    case K::Switch:
        if (equalsIgnoreCase(text, "on")) {
            *plain = 1.0;
            return true;
        }
        if (equalsIgnoreCase(text, "off")) {
            *plain = 0.0;
            return true;
        }
        if (!leadingNumber(text, &number)) {
            return false;
        }
        if (spec.index == BENDRANGE && (number == 2.0 || number == 12.0)) {
            number = number == 12.0 ? 1.0 : 0.0;
        }
        *plain = number > 0.5 ? 1.0 : 0.0;
        return true;
    case K::Legato:
        for (size_t i = 0; i < kLegatoNames.size(); ++i) {
            if (equalsIgnoreCase(text, kLegatoNames[i])) {
                *plain = static_cast<double>(i);
                return true;
            }
        }
        if (!leadingNumber(text, &number)) {
            return false;
        }
        *plain = clampPlain(spec, number);
        return true;
    case K::Voices:
        if (!leadingNumber(text, &number)) {
            return false;
        }
        *plain = clampPlain(spec, number);
        return true;
    case K::Octave:
        if (!leadingNumber(text, &number)) {
            return false;
        }
        *plain = clampPlain(spec, number / 12.0);
        return true;
    case K::Continuous:
        break;
    }

    if (!leadingNumber(text, &number)) {
        return false;
    }
    switch (spec.index) {
    case TUNE:
        *plain = clampPlain(spec, (number + 100.0) / 200.0);
        return true;
    case OSC1P:
    case OSC2P:
        *plain = clampPlain(spec, (number / 12.0 + 2.0) / 4.0);
        return true;
    case PAN1:
    case PAN2:
    case PAN3:
    case PAN4:
    case PAN5:
    case PAN6:
    case PAN7:
    case PAN8:
        *plain = clampPlain(spec, number + 0.5);
        return true;
    case BENDLFORATE:
    case NOISEMIX:
    case OSC1MIX:
    case OSC2MIX:
    case LFOFREQ:
        // Non-linear display scales: accept only the raw 0..1 value.
        if (number < 0.0 || number > 1.0) {
            return false;
        }
        *plain = number;
        return true;
    default:
        *plain = clampPlain(spec, number / 127.0);
        return true;
    }
}

} // namespace obxd
