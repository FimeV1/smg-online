#!/bin/bash
# Host: starts the server on this Mac (its own window) and one game for you.
# Needs Python 3. Forward the UDP port (default 5029) on your router.
cd "$(dirname "$0")" && bash ./start-galaxy.sh host
echo
read -n 1 -s -r -p "Press any key to close this window."
