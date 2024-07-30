#!/usr/bin/env pwsh

#show usage
./../build/uvperf

#show list of all connected usb
./check_usb.ps1

#show descriptors and architecture of specific usb
python3 "../src/find_usb.py"

#run uvperf
./../build/uvperf -v 0x1004 -p 0x61A1 -i 0 -a 0 -e 0x81 -t 1000 -l 30000 -S
