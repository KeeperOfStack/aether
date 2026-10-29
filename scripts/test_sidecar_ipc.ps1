# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

# Drives mumble-screenshare.exe over its named pipe and checks the wire protocol
# in src/sidecar/IpcProtocol.h and src/sidecar/IpcBridge.cpp.
#
# Frame: 4-byte little-endian payload length, then the payload.
# Payloads: Hello(1, version LE16), Welcome(2, version LE16), ShareState(3, 0|1),
#           Event(4, code), with Ready = 1.

param(
	[Parameter(Mandatory = $true)]
	[string]$SidecarPath,

	[string]$PipeName = 'mumble-sidecar-ipc',

	[int]$ConnectTimeoutMs = 5000
)

$ErrorActionPreference = 'Stop'
$script:failures = 0

function Write-Check([bool]$ok, [string]$name) {
	if ($ok) {
		Write-Host "PASS  $name"
	} else {
		Write-Host "FAIL  $name"
		$script:failures++
	}
}

function Connect-Sidecar {
	$pipe = New-Object System.IO.Pipes.NamedPipeClientStream('.', $PipeName, [System.IO.Pipes.PipeDirection]::InOut)
	$pipe.Connect($ConnectTimeoutMs)
	return $pipe
}

function Send-Bytes($pipe, [byte[]]$bytes) {
	$pipe.Write($bytes, 0, $bytes.Length)
	$pipe.Flush()
}

function Send-Frame($pipe, [byte[]]$payload) {
	# BitConverter is little-endian on the x64 machines this runs on.
	$length = [BitConverter]::GetBytes([uint32]$payload.Length)
	Send-Bytes $pipe ($length + $payload)
}

function Read-Exact($pipe, [int]$count) {
	$buffer = New-Object byte[] $count
	$offset = 0
	while ($offset -lt $count) {
		$read = $pipe.Read($buffer, $offset, $count - $offset)
		if ($read -eq 0) {
			return $null
		}
		$offset += $read
	}
	return ,$buffer
}

function Read-Frame($pipe) {
	try {
		$header = Read-Exact $pipe 4
		if ($null -eq $header) {
			return $null
		}
		$length = [BitConverter]::ToUInt32($header, 0)
		if ($length -eq 0 -or $length -gt 4096) {
			return $null
		}
		return ,(Read-Exact $pipe $length)
	} catch [System.IO.IOException] {
		return $null
	}
}

$sidecar = Start-Process -FilePath $SidecarPath -PassThru -WindowStyle Hidden
Write-Host "Started sidecar pid $($sidecar.Id): $SidecarPath"

try {
	# 1. Handshake: Hello (version 1) gets Welcome, then a Ready event.
	$client = Connect-Sidecar
	Send-Frame $client ([byte[]](1, 1, 0))
	$welcome = Read-Frame $client
	Write-Check ($null -ne $welcome -and $welcome.Length -eq 3 -and $welcome[0] -eq 2 -and $welcome[1] -eq 1 -and $welcome[2] -eq 0) 'handshake: Welcome received'
	$ready = Read-Frame $client
	Write-Check ($null -ne $ready -and $ready.Length -eq 2 -and $ready[0] -eq 4 -and $ready[1] -eq 1) 'handshake: Ready event received'

	# 2. ShareState On and Off are accepted without closing the pipe.
	Send-Frame $client ([byte[]](3, 1))
	Send-Frame $client ([byte[]](3, 0))
	Start-Sleep -Milliseconds 300
	Write-Check (-not $sidecar.HasExited) 'share commands: sidecar still running'
	$client.Dispose()

	# 3. A new client can connect after the previous one left.
	$client = Connect-Sidecar
	Send-Frame $client ([byte[]](1, 1, 0))
	$welcome = Read-Frame $client
	Write-Check ($null -ne $welcome -and $welcome[0] -eq 2) 'reconnect: Welcome after previous client left'
	$client.Dispose()

	# 4. Wrong protocol version: the sidecar must close the pipe without Welcome.
	$client = Connect-Sidecar
	Send-Frame $client ([byte[]](1, 2, 0))
	$reply = Read-Frame $client
	Write-Check ($null -eq $reply) 'bad version: pipe closed without Welcome'
	$client.Dispose()

	# 5. Oversized length prefix (5000 > 4096): the sidecar must close the pipe.
	$client = Connect-Sidecar
	Send-Bytes $client ([byte[]](0x88, 0x13, 0, 0))
	$reply = Read-Frame $client
	Write-Check ($null -eq $reply) 'oversized frame: pipe closed'
	$client.Dispose()

	# 6. The sidecar survives both bad clients and still accepts a good one.
	Start-Sleep -Milliseconds 300
	Write-Check (-not $sidecar.HasExited) 'after bad clients: sidecar still running'
	$client = Connect-Sidecar
	Send-Frame $client ([byte[]](1, 1, 0))
	$welcome = Read-Frame $client
	Write-Check ($null -ne $welcome -and $welcome[0] -eq 2) 'after bad clients: good client gets Welcome'
	$client.Dispose()
} catch {
	Write-Host "FAIL  unexpected error: $($_.Exception.Message)"
	$script:failures++
} finally {
	if (-not $sidecar.HasExited) {
		Stop-Process -Id $sidecar.Id -Force
	}
}

Write-Host ''
Write-Host "Failures: $script:failures"
if ($script:failures -gt 0) {
	exit 1
}
