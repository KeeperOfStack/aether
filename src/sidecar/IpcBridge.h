// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SIDECAR_IPCBRIDGE_H_
#define MUMBLE_SIDECAR_IPCBRIDGE_H_

#include "IpcQueue.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace sidecar::ipc {

enum class ShareCommand : std::uint8_t {
	Off = 0,
	On  = 1,
};

enum class EventCode : std::uint8_t {
	Ready = 1,
};

// Serves one client at a time over a named pipe. All pipe I/O runs on a
// thread owned by this object. Other threads only call the public methods,
// which never touch the pipe.
class PipeServer {
public:
	PipeServer();
	~PipeServer();

	PipeServer(const PipeServer &) = delete;
	PipeServer &operator=(const PipeServer &) = delete;

	// Returns false if the pipe could not be created. The server keeps running
	// until stop() or destruction.
	bool start(const wchar_t *pipeName);
	void stop();

	// Takes the newest command only if it changed since the caller last saw it.
	bool takeShareCommand(ShareCommand &command, std::size_t &seenGeneration);

	// Never blocks. Returns false if the event had to be dropped because the queue is full.
	bool postEvent(EventCode code);

	std::size_t droppedEvents() const;

private:
	struct Impl;
	std::unique_ptr< Impl > m_impl;
};

} // namespace sidecar::ipc

#endif
