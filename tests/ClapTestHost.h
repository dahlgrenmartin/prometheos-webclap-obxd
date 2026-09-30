#pragma once

// A minimal CLAP host for the native tests. Everything goes through the
// exported clap_entry and the C ABI, as a WebCLAP host would.

#include <clap/clap.h>
#include <clap/ext/draft/webview.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" const clap_plugin_entry_t clap_entry;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);         \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

namespace testhost {

struct Host {
    clap_host_t host{};
    clap_host_params_t params{};
    clap_host_state_t state{};
    clap_host_webview_t webview{};

    int callbacksRequested = 0;
    int flushesRequested = 0;
    int rescans = 0;
    int dirtyMarks = 0;
    bool webviewOpen = false;
    std::vector<std::string> sent;

    Host() {
        host.clap_version = CLAP_VERSION;
        host.host_data = this;
        host.name = "OB-Xd WCLAP test host";
        host.vendor = "prometheos";
        host.url = "https://example.invalid/";
        host.version = "1";
        host.get_extension = [](const clap_host_t *h, const char *id) -> const void * {
            auto *self = static_cast<Host *>(h->host_data);
            if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) {
                return &self->params;
            }
            if (std::strcmp(id, CLAP_EXT_STATE) == 0) {
                return &self->state;
            }
            if (std::strcmp(id, CLAP_EXT_WEBVIEW) == 0) {
                return &self->webview;
            }
            return nullptr;
        };
        host.request_restart = [](const clap_host_t *) {};
        host.request_process = [](const clap_host_t *) {};
        host.request_callback = [](const clap_host_t *h) {
            ++static_cast<Host *>(h->host_data)->callbacksRequested;
        };
        params.rescan = [](const clap_host_t *h, clap_param_rescan_flags) {
            ++static_cast<Host *>(h->host_data)->rescans;
        };
        params.clear = [](const clap_host_t *, clap_id, clap_param_clear_flags) {};
        params.request_flush = [](const clap_host_t *h) {
            ++static_cast<Host *>(h->host_data)->flushesRequested;
        };
        state.mark_dirty = [](const clap_host_t *h) {
            ++static_cast<Host *>(h->host_data)->dirtyMarks;
        };
        webview.send = [](const clap_host_t *h, const void *buffer, uint32_t size) {
            auto *self = static_cast<Host *>(h->host_data);
            if (!self->webviewOpen) {
                return false;
            }
            self->sent.emplace_back(static_cast<const char *>(buffer), size);
            return true;
        };
    }
};

// A sorted input event list over copies of the pushed events.
struct InputEvents {
    std::vector<std::vector<uint8_t>> storage;
    clap_input_events_t list{};

    InputEvents() {
        list.ctx = this;
        list.size = [](const clap_input_events_t *l) {
            return static_cast<uint32_t>(static_cast<InputEvents *>(l->ctx)->storage.size());
        };
        list.get = [](const clap_input_events_t *l, uint32_t i) -> const clap_event_header_t * {
            auto *self = static_cast<InputEvents *>(l->ctx);
            return i < self->storage.size()
                       ? reinterpret_cast<const clap_event_header_t *>(self->storage[i].data())
                       : nullptr;
        };
    }

    void push(const clap_event_header_t *event) {
        const auto *bytes = reinterpret_cast<const uint8_t *>(event);
        storage.emplace_back(bytes, bytes + event->size);
    }
    void clear() { storage.clear(); }

    void noteOn(uint32_t time, int16_t key, double velocity) {
        clap_event_note_t e{};
        e.header = {sizeof(e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_ON, 0};
        e.note_id = -1;
        e.port_index = 0;
        e.channel = 0;
        e.key = key;
        e.velocity = velocity;
        push(&e.header);
    }
    void noteOff(uint32_t time, int16_t key) {
        clap_event_note_t e{};
        e.header = {sizeof(e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_OFF, 0};
        e.note_id = -1;
        e.port_index = 0;
        e.channel = 0;
        e.key = key;
        e.velocity = 0.0;
        push(&e.header);
    }
    void param(uint32_t time, clap_id id, double value) {
        clap_event_param_value_t e{};
        e.header = {sizeof(e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        e.param_id = id;
        e.note_id = -1;
        e.port_index = -1;
        e.channel = -1;
        e.key = -1;
        e.value = value;
        push(&e.header);
    }
    void midi(uint32_t time, uint8_t a, uint8_t b, uint8_t c) {
        clap_event_midi_t e{};
        e.header = {sizeof(e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
        e.port_index = 0;
        e.data[0] = a;
        e.data[1] = b;
        e.data[2] = c;
        push(&e.header);
    }
};

struct OutputEvents {
    std::vector<std::vector<uint8_t>> storage;
    clap_output_events_t list{};

    OutputEvents() {
        list.ctx = this;
        list.try_push = [](const clap_output_events_t *l, const clap_event_header_t *event) {
            const auto *bytes = reinterpret_cast<const uint8_t *>(event);
            static_cast<OutputEvents *>(l->ctx)->storage.emplace_back(bytes, bytes + event->size);
            return true;
        };
    }

    const clap_event_header_t *at(size_t i) const {
        return reinterpret_cast<const clap_event_header_t *>(storage[i].data());
    }
    // The last value pushed for `id`, or NaN.
    double lastValue(clap_id id) const {
        double value = NAN;
        for (size_t i = 0; i < storage.size(); ++i) {
            const auto *h = at(i);
            if (h->type == CLAP_EVENT_PARAM_VALUE) {
                const auto *e = reinterpret_cast<const clap_event_param_value_t *>(h);
                if (e->param_id == id) {
                    value = e->value;
                }
            }
        }
        return value;
    }
    size_t count(uint16_t type) const {
        size_t n = 0;
        for (size_t i = 0; i < storage.size(); ++i) {
            n += at(i)->type == type ? 1 : 0;
        }
        return n;
    }
};

struct Buffer {
    std::vector<uint8_t> bytes;
    size_t readPos = 0;
    clap_ostream_t out{};
    clap_istream_t in{};

    Buffer() {
        out.ctx = this;
        out.write = [](const clap_ostream_t *s, const void *data, uint64_t size) -> int64_t {
            auto *self = static_cast<Buffer *>(s->ctx);
            const auto *p = static_cast<const uint8_t *>(data);
            // Accept at most 1000 bytes per call to exercise partial writes.
            const uint64_t n = size > 1000 ? 1000 : size;
            self->bytes.insert(self->bytes.end(), p, p + n);
            return static_cast<int64_t>(n);
        };
        in.ctx = this;
        in.read = [](const clap_istream_t *s, void *data, uint64_t size) -> int64_t {
            auto *self = static_cast<Buffer *>(s->ctx);
            const size_t left = self->bytes.size() - self->readPos;
            const size_t n = size < left ? static_cast<size_t>(size) : left;
            std::memcpy(data, self->bytes.data() + self->readPos, n);
            self->readPos += n;
            return static_cast<int64_t>(n);
        };
    }
};

struct Instance {
    Host host;
    const clap_plugin_t *plugin = nullptr;
    const clap_plugin_params_t *params = nullptr;
    const clap_plugin_state_t *state = nullptr;
    const clap_plugin_gui_t *gui = nullptr;
    const clap_plugin_webview_t *webview = nullptr;

    Instance() {
        CHECK(clap_entry.init("/plugin.wclap"));
        const auto *factory = static_cast<const clap_plugin_factory_t *>(
            clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
        CHECK(factory && factory->get_plugin_count(factory) == 1);
        const auto *desc = factory->get_plugin_descriptor(factory, 0);
        plugin = factory->create_plugin(factory, &host.host, desc->id);
        CHECK(plugin && plugin->init(plugin));
        params = static_cast<const clap_plugin_params_t *>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        state = static_cast<const clap_plugin_state_t *>(plugin->get_extension(plugin, CLAP_EXT_STATE));
        gui = static_cast<const clap_plugin_gui_t *>(plugin->get_extension(plugin, CLAP_EXT_GUI));
        webview = static_cast<const clap_plugin_webview_t *>(plugin->get_extension(plugin, CLAP_EXT_WEBVIEW));
        CHECK(params && state && gui && webview);
    }
    ~Instance() {
        plugin->destroy(plugin);
        clap_entry.deinit();
    }

    void start(double rate = 48000.0) {
        CHECK(plugin->activate(plugin, rate, 1, 4096));
        CHECK(plugin->start_processing(plugin));
    }

    // Renders `frames` stereo frames; returns the peak absolute sample.
    float render(uint32_t frames, InputEvents &in, OutputEvents &out, std::vector<float> *left = nullptr,
                 const clap_event_transport_t *transport = nullptr) {
        std::vector<float> l(frames), r(frames);
        float *channels[2] = {l.data(), r.data()};
        clap_audio_buffer_t output{};
        output.data32 = channels;
        output.channel_count = 2;
        clap_process_t process{};
        process.steady_time = -1;
        process.frames_count = frames;
        process.transport = transport;
        process.audio_outputs = &output;
        process.audio_outputs_count = 1;
        process.in_events = &in.list;
        process.out_events = &out.list;
        CHECK(plugin->process(plugin, &process) != CLAP_PROCESS_ERROR);
        float peak = 0.0f;
        for (uint32_t i = 0; i < frames; ++i) {
            CHECK(std::isfinite(l[i]) && std::isfinite(r[i]));
            peak = std::fmax(peak, std::fmax(std::fabs(l[i]), std::fabs(r[i])));
        }
        if (left) {
            left->insert(left->end(), l.begin(), l.end());
        }
        return peak;
    }

    double value(clap_id id) const {
        double v = NAN;
        CHECK(params->get_value(plugin, id, &v));
        return v;
    }

    void send(const std::string &json) {
        CHECK(webview->receive(plugin, json.data(), static_cast<uint32_t>(json.size())));
    }
};

} // namespace testhost
