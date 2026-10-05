#!/bin/bash
# Set or change where Dolphin and your game file are (remembered for your user).
cd "$(dirname "$0")" && bash ./start-galaxy.sh paths
echo
read -n 1 -s -r -p "Press any key to close this window."
