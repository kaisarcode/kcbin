/**
 * testjs.mjs - JavaScript contract tests.
 * Summary: Verifies the WebAssembly embdex projection.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import assert from "node:assert/strict";
import {embdex} from "../bin/wasm/wasm32/libembdex.js";

const map = embdex.build([
    {id: "find", content: "find files by name"},
    {id: "grep", content: "search inside files"}
]);
assert.ok(map instanceof Uint8Array && map.length > 0);
const index = embdex.open(map);
const matches = index.search("find files", {limit: 2});
assert.ok(matches.length > 0);
assert.equal(matches[0].id, "find");
index.close();
assert.throws(() => index.search("files"), /closed/);
console.log("[1/1] [PASS] javascript: build and search portable association map");
