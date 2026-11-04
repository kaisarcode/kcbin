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

/**
 * Allocates WebAssembly memory.
 * @param {number} size Allocation size in bytes.
 * @return {number} Allocated memory address.
 */
function allocate(size) {
    const ptr = wasm._malloc(Math.max(1, size));
    if (!ptr) throw new Error("Out of WebAssembly memory");
    return ptr;
}

/**
 * Stores a JavaScript string in WebAssembly memory.
 * @param {string} value String to store.
 * @return {number} Address of the stored string.
 */
function cstring(value) {
    if (typeof value !== "string") throw new TypeError("Expected string");
    const size = wasm.lengthBytesUTF8(value) + 1, ptr = allocate(size);
    wasm.stringToUTF8(value, ptr, size);
    return ptr;
}

/**
 * Reads a WebAssembly string.
 * @param {number} ptr Address of the string.
 * @return {?string} Decoded string or null.
 */
function string(ptr) { return ptr ? wasm.UTF8ToString(ptr) : null; }

/**
 * Reads an unsigned integer from WebAssembly memory.
 * @param {number} ptr Address of the integer.
 * @return {number} Unsigned integer value.
 */
function uint(ptr) { return wasm.getValue(ptr, "i32") >>> 0; }

/**
 * Raises an error for a native failure status.
 * @param {number} status Native status code.
 * @return {void} Nothing.
 */
function check(status) {
    if (status !== 0) throw new Error(string(wasm._kc_route_strerror(status)) || "Native error " + status);
}

/**
 * Reads route fields from WebAssembly memory.
 * @param {number} ptr Address of the fields.
 * @param {number} count Number of fields.
 * @return {Array<object>} Decoded fields.
 */
function readFields(ptr, count) {
    return Array.from({length: count}, (_, i) => ({
        name: string(uint(ptr + i * 8)),
        value: string(uint(ptr + i * 8 + 4))
    }));
}

/**
 * Reads a route request from WebAssembly memory.
 * @param {number} ptr Address of the request.
 * @return {?object} Decoded request or null.
 */
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

/**
 * Reads a route match from WebAssembly memory.
 * @param {number} ptr Address of the match.
 * @return {object} Decoded route match.
 */
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

    /**
     * Retains an allocated address for release.
     * @param {number} ptr Address to retain.
     * @return {number} Retained address.
     */
    const hold = ptr => { owned.push(ptr); return ptr; };

    /**
     * Converts an optional field to a native string address.
     * @param {*} value Field value.
     * @return {number} Native string address.
     */
    const field = value => value === null || value === undefined ? 0 : hold(cstring(value));

    /**
     * Stores request fields in native memory.
     * @param {*} items Request fields.
     * @param {number} offset Request field offset.
     * @return {void} Nothing.
     */
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

    /**
     * Opens a route registry.
     * @return {object} Route registry.
     */
    open() {
        const out = allocate(4), callbacks = [];
        let handle = 0;
        try {
            wasm.setValue(out, 0, "i32");
            check(wasm._kc_route_open(out));
            handle = uint(out);
            if (!handle) throw new Error("Route registry was not created");

            /**
             * Checks that the route registry remains open.
             * @return {number} Native route registry address.
             */
            const valid = () => {
                if (!handle) throw new Error("Route registry is closed");
                return handle;
            };
            const instance = {

                /**
                 * Adds a route handler.
                 * @param {string} method Request method.
                 * @param {string} pattern Route pattern.
                 * @param {string} id Route identifier.
                 * @param {Function} handler Optional route handler.
                 * @return {object} Route registry.
                 */
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

                /**
                 * Adds a GET route handler.
                 * @param {string} pattern Route pattern.
                 * @param {string} id Route identifier.
                 * @param {Function} handler Optional route handler.
                 * @return {object} Route registry.
                 */
                get(pattern, id, handler) { return instance.add("GET", pattern, id, handler); },

                /**
                 * Adds a POST route handler.
                 * @param {string} pattern Route pattern.
                 * @param {string} id Route identifier.
                 * @param {Function} handler Optional route handler.
                 * @return {object} Route registry.
                 */
                post(pattern, id, handler) { return instance.add("POST", pattern, id, handler); },

                /**
                 * Adds a PUT route handler.
                 * @param {string} pattern Route pattern.
                 * @param {string} id Route identifier.
                 * @param {Function} handler Optional route handler.
                 * @return {object} Route registry.
                 */
                put(pattern, id, handler) { return instance.add("PUT", pattern, id, handler); },

                /**
                 * Adds a DELETE route handler.
                 * @param {string} pattern Route pattern.
                 * @param {string} id Route identifier.
                 * @param {Function} handler Optional route handler.
                 * @return {object} Route registry.
                 */
                delete(pattern, id, handler) { return instance.add("DELETE", pattern, id, handler); },

                /**
                 * Adds a route handler for every method.
                 * @param {string} pattern Route pattern.
                 * @param {string} id Route identifier.
                 * @param {Function} handler Optional route handler.
                 * @return {object} Route registry.
                 */
                all(pattern, id, handler) { return instance.add("ALL", pattern, id, handler); },

                /**
                 * Matches a request against registered routes.
                 * @param {string} method Request method.
                 * @param {string} target Request target.
                 * @return {?object} Matched route or null.
                 */
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

                /**
                 * Matches an HTTP request against registered routes.
                 * @param {object} request HTTP request.
                 * @return {boolean} Whether a route matched.
                 */
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

                /**
                 * Closes the route registry.
                 * @return {void} Nothing.
                 */
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

    /**
     * Gets the native route library version.
     * @return {number} Library version.
     */
    version() { return Number(wasm._kc_route_version()); }
};
export default route;
