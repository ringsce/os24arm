SAY "OS/2 Warp ARM64 Booting..."

CALL START_SERVICE "console"
CALL START_SERVICE "network"

SAY "Starting shell..."
EXEC "/bin/shell.rex"
