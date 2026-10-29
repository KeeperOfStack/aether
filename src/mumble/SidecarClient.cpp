// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "SidecarClient.h"

#include "Log.h"
#include "Global.h"

#include "../sidecar/IpcProtocol.h"

#include <QtCore/QDebug>
#include <QtNetwork/QLocalSocket>

#include <algorithm>
#include <cstdint>

using sidecar::ipc::EventCode;
using sidecar::ipc::LengthPrefixSize;
using sidecar::ipc::MessageType;
using sidecar::ipc::ProtocolVersion;
using sidecar::ipc::ShareCommand;

namespace {

// Qt adds the \\.\pipe\ prefix on Windows.
const char *const PipeName = "mumble-sidecar-ipc";

constexpr int InitialBackoffMs = 1000;
constexpr int MaxBackoffMs     = 30000;

} // namespace

SidecarClient::SidecarClient(QObject *parent)
	: QObject(parent), m_socket(new QLocalSocket(this)), m_backoffMs(InitialBackoffMs), m_handshakeDone(false),
	  m_ready(false) {
	m_reconnectTimer.setSingleShot(true);

	connect(&m_reconnectTimer, &QTimer::timeout, this, &SidecarClient::connectToSidecar);
	connect(m_socket, &QLocalSocket::connected, this, &SidecarClient::onConnected);
	connect(m_socket, &QLocalSocket::disconnected, this, &SidecarClient::onDisconnected);
	connect(m_socket, &QLocalSocket::errorOccurred, this, &SidecarClient::onDisconnected);
	connect(m_socket, &QLocalSocket::readyRead, this, &SidecarClient::onReadyRead);

	connectToSidecar();
}

bool SidecarClient::isReady() const {
	return m_ready;
}

void SidecarClient::setShareState(bool on) {
	if (!m_ready) {
		return;
	}

	const auto command   = on ? ShareCommand::On : ShareCommand::Off;
	const char payload[] = { static_cast< char >(MessageType::ShareState), static_cast< char >(command) };
	sendFrame(QByteArray(payload, 2));
}

void SidecarClient::connectToSidecar() {
	if (m_socket->state() != QLocalSocket::UnconnectedState) {
		return;
	}

	m_socket->connectToServer(QLatin1String(PipeName));
}

void SidecarClient::onConnected() {
	const char hello[] = { static_cast< char >(MessageType::Hello), static_cast< char >(ProtocolVersion & 0xFF),
						   static_cast< char >(ProtocolVersion >> 8) };
	sendFrame(QByteArray(hello, 3));
}

void SidecarClient::onDisconnected() {
	m_rx.clear();
	m_handshakeDone = false;
	setReady(false);
	scheduleReconnect();
}

void SidecarClient::onReadyRead() {
	m_rx.append(m_socket->readAll());

	while (m_rx.size() >= static_cast< int >(LengthPrefixSize)) {
		const auto *data           = reinterpret_cast< const std::uint8_t * >(m_rx.constData());
		const std::uint32_t length = sidecar::ipc::decodeLength(data);
		if (!sidecar::ipc::isValidPayloadLength(length)) {
			protocolError();
			return;
		}

		const int frameSize = static_cast< int >(LengthPrefixSize + length);
		if (m_rx.size() < frameSize) {
			return;
		}

		const QByteArray payload = m_rx.mid(static_cast< int >(LengthPrefixSize), static_cast< int >(length));
		m_rx.remove(0, frameSize);

		if (!handleFrame(payload)) {
			protocolError();
			return;
		}
	}
}

bool SidecarClient::handleFrame(const QByteArray &payload) {
	// The length check in onReadyRead guarantees at least one byte.
	const auto type = static_cast< std::uint8_t >(payload[0]);
	if (!sidecar::ipc::isKnownMessageType(type)) {
		return false;
	}

	if (!m_handshakeDone) {
		const bool welcome = type == static_cast< std::uint8_t >(MessageType::Welcome) && payload.size() == 3
							 && static_cast< std::uint8_t >(payload[1]) == (ProtocolVersion & 0xFF)
							 && static_cast< std::uint8_t >(payload[2]) == (ProtocolVersion >> 8);
		if (!welcome) {
			return false;
		}

		m_handshakeDone = true;
		return true;
	}

	if (type == static_cast< std::uint8_t >(MessageType::Event)) {
		// Unknown event codes are ignored so that a newer sidecar does not break this client.
		if (payload.size() == 2
			&& static_cast< std::uint8_t >(payload[1]) == static_cast< std::uint8_t >(EventCode::Ready)) {
			m_backoffMs = InitialBackoffMs;
			setReady(true);
		}
		return true;
	}

	return false;
}

void SidecarClient::protocolError() {
	qWarning() << "SidecarClient: protocol error, dropping the connection to the sidecar";
	m_socket->abort();
	onDisconnected();
}

void SidecarClient::sendFrame(const QByteArray &payload) {
	std::uint8_t header[LengthPrefixSize];
	sidecar::ipc::encodeLength(static_cast< std::uint32_t >(payload.size()), header);

	QByteArray frame(reinterpret_cast< const char * >(header), static_cast< int >(LengthPrefixSize));
	frame.append(payload);
	m_socket->write(frame);
}

void SidecarClient::setReady(bool ready) {
	if (m_ready == ready) {
		return;
	}

	m_ready = ready;
	emit readyChanged(ready);

	if (ready) {
		Global::get().l->log(Log::Information, tr("Screen-share helper connected."));
	} else {
		Global::get().l->log(Log::Warning, tr("Screen-share helper disconnected."));
	}
}

void SidecarClient::scheduleReconnect() {
	// Several signals can report one failure. Only the first one schedules a retry.
	if (m_reconnectTimer.isActive()) {
		return;
	}

	m_reconnectTimer.start(m_backoffMs);
	m_backoffMs = std::min(m_backoffMs * 2, MaxBackoffMs);
}
