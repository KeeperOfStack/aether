# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

# The screen-share sidecar runs as its own process so that capture, encode and
# network faults cannot affect the voice client. It must not link against the
# voice client's shared library or any WebRTC, capture or SFU library.
add_executable(mumble-screenshare
	"sidecar_main.cpp"
)
