/**
 * libembdex.js - WebAssembly JavaScript projection.
 * Summary: Build and search portable association maps from JavaScript.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import createModule from "./libembdex.mjs";

const wasm = await createModule();
const filesystem = wasm.FS;
let nextPath = 0;

function allocate(size) {
    const ptr = wasm._malloc(Math.max(1, size));
    if (!ptr) throw new Error("Out of WebAssembly memory");
    return ptr;
}
function cstring(text) {
    if (typeof text !== "string") throw new TypeError("Expected a string");
    const size = wasm.lengthBytesUTF8(text) + 1;
    const ptr = allocate(size);
    wasm.stringToUTF8(text, ptr, size);
    return ptr;
}
function string(ptr) { return ptr ? wasm.UTF8ToString(ptr) : null; }
function uint(ptr) { return wasm.getValue(ptr, "i32") >>> 0; }
function check(status) {
    if (status !== 0) throw new Error(string(wasm._kc_embdex_strerror(status)) || "Native error " + status);
}
function path() { return "/tmp/embdex-" + (++nextPath); }
function safeUnlink(file) {
    try { filesystem.unlink(file); }
    catch (error) { if (error.code !== "ENOENT") throw error; }
}

/**
 * Convert a JavaScript association collection to temporary native arguments.
 * @param documents Associations with id and content.
 * @param fn Receives pointer and count.
 * @return Callback result.
 */
function withDocuments(documents, fn) {
    if (!Array.isArray(documents)) throw new TypeError("Expected document array");
    const array = allocate(documents.length * 8), owned = [];
    try {
        documents.forEach((document, i) => {
            const id = cstring(document.id), content = cstring(document.content);
            owned.push(id, content);
            wasm.setValue(array + i * 8, id, "i32");
            wasm.setValue(array + i * 8 + 4, content, "i32");
        });
        return fn(array, documents.length);
    } finally {
        for (const ptr of owned) wasm._free(ptr);
        wasm._free(array);
    }
}

export const embdex = {
    build(documents, {dictionaries = [], onProgress} = {}) {
        if (!Array.isArray(dictionaries)) throw new TypeError("Expected dictionary array");
        const destination = path() + ".bin";
        const dst = cstring(destination);
        const owned = [], files = [];
        let dictionaryArray = 0, config = 0, callback = 0;
        try {
            return withDocuments(documents, (array, count) => {
                if (dictionaries.length) {
                    dictionaryArray = allocate(dictionaries.length * 4);
                    dictionaries.forEach((dictionary, i) => {
                        const file = path() + ".json";
                        filesystem.writeFile(file, JSON.stringify(dictionary));
                        files.push(file);
                        const ptr = cstring(file);
                        owned.push(ptr);
                        wasm.setValue(dictionaryArray + i * 4, ptr, "i32");
                    });
                }
                if (onProgress) {
                    callback = wasm.addFunction((stage, completed, total) => {
                        onProgress({stage, completed: completed >>> 0, total: total >>> 0});
                    }, "viiii");
                    config = allocate(8);
                    wasm.setValue(config, callback, "i32");
                    wasm.setValue(config + 4, 0, "i32");
                }
                const status = (dictionaries.length || onProgress)
                    ? wasm._kc_embdex_build_with_dictionaries(dst, array, count,
                        dictionaryArray, dictionaries.length, config)
                    : wasm._kc_embdex_build(dst, array, count);
                check(status);
                return new Uint8Array(filesystem.readFile(destination));
            });
        } finally {
            if (callback) wasm.removeFunction(callback);
            if (config) wasm._free(config);
            if (dictionaryArray) wasm._free(dictionaryArray);
            for (const ptr of owned) wasm._free(ptr);
            for (const file of files) safeUnlink(file);
            safeUnlink(destination);
            wasm._free(dst);
        }
    },
    open(binary) {
        if (!(binary instanceof Uint8Array)) throw new TypeError("Expected Uint8Array");
        const file = path() + ".bin";
        filesystem.writeFile(file, binary);
        const name = cstring(file), out = allocate(4);
        try {
            wasm.setValue(out, 0, "i32");
            check(wasm._kc_embdex_open(out, name));
            let handle = uint(out);
            if (!handle) throw new Error("Index was not opened");
            const valid = () => {
                if (!handle) throw new Error("Index is closed");
                return handle;
            };
            return {
                search(query, {limit} = {}) {
                    const input = cstring(query), found = [];
                    let cfg = 0, limitPtr = 0, callback = 0;
                    try {
                        if (limit !== undefined) {
                            if (!Number.isSafeInteger(limit) || limit < 0 || limit > 0xffffffff) throw new RangeError("Invalid limit");
                            limitPtr = allocate(4);
                            wasm.setValue(limitPtr, limit, "i32");
                            cfg = allocate(4);
                            wasm.setValue(cfg, limitPtr, "i32");
                        }
                        callback = wasm.addFunction(ptr => {
                            found.push({
                                id: string(uint(ptr)),
                                content: string(uint(ptr + 4)),
                                score: wasm.getValue(ptr + 8, "double")
                            });
                            return 0;
                        }, "iii");
                        check(wasm._kc_embdex_search(valid(), input, cfg, callback, 0));
                        return found;
                    } finally {
                        if (callback) wasm.removeFunction(callback);
                        if (cfg) wasm._free(cfg);
                        if (limitPtr) wasm._free(limitPtr);
                        wasm._free(input);
                    }
                },
                close() {
                    if (handle) {
                        wasm._kc_embdex_close(handle);
                        handle = 0;
                        safeUnlink(file);
                    }
                }
            };
        } catch (error) {
            safeUnlink(file);
            throw error;
        } finally {
            wasm._free(out);
            wasm._free(name);
        }
    },
    version() { return Number(wasm._kc_embdex_version()); }
};
export default embdex;
