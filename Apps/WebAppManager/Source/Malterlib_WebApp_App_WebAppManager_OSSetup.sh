#!/bin/bash
# Copyright © Unbroken AB
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set -e

if [ "$PlatformFamily" == "macOS" ]; then

	COUNTER=0
	while [ $COUNTER -lt $NumNodeServers ]; do
		let IPAddress=COUNTER+2 1
		ifconfig lo0 alias "127.$LoopbackPrefix.$IPAddress.1/32"
		let COUNTER=COUNTER+1 1
	done

elif  [ "$PlatformFamily" == "Linux" ]; then

	COUNTER=0
	while [ $COUNTER -lt $NumNodeServers ]; do
		let IPAddress=COUNTER+2 1
		if which ip; then
			ip -4 addr replace 127.$LoopbackPrefix.$IPAddress.1/32 dev lo
		else
			let InterfaceAddress=$LoopbackPrefix*256+$COUNTER 1
			ifconfig lo:$InterfaceAddress 127.$LoopbackPrefix.$IPAddress.1 netmask 255.255.255.255
		fi
		let COUNTER=COUNTER+1 1
	done

elif  [ "$PlatformFamily" == "Windows" ]; then

	echo Not implemented

else
	echo Unknown platform, cannot setup OS
	exit 1
fi

exit 0
