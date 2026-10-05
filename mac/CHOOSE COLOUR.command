#!/bin/bash
# Choose your player colour (other players see you in it).
cd "$(dirname "$0")" && bash ./start-galaxy.sh colour
echo
read -n 1 -s -r -p "Press any key to close this window."
