// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "IpcProtocol.h"

namespace sidecar::ipc {

bool isKnownMessageType(std::uint8_t type) {
	switch (static_cast< MessageType >(type)) {
		case MessageType::Hello:
		case MessageType::Welcome:
		case MessageType::ShareState:
		case MessageType::Event:
			return true;
	}
	return false;
}

bool isValidPayloadLength(std::uint32_t length) {
	return length > 0 && length <= MaxPayloadSize;
}

void encodeLength(std::uint32_t length, std::uint8_t *out) {
	out[0] = static_cast< std::uint8_t >(length & 0xFFu);
	out[1] = static_cast< std::uint8_t >((length >> 8) & 0xFFu);
	out[2] = static_cast< std::uint8_t >((length >> 16) & 0xFFu);
	out[3] = static_cast< std::uint8_t >((length >> 24) & 0xFFu);
}

std::uint32_t decodeLength(const std::uint8_t *in) {
	return static_cast< std::uint32_t >(in[0]) | (static_cast< std::uint32_t >(in[1]) << 8)
		   | (static_cast< std::uint32_t >(in[2]) << 16) | (static_cast< std::uint32_t >(in[3]) << 24);
}

} // namespace sidecar::ipc
