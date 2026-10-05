#!/usr/bin/env python3
"""
Reader Board remote control.

Drives the readerBoard.ino serial menu over USB from a Raspberry Pi (or any
machine with pyserial). The Arduino sketch exposes a line-based, stateful
menu at 9600 baud (see readerBoard/readerBoard.ino -> handleSerial()) rather
than a flat command protocol, so this script drives that menu the same way a
human typing into a serial terminal would: send "cancel" to reset to a known
state, then walk the same number choices printed in the sketch's menus.

Setup on the Pi:
    python3 -m venv venv && source venv/bin/activate
    pip install pyserial
    python3 reader_board.py --list-ports
    python3 reader_board.py --port /dev/ttyACM0 message "HELLO WORLD"

Run `python3 reader_board.py --help` and `python3 reader_board.py <command> -h`
for full usage.
"""

import argparse
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is required: pip install pyserial")

BAUD = 9600
MESSAGE_CAPACITY = 96
MAX_SEQUENCE_SLOTS = 5

COLOR_PRESETS = {
    "red": 1, "green": 2, "blue": 3, "yellow": 4, "cyan": 5,
    "magenta": 6, "orange": 7, "white": 8, "pink": 9, "purple": 10,
}

COLOR_MODE_KEYS = {
    "solid": "1", "cycle": "2", "rainbow": "3",
    "fire": "4", "stripes": "5", "twinkle": "6",
}


class ReaderBoardError(Exception):
    pass


class ReaderBoard:
    """Serial session with the reader board's menu."""

    def __init__(self, port, baud=BAUD, timeout=3.0, quiet_gap=0.25, verbose=False):
        self.verbose = verbose
        self.timeout = timeout
        self.quiet_gap = quiet_gap
        try:
            self.ser = serial.Serial(port, baud, timeout=0.2)
        except serial.SerialException as e:
            raise ReaderBoardError(f"Could not open {port}: {e}") from e
        # Opening the port resets an Arduino Mega; give it time to boot and
        # print its startup menu before we send anything.
        time.sleep(2.0)
        self._read_response()

    def close(self):
        self.ser.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def _read_response(self):
        """Read until the board goes quiet for quiet_gap, or timeout hits."""
        chunks = []
        deadline = time.time() + self.timeout
        last_data = time.time()
        while time.time() < deadline:
            waiting = self.ser.in_waiting
            data = self.ser.read(waiting or 1)
            if data:
                chunks.append(data)
                last_data = time.time()
            elif chunks and (time.time() - last_data) > self.quiet_gap:
                break
        text = b"".join(chunks).decode(errors="replace")
        if self.verbose and text.strip():
            print(text)
        return text

    def send(self, line):
        """Send one line (as the sketch's serial menu expects) and return its reply text."""
        line = str(line)
        if self.verbose:
            print(f">>> {line}")
        self.ser.write((line + "\n").encode())
        return self._read_response()

    def reset_menu(self):
        """Back out of any in-progress prompt so we start from a known state."""
        return self.send("cancel")

    # ── status ──────────────────────────────────────────────────────────
    def status(self):
        self.reset_menu()
        return self.send("2")

    # ── simple settings ─────────────────────────────────────────────────
    def set_speed(self, ms):
        if not (1 <= ms <= 5000):
            raise ReaderBoardError("speed must be 1-5000 ms")
        self.reset_menu()
        self.send("3")
        return self.send(ms)

    def set_brightness(self, level):
        if not (0 <= level <= 255):
            raise ReaderBoardError("brightness must be 0-255")
        self.reset_menu()
        self.send("4")
        return self.send(level)

    def set_message(self, text):
        if len(text) >= MESSAGE_CAPACITY:
            raise ReaderBoardError(f"message must be under {MESSAGE_CAPACITY} characters")
        self.reset_menu()
        self.send("5")
        return self.send(text)

    def set_spacing(self, columns):
        if not (0 <= columns <= 120):
            raise ReaderBoardError("spacing must be 0-120 columns")
        self.reset_menu()
        self.send("6")
        return self.send(columns)

    # ── color / mode ─────────────────────────────────────────────────────
    def set_color_solid(self, preset):
        num = _preset_number(preset)
        self.reset_menu()
        self.send("7")
        self.send("1")
        return self.send(num)

    def set_color_cycle(self):
        self.reset_menu()
        self.send("7")
        return self.send("2")

    def set_color_rainbow(self, flow=False):
        self.reset_menu()
        self.send("7")
        self.send("3")
        return self.send("2" if flow else "1")

    def set_color_fire(self):
        self.reset_menu()
        self.send("7")
        return self.send("4")

    def set_color_stripes(self):
        self.reset_menu()
        self.send("7")
        return self.send("5")

    def set_color_twinkle(self):
        self.reset_menu()
        self.send("7")
        return self.send("6")

    def set_color_mode(self, mode, preset=None, flow=False):
        mode = mode.lower()
        if mode == "solid":
            if preset is None:
                raise ReaderBoardError("solid mode requires --preset")
            return self.set_color_solid(preset)
        if mode == "cycle":
            return self.set_color_cycle()
        if mode == "rainbow":
            return self.set_color_rainbow(flow=flow)
        if mode == "fire":
            return self.set_color_fire()
        if mode == "stripes":
            return self.set_color_stripes()
        if mode == "twinkle":
            return self.set_color_twinkle()
        raise ReaderBoardError(f"unknown color mode: {mode}")

    # ── sequence ─────────────────────────────────────────────────────────
    def sequence_list(self):
        self.reset_menu()
        self.send("8")
        return self.send("1")

    def sequence_set_slot(self, slot, text, mode="solid", preset=None, flow=False):
        if not (1 <= slot <= MAX_SEQUENCE_SLOTS):
            raise ReaderBoardError(f"slot must be 1-{MAX_SEQUENCE_SLOTS}")
        if len(text) >= MESSAGE_CAPACITY:
            raise ReaderBoardError(f"message must be under {MESSAGE_CAPACITY} characters")
        mode = mode.lower()
        if mode not in COLOR_MODE_KEYS:
            raise ReaderBoardError(f"unknown color mode: {mode}")

        self.reset_menu()
        self.send("8")
        self.send("2")
        self.send(slot)
        self.send(text)
        self.send(COLOR_MODE_KEYS[mode])
        if mode == "solid":
            num = _preset_number(preset)
            return self.send(num)
        if mode == "rainbow":
            return self.send("2" if flow else "1")
        return ""

    def sequence_set_duration(self, seconds):
        if not (1 <= seconds <= 3600):
            raise ReaderBoardError("duration must be 1-3600 seconds")
        self.reset_menu()
        self.send("8")
        self.send("3")
        return self.send(seconds)

    def sequence_enable(self):
        self.reset_menu()
        self.send("8")
        return self.send("4")

    def sequence_disable(self):
        self.reset_menu()
        self.send("8")
        return self.send("5")

    def sequence_clear(self):
        self.reset_menu()
        self.send("8")
        return self.send("6")

    # ── raw passthrough (advanced) ──────────────────────────────────────
    def raw(self, line):
        return self.send(line)


def _preset_number(preset):
    if preset is None:
        raise ReaderBoardError("a color preset is required, e.g. red/green/blue/...")
    if isinstance(preset, int) or str(preset).isdigit():
        num = int(preset)
    else:
        key = str(preset).lower()
        if key not in COLOR_PRESETS:
            raise ReaderBoardError(
                f"unknown preset '{preset}'. choices: {', '.join(COLOR_PRESETS)}"
            )
        num = COLOR_PRESETS[key]
    if not (1 <= num <= 10):
        raise ReaderBoardError("preset must be 1-10")
    return num


def list_serial_ports():
    ports = list(list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return
    for p in ports:
        print(f"{p.device}  {p.description}")


def build_parser():
    p = argparse.ArgumentParser(description="Control the NeoPixel reader board over serial.")
    p.add_argument("--port", help="serial device, e.g. /dev/ttyACM0")
    p.add_argument("--baud", type=int, default=BAUD)
    p.add_argument("-v", "--verbose", action="store_true", help="print raw board output")
    sub = p.add_subparsers(dest="command", required=True)

    sub.add_parser("list-ports", help="list available serial ports and exit")
    sub.add_parser("status", help="show current board settings")

    sp = sub.add_parser("speed", help="set scroll speed")
    sp.add_argument("ms", type=int, help="milliseconds per scroll step (1-5000)")

    sp = sub.add_parser("brightness", help="set LED brightness")
    sp.add_argument("level", type=int, help="0-255")

    sp = sub.add_parser("message", help="set the scrolling message")
    sp.add_argument("text", help="message text (under 96 characters)")

    sp = sub.add_parser("spacing", help="set blank columns between message repeats")
    sp.add_argument("columns", type=int, help="0-120")

    sp = sub.add_parser("color", help="set color / display mode")
    sp.add_argument("mode", choices=sorted(COLOR_MODE_KEYS))
    sp.add_argument("--preset", help=f"solid color preset: {', '.join(COLOR_PRESETS)} (or 1-10)")
    sp.add_argument("--flow", action="store_true", help="rainbow mode: flow instead of static")

    seq = sub.add_parser("sequence", help="manage the message sequence")
    seq_sub = seq.add_subparsers(dest="seq_command", required=True)

    seq_sub.add_parser("list", help="list sequence slots")

    sp = seq_sub.add_parser("set", help="set one sequence slot")
    sp.add_argument("slot", type=int, help=f"slot number (1-{MAX_SEQUENCE_SLOTS})")
    sp.add_argument("text", help="message text for this slot")
    sp.add_argument("--mode", choices=sorted(COLOR_MODE_KEYS), default="solid")
    sp.add_argument("--preset", help=f"solid color preset: {', '.join(COLOR_PRESETS)} (or 1-10)")
    sp.add_argument("--flow", action="store_true", help="rainbow mode: flow instead of static")

    sp = seq_sub.add_parser("duration", help="seconds each sequence message is shown")
    sp.add_argument("seconds", type=int)

    seq_sub.add_parser("enable", help="enable the sequence")
    seq_sub.add_parser("disable", help="disable the sequence")
    seq_sub.add_parser("clear", help="clear all sequence slots")

    sp = sub.add_parser("raw", help="send a raw line to the board's serial menu")
    sp.add_argument("line")

    sub.add_parser("shell", help="interactive prompt: raw lines in, board replies out")

    return p


def run_interactive_shell(board):
    print("Interactive mode. Type menu numbers/text as you would in a serial terminal.")
    print("Type 'exit' or Ctrl-D to quit.\n")
    print(board.status())
    while True:
        try:
            line = input("> ")
        except EOFError:
            print()
            break
        if line.strip().lower() in ("exit", "quit"):
            break
        print(board.raw(line), end="")


def main():
    args = build_parser().parse_args()

    if args.command == "list-ports":
        list_serial_ports()
        return

    if not args.port:
        sys.exit("--port is required (use 'list-ports' to see available devices)")

    try:
        board = ReaderBoard(args.port, baud=args.baud, verbose=args.verbose)
    except ReaderBoardError as e:
        sys.exit(str(e))

    try:
        try:
            if args.command == "status":
                print(board.status())
            elif args.command == "speed":
                print(board.set_speed(args.ms))
            elif args.command == "brightness":
                print(board.set_brightness(args.level))
            elif args.command == "message":
                print(board.set_message(args.text))
            elif args.command == "spacing":
                print(board.set_spacing(args.columns))
            elif args.command == "color":
                print(board.set_color_mode(args.mode, preset=args.preset, flow=args.flow))
            elif args.command == "sequence":
                if args.seq_command == "list":
                    print(board.sequence_list())
                elif args.seq_command == "set":
                    print(board.sequence_set_slot(
                        args.slot, args.text, mode=args.mode,
                        preset=args.preset, flow=args.flow,
                    ))
                elif args.seq_command == "duration":
                    print(board.sequence_set_duration(args.seconds))
                elif args.seq_command == "enable":
                    print(board.sequence_enable())
                elif args.seq_command == "disable":
                    print(board.sequence_disable())
                elif args.seq_command == "clear":
                    print(board.sequence_clear())
            elif args.command == "raw":
                print(board.raw(args.line))
            elif args.command == "shell":
                run_interactive_shell(board)
        except ReaderBoardError as e:
            sys.exit(str(e))
    finally:
        board.close()


if __name__ == "__main__":
    main()
