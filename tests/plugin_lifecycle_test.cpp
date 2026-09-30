#include "ClapTestHost.h"

#include <clap/ext/audio-ports.h>
#include <clap/ext/note-ports.h>

#include <string_view>

using namespace testhost;

int main() {
    CHECK(clap_version_is_compatible(clap_entry.clap_version));
    CHECK(clap_entry.init("/plugin.wclap"));
    const auto *factory =
        static_cast<const clap_plugin_factory_t *>(clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
    CHECK(factory);
    CHECK(clap_entry.get_factory("clap.nonexistent-factory") == nullptr);
    CHECK(factory->get_plugin_count(factory) == 1);
    CHECK(factory->get_plugin_descriptor(factory, 1) == nullptr);

    const auto *desc = factory->get_plugin_descriptor(factory, 0);
    CHECK(desc && std::string_view(desc->id) == "com.discodsp.ob-xd");
    CHECK(std::string_view(desc->name) == "OB-Xd");
    bool instrument = false;
    for (const char *const *f = desc->features; *f; ++f) {
        instrument |= std::string_view(*f) == CLAP_PLUGIN_FEATURE_INSTRUMENT;
    }
    CHECK(instrument);

    Host host;
    CHECK(factory->create_plugin(factory, &host.host, "com.example.other") == nullptr);
    clap_entry.deinit();

    {
        Instance a;
        Instance b; // several instances share one module

        const auto *audio = static_cast<const clap_plugin_audio_ports_t *>(
            a.plugin->get_extension(a.plugin, CLAP_EXT_AUDIO_PORTS));
        CHECK(audio && audio->count(a.plugin, true) == 0 && audio->count(a.plugin, false) == 1);
        clap_audio_port_info_t port{};
        CHECK(audio->get(a.plugin, 0, false, &port));
        CHECK(port.channel_count == 2 && (port.flags & CLAP_AUDIO_PORT_IS_MAIN));
        CHECK(std::string_view(port.port_type) == CLAP_PORT_STEREO);

        const auto *notes = static_cast<const clap_plugin_note_ports_t *>(
            a.plugin->get_extension(a.plugin, CLAP_EXT_NOTE_PORTS));
        CHECK(notes && notes->count(a.plugin, true) == 1 && notes->count(a.plugin, false) == 0);
        clap_note_port_info_t notePort{};
        CHECK(notes->get(a.plugin, 0, true, &notePort));
        CHECK(notePort.supported_dialects & CLAP_NOTE_DIALECT_CLAP);
        CHECK(notePort.supported_dialects & CLAP_NOTE_DIALECT_MIDI);

        CHECK(a.plugin->get_extension(a.plugin, "clap.unknown") == nullptr);

        // clap.gui offers the webview API only, embedded.
        CHECK(a.gui->is_api_supported(a.plugin, CLAP_WINDOW_API_WEBVIEW, false));
        CHECK(!a.gui->is_api_supported(a.plugin, CLAP_WINDOW_API_WEBVIEW, true));
        CHECK(!a.gui->is_api_supported(a.plugin, CLAP_WINDOW_API_X11, false));

        a.start(44100.0);
        b.start(96000.0);
        a.plugin->stop_processing(a.plugin);
        a.plugin->deactivate(a.plugin);
        a.start(48000.0); // reactivation at a new rate
        a.plugin->reset(a.plugin);
        a.plugin->stop_processing(a.plugin);
        a.plugin->deactivate(a.plugin);
        b.plugin->stop_processing(b.plugin);
        b.plugin->deactivate(b.plugin);
    }
    std::puts("plugin_lifecycle ok");
    return 0;
}
