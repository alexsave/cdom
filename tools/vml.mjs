// Checks tools/legacy/vml.js's path converter (cdomVml.path, docs/vml-design.md) in node: every SVG path command, absolute
// and relative, against VML strings worked out by hand, and arcs against points sampled on the true ellipse.
// node tools/vml.mjs; exit 1 on any failure. VML_TIME=1 also times the converter (T1 in docs/vml-design.md).
import fs from 'node:fs';

// Loaded through new Function with a window and a document of its own, not a vm context, whose global lookups (Math,
// String, parseFloat) are several times slower and would be what T1 measured.
const src = fs.readFileSync(new URL('./legacy/vml.js', import.meta.url), 'utf8');
const { path, K } = new Function('window', 'document', `${src}\nreturn cdomVml;`)({}, {});
let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };
const is = (d, want, m, ...ctm) => { const got = path(d, ...ctm); ok(got === want, `${m}: ${JSON.stringify(d)} is ${JSON.stringify(got)}${got === want ? '' : `, want ${JSON.stringify(want)}`}`); };

ok(K === 100, `K is 100 path units a pixel: ${K}`);
// Moves and lines; H and V become l.
is('M10 20 L30 40', 'm1000,2000 l3000,4000 e', 'M and L');
is('m10 20 l5 5', 'm1000,2000 l1500,2500 e', 'm and l, relative');
is('M10 20 H30 V40 h-5 v-5', 'm1000,2000 l3000,2000 l3000,4000 l2500,4000 l2500,3500 e', 'H, V, h and v as l');
is('M0 0 10 0 10 10', 'm0,0 l1000,0 l1000,1000 e', 'pairs after M are lines');
is('m1 1 2 2', 'm100,100 l300,300 e', 'pairs after m are relative lines');
is('M0 0 L1 1 2 2', 'm0,0 l100,100 l200,200 e', 'L repeated');
// Z: x, and a move back to the subpath's start when a line or curve follows.
is('M0 0 L10 0 L10 10 Z', 'm0,0 l1000,0 l1000,1000 x e', 'Z as x');
is('M5 5 L10 5 Z L5 10', 'm500,500 l1000,500 x m500,500 l500,1000 e', 'L after Z starts at the subpath start');
is('M5 5 L10 5 z l0 5', 'm500,500 l1000,500 x m500,500 l500,1000 e', 'l after z is relative to the subpath start');
is('M0 0 L1 0 Z M5 5 L6 5', 'm0,0 l100,0 x m500,500 l600,500 e', 'M after Z');
is('M0 0 L1 0 Z Z', 'm0,0 l100,0 x e', 'Z twice is one x');
// Cubics.
is('M0 0 C1 2 3 4 5 6', 'm0,0 c100,200,300,400,500,600 e', 'C');
is('M1 1 c1 2 3 4 5 6', 'm100,100 c200,300,400,500,600,700 e', 'c, relative');
is('M0 0 C1 2 3 4 5 6 7 8 9 10 11 12', 'm0,0 c100,200,300,400,500,600 c700,800,900,1000,1100,1200 e', 'C repeated');
is('M0 0 C1 2 3 4 5 6 S9 10 11 12', 'm0,0 c100,200,300,400,500,600 c700,800,900,1000,1100,1200 e', 'S reflects the C before');
is('M0 0 C1 2 3 4 5 6 s4 4 6 6', 'm0,0 c100,200,300,400,500,600 c700,800,900,1000,1100,1200 e', 's, relative');
is('M0 0 S3 4 5 6', 'm0,0 c0,0,300,400,500,600 e', 'S with no C before starts at the current point');
is('M0 0 Q3 3 6 0 S9 9 12 0', 'm0,0 c200,200,400,200,600,0 c600,0,900,900,1200,0 e', 'S after Q does not reflect the quadratic');
is('M0 0 C1 2 3 4 5 6 S9 10 11 12 S15 16 17 18', 'm0,0 c100,200,300,400,500,600 c700,800,900,1000,1100,1200 c1300,1400,1500,1600,1700,1800 e', 'S after S');
// Quadratics raised to cubics: controls two thirds of the way to the quadratic's.
is('M0 0 Q3 3 6 0', 'm0,0 c200,200,400,200,600,0 e', 'Q');
is('m0 0 q3 3 6 0', 'm0,0 c200,200,400,200,600,0 e', 'q, relative');
is('M0 0 Q3 3 6 0 T12 0', 'm0,0 c200,200,400,200,600,0 c800,-200,1000,-200,1200,0 e', 'T reflects the Q before');
is('m0 0 q3 3 6 0 t6 0', 'm0,0 c200,200,400,200,600,0 c800,-200,1000,-200,1200,0 e', 't, relative');
is('M0 0 Q3 3 6 0 T12 0 T18 0', 'm0,0 c200,200,400,200,600,0 c800,-200,1000,-200,1200,0 c1400,200,1600,200,1800,0 e', 'T after T');
is('M0 0 T6 0', 'm0,0 c0,0,200,0,600,0 e', 'T with no Q before: its control is the current point');
is('M0 0 C1 2 3 4 5 6 T11 6', 'm0,0 c100,200,300,400,500,600 c500,600,700,600,1100,600 e', 'T after C does not reflect the cubic: its control is the current point');
// Numbers as SVG writes them.
is('M.5.5L1e1-2', 'm50,50 l1000,-200 e', 'numbers run together, an exponent, a sign as separator');
is('M1,2,3,4', 'm100,200 l300,400 e', 'commas');
is('M1.5.5L2.25.75', 'm150,50 l225,75 e', 'a number ends at a second point');
is('M 1e+1 2E-1 L -.5 +3', 'm1000,20 l-50,300 e', 'exponent signs, a leading point and plus');
is('M0.004 0.005', 'm0,1 e', 'rounding to a hundredth of a pixel');
is('\tM1\n2\rL3\f4 ', 'm100,200 l300,400 e', 'every SVG white space');
// A malformed path draws what came before the error, and a path must start with a move.
is('M0 0 L10', 'm0,0 e', 'a missing number ends the path');
is('M0 0 L10 10 X 5', 'm0,0 l1000,1000 e', 'an unknown command ends the path');
is('L10 10', 'm0,0 e', 'no move first: nothing');
is('', 'm0,0 e', 'empty: nothing');
is('M0 0 L1 1 Z 5 5', 'm0,0 l100,100 x e', 'numbers after Z with no command: the end');
is('M0 0 A1 1 0 2 1 3 3', 'm0,0 e', 'an arc flag but 0 or 1 ends the path');
is('M0 0 L10 10 # 5 5', 'm0,0 l1000,1000 e', 'a character no path has ends it there');
is('M0 0 L1 1 . 5', 'm0,0 l100,100 e', 'a point alone is no number');
// The CTM, baked into the points: (sx x + tx, sy y + ty).
is('M1 2 L3 4', 'm1200,2600 l1600,3200 e', 'the CTM', 2, 3, 10, 20);
is('M1 2 Q3 3 6 0', 'm100,200 c233,267,400,200,600,0 e', 'Q under the identity, rounded');
is('M1 2 Q3 3 6 0', 'm1200,2600 c1467,2800,1800,2600,2200,2000 e', 'Q under a CTM', 2, 3, 10, 20);

// Arcs. A quarter of a circle, worked by hand: the cubic's controls are 4/3 tan(pi/8) = 0.5523 of the radius out.
is('M10 0 A10 10 0 0 1 0 10', 'm1000,0 c1000,552,552,1000,0,1000 e', 'a quarter arc, sweep 1');
is('M10 0 a10 10 0 0 1 -10 10', 'm1000,0 c1000,552,552,1000,0,1000 e', 'a, relative');
is('M10 0 A10 10 0 0 0 0 10', 'm1000,0 c448,0,0,448,0,1000 e', 'sweep 0 takes the other centre (10, 10)');
is('M0 0 A0 5 0 0 1 10 0', 'm0,0 l1000,0 e', 'a zero radius is a line');
is('M0 0 A5 5 0 0 1 0 0', 'm0,0 e', 'an arc to the point it starts at is nothing');
is('M0 0 A10 10 0 0 1 10 10 10 10 0 0 1 20 20', 'm0,0 c552,0,1000,448,1000,1000 c1552,1000,2000,1448,2000,2000 e', 'A repeated, each a quarter about (0, 10) and (10, 20)');
is('M0 0 A10 10 0 0110 10', path('M0 0 A10 10 0 0 1 10 10'), 'flags run into what follows: 0110 is 0, 1 and 10');
is('M0 0 a10 10 0 1 0 10 10', path('M0 0 A10 10 0 1 0 10 10'), 'flags 1 0, the large arc the other way');

// Arcs against the true ellipse: arcs made from a centre, radii, rotation and two angles, so the expected points come
// from the ellipse itself, not from the converter's endpoint-to-centre arithmetic. Every sampled point of the true arc
// must be within 0.0003 r + 0.01 px of the converted curve, and every point of the curve that near the true ellipse (A1).
const cubics = (vmlPath) => {
  const nums = (s) => s.split(',').map((v) => +v / K), out = [];
  let at = null;
  for (const t of vmlPath.split(' ')) {
    if (t[0] === 'm' || t[0] === 'l') { const [x, y] = nums(t.slice(1)); if (t[0] === 'l') out.push([at, at, [x, y], [x, y]]); at = [x, y]; }
    else if (t[0] === 'c') { const n = nums(t.slice(1)); out.push([at, [n[0], n[1]], [n[2], n[3]], [n[4], n[5]]]); at = [n[4], n[5]]; }
  }
  return out;
};
const bez = ([p0, p1, p2, p3], t) => { const u = 1 - t; return [0, 1].map((k) => u * u * u * p0[k] + 3 * u * u * t * p1[k] + 3 * u * t * t * p2[k] + t * t * t * p3[k]); };
const curvePoints = (segs) => segs.flatMap((s) => Array.from({ length: 401 }, (_, i) => bez(s, i / 400)));
// The distance from p to the polyline through pts (each sample joined to the next), not to the nearest sample.
const toLine = (p, pts) => {
  let near = Infinity;
  for (let i = 1; i < pts.length; i++) {
    const [ax, ay] = pts[i - 1], [bx, by] = pts[i], dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy;
    const t = l ? Math.max(0, Math.min(1, ((p[0] - ax) * dx + (p[1] - ay) * dy) / l)) : 0;
    near = Math.min(near, Math.hypot(ax + t * dx - p[0], ay + t * dy - p[1]));
  }
  return near;
};
let worst = 0;
const arcCase = (m, cx, cy, rx, ry, deg, t1, t2, ctm = [1, 1, 0, 0]) => {
  const phi = deg * Math.PI / 180;
  const on = (t) => [cx + rx * Math.cos(phi) * Math.cos(t) - ry * Math.sin(phi) * Math.sin(t), cy + rx * Math.sin(phi) * Math.cos(t) + ry * Math.cos(phi) * Math.sin(t)];
  const [x1, y1] = on(t1), [x2, y2] = on(t2), dt = t2 - t1;
  const d = `M${x1} ${y1} A${rx} ${ry} ${deg} ${Math.abs(dt) > Math.PI ? 1 : 0} ${dt > 0 ? 1 : 0} ${x2} ${y2}`;
  const map = ([x, y]) => [ctm[0] * x + ctm[2], ctm[1] * y + ctm[3]];
  const pts = curvePoints(cubics(path(d, ...ctm)));
  const tol = 0.0003 * Math.max(rx * ctm[0], ry * ctm[1]) + 0.01;
  let far = 0;
  for (let i = 0; i <= 200; i++) {
    const p = map(on(t1 + dt * i / 200));
    far = Math.max(far, toLine(p, pts));
  }
  // and the curve stays on the ellipse: each curve point near some point of the true arc, sampled finely
  const fine = Array.from({ length: 801 }, (_, i) => map(on(t1 + dt * i / 800)));
  let off = 0;
  for (const c of pts.filter((_, i) => i % 13 === 0)) {
    off = Math.max(off, toLine(c, fine));
  }
  const err = Math.max(far, off);
  worst = Math.max(worst, err / tol);
  ok(err <= tol, `${m}: within ${err.toFixed(5)} px of the true arc (tolerance ${tol.toFixed(5)})`);
};
arcCase('a quarter circle', 0, 0, 10, 10, 0, 0, Math.PI / 2);
arcCase('a half circle the other way', 50, 50, 40, 40, 0, Math.PI, 0);
arcCase('a large arc of an ellipse', 100, 80, 90, 30, 0, -0.3, 4.2);
arcCase('a rotated ellipse, three quarters', 20, 30, 60, 25, 30, 0.5, 0.5 + 1.5 * Math.PI);
arcCase('a rotated ellipse, negative sweep, large', -40, 10, 35, 70, -72, 2.0, 2.0 - 5.5);
arcCase('a small arc of a big circle', 0, 0, 1000, 1000, 0, 0.1, 0.13);
arcCase('nearly a whole turn', 10, 10, 20, 15, 15, 0, 2 * Math.PI - 0.01);
arcCase('under a CTM (scale 2.5, 0.75, then a translate)', 30, 40, 25, 10, 45, 0.2, 3.9, [2.5, 0.75, 7, -3]);
// Radii too small: scaled up until the arc fits, a half circle of radius 10 centred at (10, 0) going up (sweep 1, y down).
{
  const pts = curvePoints(cubics(path('M0 0 A1 1 0 0 1 20 0')));
  const r = Math.max(...pts.map(([x, y]) => Math.abs(Math.hypot(x - 10, y) - 10)));
  ok(r <= 0.0003 * 10 + 0.01 && pts.every(([, y]) => y <= 0.01), `radii too small scale up to the half circle of radius 10 above the line: off by ${r.toFixed(5)}`);
}
console.log(`A1: the worst arc used ${(worst * 100).toFixed(1)}% of its tolerance`);

// T1: paths converted per ms, a 12-command path with lines, cubics, a quadratic and an arc.
if (process.env.VML_TIME) {
  const d = 'M10 10 L50 10 l0 20 H80 v15 C90 50 100 60 110 50 s20 -10 30 0 Q160 70 170 50 t20 0 A15 10 30 0 1 220 60 L230 80 Z';
  for (let i = 0; i < 2000; i++) path(d, 1.5, 1.5, 3, 4);
  const runs = [];
  for (let r = 0; r < 5; r++) {
    const n = 20000, t = process.hrtime.bigint();
    for (let i = 0; i < n; i++) path(d, 1.5, 1.5, 3, 4);
    runs.push(n / (Number(process.hrtime.bigint() - t) / 1e6));
  }
  runs.sort((a, b) => a - b);
  console.log(`T1: ${Math.round(runs[2])} paths per ms (median of 5 runs of 20,000; ${runs.map(Math.round).join(', ')}), node ${process.version}`);
}

console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
