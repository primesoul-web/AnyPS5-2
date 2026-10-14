import { Archive, MEMORY_LIMIT, memoryEstimate, packageArchive } from "./archive.mjs";

let archive;
let busy = false;
let pendingWrite;
let lastReport = 0;
let lastPhase = "";

function report(event) {
  const phase = event.phase ?? String(event.stage);
  const now = performance.now();
  if (!event.message && phase === lastPhase && event.completed !== event.total && now - lastReport < 80) return;
  lastReport = now;
  lastPhase = phase;
  self.postMessage({ type: "progress", ...event });
}

function selectedRelease(release, target) {
  const commit = release?.commit;
  if (!new URL(import.meta.url).pathname.endsWith(`/native/${commit}/worker.mjs`)) throw new Error("The site has been updated; reload the page.");
  const asset = release?.platforms?.[target];
  if (typeof release?.tag !== "string" || typeof commit !== "string" || !/^[a-f0-9]{40}$/u.test(commit) || (release.tag.startsWith("vdev-") && release.tag !== `vdev-${commit}`)) throw new Error("Invalid release manifest");
  if (release.converter?.path !== `native/${commit}/relinker_wasm.mjs`) throw new Error("Invalid release manifest");
  if (!asset || !/^[a-f0-9]{64}$/u.test(asset.sha256) || asset.path !== `libraries/${target}-${asset.sha256}.zip` || !Number.isSafeInteger(asset.size) || !Number.isSafeInteger(asset.expandedBytes)) throw new Error("Invalid release manifest");
  return asset;
}

async function downloadLibraries(release, asset) {
  report({ phase: "Downloading system libraries" });
  const download = await fetch(new URL(`../../${asset.path}`, import.meta.url));
  if (!download.ok) throw new Error(`Cannot download system libraries for ${release.tag} (${download.status}). The site may have been updated; reload the page.`);
  const bytes = await download.arrayBuffer();
  if (bytes.byteLength !== asset.size) throw new Error("System library download is incomplete");
  const digest = [...new Uint8Array(await crypto.subtle.digest("SHA-256", bytes))].map(byte => byte.toString(16).padStart(2, "0")).join("");
  if (digest !== asset.sha256) throw new Error("System library checksum mismatch");
  return new Blob([bytes]);
}

async function loadConverter(release) {
  try {
    return (await import(new URL("./relinker_wasm.mjs", import.meta.url).href)).default;
  } catch {
    throw new Error(`Cannot load the converter for ${release.tag}. The site may have been updated; reload the page.`);
  }
}

self.onmessage = async ({ data }) => {
  if (data.type === "written") {
    if (!pendingWrite) return;
    const resolve = pendingWrite;
    pendingWrite = null;
    resolve();
    return;
  }
  if (busy) return;
  busy = true;
  try {
    if (data.type === "inspect") {
      if (archive) await archive.close();
      archive = new Archive(data.files ?? data.file);
      const summary = await archive.inspect(report);
      self.postMessage({ type: "inspected", summary });
    } else if (data.type === "convert") {
      if (data.target === "windows" && data.mode === "disk") throw new Error("Windows folder output is unavailable because browsers block DLL creation. Choose a ZIP save mode.");
      if (!archive) throw new Error("Select a ZIP or folder first");
      const release = data.release;
      const asset = selectedRelease(release, data.target);
      const libraries = await downloadLibraries(release, asset);
      const estimate = memoryEstimate(archive.totalBytes, archive.selectedBytes, asset.expandedBytes, archive.entries.length);
      if (data.mode === "memory" && estimate > MEMORY_LIMIT) throw new Error("This result exceeds the 256 MiB memory-download budget. Use Save ZIP to disk in desktop Chrome or Edge.");
      report({ phase: "Loading converter" });
      const createRelinker = await loadConverter(release);
      const native = await createRelinker();
      let input = await archive.conversionInput(data.target, data.toIntel, report);
      const outputName = input.outputName;
      const converted = native.Convert(input, report);
      input = null;
      if (!converted.ok) throw new Error(converted.error);
      const chunks = [];
      let size = 0;
      const send = (event, transfer = []) => new Promise(resolve => {
        pendingWrite = resolve;
        self.postMessage(event, transfer);
      });
      if (data.mode === "disk") {
        const destination = {
          directory: name => send({ type: "directory", name }),
          async file(name) {
            await send({ type: "open-file", name });
            return new WritableStream({
              async write(chunk) {
                size += chunk.byteLength;
                const bytes = chunk.slice();
                await send({ type: "write", bytes }, [bytes.buffer]);
              },
              close: () => send({ type: "close-file" })
            });
          }
        };
        await packageArchive(archive, converted, outputName, libraries, destination, report, true);
      } else if (data.mode === "memory" || data.mode === "zip") {
        const destination = new WritableStream({
          async write(chunk) {
            size += chunk.byteLength;
            if (data.mode === "zip") {
              const bytes = chunk.slice();
              await send({ type: "write", bytes }, [bytes.buffer]);
              return;
            }
            if (size > MEMORY_LIMIT) throw new Error("The compressed ZIP exceeded 256 MiB. Use Save ZIP to disk.");
            chunks.push(chunk.slice());
          }
        });
        await packageArchive(archive, converted, outputName, libraries, destination, report);
        await destination.close();
      } else throw new Error("Unknown save mode");
      self.postMessage({ type: "complete", mode: data.mode, blob: data.mode === "memory" ? new Blob(chunks, { type: "application/zip" }) : null, outputName, size, release: release.tag });
      await archive.close();
      archive = null;
    } else throw new Error("Unknown worker request");
  } catch (error) {
    self.postMessage({ type: "error", message: error instanceof Error ? error.message : String(error) });
  } finally {
    busy = false;
  }
};
