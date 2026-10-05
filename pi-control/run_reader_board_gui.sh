#!/bin/bash
# Launcher for the desktop shortcut: activates the venv if there is one,
# then opens the Reader Board control panel GUI.
set -e
cd "$(dirname "$0")"

if [ -f venv/bin/activate ]; then
  source venv/bin/activate
elif [ -f "$HOME/venv/bin/activate" ]; then
  source "$HOME/venv/bin/activate"
fi

exec python3 reader_board_gui.py
