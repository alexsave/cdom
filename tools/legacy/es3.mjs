// The fourth tier's checks and passes (IE 6 to 8, docs/ie6-design.md), for tools/legacy/build.mjs, which imports this
// only when it builds a file that tier runs (cdom.legacy-ie6.js, an app's .ie9.js); tools/es3.mjs checks each on
// code planted for it.
import * as acorn from 'acorn';

// The fourth tier (IE 6 to 8, docs/ie6-design.md) reads ES3 as JScript does: a reserved word is a syntax error as a
// property name there (`a.class`, `{ default: 1 }`), which acorn's ES3 lets through, so this walk finds them.
const RESERVED = new Set(('break case catch continue default delete do else finally for function if in instanceof new return ' +
  'switch this throw try typeof var void while with null true false abstract boolean byte char class const debugger double ' +
  'enum export extends final float goto implements import int interface long native package private protected public short ' +
  'static super synchronized throws transient volatile').split(' '));
// What ES5 added that the tier's file may name: each must be one polyfills-es3.js supplies (ES5_SUPPLIED), or be
// allowed in the one part that names it behind a check (ES5_ALLOWED). A name the old DOM lacks is OLD_DOM's.
// What JScript 5.5 (IE 5.5) added to JScript 5.0 (IE 5.0): the tier's files may name only those polyfills-es3.js supplies,
// and the boot, which runs before any polyfill, none of them (boot50).
const JSCRIPT55 = ['undefined', '.push', '.pop', '.shift', '.unshift', '.splice', '.apply', '.call', '.hasOwnProperty',
  '.propertyIsEnumerable', '.isPrototypeOf', 'encodeURI', 'decodeURI', 'encodeURIComponent', 'decodeURIComponent', '.toFixed',
  '.toPrecision', '.toExponential', '.localeCompare',
  // measured in IE 5.01 (tests/probe.html, docs/ie5-design.md): no TypeError or RangeError, and a regular expression has no
  // lastIndex or global
  'TypeError', 'RangeError', '.lastIndex', '.global'];
const ES5_NAMES = new Set(['JSON', '.keys', '.create', '.defineProperty', '.defineProperties', '.getPrototypeOf',
  '.getOwnPropertyNames', '.getOwnPropertyDescriptor', '.freeze', '.seal', '.preventExtensions', '.isFrozen', '.isSealed',
  '.isExtensible', '.isArray', '.indexOf', '.lastIndexOf', '.forEach', '.map', '.filter', '.some', '.every', '.reduce',
  '.reduceRight', '.bind', '.now', '.trim', '.toISOString', '.toJSON', ...JSCRIPT55]);
export const ES5_SUPPLIED = new Set(['.keys', '.create', '.isArray', '.now', '.trim', '.bind', '.indexOf', '.lastIndexOf',
  '.forEach', '.map', '.filter', '.some', '.every', '.reduce', 'undefined', '.push', '.pop', '.shift', '.unshift', '.splice',
  '.apply', '.call', '.hasOwnProperty', 'TypeError']);
// polyfills.js and dom-old.js try defineProperty and fall back; Babel's regenerator helper tries defineProperty and
// reads getPrototypeOf only when it exists. polyfills-es3.js defines the supplied ones, so it names them all.
const ES5_ALLOWED = { polyfills: new Set(['.defineProperty']), shim: new Set(['.defineProperty', '.getPrototypeOf']),
  'polyfills-es3': ES5_NAMES };
// JScript (IE 6 to 8) gives a catch clause no scope of its own: its parameter is a variable of the enclosing function,
// so `function f(e) { try { ... } catch (e) {} use(e); }` loses f's e to the error. Babel's own helpers do exactly
// that (_regeneratorDefine2's defineProperty probe, which throws in IE 6, so every async function failed there). So
// every catch parameter in the fourth tier's file is renamed to a name of its own (catchScoped), and the minified
// file is refused if a catch parameter's name is used anywhere else in its function (catchLeaks).
export async function catchScoped(src) {
    const { transformSync } = await import('@babel/core');
    return transformSync(src, { babelrc: false, configFile: false, sourceType: 'script', comments: false,
      plugins: [({ types: t }) => ({ visitor: { CatchClause(p) {
            if (t.isIdentifier(p.node.param)) p.scope.rename(p.node.param.name, p.scope.generateUid('caught'));
        } } })] }).code;
}
// JScript 5.0's Error(n) takes a number, not a message, so new Error("x") has no message there, and every error the tier's
// file throws would reach window.onerror blank. Each new Error(m) or Error(m) becomes cdomError(m) (polyfills.js),
// which makes the error and sets message by hand; es3() refuses a minified file with one left (errorsWithMessage).
export async function messages(src) {
    const { transformSync } = await import('@babel/core');
    return transformSync(src, { babelrc: false, configFile: false, sourceType: 'script', comments: false,
      plugins: [({ types: t }) => ({ visitor: { 'NewExpression|CallExpression'(p) {
            const n = p.node;
            if (t.isIdentifier(n.callee, { name: 'Error' }) && n.arguments.length && !p.scope.getBinding('Error')) {
                p.replaceWith(t.callExpression(t.identifier('cdomError'), [n.arguments[0]]));
            }
        } } })] }).code;
}
// How many errors tree makes with a message through Error itself: new Error(m) or Error(m).
export function errorsWithMessage(tree) {
    let found = 0;
    const walk = (n) => {
        if ((n.type === 'NewExpression' || n.type === 'CallExpression') && n.callee.type === 'Identifier' && n.callee.name === 'Error' &&
          n.arguments.length) found++;
        for (const k in n) {
            const v = n[k];
            if (Array.isArray(v)) { for (const c of v) if (c && typeof c.type === 'string') walk(c); }
            else if (v && typeof v.type === 'string' && k !== 'loc') walk(v);
        }
    };
    walk(tree);
    return found;
}
export function catchLeaks(tree) {
    const leaks = new Set();
    const kids = (node, f) => {
        for (const k in node) {
            const v = node[k];
            if (Array.isArray(v)) { for (const c of v) if (c && typeof c.type === 'string') f(c); }
            else if (v && typeof v.type === 'string') f(v);
        }
    };
    // The names a function binds itself: its name as an expression, its parameters, its vars and function declarations
    // (not those of the functions inside it).
    const binds = (fn) => {
        const out = new Set(fn.params.map((p) => p.name));
        if (fn.type === 'FunctionExpression' && fn.id) out.add(fn.id.name);
        const scan = (n) => {
            if (n.type === 'VariableDeclarator') out.add(n.id.name);
            if (n.type === 'FunctionDeclaration') { out.add(n.id.name); return; }
            if (/Function/.test(n.type)) return;
            kids(n, scan);
        };
        kids(fn.body, scan);
        return out;
    };
    // Whether name is used under node, outside skip, where it means the enclosing function's: a function inside that
    // binds name itself has its own.
    const uses = (node, name, skip) => {
        if (node === skip) return false;
        if (node.type === 'Identifier' && node.name === name) return true;
        if (/Function/.test(node.type) && binds(node).has(name)) return false;
        let found = false;
        kids(node, (c) => { found = found || uses(c, name, skip); });
        return found;
    };
    const walk = (node, fn) => {
        if (/Function/.test(node.type)) fn = node;
        if (node.type === 'CatchClause' && node.param && fn) {
            const n = node.param.name;
            let found = fn.params.some((p) => p.name === n) || (fn.type === 'FunctionExpression' && fn.id && fn.id.name === n);
            kids(fn.body, (c) => { found = found || uses(c, n, node); });
            if (found) leaks.add(n);
        }
        kids(node, (c) => walk(c, fn));
    };
    walk(tree, null);
    return [...leaks];
}

// Each part is checked for ES3 syntax and ES5 names as written (minified, the parts are one file); the reserved words
// once minified, since terser's ie8 quotes the ones the parts name (promise-polyfill's .catch).
export function es3(name, src, minified = false) {
    let tree;
    try { tree = acorn.parse(src, { ecmaVersion: 3, sourceType: 'script', allowReserved: true }); } catch (e) {
        const line = src.split('\n')[e.loc.line - 1];
        throw new Error(`${name} is not ES3: ${e.message}\n  ${line.trim().slice(0, 160)}`);
    }
    const bad = new Set();
    const regexes = new Set();
    let numKeys = 0;
    const walk = (n) => {
        if (n.type === 'MemberExpression' && !n.computed && RESERVED.has(n.property.name)) bad.add('.' + n.property.name);
        if (n.type === 'Property' && n.key.type === 'Identifier' && RESERVED.has(n.key.name)) bad.add(n.key.name + ':');
        if (n.type === 'Property' && n.key.type === 'Literal' && typeof n.key.value === 'number') numKeys++;
        if (n.type === 'Literal' && n.regex && regex50(n.regex)) regexes.add(n.raw);
        for (const k in n) {
            const v = n[k];
            if (Array.isArray(v)) { for (const c of v) if (c && typeof c.type === 'string') walk(c); }
            else if (v && typeof v.type === 'string' && k !== 'loc') walk(v);
        }
    };
    if (minified) walk(tree);
    if (binaryIn(tree)) throw new Error(`${name} uses the binary in operator, which JScript 5.0 (IE 5.0) refuses as a syntax error (only for-in has in there)`);
    const leaks = minified ? catchLeaks(tree) : [];
    if (leaks.length) throw new Error(`${name}: a catch parameter's name used elsewhere in its function, which JScript's catch would overwrite: ${leaks.join(' ')}`);
    if (minified && errorsWithMessage(tree)) throw new Error(`${name} makes an Error with a message, which JScript 5.0 (IE 5.0) drops (Error(n) takes a number there): cdomError(m) makes one`);
    if (numKeys) throw new Error(`${name} has a number as an object literal's key, which JScript 5.0 (IE 5.0) refuses as a syntax error`);
    if (regexes.size) throw new Error(`${name} has a regular expression JScript 5.0 (IE 5.0) refuses as a syntax error (a flag but g and i, a lazy quantifier, or a group with ?), or Firefox 1.0 does (a / in a class): ${[...regexes].join(' ')}`);
    if (bad.size) throw new Error(`${name} names a reserved word as a property, which JScript refuses: ${[...bad].join(' ')}`);
    if (minified) return src;
    if (replaceWithFunction(tree)) throw new Error(`${name} calls replace with a function, which JScript 5.0 (IE 5.0) does not take`);
    const ok = ES5_ALLOWED[name] || new Set(), es5 = new Set();
    let prev = null;
    for (const t of acorn.tokenizer(src, { ecmaVersion: 3, allowReserved: true })) {
        if (t.type.label === 'name') {
            const key = prev && prev.type.label === '.' ? '.' + t.value : t.value;
            if (ES5_NAMES.has(key) && !ES5_SUPPLIED.has(key) && !ok.has(key)) es5.add(key);
        }
        prev = t;
    }
    if (es5.size) throw new Error(`${name} uses ES5 names polyfills-es3.js does not supply: ${[...es5].join(' ')}`);
    // Firefox 1.0 crashes calling a function whose __proto__ was set (Babel's generator runtime did it), and IE 6 to 8
    // have no __proto__ at all: the tier's files name none.
    if (/\b__proto__\b/.test(src)) throw new Error(`${name} names __proto__, which crashes Firefox 1.0 on a function`);
    return src;
}

// The fourth tier's one table of what the old DOM (IE 6 to 8) lacks: each W3C call the shim makes, rewritten to the
// tools/legacy/dom-old.js function that does its job (the object it was called on first). call: a method call;
// zero: a call with no arguments only; read: a property read; write: a property assignment; global: a name the
// window holds, read or called. The build refuses cdom.legacy-ie6.js if one of these names is left in the shim.
export const OLD_DOM = {
    call: { addEventListener: 'on', removeEventListener: 'off', setProperty: 'setProperty', getPropertyValue: 'getPropertyValue',
      setAttribute: 'setAttribute', createElementNS: 'createElementNS', elementsFromPoint: 'elementsFromPoint' },
    zero: { remove: 'remove', getBoundingClientRect: 'rect' },
    read: { previousElementSibling: 'prevElement' },
    write: { textContent: 'setText' },
    global: { innerWidth: 'viewportWidth', innerHeight: 'viewportHeight', devicePixelRatio: 'pixelRatio', addEventListener: 'on' },
};
export function oldDom({ types: t }) {
    const helper = (name) => t.memberExpression(t.identifier('cdomOld'), t.identifier(name));
    // A call of a helper itself (cdomOld.setProperty) is what a rewrite made: never rewritten again.
    const named = (m) => t.isMemberExpression(m) && !m.computed && t.isIdentifier(m.property) &&
      !t.isIdentifier(m.object, { name: 'cdomOld' }) ? m.property.name : null;
    const own = (o, k) => Object.prototype.hasOwnProperty.call(o, k);
    return {
        visitor: {
            CallExpression(p) {
                const c = p.node.callee, m = named(c);
                if (m && (own(OLD_DOM.call, m) || (own(OLD_DOM.zero, m) && !p.node.arguments.length))) {
                    p.replaceWith(t.callExpression(helper(OLD_DOM.call[m] || OLD_DOM.zero[m]), [c.object, ...p.node.arguments]));
                } else if (t.isIdentifier(c) && own(OLD_DOM.global, c.name) && OLD_DOM.global[c.name] === 'on' && !p.scope.hasBinding(c.name)) {
                    p.replaceWith(t.callExpression(helper('on'), [t.identifier('window'), ...p.node.arguments]));
                }
            },
            AssignmentExpression(p) {
                const m = named(p.node.left);
                if (m && own(OLD_DOM.write, m) && p.node.operator === '=') {
                    p.replaceWith(t.callExpression(helper(OLD_DOM.write[m]), [p.node.left.object, p.node.right]));
                }
            },
            MemberExpression(p) {
                const m = named(p.node);
                if (m && own(OLD_DOM.read, m) && !(p.parentPath.isAssignmentExpression() && p.parentPath.node.left === p.node)) {
                    p.replaceWith(t.callExpression(helper(OLD_DOM.read[m]), [p.node.object]));
                }
            },
            Identifier(p) {
                const n = p.node.name;
                if (!own(OLD_DOM.global, n) || OLD_DOM.global[n] === 'on' || p.scope.hasBinding(n) || !p.isReferencedIdentifier()) return;
                p.replaceWith(t.callExpression(helper(OLD_DOM.global[n]), []));
            },
        },
    };
}
// The names of OLD_DOM left in the fourth tier's shim, as tokens: a method called after a dot (a read of one is a
// feature test, which the old DOM answers), a property read or written after a dot, a global bare.
export function oldDomLeftovers(src) {
    const called = new Set([...Object.keys(OLD_DOM.call), ...Object.keys(OLD_DOM.zero)]);
    const props = new Set([...Object.keys(OLD_DOM.read), ...Object.keys(OLD_DOM.write)]);
    const bare = new Set(Object.keys(OLD_DOM.global)), left = new Set();
    const ks = [...acorn.tokenizer(src, { ecmaVersion: 5 })];
    for (let i = 0; i < ks.length; i++) {
        const k = ks[i];
        if (k.type.label !== 'name') continue;
        const dot = i > 0 && ks[i - 1].type.label === '.';
        // cdomOld.setProperty is a helper's call, which the rewrite made
        if (dot && i > 1 && ks[i - 2].type.label === 'name' && ks[i - 2].value === 'cdomOld') continue;
        const call = i + 1 < ks.length && ks[i + 1].type.label === '(';
        if (dot && ((called.has(k.value) && call) || props.has(k.value))) left.add('.' + k.value);
        if (!dot && bare.has(k.value)) left.add(k.value);
    }
    return [...left];
}

// The names JScript 5.0 lacks in shim/boot.js, which every browser runs before the tier's polyfills, as tokens: a method
// after a dot, a global bare; and an Error made with a message, which JScript 5.0 drops (the boot's error() sets it).
export function boot50(src) {
    const names = new Set(JSCRIPT55), found = new Set();
    if (errorsWithMessage(acorn.parse(src, { ecmaVersion: 3, allowReserved: true }))) found.add('Error(message)');
    let prev = null;
    for (const t of acorn.tokenizer(src, { ecmaVersion: 3, allowReserved: true })) {
        if (t.type.label === 'name') {
            const key = prev && prev.type.label === '.' ? '.' + t.value : t.value;
            if (names.has(key)) found.add(key);
        }
        prev = t;
    }
    return [...found];
}
// Whether JScript 5.0 refuses a regular expression literal: it knows the flags g and i alone (/a/m is a syntax error),
// no lazy quantifier (a+?) and no group that starts with ? ((?:a), (?=a), (?!a)). And Firefox 1.0 refuses a / in a class
// unescaped (/[/]/: "unterminated character class"), which the fourth tier's file also runs in.
export function regex50({ pattern, flags }) {
    if (/[^gi]/.test(flags)) return true;
    let inClass = false, prev = '';
    for (let i = 0; i < pattern.length; i++) {
        const c = pattern[i];
        if (c === '\\') { i++; prev = 'x'; continue; }
        if (inClass) { if (c === '/') return true; if (c === ']') inClass = false; prev = 'x'; continue; }
        if (c === '[') { inClass = true; prev = 'x'; continue; }
        if (c === '?' && (prev === '(' || prev === 'q')) return true;
        prev = c === '(' ? '(' : '*+?}'.includes(c) ? 'q' : 'x';
    }
    return false;
}
// A binary in (`k in o`), which JScript 5.0 parses only as for-in's: anywhere else it is a syntax error, and the whole
// file fails. polyfills-es3.js's holds() does its job.
export function binaryIn(tree) {
    let found = 0;
    const walk = (n) => {
        if (n.type === 'BinaryExpression' && n.operator === 'in') found++;
        for (const k in n) {
            const v = n[k];
            if (Array.isArray(v)) { for (const c of v) if (c && typeof c.type === 'string') walk(c); }
            else if (v && typeof v.type === 'string' && k !== 'loc') walk(v);
        }
    };
    walk(tree);
    return found;
}
// A replace whose replacement is written as a function, which JScript 5.0 turns into the function's source text.
export function replaceWithFunction(tree) {
    let found = 0;
    const walk = (n) => {
        if (n.type === 'CallExpression' && n.callee.type === 'MemberExpression' && !n.callee.computed &&
          n.callee.property.name === 'replace' && n.arguments[1] && /Function/.test(n.arguments[1].type)) found++;
        for (const k in n) {
            const v = n[k];
            if (Array.isArray(v)) { for (const c of v) if (c && typeof c.type === 'string') walk(c); }
            else if (v && typeof v.type === 'string' && k !== 'loc') walk(v);
        }
    };
    walk(tree);
    return found;
}
