// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "IpcBridge.h"

#include <chrono>
#include <thread>

int main() {
	sidecar::ipc::PipeServer server;
	if (!server.start(L"\\\\.\\pipe\\mumble-sidecar-ipc")) {
		return 1;
	}

	// The server runs on its own thread. The client ends this process when it no longer needs it.
	for (;;) {
		std::this_thread::sleep_for(std::chrono::hours(1));
	}
}
