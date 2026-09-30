/*
 * OB-Xd web editor (clap.webview/3).
 *
 * The plugin and this page exchange one JSON object per message, as UTF-8
 * bytes in an ArrayBuffer:
 *   page -> plugin: {"type":"ready"}
 *                   {"type":"set","id":<clap id>,"value":<plain value>}
 *                   {"type":"gesture","id":<clap id>,"begin":true|false}
 *   plugin -> page: {"type":"init","params":[{id,key,name,module,min,max,default,stepped,value,text}]}
 *                   {"type":"param","id":..,"value":..,"text":".."}
 * Controls are bound by OB-Xd's parameter keys, which the init message maps
 * to CLAP ids. Opened outside a host, the page runs as a static preview.
 */
(function () {
  "use strict";

  // ---- layout (1000 x 423 logical px, following the OB-Xd 2 panel) --------

  var SECTIONS = [
    { title: "Master", x: 34, y: 101, w: 186 },
    { title: "Global", x: 34, y: 206, w: 186 },
    { title: "Oscillators", x: 235, y: 12, w: 186 },
    { title: "Mixer", x: 436, y: 12, w: 70 },
    { title: "Control", x: 235, y: 325, w: 271 },
    { title: "Filter", x: 521, y: 12, w: 186 },
    { title: "Modulation", x: 521, y: 177, w: 186 },
    { title: "Filter Envelope", x: 722, y: 12, w: 245 },
    { title: "Amplifier Envelope", x: 722, y: 121, w: 245 },
    { title: "Voice Variation", x: 722, y: 229, w: 245 },
  ];

  var DIVIDERS = [
    { x: 227, y: 10, h: 404 },
    { x: 428, y: 10, h: 305 },
    { x: 513, y: 10, h: 404 },
    { x: 714, y: 10, h: 404 },
  ];

  var SAW = '<svg viewBox="0 0 13 8"><path d="M1 7 L12 1 L12 7"/></svg>';
  var TRI = '<svg viewBox="0 0 13 8"><path d="M1 7 L6.5 1 L12 7"/></svg>';
  var SQR = '<svg viewBox="0 0 13 8"><path d="M1 7 L1 1 L7 1 L7 7 L12 7"/></svg>';
  var SINE = '<svg viewBox="0 0 13 8"><path d="M1 4 C3 -1 5 -1 6.5 4 S10 9 12 4"/></svg>';
  var SQUARE = '<svg viewBox="0 0 13 8"><path d="M1 7 L1 1 L6.5 1 L6.5 7 L12 7 L12 1"/></svg>';
  var SH = '<svg viewBox="0 0 13 8"><path d="M1 5 L3 5 L3 2 L6 2 L6 6 L9 6 L9 3 L12 3"/></svg>';

  // kind: knob | button | display. x/y are control centres (display: box).
  var CONTROLS = [
    // Master
    { key: "Volume", kind: "knob", x: 68, y: 149, label: "Volume" },
    { key: "Tune", kind: "knob", x: 126, y: 149, label: "Fine" },
    { key: "Octave", kind: "knob", x: 184, y: 149, label: "Coarse" },
    // Global
    { key: "VoiceDetune", kind: "knob", x: 68, y: 255, label: "Spread" },
    { key: "Unison", kind: "button", x: 126, y: 252, label: "Unison", labelDy: 25 },
    { key: "Portamento", kind: "knob", x: 184, y: 255, label: "Glide" },
    { key: "LegatoMode", kind: "display", x: 47, y: 306, w: 105, label: "Legato Mode", labelX: 99, format: "legato" },
    { key: "VoiceCount", kind: "display", x: 165, y: 306, w: 41, label: "Voices", labelX: 186, format: "number" },
    { key: null, kind: "menu", x: 63, y: 384, label: "Menu" },
    { key: "AsPlayedAllocation", kind: "button", x: 105, y: 384, label: "VAM", title: "Voice allocation: as played" },
    { key: null, kind: "inert", x: 148, y: 384, label: "Learn", title: "MIDI learn is not available in the WebCLAP build" },
    { key: null, kind: "inert", x: 190, y: 384, label: "Clear", title: "MIDI learn is not available in the WebCLAP build" },
    // Oscillators
    { key: "Osc1Pitch", kind: "knob", x: 269, y: 60, label: "Osc1" },
    { key: "PulseWidth", kind: "knob", x: 327, y: 60, label: "PW" },
    { key: "Osc2Pitch", kind: "knob", x: 386, y: 60, label: "Osc2" },
    { key: "Osc1Saw", kind: "button", x: 250, y: 125, label: SAW, html: true, labelX: 250 },
    { key: "Osc1Pulse", kind: "button", x: 288, y: 125, label: SQR, html: true, labelX: 288 },
    { key: null, kind: "text", x: 269, y: 146, label: TRI, html: true },
    { key: "Oscillator2detune", kind: "knob", x: 327, y: 122, label: "Detune", labelDy: 25 },
    { key: "Osc2Saw", kind: "button", x: 367, y: 125, label: SAW, html: true },
    { key: "Osc2Pulse", kind: "button", x: 405, y: 125, label: SQR, html: true },
    { key: null, kind: "text", x: 386, y: 146, label: TRI, html: true },
    { key: "Osc2HardSync", kind: "button", x: 269, y: 190, label: "Sync" },
    { key: "Xmod", kind: "knob", x: 327, y: 189, label: "Xmod", labelDy: 25 },
    { key: "PitchQuant", kind: "button", x: 386, y: 190, label: "Step" },
    { key: "Brightness", kind: "knob", x: 269, y: 254, label: "Bright<br>Amt", html: true },
    { key: "EnvelopeToPitch", kind: "knob", x: 386, y: 254, label: "Pitch<br>Env Amt", html: true },
    // Mixer
    { key: "Osc1Mix", kind: "knob", x: 470, y: 60, label: "Osc1" },
    { key: "Osc2Mix", kind: "knob", x: 470, y: 125, label: "Osc2" },
    { key: "NoiseMix", kind: "knob", x: 470, y: 190, label: "Noise" },
    // Control
    { key: "BendRange", kind: "button", x: 269, y: 380, label: "Bend<br>Octave", html: true },
    { key: "BendOsc2Only", kind: "button", x: 315, y: 380, label: "Bend<br>Osc2", html: true },
    { key: "VibratoRate", kind: "knob", x: 368, y: 372, label: "Vibrato<br>Rate", html: true, labelDy: 26 },
    { key: "VFltFactor", kind: "knob", x: 420, y: 372, label: "Flt Env<br>Velocity", html: true, labelDy: 26 },
    { key: "VAmpFactor", kind: "knob", x: 470, y: 372, label: "Amp Env<br>Velocity", html: true, labelDy: 26 },
    // Filter
    { key: "Cutoff", kind: "knob", x: 555, y: 60, label: "Cutoff" },
    { key: "Resonance", kind: "knob", x: 613, y: 60, label: "Resonance" },
    { key: "FilterEnvAmount", kind: "knob", x: 672, y: 60, label: "Env Amt" },
    { key: "FilterKeyFollow", kind: "button", x: 536, y: 125, label: "Key" },
    { key: "Filter_Warm", kind: "button", x: 574, y: 125, label: "HQ" },
    { key: "Multimode", kind: "knob", x: 613, y: 122, label: "Multi", labelDy: 25 },
    { key: "BandpassBlend", kind: "button", x: 653, y: 125, label: "BP" },
    { key: "FourPole", kind: "button", x: 691, y: 125, label: "24dB" },
    // Modulation
    { key: "LfoFrequency", kind: "knob", x: 555, y: 225, label: "LFO Rate" },
    { key: "LfoAmount1", kind: "knob", x: 613, y: 225, label: "Freq Amt" },
    { key: "LfoAmount2", kind: "knob", x: 672, y: 225, label: "PW Amt" },
    { key: "LfoSineWave", kind: "button", x: 555, y: 277, label: SINE, html: true },
    { key: "LfoSquareWave", kind: "button", x: 555, y: 330, label: SQUARE, html: true },
    { key: "LfoSampleHoldWave", kind: "button", x: 555, y: 382, label: SH, html: true },
    { key: "LfoOsc1", kind: "button", x: 613, y: 277, label: "Osc1" },
    { key: "LfoOsc2", kind: "button", x: 613, y: 330, label: "Osc2" },
    { key: "LfoFilter", kind: "button", x: 613, y: 382, label: "Filter" },
    { key: "LfoPw1", kind: "button", x: 672, y: 277, label: "Osc1" },
    { key: "LfoPw2", kind: "button", x: 672, y: 330, label: "Osc2" },
    // Filter envelope
    { key: "FilterAttack", kind: "knob", x: 756, y: 60, label: "Attack" },
    { key: "FilterDecay", kind: "knob", x: 815, y: 60, label: "Decay" },
    { key: "FilterSustain", kind: "knob", x: 873, y: 60, label: "Sustain" },
    { key: "FilterRelease", kind: "knob", x: 931, y: 60, label: "Release" },
    // Amplifier envelope
    { key: "Attack", kind: "knob", x: 756, y: 169, label: "Attack" },
    { key: "Decay", kind: "knob", x: 815, y: 169, label: "Decay" },
    { key: "Sustain", kind: "knob", x: 873, y: 169, label: "Sustain" },
    { key: "Release", kind: "knob", x: 931, y: 169, label: "Release" },
    // Voice variation
    { key: "FilterDetune", kind: "knob", x: 756, y: 276, label: "Flt Slop" },
    { key: "PortamentoDetune", kind: "knob", x: 815, y: 276, label: "Gld Slop" },
    { key: "EnvelopeDetune", kind: "knob", x: 873, y: 276, label: "Env Slop" },
    { key: "LevelDif", kind: "knob", x: 931, y: 276, label: "Lvl Slop" },
    { key: "Pan1", kind: "knob", x: 756, y: 330, label: "V1 Pan" },
    { key: "Pan2", kind: "knob", x: 815, y: 330, label: "V2 Pan" },
    { key: "Pan3", kind: "knob", x: 873, y: 330, label: "V3 Pan" },
    { key: "Pan4", kind: "knob", x: 931, y: 330, label: "V4 Pan" },
    { key: "Pan5", kind: "knob", x: 756, y: 382, label: "V5 Pan" },
    { key: "Pan6", kind: "knob", x: 815, y: 382, label: "V6 Pan" },
    { key: "Pan7", kind: "knob", x: 873, y: 382, label: "V7 Pan" },
    { key: "Pan8", kind: "knob", x: 931, y: 382, label: "V8 Pan" },
  ];

  // OB-Xd 2.x engine controls that the classic panel has no room for.
  var EXTENDED = [
    { key: "PwEnv", kind: "knob", x: 60, y: 60, label: "PW Env<br>Amt", html: true },
    { key: "PwOfs", kind: "knob", x: 130, y: 60, label: "PW Osc2<br>Offset", html: true },
    { key: "PwEnvBoth", kind: "button", x: 200, y: 62, label: "PW Env<br>Both", html: true },
    { key: "EnvPitchBoth", kind: "button", x: 262, y: 62, label: "Pitch Env<br>Both", html: true },
    { key: "FenvInvert", kind: "button", x: 324, y: 62, label: "Filter Env<br>Invert", html: true },
    { key: "SelfOscPush", kind: "button", x: 386, y: 62, label: "Self-Osc<br>Push", html: true },
    { key: "LfoSync", kind: "button", x: 448, y: 62, label: "LFO<br>Sync", html: true },
    { key: "EconomyMode", kind: "button", x: 60, y: 150, label: "Economy<br>Mode", html: true },
  ];

  var LEGATO_SHORT = ["Keep All", "Keep Flt", "Keep Amp", "Retrig"];
  var KNOB_TRAVEL = 270; // degrees, -135 .. +135
  var DRAG_PIXELS = 180; // vertical pixels for a full turn
  var PREVIEW_DELAY_MS = 700;

  // ---- state ------------------------------------------------------------

  var byKey = Object.create(null); // key -> param
  var byId = Object.create(null); // id -> param
  var widgets = Object.create(null); // key -> [{update(param)}]
  var active = null; // key being dragged
  var standalone = window.parent === window;
  var connected = false;

  // ---- transport --------------------------------------------------------

  var encoder = new TextEncoder();
  var decoder = new TextDecoder();

  function post(message) {
    if (standalone) return;
    var bytes = encoder.encode(JSON.stringify(message));
    window.parent.postMessage(bytes.buffer, "*", [bytes.buffer]);
  }

  window.addEventListener("message", function (event) {
    var data = event.data;
    if (data instanceof ArrayBuffer) {
      data = new Uint8Array(data);
    } else if (!ArrayBuffer.isView(data)) {
      return; // host loader traffic, not ours
    }
    var message;
    try {
      message = JSON.parse(decoder.decode(data));
    } catch (error) {
      return;
    }
    if (message.type === "init") onInit(message);
    else if (message.type === "param") onParam(message);
  });

  function onInit(message) {
    connected = true;
    byKey = Object.create(null);
    byId = Object.create(null);
    message.params.forEach(function (param) {
      byKey[param.key] = param;
      byId[param.id] = param;
    });
    Object.keys(widgets).forEach(refresh);
  }

  function onParam(message) {
    var param = byId[message.id];
    if (!param) return;
    if (active !== param.key) param.value = message.value;
    param.text = message.text;
    refresh(param.key);
    if (active === param.key) showReadout(param.key);
  }

  // ---- values -----------------------------------------------------------

  function norm(param) {
    return param.max > param.min ? (param.value - param.min) / (param.max - param.min) : 0;
  }

  function setValue(key, value, fromUser) {
    var param = byKey[key];
    if (!param) return;
    value = Math.min(param.max, Math.max(param.min, value));
    if (param.stepped) value = Math.round(value);
    if (value === param.value) return;
    param.value = value;
    if (!connected) param.text = previewText(param);
    refresh(key);
    if (fromUser) post({ type: "set", id: param.id, value: value });
  }

  function gesture(key, begin) {
    var param = byKey[key];
    if (param) post({ type: "gesture", id: param.id, begin: begin });
  }

  function previewText(param) {
    if (param.stepped) return String(param.value);
    return String(Math.floor(param.value * 127));
  }

  function refresh(key) {
    var list = widgets[key];
    var param = byKey[key];
    if (!list || !param) return;
    list.forEach(function (widget) {
      widget.update(param);
    });
  }

  // ---- widgets ----------------------------------------------------------

  function el(tag, className, parent) {
    var node = document.createElement(tag);
    if (className) node.className = className;
    if (parent) parent.appendChild(node);
    return node;
  }

  function place(node, x, y) {
    node.style.left = x + "px";
    node.style.top = y + "px";
  }

  function addLabel(def, parent, y) {
    var label = el("div", "label", parent);
    if (def.html) label.innerHTML = def.label;
    else label.textContent = def.label;
    place(label, def.labelX != null ? def.labelX : def.x, y);
    return label;
  }

  function register(key, widget) {
    (widgets[key] = widgets[key] || []).push(widget);
  }

  function title(def) {
    var param = byKey[def.key];
    return param ? param.name + ": " + param.text : def.label.replace(/<[^>]+>/g, " ");
  }

  function makeKnob(def, parent) {
    var knob = el("div", "knob", parent);
    knob.tabIndex = 0;
    knob.setAttribute("role", "slider");
    place(knob, def.x, def.y);
    el("div", "cap", knob);
    var pointer = el("div", "pointer", knob);
    addLabel(def, parent, def.y + (def.labelDy || 22));

    register(def.key, {
      update: function (param) {
        var angle = -KNOB_TRAVEL / 2 + norm(param) * KNOB_TRAVEL;
        pointer.style.transform = "rotate(" + angle + "deg)";
        knob.setAttribute("aria-valuetext", param.text);
        knob.setAttribute("aria-label", param.name);
        knob.title = param.name + ": " + param.text;
      },
    });

    var startY = 0;
    var startValue = 0;
    knob.addEventListener("pointerdown", function (event) {
      var param = byKey[def.key];
      if (!param || event.button !== 0) return;
      knob.setPointerCapture(event.pointerId);
      knob.focus();
      startY = event.clientY;
      startValue = param.value;
      active = def.key;
      gesture(def.key, true);
      showReadout(def.key);
      event.preventDefault();
    });
    knob.addEventListener("pointermove", function (event) {
      if (active !== def.key) return;
      var param = byKey[def.key];
      var scale = (param.max - param.min) / (event.shiftKey ? DRAG_PIXELS * 5 : DRAG_PIXELS);
      setValue(def.key, startValue + (startY - event.clientY) * scale, true);
      showReadout(def.key);
    });
    function release() {
      if (active !== def.key) return;
      active = null;
      gesture(def.key, false);
      hideReadout();
      var param = byKey[def.key];
      if (param) refresh(def.key);
    }
    knob.addEventListener("pointerup", release);
    knob.addEventListener("pointercancel", release);
    knob.addEventListener("lostpointercapture", release);
    knob.addEventListener("dblclick", function () {
      var param = byKey[def.key];
      if (!param) return;
      gesture(def.key, true);
      setValue(def.key, param["default"], true);
      gesture(def.key, false);
    });
    knob.addEventListener("wheel", function (event) {
      var param = byKey[def.key];
      if (!param) return;
      event.preventDefault();
      var step = param.stepped ? 1 : (param.max - param.min) / (event.shiftKey ? 500 : 100);
      gesture(def.key, true);
      setValue(def.key, param.value + (event.deltaY < 0 ? step : -step), true);
      gesture(def.key, false);
      showReadout(def.key);
      scheduleHide();
    }, { passive: false });
    knob.addEventListener("keydown", function (event) {
      var param = byKey[def.key];
      if (!param) return;
      var step = param.stepped ? 1 : (param.max - param.min) / (event.shiftKey ? 500 : 50);
      var delta = { ArrowUp: step, ArrowRight: step, ArrowDown: -step, ArrowLeft: -step }[event.key];
      if (delta === undefined) return;
      event.preventDefault();
      setValue(def.key, param.value + delta, true);
    });
    knob.addEventListener("pointerenter", function () {
      if (!active) showReadout(def.key);
    });
    knob.addEventListener("pointerleave", function () {
      if (!active) hideReadout();
    });
  }

  function makeButton(def, parent) {
    var button = el("div", "button", parent);
    button.tabIndex = 0;
    place(button, def.x, def.y);
    el("div", "led", button);
    addLabel(def, parent, def.y + (def.labelDy || 20));

    if (def.kind === "menu") {
      button.classList.add("menu-button");
      button.setAttribute("role", "button");
      button.setAttribute("aria-label", "Menu");
      button.title = "Menu";
      button.addEventListener("click", function (event) {
        event.stopPropagation();
        toggleMenu(def);
      });
      return;
    }
    if (def.kind === "inert") {
      button.classList.add("disabled");
      button.title = def.title;
      button.setAttribute("aria-disabled", "true");
      return;
    }

    button.setAttribute("role", "switch");
    register(def.key, {
      update: function (param) {
        var on = param.value > (param.min + param.max) / 2;
        button.classList.toggle("on", on);
        button.setAttribute("aria-checked", on ? "true" : "false");
        button.setAttribute("aria-label", param.name);
        button.title = param.name + ": " + param.text;
      },
    });
    function toggle() {
      var param = byKey[def.key];
      if (!param) return;
      gesture(def.key, true);
      setValue(def.key, param.value > (param.min + param.max) / 2 ? param.min : param.max, true);
      gesture(def.key, false);
    }
    button.addEventListener("click", toggle);
    button.addEventListener("keydown", function (event) {
      if (event.key === " " || event.key === "Enter") {
        event.preventDefault();
        toggle();
      }
    });
  }

  function makeDisplay(def, parent) {
    var box = el("div", "display", parent);
    box.tabIndex = 0;
    box.setAttribute("role", "spinbutton");
    place(box, def.x, def.y);
    box.style.width = def.w + "px";
    addLabel(def, parent, def.y + 35);
    register(def.key, {
      update: function (param) {
        var v = Math.round(param.value);
        box.textContent = def.format === "legato" ? LEGATO_SHORT[v] || String(v) : String(v);
        box.setAttribute("aria-valuenow", String(v));
        box.setAttribute("aria-valuetext", param.text);
        box.setAttribute("aria-label", param.name);
        box.title = param.name + ": " + param.text + " (click / wheel to change)";
      },
    });
    function step(delta) {
      var param = byKey[def.key];
      if (!param) return;
      var next = Math.round(param.value) + delta;
      if (next > param.max) next = param.min;
      if (next < param.min) next = param.max;
      gesture(def.key, true);
      setValue(def.key, next, true);
      gesture(def.key, false);
    }
    box.addEventListener("click", function (event) {
      step(event.shiftKey ? -1 : 1);
    });
    box.addEventListener("contextmenu", function (event) {
      event.preventDefault();
      step(-1);
    });
    box.addEventListener("wheel", function (event) {
      event.preventDefault();
      step(event.deltaY < 0 ? 1 : -1);
    }, { passive: false });
    box.addEventListener("keydown", function (event) {
      var delta = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1 }[event.key];
      if (delta === undefined) return;
      event.preventDefault();
      step(delta);
    });
  }

  function build(defs, parent) {
    defs.forEach(function (def) {
      if (def.kind === "knob") makeKnob(def, parent);
      else if (def.kind === "display") makeDisplay(def, parent);
      else if (def.kind === "text") addLabel(def, parent, def.y);
      else makeButton(def, parent);
    });
  }

  // ---- readout ----------------------------------------------------------

  var readout = document.getElementById("readout");
  var hideTimer = 0;

  function showReadout(key) {
    var param = byKey[key];
    var def = findDef(key);
    if (!param || !def) return;
    clearTimeout(hideTimer);
    readout.innerHTML = "";
    var name = el("b", "", readout);
    name.textContent = param.name;
    readout.appendChild(document.createTextNode(param.text));
    var origin = def.origin || { x: 0, y: 0 };
    var x = Math.min(930, Math.max(70, origin.x + def.x));
    var y = origin.y + def.y - 38;
    readout.style.left = x + "px";
    readout.style.top = (y < 2 ? origin.y + def.y + 20 : y) + "px";
    readout.hidden = false;
  }

  function hideReadout() {
    readout.hidden = true;
  }

  function scheduleHide() {
    clearTimeout(hideTimer);
    hideTimer = setTimeout(hideReadout, 900);
  }

  function findDef(key) {
    for (var i = 0; i < CONTROLS.length; i++) if (CONTROLS[i].key === key) return CONTROLS[i];
    for (var j = 0; j < EXTENDED.length; j++) if (EXTENDED[j].key === key) return EXTENDED[j];
    return null;
  }

  // ---- menu -------------------------------------------------------------

  var menu = document.getElementById("menu");
  var extended = document.getElementById("extended");

  function toggleMenu(def) {
    if (!menu.hidden) {
      menu.hidden = true;
      return;
    }
    menu.innerHTML = "";
    addItem("Extended controls…", function () {
      extended.hidden = false;
    });
    addItem("Initialize patch", initPatch);
    el("hr", "", menu);
    var info = el("div", "info", menu);
    info.textContent = connected ? "OB-Xd 2.10 · WebCLAP" : "OB-Xd 2.10 · preview (no host)";
    place(menu, def.x - 15, def.y - 116);
    menu.hidden = false;
    var first = menu.querySelector("button");
    if (first) first.focus();
  }

  function addItem(text, action) {
    var item = el("button", "", menu);
    item.type = "button";
    item.textContent = text;
    item.addEventListener("click", function () {
      menu.hidden = true;
      action();
    });
  }

  function initPatch() {
    Object.keys(byKey).forEach(function (key) {
      var param = byKey[key];
      if (param.value === param["default"]) return;
      gesture(key, true);
      setValue(key, param["default"], true);
      gesture(key, false);
    });
  }

  document.addEventListener("click", function () {
    menu.hidden = true;
  });
  document.addEventListener("keydown", function (event) {
    if (event.key === "Escape") {
      menu.hidden = true;
      extended.hidden = true;
    }
  });
  document.getElementById("extended-close").addEventListener("click", function () {
    extended.hidden = true;
  });

  // ---- startup ----------------------------------------------------------

  var controls = document.getElementById("controls");
  SECTIONS.forEach(function (section) {
    var node = el("div", "section", controls);
    node.textContent = section.title;
    place(node, section.x, section.y);
    node.style.width = section.w + "px";
  });
  DIVIDERS.forEach(function (divider) {
    var node = el("div", "divider", controls);
    place(node, divider.x, divider.y);
    node.style.height = divider.h + "px";
  });
  build(CONTROLS, controls);
  var extendedOrigin = { x: 232, y: 64 };
  EXTENDED.forEach(function (def) {
    def.origin = extendedOrigin;
  });
  build(EXTENDED, document.getElementById("extended-controls"));

  // Without a host (or before it answers), show the panel at rest.
  function preview() {
    if (connected) return;
    CONTROLS.concat(EXTENDED).forEach(function (def, index) {
      if (!def.key || byKey[def.key]) return;
      var param = {
        id: 1000 + index,
        key: def.key,
        name: def.label.replace(/<[^>]+>/g, " "),
        min: def.kind === "display" ? (def.format === "legato" ? 0 : 1) : 0,
        max: def.kind === "display" ? (def.format === "legato" ? 3 : 32) : 1,
        stepped: def.kind !== "knob",
        value: 0,
      };
      param["default"] = param.min;
      param.value = def.key === "VoiceCount" ? 8 : param.min;
      param.text = previewText(param);
      byKey[def.key] = param;
      byId[param.id] = param;
    });
    Object.keys(widgets).forEach(refresh);
  }

  post({ type: "ready" });
  setTimeout(preview, standalone ? 0 : PREVIEW_DELAY_MS);
})();
