#!/bin/bash

set -e

if [ "$PlatformFamily" == "OSX" ]; then

	COUNTER=0
	while [ $COUNTER -lt $NumNodeServers ]; do
		let IPAddress=COUNTER+2
		ifconfig lo0 alias "127.$LoopbackPrefix.$IPAddress.1/32"
		let COUNTER=COUNTER+1 
	done

elif  [ "$PlatformFamily" == "Linux" ]; then

	COUNTER=0
	while [ $COUNTER -lt $NumNodeServers ]; do
		let IPAddress=COUNTER+2
		let InterfaceAddress=$LoopbackPrefix*256+$COUNTER
		ifconfig lo:$InterfaceAddress 127.$LoopbackPrefix.$IPAddress.1 netmask 255.255.255.255
		let COUNTER=COUNTER+1 
	done

elif  [ "$PlatformFamily" == "Windows" ]; then

	echo Not implemented

else
	echo Unknown platform, cannot setup OS
	exit 1
fi

exit 0
