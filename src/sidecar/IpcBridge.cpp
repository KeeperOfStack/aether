// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "IpcBridge.h"

#include "IpcProtocol.h"

#include <windows.h>

#include <sddl.h>

#include <array>
#include <initializer_list>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace sidecar::ipc {

namespace {

	constexpr std::size_t EventQueueCapacity = 16;
	constexpr DWORD RetryDelayMs             = 1000;

	// Frames the sidecar sends are small and fixed in shape, so their buffer is too.
	constexpr std::size_t OutgoingPayloadMax = 8;

	// Grants access to the current user only. Remote clients are rejected separately.
	bool buildCurrentUserSecurity(SECURITY_ATTRIBUTES &attributes, PSECURITY_DESCRIPTOR &descriptor) {
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
			return false;
		}

		DWORD size = 0;
		GetTokenInformation(token, TokenUser, nullptr, 0, &size);
		std::vector< BYTE > buffer(size);
		const bool gotUser = GetTokenInformation(token, TokenUser, buffer.data(), size, &size) != FALSE;
		CloseHandle(token);
		if (!gotUser) {
			return false;
		}

		LPWSTR sidString = nullptr;
		const auto *user = reinterpret_cast< const TOKEN_USER * >(buffer.data());
		if (!ConvertSidToStringSidW(user->User.Sid, &sidString)) {
			return false;
		}

		std::wstring sddl = L"D:(A;;GA;;;";
		sddl += sidString;
		sddl += L")";
		LocalFree(sidString);

		if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor,
																  nullptr)) {
			return false;
		}

		attributes.nLength              = sizeof(attributes);
		attributes.lpSecurityDescriptor = descriptor;
		attributes.bInheritHandle       = FALSE;
		return true;
	}

} // namespace

struct PipeServer::Impl {
	HANDLE pipe         = INVALID_HANDLE_VALUE;
	HANDLE stopEvent    = nullptr;
	HANDLE wakeEvent    = nullptr;
	HANDLE connectEvent = nullptr;
	HANDLE readEvent    = nullptr;
	HANDLE writeEvent   = nullptr;

	std::thread thread;

	LatestValue< ShareCommand > command;
	DropOldestQueue< EventCode, EventQueueCapacity > events;

	// Only the I/O thread touches the fields below.
	OVERLAPPED readOverlapped{};
	bool readPending = false;
	std::array< std::uint8_t, LengthPrefixSize + MaxPayloadSize > rx{};
	std::size_t rxFilled = 0;
	std::size_t rxNeeded = LengthPrefixSize;
	bool haveHeader      = false;
	bool handshakeDone   = false;

	void closeHandles() {
		for (HANDLE *handle : { &pipe, &stopEvent, &wakeEvent, &connectEvent, &readEvent, &writeEvent }) {
			if (*handle != nullptr && *handle != INVALID_HANDLE_VALUE) {
				CloseHandle(*handle);
			}
			*handle = nullptr;
		}
		pipe = INVALID_HANDLE_VALUE;
	}

	bool stopRequested() const { return WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0; }

	// Waits for a client, or for stop. Returns true once a client is connected.
	bool waitForClient() {
		OVERLAPPED overlapped{};
		overlapped.hEvent = connectEvent;
		ResetEvent(connectEvent);

		if (ConnectNamedPipe(pipe, &overlapped)) {
			return true;
		}

		const DWORD error = GetLastError();
		if (error == ERROR_PIPE_CONNECTED) {
			return true;
		}
		if (error != ERROR_IO_PENDING) {
			return false;
		}

		const std::array< HANDLE, 2 > handles = { connectEvent, stopEvent };
		if (WaitForMultipleObjects(2, handles.data(), FALSE, INFINITE) != WAIT_OBJECT_0) {
			CancelIoEx(pipe, &overlapped);
			DWORD ignored = 0;
			GetOverlappedResult(pipe, &overlapped, &ignored, TRUE);
			return false;
		}

		DWORD transferred = 0;
		return GetOverlappedResult(pipe, &overlapped, &transferred, FALSE) != FALSE;
	}

	bool beginRead() {
		ResetEvent(readEvent);
		readOverlapped        = {};
		readOverlapped.hEvent = readEvent;

		const auto want = static_cast< DWORD >(rxNeeded - rxFilled);
		if (!ReadFile(pipe, rx.data() + rxFilled, want, nullptr, &readOverlapped)) {
			if (GetLastError() != ERROR_IO_PENDING) {
				return false;
			}
		}

		readPending = true;
		return true;
	}

	// Returns false when the connection must be closed.
	bool finishRead() {
		readPending = false;

		DWORD transferred = 0;
		if (!GetOverlappedResult(pipe, &readOverlapped, &transferred, FALSE) || transferred == 0) {
			return false;
		}

		rxFilled += transferred;
		if (rxFilled < rxNeeded) {
			return true;
		}

		if (!haveHeader) {
			const std::uint32_t length = decodeLength(rx.data());
			if (!isValidPayloadLength(length)) {
				return false;
			}

			haveHeader = true;
			rxFilled   = 0;
			rxNeeded   = length;
			return true;
		}

		const bool accepted = handleFrame(rx.data(), rxNeeded);

		haveHeader = false;
		rxFilled   = 0;
		rxNeeded   = LengthPrefixSize;
		return accepted;
	}

	bool handleFrame(const std::uint8_t *payload, std::size_t length) {
		if (!isKnownMessageType(payload[0])) {
			return false;
		}

		const auto type = static_cast< MessageType >(payload[0]);

		if (!handshakeDone) {
			if (type != MessageType::Hello || length != 3) {
				return false;
			}

			const auto version = static_cast< std::uint16_t >(payload[1] | (payload[2] << 8));
			if (version != ProtocolVersion) {
				return false;
			}

			const std::array< std::uint8_t, 3 > welcome = {
				static_cast< std::uint8_t >(MessageType::Welcome),
				static_cast< std::uint8_t >(ProtocolVersion & 0xFFu),
				static_cast< std::uint8_t >(ProtocolVersion >> 8),
			};
			if (!writeFrame(welcome.data(), welcome.size())) {
				return false;
			}

			handshakeDone = true;
			events.push(EventCode::Ready);
			return flushEvents();
		}

		if (type != MessageType::ShareState || length != 2) {
			return false;
		}

		switch (payload[1]) {
			case static_cast< std::uint8_t >(ShareCommand::Off):
				command.set(ShareCommand::Off);
				return true;
			case static_cast< std::uint8_t >(ShareCommand::On):
				command.set(ShareCommand::On);
				return true;
			default:
				return false;
		}
	}

	bool writeFrame(const std::uint8_t *payload, std::size_t length) {
		if (length == 0 || length > OutgoingPayloadMax) {
			return false;
		}

		std::array< std::uint8_t, LengthPrefixSize + OutgoingPayloadMax > frame{};
		encodeLength(static_cast< std::uint32_t >(length), frame.data());
		for (std::size_t i = 0; i < length; ++i) {
			frame[LengthPrefixSize + i] = payload[i];
		}

		const DWORD total = static_cast< DWORD >(LengthPrefixSize + length);

		OVERLAPPED overlapped{};
		overlapped.hEvent = writeEvent;
		ResetEvent(writeEvent);

		if (!WriteFile(pipe, frame.data(), total, nullptr, &overlapped)) {
			if (GetLastError() != ERROR_IO_PENDING) {
				return false;
			}
		}

		// Writes are small and happen only on this thread, so waiting here cannot stall the voice path.
		DWORD transferred = 0;
		return GetOverlappedResult(pipe, &overlapped, &transferred, TRUE) != FALSE && transferred == total;
	}

	bool flushEvents() {
		EventCode code{};
		while (events.pop(code)) {
			const std::array< std::uint8_t, 2 > payload = {
				static_cast< std::uint8_t >(MessageType::Event),
				static_cast< std::uint8_t >(code),
			};
			if (!writeFrame(payload.data(), payload.size())) {
				return false;
			}
		}
		return true;
	}

	void resetConnectionState() {
		readPending   = false;
		haveHeader    = false;
		handshakeDone = false;
		rxFilled      = 0;
		rxNeeded      = LengthPrefixSize;
	}

	void serve() {
		const std::array< HANDLE, 3 > handles = { readEvent, wakeEvent, stopEvent };

		while (!stopRequested()) {
			if (!readPending && !beginRead()) {
				break;
			}

			const DWORD result = WaitForMultipleObjects(3, handles.data(), FALSE, INFINITE);

			if (result == WAIT_OBJECT_0 + 2) {
				break;
			}

			if (result == WAIT_OBJECT_0 && !finishRead()) {
				break;
			}

			if (result == WAIT_OBJECT_0 + 1) {
				ResetEvent(wakeEvent);
				if (handshakeDone && !flushEvents()) {
					break;
				}
			}
		}

		if (readPending) {
			CancelIoEx(pipe, &readOverlapped);
			DWORD ignored = 0;
			GetOverlappedResult(pipe, &readOverlapped, &ignored, TRUE);
			readPending = false;
		}
	}

	void run() {
		while (!stopRequested()) {
			if (!waitForClient()) {
				// Back off so a persistent failure cannot spin the thread.
				WaitForSingleObject(stopEvent, RetryDelayMs);
				continue;
			}

			resetConnectionState();
			serve();
			DisconnectNamedPipe(pipe);
		}
	}
};

PipeServer::PipeServer() : m_impl(std::make_unique< Impl >()) {
}

PipeServer::~PipeServer() {
	stop();
}

bool PipeServer::start(const wchar_t *pipeName) {
	if (!m_impl || m_impl->thread.joinable()) {
		return false;
	}

	SECURITY_ATTRIBUTES attributes{};
	PSECURITY_DESCRIPTOR descriptor = nullptr;
	if (!buildCurrentUserSecurity(attributes, descriptor)) {
		return false;
	}

	Impl &impl = *m_impl;

	impl.pipe = CreateNamedPipeW(pipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
								 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
								 static_cast< DWORD >(LengthPrefixSize + MaxPayloadSize),
								 static_cast< DWORD >(LengthPrefixSize + MaxPayloadSize), 0, &attributes);
	LocalFree(descriptor);

	impl.stopEvent    = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	impl.wakeEvent    = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	impl.connectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	impl.readEvent    = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	impl.writeEvent   = CreateEventW(nullptr, TRUE, FALSE, nullptr);

	const bool ready = impl.pipe != INVALID_HANDLE_VALUE && impl.stopEvent && impl.wakeEvent && impl.connectEvent
					   && impl.readEvent && impl.writeEvent;
	if (!ready) {
		impl.closeHandles();
		return false;
	}

	try {
		impl.thread = std::thread([&impl]() { impl.run(); });
	} catch (const std::system_error &) {
		impl.closeHandles();
		return false;
	}

	return true;
}

void PipeServer::stop() {
	if (!m_impl || !m_impl->thread.joinable()) {
		return;
	}

	Impl &impl = *m_impl;
	SetEvent(impl.stopEvent);
	if (impl.pipe != INVALID_HANDLE_VALUE) {
		CancelIoEx(impl.pipe, nullptr);
	}
	impl.thread.join();
	impl.closeHandles();
}

bool PipeServer::takeShareCommand(ShareCommand &command, std::size_t &seenGeneration) {
	return m_impl->command.takeIfChanged(command, seenGeneration);
}

bool PipeServer::postEvent(EventCode code) {
	const bool droppedOldest = m_impl->events.push(code);
	if (m_impl->wakeEvent) {
		SetEvent(m_impl->wakeEvent);
	}
	return !droppedOldest;
}

std::size_t PipeServer::droppedEvents() const {
	return m_impl->events.droppedCount();
}

} // namespace sidecar::ipc
