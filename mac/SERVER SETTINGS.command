#!/bin/bash
# Opens the server settings (port, player limit, shared world...) in TextEdit.
cd "$(dirname "$0")" && open -e server/server-settings.ini
