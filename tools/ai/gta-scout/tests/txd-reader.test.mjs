import test from 'node:test';
import assert from 'node:assert/strict';
import {TXDReader} from '../shared/TXDReader.js';
test('rejects truncated input',()=>assert.throws(()=>new TXDReader().parse(new ArrayBuffer(2))));
test('rejects a non-dictionary',()=>assert.throws(()=>new TXDReader().parse(new ArrayBuffer(12)),/valid TXD/));
