#include "ObxdState.h"

#include <ObxdJuceShim.h>

#include "Engine/SynthEngine.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string_view>

static_assert(obxd::kEngineParamCount == PARAM_COUNT, "engine parameter count changed");

namespace obxd {
namespace {

constexpr uint32_t kMagicXml = 0x21324356; // JUCE AudioProcessor::magicXmlNumber
constexpr size_t kMaxStateBytes = 16u * 1024u * 1024u;

void putU32(std::vector<uint8_t> &out, size_t offset, uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out[offset + static_cast<size_t>(i)] = static_cast<uint8_t>(value >> (8 * i));
    }
}

uint32_t getU32(const uint8_t *bytes) {
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

void appendEscaped(std::string &out, std::string_view text) {
    for (char c : text) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&apos;";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "&#%d;", static_cast<int>(c));
                out += buffer;
            } else {
                out += c;
            }
        }
    }
}

void appendAttribute(std::string &out, const char *name, std::string_view value) {
    out += ' ';
    out += name;
    out += "=\"";
    appendEscaped(out, value);
    out += '"';
}

std::string floatText(float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
    return buffer;
}

void appendValues(std::string &out, const Program &program) {
    for (int k = 0; k < kEngineParamCount; ++k) {
        const std::string name = "Val_" + std::to_string(k);
        appendAttribute(out, name.c_str(), floatText(program.values[static_cast<size_t>(k)]));
    }
}

std::string unescape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out += text[i];
            continue;
        }
        const size_t end = text.find(';', i);
        if (end == std::string_view::npos) {
            out += text[i];
            continue;
        }
        const std::string_view entity = text.substr(i + 1, end - i - 1);
        if (entity == "amp") {
            out += '&';
        } else if (entity == "lt") {
            out += '<';
        } else if (entity == "gt") {
            out += '>';
        } else if (entity == "quot") {
            out += '"';
        } else if (entity == "apos") {
            out += '\'';
        } else if (!entity.empty() && entity[0] == '#') {
            const bool hex = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
            const std::string digits(entity.substr(hex ? 2 : 1));
            const long code = std::strtol(digits.c_str(), nullptr, hex ? 16 : 10);
            if (code > 0 && code < 0x80) {
                out += static_cast<char>(code);
            }
        } else {
            out.append(text.substr(i, end - i + 1));
        }
        i = end;
    }
    return out;
}

struct Element {
    std::string name;
    std::map<std::string, std::string, std::less<>> attributes;
};

// The start tags of a single-line JUCE XML document, in document order.
bool parseStartTags(std::string_view xml, std::vector<Element> &out) {
    size_t pos = 0;
    while ((pos = xml.find('<', pos)) != std::string_view::npos) {
        ++pos;
        if (pos >= xml.size()) {
            return false;
        }
        const char lead = xml[pos];
        if (lead == '?' || lead == '!' || lead == '/') {
            const size_t close = xml.find('>', pos);
            if (close == std::string_view::npos) {
                return false;
            }
            pos = close + 1;
            continue;
        }

        Element element;
        size_t i = pos;
        while (i < xml.size() && xml[i] != ' ' && xml[i] != '>' && xml[i] != '/' &&
               xml[i] != '\t' && xml[i] != '\n' && xml[i] != '\r') {
            ++i;
        }
        element.name.assign(xml.substr(pos, i - pos));
        if (element.name.empty()) {
            return false;
        }

        for (;;) {
            while (i < xml.size() && (xml[i] == ' ' || xml[i] == '\t' || xml[i] == '\n' ||
                                      xml[i] == '\r')) {
                ++i;
            }
            if (i >= xml.size()) {
                return false;
            }
            if (xml[i] == '>' || xml[i] == '/') {
                const size_t close = xml.find('>', i);
                if (close == std::string_view::npos) {
                    return false;
                }
                pos = close + 1;
                break;
            }
            const size_t eq = xml.find('=', i);
            if (eq == std::string_view::npos || eq + 1 >= xml.size()) {
                return false;
            }
            std::string_view name = xml.substr(i, eq - i);
            while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) {
                name.remove_suffix(1);
            }
            size_t q = eq + 1;
            while (q < xml.size() && (xml[q] == ' ' || xml[q] == '\t')) {
                ++q;
            }
            if (q >= xml.size() || (xml[q] != '"' && xml[q] != '\'')) {
                return false;
            }
            const char quote = xml[q];
            const size_t valueEnd = xml.find(quote, q + 1);
            if (valueEnd == std::string_view::npos) {
                return false;
            }
            element.attributes.emplace(std::string(name),
                                       unescape(xml.substr(q + 1, valueEnd - q - 1)));
            i = valueEnd + 1;
        }
        out.push_back(std::move(element));
    }
    return !out.empty();
}

const std::string *attribute(const Element &element, std::string_view name) {
    const auto it = element.attributes.find(name);
    return it == element.attributes.end() ? nullptr : &it->second;
}

void readProgram(const Element &element, Program &program) {
    program.setDefaults();
    const bool newFormat = attribute(element, "voiceCount") != nullptr;
    for (int k = 0; k < kEngineParamCount; ++k) {
        const std::string index = std::to_string(k);
        const std::string *text = attribute(element, "Val_" + index);
        if (!text) {
            text = attribute(element, index);
        }
        if (!text) {
            continue;
        }
        float value = static_cast<float>(std::strtod(text->c_str(), nullptr));
        if (!newFormat && k == VOICE_COUNT) {
            value *= 0.25f;
        }
        program.values[static_cast<size_t>(k)] = value;
    }
    const std::string *name = attribute(element, "programName");
    program.name = name ? *name : "Default";
}

} // namespace

void Program::setDefaults() {
    const ObxdParams defaults;
    name = defaults.name;
    for (int k = 0; k < kEngineParamCount; ++k) {
        values[static_cast<size_t>(k)] = defaults.values[k];
    }
}

std::vector<uint8_t> writeBankState(const Bank &bank) {
    std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?> <discoDSP";
    appendAttribute(xml, "currentProgram", std::to_string(bank.current));
    xml += "><programs>";
    for (const auto &program : bank.programs) {
        xml += "<program";
        appendAttribute(xml, "programName", program.name);
        appendAttribute(xml, "voiceCount", std::to_string(kStateVoiceCount));
        appendValues(xml, program);
        xml += "/>";
    }
    xml += "</programs></discoDSP>";

    std::vector<uint8_t> out(8 + xml.size() + 1, 0);
    putU32(out, 0, kMagicXml);
    putU32(out, 4, static_cast<uint32_t>(xml.size()));
    std::memcpy(out.data() + 8, xml.data(), xml.size());
    return out;
}

bool readBankState(const uint8_t *bytes, size_t size, Bank &bank) {
    if (!bytes || size < 9 || size > kMaxStateBytes || getU32(bytes) != kMagicXml) {
        return false;
    }
    const size_t length = getU32(bytes + 4);
    if (length == 0 || length > size - 8) {
        return false;
    }
    std::string_view xml(reinterpret_cast<const char *>(bytes + 8), length);
    if (const size_t nul = xml.find('\0'); nul != std::string_view::npos) {
        xml = xml.substr(0, nul);
    }

    std::vector<Element> elements;
    if (!parseStartTags(xml, elements)) {
        return false;
    }

    const Element &root = elements.front();
    const bool hasBank = elements.size() > 1 && elements[1].name == "programs";
    if (!hasBank) {
        // A single program (getCurrentProgramStateInformation): it replaces the
        // current program only.
        bool hasValues = false;
        for (const auto &[name, value] : root.attributes) {
            hasValues |= name.rfind("Val_", 0) == 0;
        }
        if (!hasValues) {
            return false;
        }
        readProgram(root, bank.currentProgram());
        return true;
    }

    Bank loaded;
    size_t slot = 0;
    for (size_t i = 2; i < elements.size() && slot < loaded.programs.size(); ++i) {
        if (elements[i].name == "program") {
            readProgram(elements[i], loaded.programs[slot++]);
        }
    }
    int current = 0;
    if (const std::string *text = attribute(root, "currentProgram")) {
        current = std::atoi(text->c_str());
    }
    loaded.current = current < 0 || current >= kProgramCount ? 0 : current;
    bank = loaded;
    return true;
}

} // namespace obxd
