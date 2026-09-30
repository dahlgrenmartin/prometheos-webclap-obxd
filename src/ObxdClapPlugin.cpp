#include "ObxdClapPlugin.h"

#include "ObxdWebUi.h"

#include <ObxdJuceShim.h>

#include "Engine/SynthEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string_view>
#include <vector>

namespace obxd {
namespace {

constexpr size_t kMaxStateBytes = 16u * 1024u * 1024u;

const char *const kFeatures[] = {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const clap_plugin_descriptor_t kDescriptor{
    CLAP_VERSION,
    kPluginId,
    "OB-Xd",
    "discoDSP",
    "https://www.discodsp.com/obxd/",
    "",
    "https://github.com/reales/OB-Xd/issues",
    "2.10.0-wclap",
    "Oberheim OB-X inspired virtual analog synthesizer, as a direct WebCLAP build",
    kFeatures,
};

// ---- small JSON helpers for the editor protocol ------------------------------
//
// The editor and the plugin exchange one flat JSON object per message, as
// UTF-8 bytes in an ArrayBuffer (clap.webview/3). Page -> plugin:
//   {"type":"ready"}
//   {"type":"set","id":<clap id>,"value":<plain value>}
//   {"type":"gesture","id":<clap id>,"begin":true|false}
// Plugin -> page:
//   {"type":"init","params":[{...param...}, ...]}
//   {"type":"param","id":..,"value":..,"text":".."}

void appendJsonString(std::string &out, std::string_view text) {
    out += '"';
    for (char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<int>(c));
                out += buffer;
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

std::string jsonNumber(double value) {
    if (!std::isfinite(value)) {
        return "0";
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.9g", value);
    return buffer;
}

// The raw text after "key": in a flat JSON object, or empty when absent.
std::string_view jsonField(std::string_view json, std::string_view key) {
    std::string pattern = "\"";
    pattern += key;
    pattern += "\"";
    size_t pos = 0;
    while ((pos = json.find(pattern, pos)) != std::string_view::npos) {
        size_t i = pos + pattern.size();
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) {
            ++i;
        }
        if (i < json.size() && json[i] == ':') {
            ++i;
            while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) {
                ++i;
            }
            size_t end = i;
            if (end < json.size() && json[end] == '"') {
                end = json.find('"', end + 1);
                return end == std::string_view::npos ? std::string_view{}
                                                     : json.substr(i + 1, end - i - 1);
            }
            while (end < json.size() && json[end] != ',' && json[end] != '}' &&
                   json[end] != ' ') {
                ++end;
            }
            return json.substr(i, end - i);
        }
        pos += pattern.size();
    }
    return {};
}

bool jsonNumberField(std::string_view json, std::string_view key, double *out) {
    const std::string text(jsonField(json, key));
    if (text.empty()) {
        return false;
    }
    char *end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(value)) {
        return false;
    }
    *out = value;
    return true;
}

// ---- stream helpers ---------------------------------------------------------

bool writeAll(const clap_ostream_t *stream, const uint8_t *data, size_t size) {
    while (size > 0) {
        const int64_t written = stream->write(stream, data, size);
        if (written <= 0) {
            return false;
        }
        data += written;
        size -= static_cast<size_t>(written);
    }
    return true;
}

// ---- C ABI trampolines --------------------------------------------------------

ObxdClapPlugin *of(const clap_plugin_t *plugin) { return ObxdClapPlugin::self(plugin); }

bool CLAP_ABI pluginInit(const clap_plugin_t *p) { return of(p)->init(); }
void CLAP_ABI pluginDestroy(const clap_plugin_t *p) { delete of(p); }
bool CLAP_ABI pluginActivate(const clap_plugin_t *p, double rate, uint32_t minFrames,
                             uint32_t maxFrames) {
    return of(p)->activate(rate, minFrames, maxFrames);
}
void CLAP_ABI pluginDeactivate(const clap_plugin_t *p) { of(p)->deactivate(); }
bool CLAP_ABI pluginStartProcessing(const clap_plugin_t *p) { return of(p)->startProcessing(); }
void CLAP_ABI pluginStopProcessing(const clap_plugin_t *p) { of(p)->stopProcessing(); }
void CLAP_ABI pluginReset(const clap_plugin_t *p) { of(p)->reset(); }
clap_process_status CLAP_ABI pluginProcess(const clap_plugin_t *p, const clap_process_t *process) {
    return of(p)->process(process);
}
const void *CLAP_ABI pluginGetExtension(const clap_plugin_t *p, const char *id) {
    return of(p)->getExtension(id);
}
void CLAP_ABI pluginOnMainThread(const clap_plugin_t *p) { of(p)->onMainThread(); }

uint32_t CLAP_ABI audioPortsCount(const clap_plugin_t *, bool isInput) { return isInput ? 0 : 1; }
bool CLAP_ABI audioPortsGet(const clap_plugin_t *, uint32_t index, bool isInput,
                            clap_audio_port_info_t *info) {
    if (isInput || index != 0 || !info) {
        return false;
    }
    std::memset(info, 0, sizeof(*info));
    info->id = 0;
    std::snprintf(info->name, sizeof(info->name), "%s", "Output");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t kAudioPorts{audioPortsCount, audioPortsGet};

uint32_t CLAP_ABI notePortsCount(const clap_plugin_t *, bool isInput) { return isInput ? 1 : 0; }
bool CLAP_ABI notePortsGet(const clap_plugin_t *, uint32_t index, bool isInput,
                           clap_note_port_info_t *info) {
    if (!isInput || index != 0 || !info) {
        return false;
    }
    std::memset(info, 0, sizeof(*info));
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::snprintf(info->name, sizeof(info->name), "%s", "Notes");
    return true;
}
const clap_plugin_note_ports_t kNotePorts{notePortsCount, notePortsGet};

uint32_t CLAP_ABI paramsCount(const clap_plugin_t *p) { return of(p)->paramCount(); }
bool CLAP_ABI paramsGetInfo(const clap_plugin_t *p, uint32_t index, clap_param_info_t *info) {
    return of(p)->paramInfo(index, info);
}
bool CLAP_ABI paramsGetValue(const clap_plugin_t *p, clap_id id, double *value) {
    return of(p)->paramValue(id, value);
}
bool CLAP_ABI paramsValueToText(const clap_plugin_t *p, clap_id id, double value, char *out,
                                uint32_t capacity) {
    return of(p)->paramValueToText(id, value, out, capacity);
}
bool CLAP_ABI paramsTextToValue(const clap_plugin_t *p, clap_id id, const char *text,
                                double *value) {
    return of(p)->paramTextToValue(id, text, value);
}
void CLAP_ABI paramsFlush(const clap_plugin_t *p, const clap_input_events_t *in,
                          const clap_output_events_t *out) {
    of(p)->paramsFlush(in, out);
}
const clap_plugin_params_t kParams{paramsCount,       paramsGetInfo,     paramsGetValue,
                                   paramsValueToText, paramsTextToValue, paramsFlush};

bool CLAP_ABI stateSave(const clap_plugin_t *p, const clap_ostream_t *stream) {
    return of(p)->stateSave(stream);
}
bool CLAP_ABI stateLoad(const clap_plugin_t *p, const clap_istream_t *stream) {
    return of(p)->stateLoad(stream);
}
const clap_plugin_state_t kState{stateSave, stateLoad};

bool CLAP_ABI guiIsApiSupported(const clap_plugin_t *, const char *api, bool isFloating) {
    return api && !isFloating && std::strcmp(api, CLAP_WINDOW_API_WEBVIEW) == 0;
}
bool CLAP_ABI guiGetPreferredApi(const clap_plugin_t *, const char **api, bool *isFloating) {
    if (api) {
        *api = CLAP_WINDOW_API_WEBVIEW;
    }
    if (isFloating) {
        *isFloating = false;
    }
    return true;
}
bool CLAP_ABI guiCreate(const clap_plugin_t *p, const char *api, bool isFloating) {
    return of(p)->guiCreate(api, isFloating);
}
void CLAP_ABI guiDestroy(const clap_plugin_t *p) { of(p)->guiDestroy(); }
bool CLAP_ABI guiSetScale(const clap_plugin_t *, double) { return false; }
bool CLAP_ABI guiGetSize(const clap_plugin_t *p, uint32_t *width, uint32_t *height) {
    if (!of(p)->guiOpen() || !width || !height) {
        return false;
    }
    *width = kGuiWidth;
    *height = kGuiHeight;
    return true;
}
bool CLAP_ABI guiCanResize(const clap_plugin_t *) { return false; }
bool CLAP_ABI guiGetResizeHints(const clap_plugin_t *, clap_gui_resize_hints_t *) { return false; }
bool CLAP_ABI guiAdjustSize(const clap_plugin_t *, uint32_t *width, uint32_t *height) {
    if (!width || !height) {
        return false;
    }
    *width = kGuiWidth;
    *height = kGuiHeight;
    return true;
}
bool CLAP_ABI guiSetSize(const clap_plugin_t *, uint32_t width, uint32_t height) {
    return width == kGuiWidth && height == kGuiHeight;
}
bool CLAP_ABI guiSetParent(const clap_plugin_t *p, const clap_window_t *window) {
    return of(p)->guiOpen() && window && std::strcmp(window->api, CLAP_WINDOW_API_WEBVIEW) == 0;
}
bool CLAP_ABI guiSetTransient(const clap_plugin_t *, const clap_window_t *) { return false; }
void CLAP_ABI guiSuggestTitle(const clap_plugin_t *, const char *) {}
bool CLAP_ABI guiShow(const clap_plugin_t *p) { return of(p)->guiShow(); }
bool CLAP_ABI guiHide(const clap_plugin_t *p) { return of(p)->guiHide(); }
const clap_plugin_gui_t kGui{guiIsApiSupported, guiGetPreferredApi, guiCreate,     guiDestroy,
                             guiSetScale,       guiGetSize,         guiCanResize,  guiGetResizeHints,
                             guiAdjustSize,     guiSetSize,         guiSetParent,  guiSetTransient,
                             guiSuggestTitle,   guiShow,            guiHide};

int32_t CLAP_ABI webviewGetUri(const clap_plugin_t *p, char *uri, uint32_t capacity) {
    return of(p)->webviewGetUri(uri, capacity);
}
bool CLAP_ABI webviewGetResource(const clap_plugin_t *p, const char *path, char *mime,
                                 uint32_t mimeCapacity, const clap_ostream_t *stream) {
    return of(p)->webviewGetResource(path, mime, mimeCapacity, stream);
}
bool CLAP_ABI webviewReceive(const clap_plugin_t *p, const void *buffer, uint32_t size) {
    return of(p)->webviewReceive(buffer, size);
}
const clap_plugin_webview_t kWebview{webviewGetUri, webviewGetResource, webviewReceive};

} // namespace

// ---- lifecycle ----------------------------------------------------------------

ObxdClapPlugin::ObxdClapPlugin(const clap_host_t *host) : host_(host) {
    plugin_.desc = &kDescriptor;
    plugin_.plugin_data = this;
    plugin_.init = pluginInit;
    plugin_.destroy = pluginDestroy;
    plugin_.activate = pluginActivate;
    plugin_.deactivate = pluginDeactivate;
    plugin_.start_processing = pluginStartProcessing;
    plugin_.stop_processing = pluginStopProcessing;
    plugin_.reset = pluginReset;
    plugin_.process = pluginProcess;
    plugin_.get_extension = pluginGetExtension;
    plugin_.on_main_thread = pluginOnMainThread;
}

ObxdClapPlugin::~ObxdClapPlugin() {
    delete stagedBank_.exchange(nullptr);
    delete retiredBank_.exchange(nullptr);
}

ObxdClapPlugin *ObxdClapPlugin::self(const clap_plugin_t *plugin) noexcept {
    return static_cast<ObxdClapPlugin *>(plugin->plugin_data);
}

bool ObxdClapPlugin::init() noexcept {
    if (initialized_) {
        return true;
    }
    synth_.reset(new (std::nothrow) SynthEngine());
    bank_.reset(new (std::nothrow) Bank());
    if (!synth_ || !bank_) {
        return false;
    }
    if (host_ && host_->get_extension) {
        hostParams_ =
            static_cast<const clap_host_params_t *>(host_->get_extension(host_, CLAP_EXT_PARAMS));
        hostState_ =
            static_cast<const clap_host_state_t *>(host_->get_extension(host_, CLAP_EXT_STATE));
        hostWebview_ = static_cast<const clap_host_webview_t *>(
            host_->get_extension(host_, CLAP_EXT_WEBVIEW));
    }
    synth_->setSampleRate(static_cast<float>(sampleRate_));
    applyProgram();
    initialized_ = true;
    return true;
}

bool ObxdClapPlugin::activate(double sampleRate, uint32_t, uint32_t) noexcept {
    if (!initialized_ || sampleRate <= 0.0) {
        return false;
    }
    sampleRate_ = sampleRate;
    synth_->setSampleRate(static_cast<float>(sampleRate));
    active_ = true;
    return true;
}

void ObxdClapPlugin::deactivate() noexcept { active_ = false; }

bool ObxdClapPlugin::startProcessing() noexcept {
    processing_ = true;
    return true;
}

void ObxdClapPlugin::stopProcessing() noexcept { processing_ = false; }

void ObxdClapPlugin::reset() noexcept {
    if (synth_) {
        synth_->sustainOff();
        synth_->allSoundOff();
    }
}

const void *ObxdClapPlugin::getExtension(const char *id) noexcept {
    if (!id) {
        return nullptr;
    }
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
        return &kAudioPorts;
    }
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) {
        return &kNotePorts;
    }
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) {
        return &kParams;
    }
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) {
        return &kState;
    }
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) {
        return &kGui;
    }
    if (std::strcmp(id, CLAP_EXT_WEBVIEW) == 0) {
        return &kWebview;
    }
    return nullptr;
}

void ObxdClapPlugin::onMainThread() noexcept {
    callbackRequested_.store(false);
    delete retiredBank_.exchange(nullptr);
    sendGuiDirty();
}

// ---- engine -------------------------------------------------------------------

void ObxdClapPlugin::applyEngine(int index, float v) noexcept {
    bank_->currentProgram().values[static_cast<size_t>(index)] = v;
    SynthEngine &s = *synth_;
    switch (index) {
    case SELF_OSC_PUSH: s.processSelfOscPush(v); break;
    case PW_ENV_BOTH: s.processPwEnvBoth(v); break;
    case PW_OSC2_OFS: s.processPwOfs(v); break;
    case ENV_PITCH_BOTH: s.processPitchModBoth(v); break;
    case FENV_INVERT: s.processInvertFenv(v); break;
    case LEVEL_DIF: s.processLoudnessDetune(v); break;
    case PW_ENV: s.processPwEnv(v); break;
    case LFO_SYNC: s.procLfoSync(v); break;
    case ECONOMY_MODE: s.procEconomyMode(v); break;
    case VAMPENV: s.procAmpVelocityAmount(v); break;
    case VFLTENV: s.procFltVelocityAmount(v); break;
    case ASPLAYEDALLOCATION: s.procAsPlayedAlloc(v); break;
    case BENDLFORATE: s.procModWheelFrequency(v); break;
    case FOURPOLE: s.processFourPole(v); break;
    case LEGATOMODE: s.processLegatoMode(v); break;
    case ENVPITCH: s.processEnvelopeToPitch(v); break;
    case OSCQuantize: s.processPitchQuantization(v); break;
    case VOICE_COUNT: s.setVoiceCount(v); break;
    case BANDPASS: s.processBandpassSw(v); break;
    case FILTER_WARM: s.processOversampling(v); break;
    case BENDOSC2: s.procPitchWheelOsc2Only(v); break;
    case BENDRANGE: s.procPitchWheelAmount(v); break;
    case NOISEMIX: s.processNoiseMix(v); break;
    case OCTAVE: s.processOctave(v); break;
    case TUNE: s.processTune(v); break;
    case BRIGHTNESS: s.processBrightness(v); break;
    case MULTIMODE: s.processMultimode(v); break;
    case LFOFREQ: s.processLfoFrequency(v); break;
    case LFO1AMT: s.processLfoAmt1(v); break;
    case LFO2AMT: s.processLfoAmt2(v); break;
    case LFOSINWAVE: s.processLfoSine(v); break;
    case LFOSQUAREWAVE: s.processLfoSquare(v); break;
    case LFOSHWAVE: s.processLfoSH(v); break;
    case LFOFILTER: s.processLfoFilter(v); break;
    case LFOOSC1: s.processLfoOsc1(v); break;
    case LFOOSC2: s.processLfoOsc2(v); break;
    case LFOPW1: s.processLfoPw1(v); break;
    case LFOPW2: s.processLfoPw2(v); break;
    case PORTADER: s.processPortamentoDetune(v); break;
    case FILTERDER: s.processFilterDetune(v); break;
    case ENVDER: s.processEnvelopeDetune(v); break;
    case XMOD: s.processOsc2Xmod(v); break;
    case OSC2HS: s.processOsc2HardSync(v); break;
    case OSC2P: s.processOsc2Pitch(v); break;
    case OSC1P: s.processOsc1Pitch(v); break;
    case PORTAMENTO: s.processPortamento(v); break;
    case UNISON: s.processUnison(v); break;
    case FLT_KF: s.processFilterKeyFollow(v); break;
    case OSC1MIX: s.processOsc1Mix(v); break;
    case OSC2MIX: s.processOsc2Mix(v); break;
    case PW: s.processPulseWidth(v); break;
    case OSC1Saw: s.processOsc1Saw(v); break;
    case OSC2Saw: s.processOsc2Saw(v); break;
    case OSC1Pul: s.processOsc1Pulse(v); break;
    case OSC2Pul: s.processOsc2Pulse(v); break;
    case VOLUME: s.processVolume(v); break;
    case UDET: s.processDetune(v); break;
    case OSC2_DET: s.processOsc2Det(v); break;
    case CUTOFF: s.processCutoff(v); break;
    case RESONANCE: s.processResonance(v); break;
    case ENVELOPE_AMT: s.processFilterEnvelopeAmt(v); break;
    case LATK: s.processLoudnessEnvelopeAttack(v); break;
    case LDEC: s.processLoudnessEnvelopeDecay(v); break;
    case LSUS: s.processLoudnessEnvelopeSustain(v); break;
    case LREL: s.processLoudnessEnvelopeRelease(v); break;
    case FATK: s.processFilterEnvelopeAttack(v); break;
    case FDEC: s.processFilterEnvelopeDecay(v); break;
    case FSUS: s.processFilterEnvelopeSustain(v); break;
    case FREL: s.processFilterEnvelopeRelease(v); break;
    case PAN1: s.processPan(v, 1); break;
    case PAN2: s.processPan(v, 2); break;
    case PAN3: s.processPan(v, 3); break;
    case PAN4: s.processPan(v, 4); break;
    case PAN5: s.processPan(v, 5); break;
    case PAN6: s.processPan(v, 6); break;
    case PAN7: s.processPan(v, 7); break;
    case PAN8: s.processPan(v, 8); break;
    default: break;
    }
}

void ObxdClapPlugin::applyProgram() noexcept {
    const Program &program = bank_->currentProgram();
    for (int k = 0; k < kEngineParamCount; ++k) {
        applyEngine(k, program.values[static_cast<size_t>(k)]);
    }
}

void ObxdClapPlugin::setPlain(int index, double plain) noexcept {
    const ParamSpec *spec = paramSpecForIndex(index);
    if (!spec) {
        return;
    }
    applyEngine(index, plainToEngine(*spec, plain));
    markGuiDirty(index);
}

void ObxdClapPlugin::selectProgram(int program, const clap_output_events_t *out) noexcept {
    if (program < 0 || program >= kProgramCount) {
        return;
    }
    bank_->current = program;
    applyProgram();
    for (size_t i = 0; i < paramSpecCount(); ++i) {
        pushParamValue(out, paramSpecs()[i].index);
    }
    markAllGuiDirty();
}

void ObxdClapPlugin::takeStagedBank(const clap_output_events_t *out) noexcept {
    Bank *staged = stagedBank_.exchange(nullptr);
    if (!staged) {
        return;
    }
    *bank_ = *staged;
    delete retiredBank_.exchange(staged);
    applyProgram();
    for (size_t i = 0; i < paramSpecCount(); ++i) {
        pushParamValue(out, paramSpecs()[i].index);
    }
    markAllGuiDirty();
}

// ---- events -------------------------------------------------------------------

void ObxdClapPlugin::pushParamValue(const clap_output_events_t *out, int index) noexcept {
    const ParamSpec *spec = paramSpecForIndex(index);
    if (!out || !spec) {
        return;
    }
    clap_event_param_value_t event{};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_PARAM_VALUE;
    event.header.flags = 0;
    event.param_id = static_cast<clap_id>(index);
    event.cookie = nullptr;
    event.note_id = -1;
    event.port_index = -1;
    event.channel = -1;
    event.key = -1;
    event.value = engineToPlain(*spec, bank_->currentProgram().values[static_cast<size_t>(index)]);
    out->try_push(out, &event.header);
}

void ObxdClapPlugin::pushGesture(const clap_output_events_t *out, int index, bool begin) noexcept {
    if (!out) {
        return;
    }
    clap_event_param_gesture_t event{};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = begin ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END;
    event.header.flags = 0;
    event.param_id = static_cast<clap_id>(index);
    out->try_push(out, &event.header);
}

void ObxdClapPlugin::drainGuiEdits(const clap_output_events_t *out) noexcept {
    if (!guiEditsPending_.exchange(false)) {
        return;
    }
    for (int index = 0; index < kEngineParamCount; ++index) {
        const uint8_t flags = guiFlags_[static_cast<size_t>(index)].exchange(0);
        if (flags == 0) {
            continue;
        }
        if (flags & kGuiBegin) {
            pushGesture(out, index, true);
        }
        if (flags & kGuiValue) {
            setPlain(index, guiValue_[static_cast<size_t>(index)].load());
            pushParamValue(out, index);
        }
        if (flags & kGuiEnd) {
            pushGesture(out, index, false);
        }
    }
}

void ObxdClapPlugin::handleEvent(const clap_event_header_t *event,
                                 const clap_output_events_t *out) noexcept {
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) {
        return;
    }
    switch (event->type) {
    case CLAP_EVENT_NOTE_ON: {
        const auto *note = reinterpret_cast<const clap_event_note_t *>(event);
        if (note->key < 0 || note->key > 127) {
            break;
        }
        if (note->velocity <= 0.0) {
            synth_->procNoteOff(note->key);
        } else {
            synth_->procNoteOn(note->key, static_cast<float>(std::min(note->velocity, 1.0)));
        }
        break;
    }
    case CLAP_EVENT_NOTE_OFF:
    case CLAP_EVENT_NOTE_CHOKE: {
        const auto *note = reinterpret_cast<const clap_event_note_t *>(event);
        if (note->key >= 0 && note->key <= 127) {
            synth_->procNoteOff(note->key);
        } else if (note->key == -1) {
            synth_->allNotesOff();
        }
        break;
    }
    case CLAP_EVENT_PARAM_VALUE: {
        const auto *param = reinterpret_cast<const clap_event_param_value_t *>(event);
        if (paramSpecForIndex(static_cast<int>(param->param_id))) {
            setPlain(static_cast<int>(param->param_id), param->value);
        }
        break;
    }
    case CLAP_EVENT_MIDI: {
        const auto *midi = reinterpret_cast<const clap_event_midi_t *>(event);
        handleMidi(midi->data, out);
        break;
    }
    default:
        break;
    }
}

void ObxdClapPlugin::handleMidi(const uint8_t data[3], const clap_output_events_t *out) noexcept {
    const uint8_t status = data[0] & 0xf0;
    switch (status) {
    case 0x90:
        if (data[2] == 0) {
            synth_->procNoteOff(data[1] & 0x7f);
        } else {
            synth_->procNoteOn(data[1] & 0x7f, (data[2] & 0x7f) / 127.0f);
        }
        break;
    case 0x80:
        synth_->procNoteOff(data[1] & 0x7f);
        break;
    case 0xe0: {
        const int bend = (data[1] & 0x7f) | ((data[2] & 0x7f) << 7);
        synth_->procPitchWheel((bend - 8192) / 8192.0f);
        break;
    }
    case 0xb0: {
        const int controller = data[1] & 0x7f;
        const int value = data[2] & 0x7f;
        if (controller == 1) {
            synth_->procModWheel(value / 127.0f);
        } else if (controller == 64) {
            if (value >= 64) {
                synth_->sustainOn();
            } else {
                synth_->sustainOff();
            }
        } else if (controller == 120) {
            synth_->sustainOff();
            synth_->allSoundOff();
        } else if (controller == 123) {
            synth_->sustainOff();
            synth_->allNotesOff();
        }
        break;
    }
    case 0xc0:
        selectProgram(data[1] & 0x7f, out);
        break;
    default:
        break;
    }
}

clap_process_status ObxdClapPlugin::process(const clap_process_t *process) noexcept {
    if (!process || !synth_) {
        return CLAP_PROCESS_ERROR;
    }
    const clap_output_events_t *out = process->out_events;
    takeStagedBank(out);
    drainGuiEdits(out);

    if (const clap_event_transport_t *transport = process->transport) {
        constexpr uint32_t kTimeline = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE;
        if ((transport->flags & kTimeline) == kTimeline) {
            synth_->setPlayHead(static_cast<float>(transport->tempo),
                                static_cast<float>(static_cast<double>(transport->song_pos_beats) /
                                                   CLAP_BEATTIME_FACTOR));
        }
    }

    float *left = nullptr;
    float *right = nullptr;
    if (process->audio_outputs_count > 0 && process->audio_outputs[0].data32 &&
        process->audio_outputs[0].channel_count > 0) {
        left = process->audio_outputs[0].data32[0];
        right = process->audio_outputs[0].channel_count > 1 ? process->audio_outputs[0].data32[1]
                                                            : nullptr;
    }

    const clap_input_events_t *in = process->in_events;
    const uint32_t eventCount = in ? in->size(in) : 0;
    uint32_t nextEvent = 0;
    const uint32_t frames = process->frames_count;

    for (uint32_t frame = 0; frame < frames; ++frame) {
        while (nextEvent < eventCount) {
            const clap_event_header_t *event = in->get(in, nextEvent);
            if (event && event->time > frame) {
                break;
            }
            handleEvent(event, out);
            ++nextEvent;
        }
        float l = 0.0f;
        float r = 0.0f;
        synth_->processSample(&l, &r);
        if (left) {
            left[frame] = l;
        }
        if (right) {
            right[frame] = r;
        }
    }
    // Events stamped at or after the block end still belong to this block.
    while (nextEvent < eventCount) {
        handleEvent(in->get(in, nextEvent++), out);
    }

    if (guiAnyDirty_.load() && guiCreated_) {
        requestCallback();
    }
    return CLAP_PROCESS_CONTINUE;
}

// ---- params ---------------------------------------------------------------------

uint32_t ObxdClapPlugin::paramCount() const noexcept {
    return static_cast<uint32_t>(paramSpecCount());
}

bool ObxdClapPlugin::paramInfo(uint32_t index, clap_param_info_t *info) const noexcept {
    if (!info || index >= paramSpecCount()) {
        return false;
    }
    const ParamSpec &spec = paramSpecs()[index];
    std::memset(info, 0, sizeof(*info));
    info->id = static_cast<clap_id>(spec.index);
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (isStepped(spec)) {
        info->flags |= CLAP_PARAM_IS_STEPPED;
    }
    info->cookie = nullptr;
    std::snprintf(info->name, sizeof(info->name), "%s", spec.name);
    std::snprintf(info->module, sizeof(info->module), "%s", spec.module);
    info->min_value = plainMin(spec);
    info->max_value = plainMax(spec);
    const Program defaults;
    info->default_value = engineToPlain(spec, defaults.values[static_cast<size_t>(spec.index)]);
    return true;
}

bool ObxdClapPlugin::paramValue(clap_id id, double *value) const noexcept {
    const ParamSpec *spec = paramSpecForIndex(static_cast<int>(id));
    if (!spec || !value || !bank_) {
        return false;
    }
    *value = engineToPlain(*spec, bank_->currentProgram().values[id]);
    return true;
}

bool ObxdClapPlugin::paramValueToText(clap_id id, double value, char *out,
                                      uint32_t capacity) const noexcept {
    const ParamSpec *spec = paramSpecForIndex(static_cast<int>(id));
    if (!spec || !out || capacity == 0) {
        return false;
    }
    const std::string text = formatValue(*spec, value);
    std::snprintf(out, capacity, "%s", text.c_str());
    return true;
}

bool ObxdClapPlugin::paramTextToValue(clap_id id, const char *text,
                                      double *value) const noexcept {
    const ParamSpec *spec = paramSpecForIndex(static_cast<int>(id));
    return spec && parseValue(*spec, text, value);
}

void ObxdClapPlugin::paramsFlush(const clap_input_events_t *in,
                                 const clap_output_events_t *out) noexcept {
    if (!synth_) {
        return;
    }
    takeStagedBank(out);
    drainGuiEdits(out);
    const uint32_t count = in ? in->size(in) : 0;
    for (uint32_t i = 0; i < count; ++i) {
        handleEvent(in->get(in, i), out);
    }
    if (guiAnyDirty_.load() && guiCreated_) {
        requestCallback();
    }
}

// ---- state ----------------------------------------------------------------------

bool ObxdClapPlugin::stateSave(const clap_ostream_t *stream) noexcept {
    if (!stream || !bank_) {
        return false;
    }
    const std::vector<uint8_t> bytes = writeBankState(*bank_);
    return writeAll(stream, bytes.data(), bytes.size());
}

bool ObxdClapPlugin::stateLoad(const clap_istream_t *stream) noexcept {
    if (!stream || !bank_) {
        return false;
    }
    std::vector<uint8_t> bytes;
    uint8_t chunk[4096];
    for (;;) {
        const int64_t got = stream->read(stream, chunk, sizeof(chunk));
        if (got < 0) {
            return false;
        }
        if (got == 0) {
            break;
        }
        if (bytes.size() + static_cast<size_t>(got) > kMaxStateBytes) {
            return false;
        }
        bytes.insert(bytes.end(), chunk, chunk + got);
    }

    std::unique_ptr<Bank> loaded(new (std::nothrow) Bank(*bank_));
    if (!loaded || !readBankState(bytes.data(), bytes.size(), *loaded)) {
        return false;
    }

    if (active_ && processing_) {
        // The audio thread owns the engine: hand the bank over for its next block.
        delete stagedBank_.exchange(loaded.release());
        return true;
    }
    *bank_ = *loaded;
    applyProgram();
    markAllGuiDirty();
    if (hostParams_ && host_) {
        hostParams_->rescan(host_, CLAP_PARAM_RESCAN_VALUES);
    }
    sendGuiDirty();
    return true;
}

// ---- gui / webview ------------------------------------------------------------------

bool ObxdClapPlugin::guiCreate(const char *api, bool isFloating) noexcept {
    if (!api || isFloating || std::strcmp(api, CLAP_WINDOW_API_WEBVIEW) != 0) {
        return false;
    }
    guiCreated_ = true;
    guiReady_ = false;
    return true;
}

void ObxdClapPlugin::guiDestroy() noexcept {
    guiCreated_ = false;
    guiReady_ = false;
}

bool ObxdClapPlugin::guiShow() noexcept { return guiCreated_; }
bool ObxdClapPlugin::guiHide() noexcept { return guiCreated_; }

int32_t ObxdClapPlugin::webviewGetUri(char *uri, uint32_t capacity) const noexcept {
    static constexpr char kUri[] = "/index.html";
    if (uri && capacity > 0) {
        std::snprintf(uri, capacity, "%s", kUri);
    }
    return static_cast<int32_t>(sizeof(kUri));
}

bool ObxdClapPlugin::webviewGetResource(const char *path, char *mime, uint32_t mimeCapacity,
                                        const clap_ostream_t *stream) const noexcept {
    if (!path || !stream) {
        return false;
    }
    std::string_view clean(path);
    if (const size_t cut = clean.find_first_of("?#"); cut != std::string_view::npos) {
        clean = clean.substr(0, cut);
    }
    const std::string key = clean == "/" ? std::string("/index.html") : std::string(clean);
    const WebUiResource *resource = findWebUiResource(key.c_str());
    if (!resource) {
        return false;
    }
    if (mime && mimeCapacity > 0) {
        std::snprintf(mime, mimeCapacity, "%s", resource->mime);
    }
    return writeAll(stream, resource->data, resource->size);
}

bool ObxdClapPlugin::webviewReceive(const void *buffer, uint32_t size) noexcept {
    if (!guiCreated_ || !buffer || size == 0 || !bank_) {
        return false;
    }
    const std::string_view json(static_cast<const char *>(buffer), size);
    const std::string_view type = jsonField(json, "type");

    if (type == "ready") {
        guiReady_ = true;
        sendGuiInit();
        return true;
    }

    double idValue = -1.0;
    if (!jsonNumberField(json, "id", &idValue)) {
        return false;
    }
    const int index = static_cast<int>(idValue);
    const ParamSpec *spec = paramSpecForIndex(index);
    if (!spec || static_cast<double>(index) != idValue) {
        return false;
    }
    const size_t slot = static_cast<size_t>(index);

    if (type == "gesture") {
        const bool begin = jsonField(json, "begin") == "true";
        if (active_) {
            guiFlags_[slot].fetch_or(begin ? kGuiBegin : kGuiEnd);
            guiEditsPending_.store(true);
            if (hostParams_ && host_) {
                hostParams_->request_flush(host_);
            }
        }
        return true;
    }

    if (type == "set") {
        double plain = 0.0;
        if (!jsonNumberField(json, "value", &plain)) {
            return false;
        }
        const double lo = plainMin(*spec);
        const double hi = plainMax(*spec);
        plain = plain < lo ? lo : (plain > hi ? hi : plain);
        if (isStepped(*spec)) {
            plain = std::round(plain);
        }
        if (active_) {
            guiValue_[slot].store(plain);
            guiFlags_[slot].fetch_or(kGuiValue);
            guiEditsPending_.store(true);
            if (hostParams_ && host_) {
                hostParams_->request_flush(host_);
            }
        } else {
            // Nothing is processing: the main thread owns the engine.
            setPlain(index, plain);
            if (hostParams_ && host_) {
                hostParams_->rescan(host_, CLAP_PARAM_RESCAN_VALUES);
            }
            sendGuiDirty();
        }
        if (hostState_ && host_) {
            hostState_->mark_dirty(host_);
        }
        return true;
    }
    return false;
}

void ObxdClapPlugin::markGuiDirty(int index) noexcept {
    guiDirty_[static_cast<size_t>(index)].store(true);
    guiAnyDirty_.store(true);
}

void ObxdClapPlugin::markAllGuiDirty() noexcept {
    for (size_t i = 0; i < paramSpecCount(); ++i) {
        guiDirty_[static_cast<size_t>(paramSpecs()[i].index)].store(true);
    }
    guiAnyDirty_.store(true);
}

void ObxdClapPlugin::requestCallback() noexcept {
    if (host_ && host_->request_callback && !callbackRequested_.exchange(true)) {
        host_->request_callback(host_);
    }
}

void ObxdClapPlugin::sendToGui(const std::string &message) noexcept {
    if (guiCreated_ && guiReady_ && hostWebview_ && host_) {
        hostWebview_->send(host_, message.data(), static_cast<uint32_t>(message.size()));
    }
}

std::string ObxdClapPlugin::paramMessage(const ParamSpec &spec) const {
    const double value =
        engineToPlain(spec, bank_->currentProgram().values[static_cast<size_t>(spec.index)]);
    std::string out = "{\"type\":\"param\",\"id\":";
    out += std::to_string(spec.index);
    out += ",\"value\":";
    out += jsonNumber(value);
    out += ",\"text\":";
    appendJsonString(out, formatValue(spec, value));
    out += '}';
    return out;
}

void ObxdClapPlugin::sendGuiInit() noexcept {
    const Program defaults;
    std::string out = "{\"type\":\"init\",\"plugin\":";
    appendJsonString(out, kDescriptor.name);
    out += ",\"version\":";
    appendJsonString(out, kDescriptor.version);
    out += ",\"params\":[";
    for (size_t i = 0; i < paramSpecCount(); ++i) {
        const ParamSpec &spec = paramSpecs()[i];
        const double value =
            engineToPlain(spec, bank_->currentProgram().values[static_cast<size_t>(spec.index)]);
        if (i > 0) {
            out += ',';
        }
        out += "{\"id\":";
        out += std::to_string(spec.index);
        out += ",\"key\":";
        appendJsonString(out, spec.key);
        out += ",\"name\":";
        appendJsonString(out, spec.name);
        out += ",\"module\":";
        appendJsonString(out, spec.module);
        out += ",\"min\":";
        out += jsonNumber(plainMin(spec));
        out += ",\"max\":";
        out += jsonNumber(plainMax(spec));
        out += ",\"default\":";
        out += jsonNumber(engineToPlain(spec, defaults.values[static_cast<size_t>(spec.index)]));
        out += ",\"stepped\":";
        out += isStepped(spec) ? "true" : "false";
        out += ",\"value\":";
        out += jsonNumber(value);
        out += ",\"text\":";
        appendJsonString(out, formatValue(spec, value));
        out += '}';
    }
    out += "]}";
    // The page now has every value; earlier change marks are satisfied.
    for (auto &dirty : guiDirty_) {
        dirty.store(false);
    }
    guiAnyDirty_.store(false);
    sendToGui(out);
}

void ObxdClapPlugin::sendGuiDirty() noexcept {
    if (!guiAnyDirty_.exchange(false)) {
        return;
    }
    const bool deliver = guiCreated_ && guiReady_;
    for (size_t i = 0; i < paramSpecCount(); ++i) {
        const ParamSpec &spec = paramSpecs()[i];
        if (guiDirty_[static_cast<size_t>(spec.index)].exchange(false) && deliver) {
            sendToGui(paramMessage(spec));
        }
    }
}

// ---- entry and factory -------------------------------------------------------------

namespace {

uint32_t gEntryInitCount = 0;

bool CLAP_ABI entryInit(const char *) {
    ++gEntryInitCount;
    return true;
}

void CLAP_ABI entryDeinit() {
    if (gEntryInitCount > 0) {
        --gEntryInitCount;
    }
}

uint32_t CLAP_ABI factoryCount(const clap_plugin_factory_t *) { return 1; }

const clap_plugin_descriptor_t *CLAP_ABI factoryDescriptor(const clap_plugin_factory_t *,
                                                           uint32_t index) {
    return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_t *CLAP_ABI factoryCreate(const clap_plugin_factory_t *,
                                            const clap_host_t *host, const char *pluginId) {
    if (gEntryInitCount == 0 || !host || !pluginId || std::strcmp(pluginId, kPluginId) != 0 ||
        !clap_version_is_compatible(host->clap_version)) {
        return nullptr;
    }
    auto *plugin = new (std::nothrow) ObxdClapPlugin(host);
    return plugin ? plugin->clapPlugin() : nullptr;
}

const clap_plugin_factory_t kFactory{factoryCount, factoryDescriptor, factoryCreate};

const void *CLAP_ABI entryGetFactory(const char *factoryId) {
    if (factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0) {
        return &kFactory;
    }
    return nullptr;
}

} // namespace

} // namespace obxd

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{
    CLAP_VERSION,
    obxd::entryInit,
    obxd::entryDeinit,
    obxd::entryGetFactory,
};
