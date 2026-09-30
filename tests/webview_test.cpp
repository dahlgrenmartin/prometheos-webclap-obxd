#include "ClapTestHost.h"

#include <ObxdJuceShim.h>

#include "Engine/SynthEngine.h"

#include <string>
#include <string_view>

using namespace testhost;

namespace {

struct Resource {
    bool ok = false;
    std::string mime;
    std::string body;
};

Resource fetch(Instance &inst, const char *path) {
    Buffer buffer;
    char mime[CLAP_NAME_SIZE] = {};
    Resource r;
    r.ok = inst.webview->get_resource(inst.plugin, path, mime, sizeof(mime), &buffer.out);
    r.mime = mime;
    r.body.assign(buffer.bytes.begin(), buffer.bytes.end());
    return r;
}

bool contains(const std::string &haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

void openGui(Instance &inst) {
    CHECK(inst.gui->create(inst.plugin, CLAP_WINDOW_API_WEBVIEW, false));
    uint32_t w = 0, h = 0;
    CHECK(inst.gui->get_size(inst.plugin, &w, &h));
    CHECK(w > 0 && h > 0);
    clap_window_t window{};
    window.api = CLAP_WINDOW_API_WEBVIEW;
    window.ptr = nullptr;
    CHECK(inst.gui->set_parent(inst.plugin, &window));
    CHECK(inst.gui->show(inst.plugin));
    inst.host.webviewOpen = true;
}

} // namespace

int main() {
    Instance inst;

    // get_size is only valid on a created GUI.
    uint32_t w = 0, h = 0;
    CHECK(!inst.gui->get_size(inst.plugin, &w, &h));
    CHECK(!inst.gui->create(inst.plugin, CLAP_WINDOW_API_WEBVIEW, true));

    // The start page is relative, served through get_resource.
    char uri[256];
    const int32_t length = inst.webview->get_uri(inst.plugin, uri, sizeof(uri));
    CHECK(length > 1 && std::string_view(uri) == "/index.html");
    CHECK(length == static_cast<int32_t>(std::strlen(uri) + 1));
    CHECK(inst.webview->get_uri(inst.plugin, nullptr, 0) == length);

    const Resource page = fetch(inst, "/index.html");
    CHECK(page.ok && contains(page.mime, "text/html"));
    CHECK(contains(page.body, "<html") || contains(page.body, "<!doctype"));
    CHECK(fetch(inst, "/").ok);
    CHECK(fetch(inst, "/index.html?v=1").ok);
    CHECK(!fetch(inst, "/missing.js").ok);
    CHECK(!fetch(inst, "/../module.wasm").ok);

    // Every file the page references is served.
    for (const char *path : {"/obxd.css", "/obxd.js"}) {
        const Resource r = fetch(inst, path);
        CHECK(r.ok && !r.body.empty());
    }

    // Messages are refused while no GUI exists.
    const std::string ready = R"({"type":"ready"})";
    CHECK(!inst.webview->receive(inst.plugin, ready.data(), static_cast<uint32_t>(ready.size())));

    openGui(inst);

    // Before activation the main thread owns the engine: edits apply at once.
    inst.send(ready);
    CHECK(inst.host.sent.size() == 1);
    const std::string &init = inst.host.sent[0];
    CHECK(contains(init, R"("type":"init")"));
    CHECK(contains(init, R"("id":44,"key":"Cutoff","name":"Cutoff","module":"Filter")"));
    size_t entries = 0;
    for (size_t at = init.find("\"key\":"); at != std::string::npos; at = init.find("\"key\":", at + 1)) {
        ++entries;
    }
    CHECK(entries == inst.params->count(inst.plugin));

    inst.send(R"({"type":"set","id":44,"value":0.375})");
    CHECK(inst.value(CUTOFF) == 0.375);
    CHECK(inst.host.rescans == 1);
    CHECK(inst.host.dirtyMarks == 1);
    CHECK(contains(inst.host.sent.back(), R"({"type":"param","id":44,"value":0.375,"text":"47"})"));

    // While processing, edits travel to the audio thread and back to the host
    // as gesture + value output events.
    inst.start();
    InputEvents in;
    OutputEvents out;
    inst.send(R"({"type":"gesture","id":45,"begin":true})");
    inst.send(R"({"type":"set","id":45,"value":0.75})");
    inst.send(R"({"type":"gesture","id":45,"begin":false})");
    CHECK(inst.host.flushesRequested >= 2);
    CHECK(inst.value(RESONANCE) == 0.0); // not applied until the audio thread runs
    inst.render(64, in, out);
    CHECK(inst.value(RESONANCE) == 0.75);
    CHECK(out.storage.size() == 3);
    CHECK(out.at(0)->type == CLAP_EVENT_PARAM_GESTURE_BEGIN);
    CHECK(out.at(1)->type == CLAP_EVENT_PARAM_VALUE);
    CHECK(out.at(2)->type == CLAP_EVENT_PARAM_GESTURE_END);
    CHECK(out.lastValue(RESONANCE) == 0.75);

    // Stepped values are rounded and clamped; malformed messages are refused.
    inst.send(R"({"type":"set","id":3,"value":40.2})");
    const std::string bad[] = {R"({"type":"set","id":1,"value":1})", R"({"type":"set","id":44})",
                               R"({"type":"set","id":2.5,"value":0})", R"({"type":"nope"})",
                               R"(not json)"};
    for (const auto &message : bad) {
        CHECK(!inst.webview->receive(inst.plugin, message.data(), static_cast<uint32_t>(message.size())));
    }
    OutputEvents out2;
    inst.render(64, in, out2);
    CHECK(inst.value(VOICE_COUNT) == 32.0);
    inst.plugin->on_main_thread(inst.plugin); // the host services the pending callback

    // Host automation reaches the page on the next main-thread callback.
    const size_t before = inst.host.sent.size();
    const int callbacks = inst.host.callbacksRequested;
    in.param(10, LATK, 0.875);
    OutputEvents out3;
    inst.render(64, in, out3);
    CHECK(out3.storage.empty());
    CHECK(inst.host.callbacksRequested > callbacks);
    inst.plugin->on_main_thread(inst.plugin);
    bool sawAttack = false;
    for (size_t i = before; i < inst.host.sent.size(); ++i) {
        sawAttack |= contains(inst.host.sent[i], R"("type":"param","id":51,"value":0.875)");
    }
    CHECK(sawAttack);

    // Closing the GUI stops messages.
    inst.gui->hide(inst.plugin);
    inst.gui->destroy(inst.plugin);
    inst.host.webviewOpen = false;
    CHECK(!inst.webview->receive(inst.plugin, ready.data(), static_cast<uint32_t>(ready.size())));

    std::puts("webview ok");
    return 0;
}
