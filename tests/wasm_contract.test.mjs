// The packaged module is a WCLAP reactor: the WebCLAP export surface, WASI
// preview1 as its only import namespace, and no wasm exception handling, so
// hosts need no exnref support (Node runs this test without flags).
import assert from "node:assert/strict";
import fs from "node:fs";

const modulePath = process.argv[2];
assert(modulePath, "usage: node wasm_contract.test.mjs <module.wasm>");

const bytes = fs.readFileSync(modulePath);
const module = await WebAssembly.compile(bytes);
const exports = WebAssembly.Module.exports(module);
const imports = WebAssembly.Module.imports(module);

assert(exports.some((e) => e.name === "_initialize" && e.kind === "function"));
assert(exports.some((e) => e.name === "clap_entry" && e.kind === "global"));
assert(exports.some((e) => e.name === "malloc" && e.kind === "function"));
assert(exports.some((e) => e.name === "free" && e.kind === "function"));
assert.equal(exports.filter((e) => e.kind === "table").length, 1);
assert.equal(exports.filter((e) => e.kind === "memory").length, 1);
assert(!exports.some((e) => ["_start", "main", "_main"].includes(e.name)));
assert(!exports.some((e) => e.kind === "tag"), "no wasm exception tags");
assert(!imports.some((e) => e.kind === "tag"), "no wasm exception tags");

const stubs = {};
for (const entry of imports) {
  assert.equal(entry.module, "wasi_snapshot_preview1", `unexpected import ${entry.module}.${entry.name}`);
  assert.equal(entry.kind, "function", `unexpected non-function import ${entry.name}`);
  stubs[entry.module] ??= {};
  stubs[entry.module][entry.name] = () => 0;
}

const instance = await WebAssembly.instantiate(module, stubs);
const entry = instance.exports.clap_entry;
assert(entry instanceof WebAssembly.Global);
assert(entry.value > 0, "clap_entry must point at a clap_plugin_entry_t");

const table = instance.exports[exports.find((e) => e.kind === "table").name];
const memory = instance.exports[exports.find((e) => e.kind === "memory").name];
const tableBefore = table.length;
table.grow(1);
assert.equal(table.length, tableBefore + 1, "function table must be growable");
const memoryBefore = memory.buffer.byteLength;
memory.grow(1);
assert(memory.buffer.byteLength > memoryBefore, "memory must be growable");

// clap_plugin_entry_t: clap_version (3 x u32), then init/deinit/get_factory.
const view = new DataView(memory.buffer);
const base = entry.value;
assert.equal(view.getUint32(base, true), 1, "CLAP major version 1");
for (const offset of [12, 16, 20]) {
  const index = view.getUint32(base + offset, true);
  assert(index > 0 && index < table.length && table.get(index), "entry function pointer");
}

console.log(`PASS wasm contract (${imports.length} WASI imports: ${imports.map((i) => i.name).join(", ")})`);
