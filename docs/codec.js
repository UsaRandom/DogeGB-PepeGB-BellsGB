// DogeGB typed-payment string, format version 1.
//
// The page builds these bytes, base58-encodes them, and inserts a check
// character as the 8th character of every group. A 4-byte double-SHA256
// is fixed to 6 characters at the end.
//
// Bytes, big-endian:
//   0x01            version
//   u8              input count, 1..4
//   repeated:
//     32            txid, block-explorer order
//     u16           vout
//     u64           value, koinu
//   u64             pay value, koinu
//   u32             fee, koinu
//   u8              0 = pay-to-address, 1 = pay-to-script
//   20              hash160 of the destination
//
// Change is not in the string. It is the inputs minus the payment minus
// the fee, and it pays this wallet. Version, locktime, and sequence stay
// at 1, 0, and 0xffffffff and are not in the string.

globalThis.DogeCodec = (function () {
const ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
const KOINU = 100_000_000n;
const DUST = 1_000_000n;
const MAX_INPUTS = 4;
const FORMAT_VERSION = 0x01;
const TAIL_CHARS = 6;

const P2PKH = 0x1e;
const P2SH = 0x16;

function alphabetIndex(ch) {
  const i = ALPHABET.indexOf(ch);
  if (i < 0) throw new Error("Character " + ch + " is not on the base58 keyboard.");
  return i;
}

function b58encode(data) {
  let n = 0n;
  for (const b of data) n = (n << 8n) + BigInt(b);
  let out = "";
  while (n > 0n) {
    const r = n % 58n;
    n /= 58n;
    out = ALPHABET[Number(r)] + out;
  }
  for (const b of data) {
    if (b === 0) out = "1" + out;
    else break;
  }
  return out || "1";
}

function b58decode(text) {
  let n = 0n;
  for (const ch of text) n = n * 58n + BigInt(alphabetIndex(ch));
  let body = [];
  while (n > 0n) {
    body.push(Number(n & 0xffn));
    n >>= 8n;
  }
  body.reverse();
  let zeros = 0;
  for (const ch of text) {
    if (ch === "1") zeros++;
    else break;
  }
  const raw = new Uint8Array(zeros + body.length);
  raw.set(body, zeros);
  return raw;
}

async function sha256(data) {
  const digest = await crypto.subtle.digest("SHA-256", data);
  return new Uint8Array(digest);
}

async function sha256d(data) {
  return sha256(await sha256(data));
}

function b58checkDecode(text) {
  const raw = b58decode(text.trim());
  if (raw.length < 5) throw new Error("Address is too short.");
  return { body: raw.slice(0, -4), checksum: raw.slice(-4) };
}

const ADDRESS_RUN = /[1-9A-HJ-NP-Za-km-z]{25,44}/;

function extractPayment(raw) {
  let text = String(raw).replace(/\0/g, " ").trim();
  let amount = null;
  const uri = text.match(/dogecoin:([^?\s]+)(?:\?(\S+))?/i);
  if (uri) {
    text = decodeURIComponent(uri[1]);
    if (uri[2]) {
      const amountText = new URLSearchParams(uri[2]).get("amount");
      if (amountText) amount = amountText;
    }
  }
  const found = text.match(ADDRESS_RUN);
  if (!found) throw new Error("That code is not a Dogecoin address.");
  return { address: found[0], amount };
}

async function decodeAddress(text) {
  const trimmed = text.trim();
  const { body, checksum } = b58checkDecode(trimmed);
  const hash = await sha256d(body);
  for (let i = 0; i < 4; i++) {
    if (hash[i] !== checksum[i]) throw new Error("Address checksum does not match.");
  }
  if (body.length !== 21) throw new Error("Address payload is the wrong length.");
  const version = body[0];
  const hash160 = body.slice(1);
  if (version === P2PKH) return { kind: "p2pkh", typeByte: 0, hash160, text: trimmed };
  if (version === P2SH) return { kind: "p2sh", typeByte: 1, hash160, text: trimmed };
  throw new Error("Address version 0x" + version.toString(16) + " is not Dogecoin.");
}

function scriptP2pkh(hash160) {
  const out = new Uint8Array(25);
  out[0] = 0x76;
  out[1] = 0xa9;
  out[2] = 20;
  out.set(hash160, 3);
  out[23] = 0x88;
  out[24] = 0xac;
  return out;
}

function dogeToKoinu(text) {
  const s = String(text).trim();
  if (!/^\d+(\.\d{0,8})?$/.test(s)) {
    throw new Error("Amount must be a DOGE number with at most 8 decimals.");
  }
  const [whole, frac = ""] = s.split(".");
  const padded = (frac + "00000000").slice(0, 8);
  return BigInt(whole) * KOINU + BigInt(padded);
}

function formatDoge(koinu) {
  const neg = koinu < 0n;
  const v = neg ? -koinu : koinu;
  const whole = v / KOINU;
  const frac = (v % KOINU).toString().padStart(8, "0");
  return (neg ? "-" : "") + whole.toString() + "." + frac;
}

function feeRatePerByte(dogePerKb) {
  const perKb = dogeToKoinu(dogePerKb);
  const q = perKb / 1000n;
  const r = perKb % 1000n;
  const rate = q + (r >= 500n ? 1n : 0n);
  return rate < 1n ? 1n : rate;
}

function estimateSize(numInputs, recipientIsP2sh, withChange) {
  const p2pkh = (recipientIsP2sh ? 0 : 1) + (withChange ? 1 : 0);
  const p2sh = recipientIsP2sh ? 1 : 0;
  return 10 + numInputs * 148 + p2pkh * 34 + p2sh * 32;
}

function pushU16(out, n) {
  if (!Number.isInteger(n) || n < 0 || n > 0xffff) {
    throw new Error("Output index must be 0..65535.");
  }
  out.push((n >> 8) & 255, n & 255);
}

function pushU32(out, n) {
  if (n < 0n || n > 0xffffffffn) throw new Error("Fee is too large to encode.");
  let x = n;
  const b = [0, 0, 0, 0];
  for (let i = 3; i >= 0; i--) {
    b[i] = Number(x & 0xffn);
    x >>= 8n;
  }
  out.push(...b);
}

function pushU64(out, n) {
  if (n < 0n || n > 0xffffffffffffffffn) throw new Error("Value does not fit in 8 bytes.");
  let x = n;
  const b = [0, 0, 0, 0, 0, 0, 0, 0];
  for (let i = 7; i >= 0; i--) {
    b[i] = Number(x & 0xffn);
    x >>= 8n;
  }
  out.push(...b);
}

function readU16(bytes, p) {
  return (bytes[p] << 8) | bytes[p + 1];
}

function readU32(bytes, p) {
  return (BigInt(bytes[p]) << 24n) | (BigInt(bytes[p + 1]) << 16n) |
    (BigInt(bytes[p + 2]) << 8n) | BigInt(bytes[p + 3]);
}

function readU64(bytes, p) {
  let n = 0n;
  for (let i = 0; i < 8; i++) n = (n << 8n) | BigInt(bytes[p + i]);
  return n;
}

function encodePayload(payment) {
  const inputs = payment.inputs;
  if (inputs.length < 1 || inputs.length > MAX_INPUTS) {
    throw new Error("Use 1 to " + MAX_INPUTS + " coins.");
  }
  const out = [];
  out.push(FORMAT_VERSION);
  out.push(inputs.length);
  let total = 0n;
  for (const inp of inputs) {
    const txid = inp.txid.toLowerCase();
    if (!/^[0-9a-f]{64}$/.test(txid)) throw new Error("A txid must be 64 hex characters.");
    for (let i = 0; i < 64; i += 2) out.push(parseInt(txid.slice(i, i + 2), 16));
    pushU16(out, inp.vout);
    pushU64(out, inp.value);
    total += inp.value;
  }
  if (payment.pay <= 0n) throw new Error("Amount must be more than 0.");
  if (payment.fee < 0n) throw new Error("Fee is negative.");
  if (total !== payment.pay + payment.fee + payment.change) {
    throw new Error("Coins, payment, fee, and change do not add up.");
  }
  pushU64(out, payment.pay);
  pushU32(out, payment.fee);
  if (payment.typeByte !== 0 && payment.typeByte !== 1) {
    throw new Error("Destination type is unknown.");
  }
  out.push(payment.typeByte);
  if (payment.hash160.length !== 20) throw new Error("Destination hash is the wrong length.");
  for (const b of payment.hash160) out.push(b);
  return new Uint8Array(out);
}

function decodePayload(bytes) {
  let p = 0;
  const need = (n) => {
    if (p + n > bytes.length) throw new Error("String ended early.");
    const s = bytes.slice(p, p + n);
    p += n;
    return s;
  };
  const version = need(1)[0];
  if (version !== FORMAT_VERSION) throw new Error("Unknown string version.");
  const n = need(1)[0];
  if (n < 1 || n > MAX_INPUTS) throw new Error("Input count is out of range.");
  const inputs = [];
  let total = 0n;
  for (let i = 0; i < n; i++) {
    const txidBytes = need(32);
    let txid = "";
    for (const b of txidBytes) txid += b.toString(16).padStart(2, "0");
    const vout = readU16(need(2), 0);
    const value = readU64(need(8), 0);
    total += value;
    inputs.push({ txid, vout, value });
  }
  const pay = readU64(need(8), 0);
  const fee = readU32(need(4), 0);
  const typeByte = need(1)[0];
  const hash160 = need(20);
  if (p !== bytes.length) throw new Error("String has extra bytes.");
  const change = total - pay - fee;
  return { inputs, pay, fee, change, typeByte, hash160 };
}

function checkChar(chunk) {
  let sum = 0;
  for (const ch of chunk) sum += alphabetIndex(ch);
  return ALPHABET[sum % 58];
}

function groupData(dataChars) {
  const groups = [];
  for (let i = 0; i < dataChars.length; i += 7) {
    const chunk = dataChars.slice(i, i + 7);
    groups.push(chunk + checkChar(chunk));
  }
  return groups;
}

function ungroup(body) {
  let data = "";
  let i = 0;
  let groupNo = 1;
  while (i < body.length) {
    const n = body.length - i >= 8 ? 8 : body.length - i;
    if (n < 2) throw new Error("Group " + groupNo + " is too short.");
    const group = body.slice(i, i + n);
    const chunk = group.slice(0, -1);
    if (group[group.length - 1] !== checkChar(chunk)) {
      throw new Error("Group " + groupNo + " does not check.");
    }
    data += chunk;
    i += n;
    groupNo++;
  }
  return data;
}

function encodeTail(hash4) {
  if (hash4.length !== 4) throw new Error("End hash must be 4 bytes.");
  let n = 0n;
  for (const b of hash4) n = (n << 8n) + BigInt(b);
  let out = "";
  for (let i = 0; i < TAIL_CHARS; i++) {
    const r = n % 58n;
    n /= 58n;
    out = ALPHABET[Number(r)] + out;
  }
  if (n !== 0n) throw new Error("End hash does not fit in 6 characters.");
  return out;
}

function decodeTail(text) {
  if (text.length !== TAIL_CHARS) throw new Error("End check must be 6 characters.");
  let n = 0n;
  for (const ch of text) n = n * 58n + BigInt(alphabetIndex(ch));
  const out = new Uint8Array(4);
  for (let i = 3; i >= 0; i--) {
    out[i] = Number(n & 0xffn);
    n >>= 8n;
  }
  if (n !== 0n) throw new Error("End check is too large.");
  return out;
}

async function encodeString(payment) {
  const payload = encodePayload(payment);
  const groups = groupData(b58encode(payload));
  const tailHash = (await sha256d(payload)).slice(0, 4);
  const tail = encodeTail(tailHash);
  return {
    payload,
    groups,
    tail,
    typedCount: groups.reduce((n, g) => n + g.length, 0) + tail.length,
  };
}

function stripTyped(text) {
  return text.replace(/\s+/g, "");
}

async function decodeString(text) {
  const raw = stripTyped(text);
  if (raw.length <= TAIL_CHARS) throw new Error("String is too short.");
  const body = raw.slice(0, -TAIL_CHARS);
  const tail = raw.slice(-TAIL_CHARS);
  const dataChars = ungroup(body);
  const payload = b58decode(dataChars);
  const want = (await sha256d(payload)).slice(0, 4);
  const got = decodeTail(tail);
  for (let i = 0; i < 4; i++) {
    if (want[i] !== got[i]) throw new Error("The end check does not match.");
  }
  return decodePayload(payload);
}

function priceFixed(numInputs, totalIn, sendValue, feeRate, recipientIsP2sh) {
  const withChange = estimateSize(numInputs, recipientIsP2sh, true);
  const feeWith = BigInt(withChange) * feeRate;
  const change = totalIn - sendValue - feeWith;
  if (change >= DUST) {
    return { fee: feeWith, change, size: withChange, totalIn };
  }
  const noChangeSize = estimateSize(numInputs, recipientIsP2sh, false);
  const feeNo = BigInt(noChangeSize) * feeRate;
  if (totalIn === sendValue + feeNo) {
    return { fee: feeNo, change: 0n, size: noChangeSize, totalIn };
  }
  if (totalIn >= sendValue + feeNo && change < DUST) {
    const fee = totalIn - sendValue;
    if (fee > 0xffffffffn) throw new Error("Fee is too large to encode.");
    return { fee, change: 0n, size: noChangeSize, totalIn };
  }
  return null;
}

function selectCoins(utxos, sendValue, feeRate, recipientIsP2sh) {
  if (sendValue <= 0n) throw new Error("Amount must be more than 0.");
  const sorted = utxos.filter((u) => u.spendable !== false).sort((a, b) => {
    if (a.value === b.value) return 0;
    return a.value > b.value ? -1 : 1;
  });
  const chosen = [];
  let total = 0n;
  for (const u of sorted) {
    if (chosen.length >= MAX_INPUTS) break;
    chosen.push(u);
    total += u.value;
    const plan = priceFixed(chosen.length, total, sendValue, feeRate, recipientIsP2sh);
    if (plan) return { chosen, ...plan };
  }
  throw new Error("The first 4 coins do not cover this payment and fee.");
}

return {
  ALPHABET,
  KOINU,
  DUST,
  MAX_INPUTS,
  FORMAT_VERSION,
  TAIL_CHARS,
  alphabetIndex,
  b58encode,
  b58decode,
  sha256d,
  b58checkDecode,
  extractPayment,
  decodeAddress,
  scriptP2pkh,
  dogeToKoinu,
  formatDoge,
  feeRatePerByte,
  estimateSize,
  encodePayload,
  decodePayload,
  groupData,
  ungroup,
  encodeTail,
  decodeTail,
  encodeString,
  stripTyped,
  decodeString,
  priceFixed,
  selectCoins,
};
})();
