// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SIDECAR_IPCPROTOCOL_H_
#define MUMBLE_SIDECAR_IPCPROTOCOL_H_

#include <cstddef>
#include <cstdint>

namespace sidecar::ipc {

constexpr std::uint16_t ProtocolVersion = 1;

constexpr std::size_t LengthPrefixSize = 4;
constexpr std::uint32_t MaxPayloadSize = 4096;

enum class MessageType : std::uint8_t {
	Hello      = 1,
	Welcome    = 2,
	ShareState = 3,
	Event      = 4,
};

enum class ShareCommand : std::uint8_t {
	Off = 0,
	On  = 1,
};

enum class EventCode : std::uint8_t {
	Ready = 1,
};

bool isKnownMessageType(std::uint8_t type);

// A declared payload length is only trusted when it is non-zero and within the limit.
bool isValidPayloadLength(std::uint32_t length);

// Length prefixes are little-endian and fixed-width, so decoding never depends on the host.
void encodeLength(std::uint32_t length, std::uint8_t *out);
std::uint32_t decodeLength(const std::uint8_t *in);

} // namespace sidecar::ipc

#endif
