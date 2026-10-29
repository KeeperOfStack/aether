// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_SIDECARCLIENT_H_
#define MUMBLE_MUMBLE_SIDECARCLIENT_H_

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QTimer>

class QLocalSocket;

// Client end of the screen-share sidecar's named pipe. It runs on the GUI thread
// and never blocks: all I/O is asynchronous. A missing or failing sidecar only
// affects this object, so voice is never affected.
class SidecarClient : public QObject {
	Q_OBJECT

public:
	explicit SidecarClient(QObject *parent = nullptr);

	bool isReady() const;
	// Sends a ShareState command. Does nothing unless the sidecar is ready.
	void setShareState(bool on);

signals:
	void readyChanged(bool ready);

private:
	void connectToSidecar();
	void onConnected();
	void onDisconnected();
	void onReadyRead();
	bool handleFrame(const QByteArray &payload);
	void protocolError();
	void sendFrame(const QByteArray &payload);
	void setReady(bool ready);
	void scheduleReconnect();

	QLocalSocket *m_socket;
	QTimer m_reconnectTimer;
	int m_backoffMs;
	bool m_handshakeDone;
	bool m_ready;
	QByteArray m_rx;
};

#endif
