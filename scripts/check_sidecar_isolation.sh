#!/bin/sh
# Fails if the voice client includes WebRTC, capture or SFU headers. Screen
# sharing must stay in src/sidecar/ so that it cannot affect voice.

set -eu

cd "$(dirname "$0")/.."

pattern='#include.*(webrtc|livekit|mediasoup|dxgi|d3d11|mfapi|graphics\.capture)'

if grep -rEin "$pattern" src/mumble --include='*.cpp' --include='*.h' --include='*.mm'; then
	echo "error: screen-share dependencies included in src/mumble (see above)" >&2
	exit 1
fi

echo "sidecar isolation: ok"
