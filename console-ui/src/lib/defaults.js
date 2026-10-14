// Defaults shared with the engine. Keep in step with the engine's
// DEFAULT_SERVER_HOST / DEFAULT_SERVER_PORT (sonobus/Source/SonobusPluginProcessor.h):
// the settings sheet seeds this so a fresh install only needs a group (spec §3.7),
// and the engine falls back to the same host when connection.connect is called
// without a server (api §5.1).
export const DEFAULT_SERVER = 'aoo.sonobus.net:10998';
