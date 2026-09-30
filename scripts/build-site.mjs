#!/usr/bin/env node
// Assembles the GitHub Pages download site from a packaged build.
//
//   node scripts/build-site.mjs <dist dir> <output dir>
//
// Output:
//   index.html                download page
//   OB-Xd.wclap.tar.gz        the package (stable URL, installable by URL)
//   OB-Xd.wclap.sha256        per-file checksums of the bundle
//   OB-Xd.wclap.tar.gz.sha256 archive checksum
//   build.json                machine-readable build facts
//   editor/                   the web editor, runnable as a static preview
//   img/                      screenshots
import { createHash } from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const [distArg, outArg] = process.argv.slice(2);
if (!distArg || !outArg) {
  console.error("usage: node scripts/build-site.mjs <dist dir> <output dir>");
  process.exit(2);
}
const dist = path.resolve(distArg);
const out = path.resolve(outArg);

const archiveName = "OB-Xd.wclap.tar.gz";
const archive = fs.readFileSync(path.join(dist, archiveName));
const moduleBytes = fs.readFileSync(path.join(dist, "OB-Xd.wclap", "module.wasm"));
const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");

const facts = {
  package: archiveName,
  archiveSha256: sha256(archive),
  archiveBytes: archive.length,
  moduleSha256: sha256(moduleBytes),
  moduleBytes: moduleBytes.length,
  pluginId: "com.discodsp.ob-xd",
  version: "2.10.0-wclap",
  commit: process.env.GITHUB_SHA ?? "local",
  repository: process.env.GITHUB_REPOSITORY ?? "dahlgrenmartin/prometheos-webclap-obxd",
  builtAt: new Date().toISOString(),
};

fs.rmSync(out, { recursive: true, force: true });
fs.mkdirSync(path.join(out, "img"), { recursive: true });
fs.copyFileSync(path.join(dist, archiveName), path.join(out, archiveName));
fs.copyFileSync(path.join(dist, "OB-Xd.wclap.sha256"), path.join(out, "OB-Xd.wclap.sha256"));
fs.writeFileSync(path.join(out, `${archiveName}.sha256`), `${facts.archiveSha256}  ${archiveName}\n`);
fs.writeFileSync(path.join(out, "build.json"), `${JSON.stringify(facts, null, 2)}\n`);
fs.cpSync(path.join(root, "webui"), path.join(out, "editor"), { recursive: true });
for (const image of ["obxd-webui.png", "obxd-in-buzz-remote.png"]) {
  fs.copyFileSync(path.join(root, "docs", image), path.join(out, "img", image));
}
fs.writeFileSync(path.join(out, ".nojekyll"), "");

const escape = (text) =>
  String(text).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
const kib = (bytes) => `${(bytes / 1024).toFixed(0)} KB`;
const repoUrl = `https://github.com/${facts.repository}`;
const commitLink =
  facts.commit === "local"
    ? "local build"
    : `<a href="${repoUrl}/commit/${escape(facts.commit)}"><code>${escape(facts.commit.slice(0, 12))}</code></a>`;

const html = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>OB-Xd WebCLAP</title>
<meta name="description" content="discoDSP OB-Xd 2.10 as a WebCLAP plugin with a web editor.">
<style>
  :root {
    --bg: #f4f4f1; --fg: #1d2025; --muted: #5d636b; --card: #ffffff; --line: #d9dad6;
    --accent: #2f7fe0; --accent-ink: #ffffff; --code: #eef0f2;
  }
  @media (prefers-color-scheme: dark) {
    :root { --bg: #15171a; --fg: #e9eaec; --muted: #9aa0a8; --card: #1d2025; --line: #2c3037;
      --accent: #4c95f0; --accent-ink: #0b0d10; --code: #262a31; }
  }
  * { box-sizing: border-box; }
  body { margin: 0; background: var(--bg); color: var(--fg);
    font: 16px/1.55 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif; }
  main { max-width: 980px; margin: 0 auto; padding: 32px 16px 56px; }
  h1 { font-size: 2rem; margin: 0 0 4px; letter-spacing: -0.01em; }
  h2 { font-size: 1.15rem; margin: 36px 0 10px; }
  p { margin: 0 0 12px; }
  .lede { color: var(--muted); margin-bottom: 24px; }
  img { display: block; max-width: 100%; height: auto; border-radius: 6px; border: 1px solid var(--line); }
  .download { display: flex; flex-wrap: wrap; gap: 16px; align-items: center; margin: 24px 0 8px;
    padding: 18px; background: var(--card); border: 1px solid var(--line); border-radius: 8px; }
  .button { display: inline-block; padding: 10px 18px; border-radius: 6px; background: var(--accent);
    color: var(--accent-ink); font-weight: 600; text-decoration: none; }
  .button:focus-visible, a:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
  .meta { color: var(--muted); font-size: 0.9rem; }
  code { background: var(--code); padding: 1px 5px; border-radius: 4px; font-size: 0.88em; word-break: break-all; }
  pre { background: var(--code); padding: 12px; border-radius: 6px; overflow-x: auto; font-size: 0.88em; }
  pre code { background: none; padding: 0; }
  ol, ul { padding-left: 22px; margin: 0 0 12px; }
  a { color: var(--accent); }
  table { border-collapse: collapse; width: 100%; font-size: 0.9rem; }
  td { border-top: 1px solid var(--line); padding: 6px 8px 6px 0; vertical-align: top; }
  td:first-child { color: var(--muted); white-space: nowrap; width: 1%; }
  footer { margin-top: 40px; color: var(--muted); font-size: 0.85rem; }
</style>
</head>
<body>
<main>
  <h1>OB-Xd WebCLAP</h1>
  <p class="lede">discoDSP OB-Xd 2.10, the Oberheim OB-X inspired virtual analog synthesizer, compiled as a
  direct WebCLAP (WCLAP) plugin with its own web editor.</p>

  <img src="img/obxd-webui.png" width="1500" height="635" alt="The OB-Xd web editor panel">

  <div class="download">
    <a class="button" href="${archiveName}" download>Download ${archiveName}</a>
    <span class="meta">${kib(facts.archiveBytes)} · version ${escape(facts.version)} · ${commitLink}</span>
  </div>
  <p class="meta">SHA-256 <code>${facts.archiveSha256}</code>
  · <a href="${archiveName}.sha256">checksum file</a> · <a href="OB-Xd.wclap.sha256">bundle file checksums</a>
  · <a href="build.json">build.json</a></p>

  <h2>Install in buzz-remote</h2>
  <ol>
    <li>Open <strong>Machines → Plugins…</strong></li>
    <li>Paste this URL into <strong>Package URL</strong> and choose <strong>Install URL</strong>
      (or download the archive and use <strong>Install File…</strong>):
      <pre><code id="package-url">${archiveName}</code></pre></li>
    <li>Choose <strong>Restart Audio Engine</strong>, then add it from
      <strong>Machines → New Machine → Generators → discoDSP → OB-Xd</strong>.</li>
    <li>Double-click the machine to open its editor.</li>
  </ol>
  <img src="img/obxd-in-buzz-remote.png" width="1070" height="492" alt="The OB-Xd editor open inside buzz-remote" loading="lazy">

  <h2>Editor preview</h2>
  <p>The <a href="editor/">web editor</a> also opens on its own as a static preview, without a host or sound.</p>

  <h2>What's in the package</h2>
  <table>
    <tr><td>Plugin</td><td><code>${escape(facts.pluginId)}</code>, stereo instrument, CLAP and MIDI note input</td></tr>
    <tr><td>Parameters</td><td>all 77 OB-Xd engine parameters, stable ids, OB-Xd's display text</td></tr>
    <tr><td>State</td><td>OB-Xd's 128-program bank in desktop OB-Xd's own format</td></tr>
    <tr><td>Editor</td><td><code>clap.gui</code> (webview API) and <code>clap.webview/3</code>, embedded in the module</td></tr>
    <tr><td>Module</td><td><code>module.wasm</code>, ${kib(facts.moduleBytes)}, wasi-sdk 34, no wasm exceptions, WASI preview1 only
      <br>SHA-256 <code>${facts.moduleSha256}</code></td></tr>
    <tr><td>License</td><td>GPL-3.0 (OB-Xd); the license text ships in the bundle</td></tr>
  </table>

  <footer>
    Built ${escape(facts.builtAt.slice(0, 10))} from <a href="${repoUrl}">${escape(facts.repository)}</a>.
    OB-Xd is developed by discoDSP and 2Dat; this is an unofficial WebCLAP build of its GPL source.
  </footer>
</main>
<script>
  // Show the absolute package URL for pasting into a host.
  var url = new URL(${JSON.stringify(archiveName)}, location.href).href;
  document.getElementById("package-url").textContent = url;
</script>
</body>
</html>
`;
fs.writeFileSync(path.join(out, "index.html"), html);
console.log(`site written to ${out} (${archiveName} ${facts.archiveSha256})`);
