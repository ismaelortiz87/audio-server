// Mock scenarios: the same situations as the design prototypes
// (design/prototypes/console, design/prototypes/vdi), expressed as engine state
// per docs/control-api.md §3. `activity` (0..1) only drives simulated meters.

const ago = min => new Date(Date.now() - min * 60_000).toISOString();

const agentOk = node => ({
  input: 'ok', inputNode: 'loopback_sink.monitor', silentForMin: null,
  output: 'ok', outputNode: node ?? 'crosspoint_mic', paused: false, configError: null,
});

function station(id, colorIndex, o = {}) {
  return {
    id, name: id, colorIndex,
    presence: 'online', lostForSec: null, lastSeen: ago(0),
    health: 'clear', latencyMs: 32, lossPct: 0, jitterBufferMs: 24,
    agent: agentOk(),
    level: 0, pan: 0, mute: false, solo: false, talk: true, hearsYou: false,
    ...o,
    _activity: o._activity ?? 0.3,
  };
}

function consoleState(stations, extra = {}) {
  const map = {};
  for (const s of stations) map[s.id] = s;
  return {
    self: { name: 'maelo-mac', role: 'console', kind: 'mac' },
    connection: { state: 'connected', server: 'aoo.sonobus.net:10998', group: 'ops', passwordSaved: true, reason: null, retryInSec: null },
    stationOrder: stations.map(s => s.id),
    stations: map,
    unknownPeers: [],
    otherConsoles: [],
    mic: { mode: 'open', on: true, pttHeld: false, transmitting: true, device: 'MacBook Pro Microphone' },
    output: { device: 'AirPods Pro', level: 0 },
    settings: { soloDimDb: -18, pttHotkey: '⌥Space', codec: 'opus', bitrateKbps: 96, networkBuffer: { mode: 'auto', currentMs: 24 } },
    devices: {
      inputs: [{ id: 'builtin-mic', name: 'MacBook Pro Microphone' }, { id: 'airpods-mic', name: 'AirPods Pro' }],
      outputs: [{ id: 'airpods', name: 'AirPods Pro' }, { id: 'builtin-out', name: 'MacBook Pro Speakers' }],
    },
    ...extra,
  };
}

export const CONSOLE_SCENARIOS = {
  everyday: () => consoleState([
    station('VDI-ACCT-07', 0, { pan: -0.5, _activity: 0.35 }),
    station('VDI-DEV-02', 1, { level: -6, latencyMs: 41, _activity: 0.15 }),
    station('VDI-QA-11', 2, { pan: 0.5, level: -3, latencyMs: 38, lossPct: 0.1, _activity: 0.05 }),
  ]),
  twocalls: () => consoleState([
    station('VDI-ACCT-07', 0, { pan: -1, solo: true, _activity: 0.9 }),
    station('VDI-DEV-02', 1, { pan: 1, latencyMs: 44, _activity: 0.85 }),
    station('VDI-QA-11', 2, { level: -6, latencyMs: 38, mute: true, talk: false, _activity: 0.1 }),
  ]),
  problem: () => consoleState([
    station('VDI-ACCT-07', 0, { pan: -1, _activity: 0, agent: { ...agentOk(), input: 'silent', silentForMin: 12 } }),
    station('VDI-DEV-02', 1, { level: -6, health: 'unstable', latencyMs: 148, lossPct: 3.2, jitterBufferMs: 80, _activity: 0.5 }),
    station('VDI-QA-11', 2, { pan: 0.5, level: -3, presence: 'lost', lostForSec: 12, latencyMs: null, lossPct: null, jitterBufferMs: null, _activity: 0 }),
    station('VDI-OPS-04', 3, { pan: 1, level: -3, presence: 'offline', lastSeen: ago(95), latencyMs: null, lossPct: null, jitterBufferMs: null, agent: null, _activity: 0 }),
  ]),
  four: () => consoleState([
    station('VDI-ACCT-07', 0, { pan: -1, _activity: 0.3 }),
    station('VDI-DEV-02', 1, { pan: -0.5, level: -6, latencyMs: 41, _activity: 0.2 }),
    station('VDI-QA-11', 2, { pan: 0.5, level: -3, latencyMs: 38, _activity: 0.1 }),
    station('VDI-OPS-04', 3, { pan: 1, level: -3, latencyMs: 29, _activity: 0.45 }),
  ], { unknownPeers: ['bob-laptop'] }),
  connecting: () => consoleState([], {
    connection: { state: 'connecting', server: 'aoo.sonobus.net:10998', group: 'ops', passwordSaved: true, reason: null, retryInSec: null },
  }),
  empty: () => consoleState([]),
  failed: () => consoleState([], {
    connection: { state: 'failed', server: 'aoo.sonobus.net:10998', group: 'ops', passwordSaved: true, reason: 'Wrong group password', retryInSec: null },
  }),
};

// ---------------------------------------------------------------- agent

const INPUTS = [
  { node: 'loopback_sink.monitor', description: 'Monitor of System Loopback', hint: 'captures system audio', _activity: 0.7 },
  { node: 'alsa_output.platform.monitor', description: 'Monitor of Built-in Audio', hint: 'session audio device', _activity: 0.15 },
  { node: 'remote_mic', description: 'Remote Microphone', hint: 'redirected mic from the VDI client', _activity: 0.05 },
];
const OUTPUTS = [
  { node: 'crosspoint_mic', description: 'Crosspoint Virtual Mic', hint: 'apps on this VDI pick it as their mic' },
  { node: 'alsa_output.platform', description: 'Built-in Audio', hint: 'session speakers' },
  { node: 'loopback_sink', description: 'System Loopback', hint: 'would feed audio back to Consoles' },
];

function agentState(o = {}) {
  return {
    self: { name: 'VDI-ACCT-07', role: 'vdi' },
    connection: { state: 'connected', server: 'aoo.sonobus.net:10998', group: 'ops', attempt: null, retryInSec: null, error: null },
    sending: 'active',
    consoles: [{ name: 'maelo-mac', kind: 'mac', latencyMs: 32, talking: false }],
    input: { node: INPUTS[0].node, description: INPUTS[0].description, status: 'ok', silentForMin: null },
    output: { node: OUTPUTS[0].node, description: OUTPUTS[0].description, status: 'ok' },
    devices: {
      inputs: INPUTS.map(({ _activity, ...d }) => d),
      outputs: OUTPUTS.map(d => ({ ...d })),
    },
    configPath: '/home/maelo/.config/crosspoint/vdi.yaml',
    configError: null,
    ...o,
  };
}

export const AGENT_INPUT_ACTIVITY = Object.fromEntries(INPUTS.map(d => [d.node, d._activity]));

export const AGENT_SCENARIOS = {
  listening: () => agentState(),
  talking: () => agentState({
    consoles: [
      { name: 'maelo-mac', kind: 'mac', latencyMs: 32, talking: true },
      { name: 'maelo-web', kind: 'web', latencyMs: 58, talking: false },
    ],
  }),
  waiting: () => agentState({ consoles: [], sending: 'idle' }),
  paused: () => agentState({ sending: 'paused' }),
  reconnecting: () => agentState({
    consoles: [], sending: 'idle',
    connection: { state: 'reconnecting', server: 'aoo.sonobus.net:10998', group: 'ops', attempt: 3, retryInSec: 8, error: null },
  }),
  password: () => agentState({
    consoles: [], sending: 'idle',
    connection: { state: 'error', server: 'aoo.sonobus.net:10998', group: 'ops', attempt: null, retryInSec: null,
      error: { code: 'bad_password', message: 'The server rejected the password for group "ops".' } },
  }),
  nodevice: () => agentState({
    input: { node: 'loopback_sink.monitor', description: 'loopback_sink.monitor', status: 'missing', silentForMin: null },
    devices: {
      inputs: INPUTS.slice(1).map(({ _activity, ...d }) => d).concat([{ node: 'loopback.monitor', description: 'Monitor of Loopback', hint: 'captures system audio' }]),
      outputs: OUTPUTS.map(d => ({ ...d })),
    },
  }),
};
AGENT_INPUT_ACTIVITY['loopback.monitor'] = 0.7;
