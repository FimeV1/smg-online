#!/bin/bash
# Join a friend's SMG Online server. Asks for the address; Enter reuses the last one.
cd "$(dirname "$0")" && bash ./start-galaxy.sh join
echo
read -n 1 -s -r -p "Press any key to close this window."
