// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SIDECAR_IPCQUEUE_H_
#define MUMBLE_SIDECAR_IPCQUEUE_H_

#include <array>
#include <cstddef>
#include <mutex>

namespace sidecar::ipc {

// Fixed-capacity FIFO that never allocates. When full, the oldest element is
// dropped and counted. Used for informational events, where losing an old one is safe.
template< typename T, std::size_t Capacity > class DropOldestQueue {
	static_assert(Capacity > 0, "queue needs at least one slot");

public:
	// Returns true if an older element had to be dropped to make room.
	bool push(const T &value) {
		std::lock_guard< std::mutex > lock(m_mutex);

		bool dropped = false;
		if (m_count == Capacity) {
			m_head = (m_head + 1) % Capacity;
			--m_count;
			++m_dropped;
			dropped = true;
		}

		m_items[(m_head + m_count) % Capacity] = value;
		++m_count;
		return dropped;
	}

	bool pop(T &out) {
		std::lock_guard< std::mutex > lock(m_mutex);

		if (m_count == 0) {
			return false;
		}

		out    = m_items[m_head];
		m_head = (m_head + 1) % Capacity;
		--m_count;
		return true;
	}

	std::size_t droppedCount() const {
		std::lock_guard< std::mutex > lock(m_mutex);
		return m_dropped;
	}

private:
	mutable std::mutex m_mutex;
	std::array< T, Capacity > m_items{};
	std::size_t m_head    = 0;
	std::size_t m_count   = 0;
	std::size_t m_dropped = 0;
};

// Holds only the most recent value. Intended for desired state, where an
// older value is never worth keeping once a newer one has been set.
template< typename T > class LatestValue {
public:
	void set(const T &value) {
		std::lock_guard< std::mutex > lock(m_mutex);
		m_value = value;
		++m_generation;
	}

	// Copies the value out if it changed since the caller last took it.
	bool takeIfChanged(T &out, std::size_t &seenGeneration) {
		std::lock_guard< std::mutex > lock(m_mutex);

		if (m_generation == seenGeneration) {
			return false;
		}

		out            = m_value;
		seenGeneration = m_generation;
		return true;
	}

private:
	mutable std::mutex m_mutex;
	T m_value{};
	std::size_t m_generation = 0;
};

} // namespace sidecar::ipc

#endif
