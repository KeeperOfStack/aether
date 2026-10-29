# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(TestSidecarIpc TestSidecarIpc.cpp ${CMAKE_SOURCE_DIR}/src/sidecar/IpcProtocol.cpp)

set_target_properties(TestSidecarIpc PROPERTIES AUTOMOC ON)

target_include_directories(TestSidecarIpc PRIVATE ${CMAKE_SOURCE_DIR}/src/sidecar)

target_link_libraries(TestSidecarIpc PRIVATE Qt6::Test)

add_test(NAME TestSidecarIpc COMMAND $<TARGET_FILE:TestSidecarIpc>)
