/**
 * libroute.js - WebAssembly JavaScript projection.
 * Summary: JavaScript route registry and ordered request dispatch.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import createModule from "./libroute.mjs";

const wasm = await createModule();

function allocate(size) {
    const ptr = wasm._malloc(Math.max(1, size));
    if (!ptr) throw new Error("Out of WebAssembly memory");
    return ptr;
}
function cstring(value) {
    if (typeof value !== "string") throw new TypeError("Expected string");
    const size = wasm.lengthBytesUTF8(value) + 1, ptr = allocate(size);
    wasm.stringToUTF8(value, ptr, size);
    return ptr;
}
function string(ptr) { return ptr ? wasm.UTF8ToString(ptr) : null; }
function uint(ptr) { return wasm.getValue(ptr, "i32") >>> 0; }
function check(status) {
    if (status !== 0) throw new Error(string(wasm._kc_route_strerror(status)) || "Native error " + status);
}

function readFields(ptr, count) {
    return Array.from({length: count}, (_, i) => ({
        name: string(uint(ptr + i * 8)),
        value: string(uint(ptr + i * 8 + 4))
    }));
}

function readRequest(ptr) {
    if (!ptr) return null;
    const body = uint(ptr + 28);
    return {
        version: string(uint(ptr)),
        method: string(uint(ptr + 4)),
        target: string(uint(ptr + 8)),
        path: string(uint(ptr + 12)),
        query: string(uint(ptr + 16)),
        headers: readFields(uint(ptr + 20), uint(ptr + 24)),
        body: wasm.HEAPU8.slice(body, body + uint(ptr + 32)),
        trailers: readFields(uint(ptr + 36), uint(ptr + 40)),
        chunked: Boolean(wasm.getValue(ptr + 44, "i32")),
        chunkSize: uint(ptr + 48)
    };
}

function readMatch(ptr) {
    const items = uint(ptr + 20), count = uint(ptr + 24);
    return {
        request: readRequest(uint(ptr)),
        method: string(uint(ptr + 4)),
        path: string(uint(ptr + 8)),
        query: string(uint(ptr + 12)),
        id: string(uint(ptr + 16)),
        captures: Array.from({length: count}, (_, i) => string(uint(items + i * 4)) || "")
    };
}

/**
 * Marshal a request object into a temporary native HTTP request.
 * @param request JavaScript HTTP request.
 * @param invoke Function accepting native request pointer.
 * @return Function result.
 */
function withRequest(request, invoke) {
    const owned = [], native = allocate(52);
    const hold = ptr => { owned.push(ptr); return ptr; };
    const field = value => value === null || value === undefined ? 0 : hold(cstring(value));
    const fields = (items, offset) => {
        const entries = Array.isArray(items)
            ? items.map(x => [x.name, x.value])
            : Object.entries(items || {});
        const array = entries.length ? hold(allocate(entries.length * 8)) : 0;
        for (let i = 0; i < entries.length; i++) {
            wasm.setValue(array + i * 8, field(entries[i][0]), "i32");
            wasm.setValue(array + i * 8 + 4, field(entries[i][1]), "i32");
        }
        wasm.setValue(native + offset, array, "i32");
        wasm.setValue(native + offset + 4, entries.length, "i32");
    };
    try {
        wasm.HEAPU8.fill(0, native, native + 52);
        for (const [offset, key] of [[0, "version"], [4, "method"], [8, "target"],
            [12, "path"], [16, "query"]]) {
            wasm.setValue(native + offset, field(request[key]), "i32");
        }
        fields(request.headers, 20);
        const body = typeof request.body === "string"
            ? new TextEncoder().encode(request.body)
            : request.body || new Uint8Array();
        if (!(body instanceof Uint8Array)) throw new TypeError("Expected Uint8Array body");
        let binary = 0;
        if (body.length) {
            binary = hold(allocate(body.length));
            wasm.HEAPU8.set(body, binary);
        }
        wasm.setValue(native + 28, binary, "i32");
        wasm.setValue(native + 32, body.length, "i32");
        fields(request.trailers, 36);
        wasm.setValue(native + 44, request.chunked ? 1 : 0, "i32");
        wasm.setValue(native + 48, request.chunkSize || 0, "i32");
        return invoke(native);
    } finally {
        for (const ptr of owned) wasm._free(ptr);
        wasm._free(native);
    }
}

export const route = {
    open() {
        const out = allocate(4), callbacks = [];
        let handle = 0;
        try {
            wasm.setValue(out, 0, "i32");
            check(wasm._kc_route_open(out));
            handle = uint(out);
            if (!handle) throw new Error("Route registry was not created");
            const valid = () => {
                if (!handle) throw new Error("Route registry is closed");
                return handle;
            };
            const instance = {
                add(method, pattern, id, handler) {
                    const inputs = [method, pattern, id].map(cstring);
                    let callback = 0;
                    try {
                        if (handler !== undefined && handler !== null) {
                            if (typeof handler !== "function") throw new TypeError("Expected route handler");
                            callback = wasm.addFunction(ptr => handler(readMatch(ptr)) ? 1 : 0, "iii");
                        }
                        check(wasm._kc_route_add(valid(), ...inputs, callback, 0));
                        if (callback) callbacks.push(callback);
                        return instance;
                    } catch (error) {
                        if (callback) wasm.removeFunction(callback);
                        throw error;
                    } finally {
                        for (const ptr of inputs) wasm._free(ptr);
                    }
                },
                get(pattern, id, handler) { return instance.add("GET", pattern, id, handler); },
                post(pattern, id, handler) { return instance.add("POST", pattern, id, handler); },
                put(pattern, id, handler) { return instance.add("PUT", pattern, id, handler); },
                delete(pattern, id, handler) { return instance.add("DELETE", pattern, id, handler); },
                all(pattern, id, handler) { return instance.add("ALL", pattern, id, handler); },
                run(method, target) {
                    const a = cstring(method), b = cstring(target), matched = allocate(4);
                    try {
                        wasm.setValue(matched, 0, "i32");
                        check(wasm._kc_route_run(valid(), a, b, matched));
                        return Boolean(wasm.getValue(matched, "i32"));
                    } finally {
                        wasm._free(matched);
                        wasm._free(b);
                        wasm._free(a);
                    }
                },
                http(request) {
                    return withRequest(request, ptr => {
                        const matched = allocate(4);
                        try {
                            wasm.setValue(matched, 0, "i32");
                            check(wasm._kc_route_http(valid(), ptr, matched));
                            return Boolean(wasm.getValue(matched, "i32"));
                        } finally { wasm._free(matched); }
                    });
                },
                close() {
                    if (handle) {
                        wasm._kc_route_close(handle);
                        handle = 0;
                        for (const ptr of callbacks) wasm.removeFunction(ptr);
                    }
                }
            };
            return instance;
        } finally { wasm._free(out); }
    },
    version() { return Number(wasm._kc_route_version()); }
};
export default route;
