// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include "IpcProtocol.h"
#include "IpcQueue.h"

using namespace sidecar::ipc;

class TestSidecarIpc : public QObject {
	Q_OBJECT
private slots:
	void lengthIsLittleEndian();
	void lengthRoundTrip();
	void payloadLengthLimits();
	void knownMessageTypes();
	void dropOldestKeepsOrder();
	void dropOldestCountsDrops();
	void latestValueReportsChanges();
	void backoffDoublesAndCaps();
};

void TestSidecarIpc::lengthIsLittleEndian() {
	std::uint8_t bytes[LengthPrefixSize] = {};
	encodeLength(0x04030201u, bytes);

	QCOMPARE(bytes[0], std::uint8_t(0x01));
	QCOMPARE(bytes[1], std::uint8_t(0x02));
	QCOMPARE(bytes[2], std::uint8_t(0x03));
	QCOMPARE(bytes[3], std::uint8_t(0x04));
}

void TestSidecarIpc::lengthRoundTrip() {
	const std::uint32_t values[] = { 0u, 1u, 255u, 256u, 4096u, 0x7FFFFFFFu, 0xFFFFFFFFu };
	for (const std::uint32_t value : values) {
		std::uint8_t bytes[LengthPrefixSize] = {};
		encodeLength(value, bytes);
		QCOMPARE(decodeLength(bytes), value);
	}
}

void TestSidecarIpc::payloadLengthLimits() {
	QVERIFY(!isValidPayloadLength(0));
	QVERIFY(isValidPayloadLength(1));
	QVERIFY(isValidPayloadLength(MaxPayloadSize));
	QVERIFY(!isValidPayloadLength(MaxPayloadSize + 1));
	QVERIFY(!isValidPayloadLength(0xFFFFFFFFu));
}

void TestSidecarIpc::knownMessageTypes() {
	QVERIFY(isKnownMessageType(static_cast< std::uint8_t >(MessageType::Hello)));
	QVERIFY(isKnownMessageType(static_cast< std::uint8_t >(MessageType::Welcome)));
	QVERIFY(isKnownMessageType(static_cast< std::uint8_t >(MessageType::ShareState)));
	QVERIFY(isKnownMessageType(static_cast< std::uint8_t >(MessageType::Event)));
	QVERIFY(!isKnownMessageType(0));
	QVERIFY(!isKnownMessageType(5));
	QVERIFY(!isKnownMessageType(0xFF));
}

void TestSidecarIpc::dropOldestKeepsOrder() {
	DropOldestQueue< int, 3 > queue;
	QVERIFY(!queue.push(1));
	QVERIFY(!queue.push(2));
	QVERIFY(!queue.push(3));
	QVERIFY(queue.push(4));

	int value = 0;
	QVERIFY(queue.pop(value));
	QCOMPARE(value, 2);
	QVERIFY(queue.pop(value));
	QCOMPARE(value, 3);
	QVERIFY(queue.pop(value));
	QCOMPARE(value, 4);
	QVERIFY(!queue.pop(value));
}

void TestSidecarIpc::dropOldestCountsDrops() {
	DropOldestQueue< int, 2 > queue;
	for (int i = 0; i < 10; ++i) {
		queue.push(i);
	}

	QCOMPARE(queue.droppedCount(), std::size_t(8));

	int value = 0;
	QVERIFY(queue.pop(value));
	QCOMPARE(value, 8);
	QVERIFY(queue.pop(value));
	QCOMPARE(value, 9);
}

void TestSidecarIpc::latestValueReportsChanges() {
	LatestValue< ShareCommand > latest;
	std::size_t seen     = 0;
	ShareCommand command = ShareCommand::Off;

	QVERIFY(!latest.takeIfChanged(command, seen));

	latest.set(ShareCommand::On);
	latest.set(ShareCommand::Off);
	latest.set(ShareCommand::On);

	QVERIFY(latest.takeIfChanged(command, seen));
	QCOMPARE(command, ShareCommand::On);
	QVERIFY(!latest.takeIfChanged(command, seen));
}

void TestSidecarIpc::backoffDoublesAndCaps() {
	int delay = InitialBackoffMs;
	QCOMPARE(delay, 1000);

	delay = nextBackoffMs(delay);
	QCOMPARE(delay, 2000);
	delay = nextBackoffMs(delay);
	QCOMPARE(delay, 4000);
	delay = nextBackoffMs(delay);
	QCOMPARE(delay, 8000);
	delay = nextBackoffMs(delay);
	QCOMPARE(delay, 16000);
	delay = nextBackoffMs(delay);
	QCOMPARE(delay, MaxBackoffMs);

	// Once at the cap, further failures keep the delay there.
	delay = nextBackoffMs(delay);
	QCOMPARE(delay, MaxBackoffMs);
}

QTEST_GUILESS_MAIN(TestSidecarIpc)
#include "TestSidecarIpc.moc"
