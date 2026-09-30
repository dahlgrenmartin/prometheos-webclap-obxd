#pragma once

// OB-Xd 2.10 as a direct CLAP plugin.
//
// ObxdClapPlugin owns OB-Xd's SynthEngine and speaks the CLAP C ABI itself:
// audio-ports, note-ports, params, state, gui and the clap.webview/3 draft.
// There is no JUCE, wrapper framework or host-specific glue.
//
// Threads. Engine state is touched only on the audio thread while the plugin
// is processing, and on the main thread otherwise. Edits made in the web GUI
// (main thread) reach the audio thread through per-parameter atomics and come
// back to the host as CLAP_EVENT_PARAM_VALUE output events; values the host or
// a MIDI program change set on the audio thread are marked for the GUI, which
// the main thread sends on its next on_main_thread() callback.

#include "ObxdParameters.h"
#include "ObxdState.h"

#include <clap/clap.h>
#include <clap/ext/draft/webview.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

class SynthEngine;

namespace obxd {

constexpr const char *kPluginId = "com.discodsp.ob-xd";
constexpr uint32_t kGuiWidth = 1000;
constexpr uint32_t kGuiHeight = 423;

class ObxdClapPlugin final {
  public:
    explicit ObxdClapPlugin(const clap_host_t *host);
    ~ObxdClapPlugin();

    ObxdClapPlugin(const ObxdClapPlugin &) = delete;
    ObxdClapPlugin &operator=(const ObxdClapPlugin &) = delete;

    const clap_plugin_t *clapPlugin() noexcept { return &plugin_; }
    static ObxdClapPlugin *self(const clap_plugin_t *plugin) noexcept;

    bool init() noexcept;
    bool activate(double sampleRate, uint32_t minFrames, uint32_t maxFrames) noexcept;
    void deactivate() noexcept;
    bool startProcessing() noexcept;
    void stopProcessing() noexcept;
    void reset() noexcept;
    clap_process_status process(const clap_process_t *process) noexcept;
    const void *getExtension(const char *id) noexcept;
    void onMainThread() noexcept;

    // clap.params
    uint32_t paramCount() const noexcept;
    bool paramInfo(uint32_t index, clap_param_info_t *info) const noexcept;
    bool paramValue(clap_id id, double *value) const noexcept;
    bool paramValueToText(clap_id id, double value, char *out, uint32_t capacity) const noexcept;
    bool paramTextToValue(clap_id id, const char *text, double *value) const noexcept;
    void paramsFlush(const clap_input_events_t *in, const clap_output_events_t *out) noexcept;

    // clap.state
    bool stateSave(const clap_ostream_t *stream) noexcept;
    bool stateLoad(const clap_istream_t *stream) noexcept;

    // clap.gui (webview API only) and clap.webview/3
    bool guiCreate(const char *api, bool isFloating) noexcept;
    void guiDestroy() noexcept;
    bool guiShow() noexcept;
    bool guiHide() noexcept;
    bool guiOpen() const noexcept { return guiCreated_; }
    int32_t webviewGetUri(char *uri, uint32_t capacity) const noexcept;
    bool webviewGetResource(const char *path, char *mime, uint32_t mimeCapacity,
                            const clap_ostream_t *stream) const noexcept;
    bool webviewReceive(const void *buffer, uint32_t size) noexcept;

    // Test hooks.
    const Bank &bankForTests() const noexcept { return *bank_; }

  private:
    enum GuiFlag : uint8_t { kGuiValue = 1, kGuiBegin = 2, kGuiEnd = 4 };

    void applyEngine(int index, float engineValue) noexcept;
    void applyProgram() noexcept;
    void setPlain(int index, double plain) noexcept;
    void handleEvent(const clap_event_header_t *event, const clap_output_events_t *out) noexcept;
    void handleMidi(const uint8_t data[3], const clap_output_events_t *out) noexcept;
    void selectProgram(int program, const clap_output_events_t *out) noexcept;
    void drainGuiEdits(const clap_output_events_t *out) noexcept;
    void takeStagedBank(const clap_output_events_t *out) noexcept;
    void pushParamValue(const clap_output_events_t *out, int index) noexcept;
    void pushGesture(const clap_output_events_t *out, int index, bool begin) noexcept;
    void markGuiDirty(int index) noexcept;
    void markAllGuiDirty() noexcept;
    void requestCallback() noexcept;
    void sendToGui(const std::string &message) noexcept;
    void sendGuiInit() noexcept;
    void sendGuiDirty() noexcept;
    std::string paramMessage(const ParamSpec &spec) const;

    clap_plugin_t plugin_{};
    const clap_host_t *host_{nullptr};
    const clap_host_params_t *hostParams_{nullptr};
    const clap_host_state_t *hostState_{nullptr};
    const clap_host_webview_t *hostWebview_{nullptr};

    std::unique_ptr<SynthEngine> synth_;
    std::unique_ptr<Bank> bank_;
    std::atomic<Bank *> stagedBank_{nullptr};
    std::atomic<Bank *> retiredBank_{nullptr};

    std::array<std::atomic<double>, kEngineParamCount> guiValue_{};
    std::array<std::atomic<uint8_t>, kEngineParamCount> guiFlags_{};
    std::atomic<bool> guiEditsPending_{false};
    std::array<std::atomic<bool>, kEngineParamCount> guiDirty_{};
    std::atomic<bool> guiAnyDirty_{false};
    std::atomic<bool> callbackRequested_{false};

    bool initialized_{false};
    bool active_{false};
    bool processing_{false};
    bool guiCreated_{false};
    bool guiReady_{false};
    double sampleRate_{44100.0};
};

} // namespace obxd
