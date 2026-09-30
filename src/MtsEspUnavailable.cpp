// OB-Xd's Tuning class links the ODDSound MTS-ESP client. MTS-ESP discovers a
// tuning master through a shared library in the host process, which a WCLAP
// sandbox cannot load, so the client is replaced by one that never finds a
// master: Tuning stays in its 12-TET mode and every note is untuned.

#include "MTS/libMTSClient.h"

struct MTSClient {};

namespace {
MTSClient gClient;
}

extern "C" {

MTSClient *MTS_RegisterClient() { return &gClient; }
void MTS_DeregisterClient(MTSClient *) {}
bool MTS_HasMaster(MTSClient *) { return false; }
bool MTS_ShouldFilterNote(MTSClient *, char, char) { return false; }
double MTS_NoteToFrequency(MTSClient *, char midinote, char) {
    return 440.0 * __builtin_pow(2.0, (static_cast<double>(midinote) - 69.0) / 12.0);
}
double MTS_RetuningInSemitones(MTSClient *, char, char) { return 0.0; }
double MTS_RetuningAsRatio(MTSClient *, char, char) { return 1.0; }
char MTS_FrequencyToNote(MTSClient *, double freq, char) {
    const double note = 69.0 + 12.0 * __builtin_log2(freq / 440.0);
    return static_cast<char>(note < 0.0 ? 0 : (note > 127.0 ? 127 : note + 0.5));
}
char MTS_FrequencyToNoteAndChannel(MTSClient *client, double freq, char *midichannel) {
    if (midichannel) {
        *midichannel = 0;
    }
    return MTS_FrequencyToNote(client, freq, 0);
}
const char *MTS_GetScaleName(MTSClient *) { return "12-TET"; }
void MTS_ParseMIDIDataU(MTSClient *, const unsigned char *, int) {}
void MTS_ParseMIDIData(MTSClient *, const char *, int) {}

}
