// JSON Pointer patches, as used by the control API (docs/control-api.md §3.1).
// `diff` is used by the mock engine to emit patches; `applyPatch` by the client.

/** @param {string} s */
const unescape = s => s.replace(/~1/g, '/').replace(/~0/g, '~');
/** @param {string} s */
const escape = s => String(s).replace(/~/g, '~0').replace(/\//g, '~1');

/** @param {string} path @returns {string[]} */
export function parsePointer(path) {
  if (path === '') return [];
  if (!path.startsWith('/')) throw new Error(`bad pointer: ${path}`);
  return path.slice(1).split('/').map(unescape);
}

/** @param {string[]} keys */
export const toPointer = keys => keys.map(k => '/' + escape(k)).join('');

/**
 * Apply ops to `doc` in place. `set` creates intermediate objects as needed.
 * @param {any} doc @param {{op:'set'|'del', path:string, value?:any}[]} ops
 */
export function applyPatch(doc, ops) {
  for (const { op, path, value } of ops) {
    const keys = parsePointer(path);
    if (!keys.length) throw new Error('cannot patch the root');
    let node = doc;
    for (const k of keys.slice(0, -1)) {
      if (node[k] === undefined || node[k] === null) node[k] = {};
      node = node[k];
    }
    const last = keys[keys.length - 1];
    if (op === 'set') node[last] = clone(value);
    else if (op === 'del') {
      if (Array.isArray(node)) node.splice(Number(last), 1);
      else delete node[last];
    } else throw new Error(`unknown op: ${op}`);
  }
  return doc;
}

/**
 * Minimal set of ops that turns `a` into `b`. Objects are walked key by key;
 * arrays and scalars are replaced whole when they differ.
 * @returns {{op:'set'|'del', path:string, value?:any}[]}
 */
export function diff(a, b, keys = [], out = []) {
  if (isPlainObject(a) && isPlainObject(b)) {
    for (const k of Object.keys(a)) {
      if (!(k in b)) out.push({ op: 'del', path: toPointer([...keys, k]) });
    }
    for (const k of Object.keys(b)) {
      if (!(k in a)) out.push({ op: 'set', path: toPointer([...keys, k]), value: clone(b[k]) });
      else diff(a[k], b[k], [...keys, k], out);
    }
  } else if (!deepEqual(a, b)) {
    out.push({ op: 'set', path: toPointer(keys), value: clone(b) });
  }
  return out;
}

export const clone = v => (v === undefined ? undefined : structuredClone(v));
const isPlainObject = v => v !== null && typeof v === 'object' && !Array.isArray(v);

export function deepEqual(a, b) {
  if (a === b) return true;
  if (typeof a !== typeof b || a === null || b === null || typeof a !== 'object') return false;
  if (Array.isArray(a) !== Array.isArray(b)) return false;
  const ka = Object.keys(a), kb = Object.keys(b);
  if (ka.length !== kb.length) return false;
  return ka.every(k => deepEqual(a[k], b[k]));
}
