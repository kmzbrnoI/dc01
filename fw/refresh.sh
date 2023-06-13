#!/bin/bash
while :
do
	echo -ne '\x37\xE2\x02\x11\x01' > $1 # turn DCC on
	echo 'sent'
	sleep 0.2
done
