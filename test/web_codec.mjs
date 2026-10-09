import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";

const sandbox = vm.createContext({
  crypto: globalThis.crypto,
  URLSearchParams: globalThis.URLSearchParams,
});
vm.runInContext(
  readFileSync(new URL("../docs/codec.js", import.meta.url), "utf8"),
  sandbox,
  { filename: "codec.js" }
);
const {
  decodeAddress,
  decodeString,
  dogeToKoinu,
  encodeString,
  extractPayment,
  feeRatePerByte,
  formatDoge,
  priceFixed,
  selectCoins,
} = sandbox.DogeCodec;

const known = "DBus3bamQjgJULBJtYXpEzDWQRwF5iwxgC";
assert.equal(extractPayment(known).address, known);
assert.equal(extractPayment("  " + known + "\n").address, known);
assert.equal(extractPayment("dogecoin:" + known + "?amount=1.5").amount, "1.5");
assert.equal(extractPayment("\0" + known).address, known);
assert.throws(() => extractPayment("hello"), /not a Dogecoin/);

const addr = await decodeAddress("DBus3bamQjgJULBJtYXpEzDWQRwF5iwxgC");
assert.equal(addr.kind, "p2pkh");
assert.equal(addr.typeByte, 0);
assert.equal(
  [...addr.hash160].map((b) => b.toString(16).padStart(2, "0")).join(""),
  "4a483568665dcdfa68dd58a1f62893448a643339"
);

assert.equal(dogeToKoinu("1"), 100_000_000n);
assert.equal(dogeToKoinu("1.5"), 150_000_000n);
assert.equal(dogeToKoinu("0.01"), 1_000_000n);
assert.equal(feeRatePerByte("1.0"), 100_000n);
assert.equal(feeRatePerByte("0.01"), 1_000n);
assert.equal(formatDoge(100_000_000n), "1.00000000");

const utxo = {
  txid: "ab".repeat(32),
  vout: 1,
  value: 1_000_000_000n,
  spendable: true,
};
const plan = priceFixed(1, utxo.value, 100_000_000n, 1000n, false);
assert.equal(plan.size, 226);
assert.equal(plan.fee, 226_000n);
assert.equal(plan.change, 899_774_000n);

const payment = {
  inputs: [utxo],
  pay: 100_000_000n,
  fee: plan.fee,
  change: plan.change,
  typeByte: 0,
  hash160: addr.hash160,
};
const encoded = await encodeString(payment);
assert.ok(encoded.groups.length >= 1);
assert.equal(encoded.tail.length, 6);
const typed = encoded.groups.join("") + encoded.tail;
const back = await decodeString(typed);
assert.equal(back.inputs.length, 1);
assert.equal(back.inputs[0].txid, utxo.txid);
assert.equal(back.inputs[0].vout, 1);
assert.equal(back.pay, payment.pay);
assert.equal(back.fee, payment.fee);
assert.equal(back.change, payment.change);

const chars = encoded.groups[0].split("");
const swap = chars.slice();
const tmp = swap[0];
swap[0] = swap[1];
swap[1] = tmp;
const swapped = swap.join("") + encoded.groups.slice(1).join("") + encoded.tail;
await assert.rejects(decodeString(swapped), /end check/);

const bad = chars.slice();
const next = bad[0] === "2" ? "3" : "2";
bad[0] = next;
const substituted = bad.join("") + encoded.groups.slice(1).join("") + encoded.tail;
await assert.rejects(decodeString(substituted), /Group 1/);

const spaced = encoded.groups.map((g, i) => (i % 2 ? g : g)).join("  ") + "\n" + encoded.tail;
const fromSpaces = await decodeString(spaced);
assert.equal(fromSpaces.pay, payment.pay);

const picked = selectCoins(
  [
    { txid: "11".repeat(32), vout: 0, value: 50_000_000n, spendable: true },
    { txid: "22".repeat(32), vout: 0, value: 1_000_000_000n, spendable: true },
    { txid: "33".repeat(32), vout: 0, value: 10_000_000n, spendable: false },
  ],
  100_000_000n,
  1000n,
  false
);
assert.equal(picked.chosen.length, 1);
assert.equal(picked.chosen[0].txid, "22".repeat(32));
assert.equal(picked.change, 899_774_000n);

console.log("web codec ok", encoded.typedCount, "chars", encoded.groups.length, "groups");
