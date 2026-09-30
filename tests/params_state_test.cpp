#include "ClapTestHost.h"

#include <ObxdJuceShim.h>

#include "Engine/SynthEngine.h"

#include <set>
#include <string>
#include <string_view>

using namespace testhost;

namespace {

std::string text(Instance &inst, clap_id id, double value) {
    char buffer[CLAP_NAME_SIZE];
    CHECK(inst.params->value_to_text(inst.plugin, id, value, buffer, sizeof(buffer)));
    return buffer;
}

// A state blob exactly as desktop OB-Xd's copyXmlToBinary() writes it.
std::vector<uint8_t> juceBlob(const std::string &xml) {
    std::vector<uint8_t> out(8 + xml.size() + 1, 0);
    const uint32_t magic = 0x21324356;
    const auto size = static_cast<uint32_t>(xml.size());
    for (int i = 0; i < 4; ++i) {
        out[static_cast<size_t>(i)] = static_cast<uint8_t>(magic >> (8 * i));
        out[static_cast<size_t>(4 + i)] = static_cast<uint8_t>(size >> (8 * i));
    }
    std::memcpy(out.data() + 8, xml.data(), xml.size());
    return out;
}

bool load(Instance &inst, const std::vector<uint8_t> &bytes) {
    Buffer buffer;
    buffer.bytes = bytes;
    return inst.state->load(inst.plugin, &buffer.in);
}

} // namespace

int main() {
    Instance inst;

    // Every engine parameter except UNDEFINED/MIDILEARN/UNLEARN, ids = engine indices.
    const uint32_t count = inst.params->count(inst.plugin);
    CHECK(count == PARAM_COUNT - 3);
    std::set<clap_id> ids;
    for (uint32_t i = 0; i < count; ++i) {
        clap_param_info_t info{};
        CHECK(inst.params->get_info(inst.plugin, i, &info));
        CHECK(info.id != UNDEFINED && info.id != MIDILEARN && info.id != UNLEARN);
        CHECK(info.id < PARAM_COUNT);
        CHECK(ids.insert(info.id).second);
        CHECK(info.flags & CLAP_PARAM_IS_AUTOMATABLE);
        CHECK(info.min_value < info.max_value);
        CHECK(info.default_value >= info.min_value && info.default_value <= info.max_value);
        CHECK(info.name[0] != '\0' && info.module[0] != '\0');
        CHECK(inst.value(info.id) == info.default_value);
    }
    clap_param_info_t info{};
    CHECK(!inst.params->get_info(inst.plugin, count, &info));
    double v = 0;
    CHECK(!inst.params->get_value(inst.plugin, MIDILEARN, &v));

    // OB-Xd defaults and their stepped/natural units.
    CHECK(inst.value(VOLUME) == 0.5);
    CHECK(inst.value(CUTOFF) == 1.0);
    CHECK(inst.value(VOICE_COUNT) == 7.0); // 0.2 -> roundToInt(0.2 * 31) + 1
    CHECK(inst.value(OCTAVE) == 0.0);
    CHECK(inst.value(OSC1Saw) == 1.0);
    CHECK(inst.value(ECONOMY_MODE) == 1.0);

    // Display text follows OB-Xd's getTrueParameterValueFromNormalizedRange.
    CHECK(text(inst, TUNE, 0.5) == "0.0 Cents");
    CHECK(text(inst, OSC1P, 0.75) == "12.0 Semitones");
    CHECK(text(inst, PAN3, 0.25) == "-0.25 (Left)");
    CHECK(text(inst, OSC1MIX, 0.0) == "-Inf");
    CHECK(text(inst, CUTOFF, 1.0) == "127");
    CHECK(text(inst, VOICE_COUNT, 8.0) == "8 Voices");
    CHECK(text(inst, LEGATOMODE, 0.0) == "Keep All");
    CHECK(text(inst, OCTAVE, -1.0) == "-12 Semitones");
    CHECK(text(inst, UNISON, 1.0) == "On");
    CHECK(text(inst, BENDRANGE, 0.0) == "2 Semitones");

    double parsed = -1;
    CHECK(inst.params->text_to_value(inst.plugin, TUNE, "50 Cents", &parsed) && parsed == 0.75);
    CHECK(inst.params->text_to_value(inst.plugin, LEGATOMODE, "Retrig", &parsed) && parsed == 3.0);
    CHECK(inst.params->text_to_value(inst.plugin, UNISON, "on", &parsed) && parsed == 1.0);
    CHECK(inst.params->text_to_value(inst.plugin, VOICE_COUNT, "99", &parsed) && parsed == 32.0);
    CHECK(!inst.params->text_to_value(inst.plugin, CUTOFF, "wide", &parsed));

    // params.flush applies host changes without processing.
    InputEvents in;
    OutputEvents out;
    in.param(0, CUTOFF, 0.25);
    in.param(0, VOICE_COUNT, 3.0);
    in.param(0, OCTAVE, 1.0);
    inst.params->flush(inst.plugin, &in.list, &out.list);
    CHECK(inst.value(CUTOFF) == 0.25);
    CHECK(inst.value(VOICE_COUNT) == 3.0);
    CHECK(inst.value(OCTAVE) == 1.0);
    CHECK(out.storage.empty()); // host-originated changes are not echoed

    // State round-trip through partial writes.
    Buffer saved;
    CHECK(inst.state->save(inst.plugin, &saved.out));
    CHECK(saved.bytes.size() > 1000);
    CHECK(saved.bytes[0] == 0x56 && saved.bytes[1] == 0x43 && saved.bytes[2] == 0x32 &&
          saved.bytes[3] == 0x21);
    const std::string xml(reinterpret_cast<const char *>(saved.bytes.data()) + 8,
                          saved.bytes.size() - 9);
    CHECK(xml.find("<discoDSP currentProgram=\"0\"><programs><program programName=\"Default\" "
                   "voiceCount=\"32\" Val_0=\"0\"") != std::string::npos);

    {
        Instance other;
        CHECK(load(other, saved.bytes));
        CHECK(other.value(CUTOFF) == 0.25);
        CHECK(other.value(VOICE_COUNT) == 3.0);
        CHECK(other.value(OCTAVE) == 1.0);
        CHECK(other.host.rescans == 1);
        Buffer again;
        CHECK(other.state->save(other.plugin, &again.out));
        CHECK(again.bytes == saved.bytes);
    }

    // Desktop OB-Xd states: bank with currentProgram, escaped names, and the
    // pre-voiceCount format whose VOICE_COUNT is scaled by 0.25.
    {
        Instance other;
        const std::string bank =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?> <discoDSP currentProgram=\"1\"><programs>"
            "<program programName=\"First\" voiceCount=\"32\" Val_44=\"0.1\"/>"
            "<program programName=\"Brass &amp; Pads\" voiceCount=\"32\" Val_44=\"0.625\" "
            "Val_14=\"1\" Val_3=\"1\"/></programs></discoDSP>";
        CHECK(load(other, juceBlob(bank)));
        CHECK(std::abs(other.value(CUTOFF) - 0.625) < 1e-7);
        CHECK(other.value(UNISON) == 1.0);
        CHECK(other.value(VOICE_COUNT) == 32.0);
        CHECK(other.value(VOLUME) == 0.5); // missing attributes keep defaults

        const std::string old =
            "<?xml version=\"1.0\"?><Datsounds currentProgram=\"0\"><programs>"
            "<program programName=\"Old\" 3=\"1\" 44=\"0.5\"/></programs></Datsounds>";
        CHECK(load(other, juceBlob(old)));
        CHECK(other.value(VOICE_COUNT) == 9.0); // 1 * 0.25 -> roundToInt(0.25 * 31) + 1
        CHECK(other.value(CUTOFF) == 0.5);

        const std::string single =
            "<?xml version=\"1.0\"?> <discoDSP Val_44=\"0.75\" voiceCount=\"32\" programName=\"One\"/>";
        CHECK(load(other, juceBlob(single)));
        CHECK(other.value(CUTOFF) == 0.75);

        // Garbage leaves the plugin untouched.
        CHECK(!load(other, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));
        auto truncated = juceBlob(bank);
        truncated[4] = 0xff;
        truncated[5] = 0xff;
        CHECK(!load(other, truncated));
        CHECK(other.value(CUTOFF) == 0.75);
    }

    std::puts("params_state ok");
    return 0;
}
