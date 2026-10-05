#!/bin/bash
# Launcher for the desktop shortcut: finds the board's serial port, activates
# the venv if there is one, and drops into the interactive shell.
set -e
cd "$(dirname "$0")"

if [ -f venv/bin/activate ]; then
  source venv/bin/activate
elif [ -f "$HOME/venv/bin/activate" ]; then
  source "$HOME/venv/bin/activate"
fi

PORT=""
for candidate in /dev/ttyACM0 /dev/ttyACM1 /dev/ttyUSB0 /dev/ttyUSB1; do
  if [ -e "$candidate" ]; then
    PORT="$candidate"
    break
  fi
done

if [ -z "$PORT" ]; then
  echo "No reader board found on /dev/ttyACM* or /dev/ttyUSB*."
  echo "Check the USB cable, then run manually with --port."
  read -rp "Press Enter to close..."
  exit 1
fi

echo "Connecting to reader board on $PORT ..."
python3 reader_board.py --port "$PORT" shell

read -rp "Press Enter to close..."
