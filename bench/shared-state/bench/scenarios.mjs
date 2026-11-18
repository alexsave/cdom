// The scenarios and the entries each runs in (docs/shared-state-design.md, S1 to S13), in the order the
// runner takes them; page.html holds what each scenario does.
const ALL = ['A', 'B', 'C', 'E', 'L'];
export const SCENARIOS = [
  { sc: 'S1', entries: ALL },
  { sc: 'S2', entries: ALL },
  { sc: 'S3_10k', entries: ALL },
  { sc: 'S3_100k', entries: ALL },
  { sc: 'S4', entries: ALL },
  { sc: 'S5', entries: ALL },
  { sc: 'S6', entries: ALL },            // B with m = 10, the rest m = 1,000
  { sc: 'S7', entries: ALL },
  { sc: 'S8', entries: [...ALL, 'CL'] },
  { sc: 'S9a1', entries: ['L', 'E'] },   // E is S9 (c)
  { sc: 'S9a2', entries: ['L'] },
  { sc: 'S9b', entries: ['L'] },
  { sc: 'S9d', entries: ['L'] },
  { sc: 'S10', entries: ['A', 'C', 'E'] },
  { sc: 'S11', entries: ['A', 'C', 'E'] },
  { sc: 'S12_1k', entries: ['L'] },
  { sc: 'S12_10k', entries: ['L'] },
  { sc: 'S13i_10k', entries: ['L'] },
  { sc: 'S13i_100k', entries: ['L'] },
  { sc: 'S13ii_10k', entries: ['L'] },
  { sc: 'S13ii_100k', entries: ['L'] },
];
