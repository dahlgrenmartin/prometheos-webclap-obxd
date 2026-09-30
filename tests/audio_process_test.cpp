#include "ClapTestHost.h"

#include <ObxdJuceShim.h>

#include "Engine/SynthEngine.h"

#include <cmath>
#include <vector>

using namespace testhost;

namespace {

double rms(const std::vector<float> &x, size_t from, size_t to) {
    double sum = 0.0;
    for (size_t i = from; i < to; ++i) {
        sum += static_cast<double>(x[i]) * x[i];
    }
    return std::sqrt(sum / static_cast<double>(to - from));
}

} // namespace

int main() {
    // Silence before any note, sound after a note-on at a sample offset.
    {
        Instance inst;
        inst.start(48000.0);
        InputEvents in;
        OutputEvents out;
        CHECK(inst.render(512, in, out) == 0.0f);

        std::vector<float> left;
        in.noteOn(300, 57, 0.8);
        inst.render(512, in, out, &left);
        for (size_t i = 0; i < 300; ++i) {
            CHECK(left[i] == 0.0f); // sample-accurate onset
        }
        in.clear();
        for (int block = 0; block < 40; ++block) {
            inst.render(480, in, out, &left);
        }
        CHECK(rms(left, 512, left.size()) > 0.01);

        // Note-off: the default release decays to silence.
        in.noteOff(0, 57);
        inst.render(480, in, out, &left);
        in.clear();
        float tail = 1.0f;
        for (int block = 0; block < 200; ++block) {
            tail = inst.render(480, in, out);
        }
        CHECK(tail < 1e-4f);
    }

    // Arbitrary process sizes, MIDI dialect, pitch bend and parameters.
    {
        Instance inst;
        inst.start(44100.0);
        InputEvents in;
        OutputEvents out;
        std::vector<float> left;
        in.midi(0, 0x90, 60, 100);
        inst.render(1, in, out, &left);
        in.clear();
        const uint32_t sizes[] = {1, 7, 31, 128, 257, 1000, 4096};
        for (uint32_t frames : sizes) {
            inst.render(frames, in, out, &left);
        }
        CHECK(rms(left, 2000, left.size()) > 0.01);

        in.midi(0, 0xe0, 0x7f, 0x7f); // full bend up
        in.midi(0, 0xb0, 1, 127);     // mod wheel
        inst.render(256, in, out);
        in.clear();

        // Closing the filter makes the sustained note much quieter.
        std::vector<float> open;
        inst.render(4410, in, out, &open);
        in.param(0, CUTOFF, 0.0);
        in.param(0, RESONANCE, 0.0);
        in.param(0, ENVELOPE_AMT, 0.0);
        std::vector<float> closed;
        inst.render(4410, in, out, &closed);
        in.clear();
        closed.clear();
        inst.render(4410, in, out, &closed);
        CHECK(rms(closed, 0, closed.size()) < 0.5 * rms(open, 0, open.size()));

        // All-sound-off silences immediately-ish.
        in.midi(0, 0xb0, 120, 0);
        inst.render(64, in, out);
        in.clear();
        float peak = 1.0f;
        for (int block = 0; block < 20; ++block) {
            peak = inst.render(512, in, out);
        }
        CHECK(peak < 1e-4f);
    }

    // A MIDI program change selects another program of the bank and reports
    // every parameter to the host.
    {
        Instance inst;
        inst.start();
        InputEvents in;
        OutputEvents out;
        in.param(0, CUTOFF, 0.3125);
        inst.render(16, in, out);
        in.clear();
        CHECK(inst.value(CUTOFF) == 0.3125);
        in.midi(0, 0xc0, 5, 0);
        inst.render(16, in, out);
        CHECK(inst.value(CUTOFF) == 1.0); // program 5 still holds defaults
        CHECK(out.count(CLAP_EVENT_PARAM_VALUE) == inst.params->count(inst.plugin));
        CHECK(out.lastValue(CUTOFF) == 1.0);
        in.clear();
        OutputEvents out2;
        in.midi(0, 0xc0, 0, 0);
        inst.render(16, in, out2);
        CHECK(out2.lastValue(CUTOFF) == 0.3125);
    }

    // A state load while processing is applied by the audio thread on its next
    // block, which reports the new values to the host.
    {
        Instance source;
        InputEvents in;
        OutputEvents out;
        in.param(0, RESONANCE, 0.625);
        source.params->flush(source.plugin, &in.list, &out.list);
        Buffer saved;
        CHECK(source.state->save(source.plugin, &saved.out));

        Instance inst;
        inst.start();
        in.clear();
        inst.render(16, in, out);
        CHECK(inst.state->load(inst.plugin, &saved.in));
        CHECK(inst.value(RESONANCE) == 0.0); // not yet: the audio thread owns the engine
        OutputEvents loaded;
        inst.render(16, in, loaded);
        CHECK(inst.value(RESONANCE) == 0.625);
        CHECK(loaded.lastValue(RESONANCE) == 0.625);
    }

    // Host transport drives the LFO's tempo sync without disturbing output.
    {
        Instance inst;
        inst.start();
        InputEvents in;
        OutputEvents out;
        in.param(0, LFO_SYNC, 1.0);
        in.noteOn(0, 64, 1.0);
        clap_event_transport_t transport{};
        transport.header = {sizeof(transport), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0};
        transport.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE |
                          CLAP_TRANSPORT_IS_PLAYING;
        transport.tempo = 128.0;
        std::vector<float> left;
        for (int block = 0; block < 50; ++block) {
            transport.song_pos_beats = static_cast<clap_beattime>(block * 0.05 * CLAP_BEATTIME_FACTOR);
            inst.render(256, in, out, &left, &transport);
            in.clear();
        }
        CHECK(rms(left, 1000, left.size()) > 0.01);
    }

    std::puts("audio_process ok");
    return 0;
}
