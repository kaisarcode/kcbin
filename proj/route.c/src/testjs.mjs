/**
 * testjs.mjs - JavaScript contract tests.
 * Summary: Verifies route WASM registration and captures.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import assert from "node:assert/strict";
import {route} from "../bin/wasm/wasm32/libroute.js";

const registry = route.open();
let capture = null;
registry.get("/users/([0-9]+)", "user", match => {
    capture = match.captures[0];
    return true;
});
assert.equal(registry.run("GET", "/users/42"), true);
assert.equal(capture, "42");
assert.equal(registry.run("POST", "/users/42"), false);
registry.close();
assert.throws(() => registry.run("GET", "/users/42"), /closed/);
console.log("[1/1] [PASS] javascript: route registration and captures");
