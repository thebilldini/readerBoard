#!/usr/bin/env python3
"""
Reader Board control panel — a desktop GUI (Tkinter) for reader_board.py.

All serial I/O runs on a background worker thread so the UI never freezes
while waiting on the board's replies; button callbacks just enqueue a job
and the worker reports back through a queue that the UI polls.

Run directly: python3 reader_board_gui.py
Requires: pyserial (pip install pyserial). Tkinter ships with Python on
Raspberry Pi OS; if missing, `sudo apt install python3-tk`.
"""

import os
import queue
import sys
import threading
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reader_board as rb  # noqa: E402

try:
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is required: pip install pyserial")

COLOR_MODES = ["solid", "cycle", "rainbow", "fire", "stripes", "twinkle"]
PRESETS = list(rb.COLOR_PRESETS.keys())


class ModeControls(ttk.Frame):
    """Mode combobox + preset combobox (solid) + flow checkbox (rainbow),
    the same trio the CLI's --preset/--flow options cover, wired to update
    which extra control is enabled as the mode changes."""

    def __init__(self, parent):
        super().__init__(parent)
        self.mode_var = tk.StringVar(value="solid")
        self.preset_var = tk.StringVar(value=PRESETS[0])
        self.flow_var = tk.BooleanVar(value=False)

        self.mode_box = ttk.Combobox(
            self, textvariable=self.mode_var, values=COLOR_MODES,
            state="readonly", width=10,
        )
        self.mode_box.grid(row=0, column=0, padx=(0, 6))
        self.mode_box.bind("<<ComboboxSelected>>", lambda e: self._sync())

        self.preset_box = ttk.Combobox(
            self, textvariable=self.preset_var, values=PRESETS,
            state="readonly", width=10,
        )
        self.preset_box.grid(row=0, column=1, padx=(0, 6))

        self.flow_check = ttk.Checkbutton(
            self, text="flow", variable=self.flow_var,
        )
        self.flow_check.grid(row=0, column=2)

        self._sync()

    def _sync(self):
        mode = self.mode_var.get()
        self.preset_box.configure(state="readonly" if mode == "solid" else "disabled")
        self.flow_check.configure(state="normal" if mode == "rainbow" else "disabled")

    def values(self):
        return {
            "mode": self.mode_var.get(),
            "preset": self.preset_var.get() if self.mode_var.get() == "solid" else None,
            "flow": self.flow_var.get() if self.mode_var.get() == "rainbow" else False,
        }


class SequenceRow(ttk.Frame):
    def __init__(self, parent, slot, on_save):
        super().__init__(parent)
        self.slot = slot
        self.on_save = on_save

        ttk.Label(self, text=f"Slot {slot}", width=7).grid(row=0, column=0, padx=(0, 6))
        self.text_var = tk.StringVar()
        ttk.Entry(self, textvariable=self.text_var, width=28).grid(row=0, column=1, padx=(0, 6))

        self.controls = ModeControls(self)
        self.controls.grid(row=0, column=2, padx=(0, 6))

        ttk.Button(self, text="Save Slot", command=self._save).grid(row=0, column=3)

    def _save(self):
        self.on_save(self.slot, self.text_var.get(), self.controls.values())


class ReaderBoardApp:
    def __init__(self, root):
        self.root = root
        root.title("Reader Board Control")
        root.geometry("760x620")

        self.board = None
        self.connected = False
        self.job_queue = queue.Queue()
        self.log_queue = queue.Queue()

        self._build_ui()
        self._set_controls_enabled(False)

        self.worker = threading.Thread(target=self._worker_loop, daemon=True)
        self.worker.start()
        self.root.after(100, self._poll_log)
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

        self._refresh_ports()

    # ── worker thread plumbing ──────────────────────────────────────────
    def _worker_loop(self):
        while True:
            func, args, kwargs, label = self.job_queue.get()
            if func is None:
                break
            try:
                result = func(*args, **kwargs)
                self.log_queue.put((label, result, None))
            except Exception as e:
                self.log_queue.put((label, None, str(e)))

    def _submit(self, label, func, *args, **kwargs):
        if not self.connected and func != self._do_connect:
            messagebox.showwarning("Not connected", "Connect to the board first.")
            return
        self.job_queue.put((func, args, kwargs, label))

    def _poll_log(self):
        try:
            while True:
                label, result, err = self.log_queue.get_nowait()
                if err:
                    self._log(f"[{label}] ERROR: {err}")
                    if label == "connect":
                        self.connected = False
                        self._set_controls_enabled(False)
                        self.status_var.set("Disconnected")
                else:
                    if result:
                        self._log(f"[{label}]\n{result}".rstrip())
                    if label == "connect":
                        self.connected = True
                        self._set_controls_enabled(True)
                        self.status_var.set(f"Connected: {self.port_var.get()}")
                    elif label == "disconnect":
                        self.connected = False
                        self._set_controls_enabled(False)
                        self.status_var.set("Disconnected")
        except queue.Empty:
            pass
        self.root.after(150, self._poll_log)

    def _log(self, text):
        self.log.configure(state="normal")
        self.log.insert("end", text + "\n\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    # ── connection actions (run on worker thread) ───────────────────────
    def _do_connect(self, port):
        if self.board:
            self.board.close()
        self.board = rb.ReaderBoard(port, verbose=False)
        return self.board.status()

    def _do_disconnect(self):
        if self.board:
            self.board.close()
            self.board = None
        return ""

    # ── UI construction ──────────────────────────────────────────────────
    def _build_ui(self):
        top = ttk.Frame(self.root, padding=8)
        top.pack(fill="x")

        ttk.Label(top, text="Port:").pack(side="left")
        self.port_var = tk.StringVar()
        self.port_box = ttk.Combobox(top, textvariable=self.port_var, width=20, state="readonly")
        self.port_box.pack(side="left", padx=(4, 6))
        ttk.Button(top, text="Refresh", command=self._refresh_ports).pack(side="left", padx=(0, 6))
        self.connect_btn = ttk.Button(top, text="Connect", command=self._toggle_connect)
        self.connect_btn.pack(side="left", padx=(0, 12))
        self.status_var = tk.StringVar(value="Disconnected")
        ttk.Label(top, textvariable=self.status_var).pack(side="left")
        ttk.Button(top, text="Refresh Status", command=lambda: self._submit("status", self.board_status)).pack(side="right")

        notebook = ttk.Notebook(self.root)
        notebook.pack(fill="both", expand=True, padx=8, pady=(0, 8))

        self.display_tab = ttk.Frame(notebook, padding=10)
        self.sequence_tab = ttk.Frame(notebook, padding=10)
        self.console_tab = ttk.Frame(notebook, padding=10)
        notebook.add(self.display_tab, text="Display")
        notebook.add(self.sequence_tab, text="Sequence")
        notebook.add(self.console_tab, text="Console")

        self._build_display_tab()
        self._build_sequence_tab()
        self._build_console_tab()

        ttk.Label(self.root, text="Log", padding=(8, 0)).pack(anchor="w")
        self.log = scrolledtext.ScrolledText(self.root, height=10, state="disabled", wrap="word")
        self.log.pack(fill="both", expand=False, padx=8, pady=(0, 8))

    def _build_display_tab(self):
        f = self.display_tab

        row = ttk.Frame(f)
        row.pack(fill="x", pady=4)
        ttk.Label(row, text="Message:", width=12).pack(side="left")
        self.message_var = tk.StringVar()
        ttk.Entry(row, textvariable=self.message_var, width=40).pack(side="left", padx=(0, 6))
        ttk.Button(row, text="Set", command=self._set_message).pack(side="left")

        row = ttk.Frame(f)
        row.pack(fill="x", pady=4)
        ttk.Label(row, text="Speed (ms):", width=12).pack(side="left")
        self.speed_var = tk.IntVar(value=100)
        tk.Scale(row, from_=1, to=5000, orient="horizontal", variable=self.speed_var, length=300).pack(side="left")
        ttk.Button(row, text="Set", command=self._set_speed).pack(side="left", padx=(6, 0))

        row = ttk.Frame(f)
        row.pack(fill="x", pady=4)
        ttk.Label(row, text="Brightness:", width=12).pack(side="left")
        self.brightness_var = tk.IntVar(value=40)
        tk.Scale(row, from_=0, to=255, orient="horizontal", variable=self.brightness_var, length=300).pack(side="left")
        ttk.Button(row, text="Set", command=self._set_brightness).pack(side="left", padx=(6, 0))

        row = ttk.Frame(f)
        row.pack(fill="x", pady=4)
        ttk.Label(row, text="Spacing:", width=12).pack(side="left")
        self.spacing_var = tk.IntVar(value=12)
        tk.Scale(row, from_=0, to=120, orient="horizontal", variable=self.spacing_var, length=300).pack(side="left")
        ttk.Button(row, text="Set", command=self._set_spacing).pack(side="left", padx=(6, 0))

        row = ttk.Frame(f)
        row.pack(fill="x", pady=(12, 4))
        ttk.Label(row, text="Color / Mode:", width=12).pack(side="left")
        self.color_controls = ModeControls(row)
        self.color_controls.pack(side="left", padx=(0, 6))
        ttk.Button(row, text="Apply", command=self._set_color).pack(side="left")

    def _build_sequence_tab(self):
        f = self.sequence_tab

        row = ttk.Frame(f)
        row.pack(fill="x", pady=4)
        ttk.Label(row, text="Duration (sec):", width=14).pack(side="left")
        self.duration_var = tk.IntVar(value=10)
        ttk.Spinbox(row, from_=1, to=3600, textvariable=self.duration_var, width=8).pack(side="left", padx=(0, 6))
        ttk.Button(row, text="Set", command=self._set_duration).pack(side="left", padx=(0, 12))
        ttk.Button(row, text="Enable", command=lambda: self._submit("sequence enable", self.board_op, "sequence_enable")).pack(side="left", padx=(0, 4))
        ttk.Button(row, text="Disable", command=lambda: self._submit("sequence disable", self.board_op, "sequence_disable")).pack(side="left", padx=(0, 4))
        ttk.Button(row, text="Clear All", command=self._clear_sequence).pack(side="left", padx=(0, 4))
        ttk.Button(row, text="List", command=lambda: self._submit("sequence list", self.board_op, "sequence_list")).pack(side="left")

        ttk.Separator(f).pack(fill="x", pady=8)

        self.sequence_rows = []
        for slot in range(1, rb.MAX_SEQUENCE_SLOTS + 1):
            r = SequenceRow(f, slot, self._save_sequence_slot)
            r.pack(fill="x", pady=3)
            self.sequence_rows.append(r)

    def _build_console_tab(self):
        f = self.console_tab
        ttk.Label(f, text="Send a raw line to the board's serial menu (advanced):").pack(anchor="w", pady=(0, 6))
        row = ttk.Frame(f)
        row.pack(fill="x")
        self.raw_var = tk.StringVar()
        entry = ttk.Entry(row, textvariable=self.raw_var, width=40)
        entry.pack(side="left", padx=(0, 6))
        entry.bind("<Return>", lambda e: self._send_raw())
        ttk.Button(row, text="Send", command=self._send_raw).pack(side="left")

    # ── control state ────────────────────────────────────────────────────
    def _set_controls_enabled(self, enabled):
        state = "!disabled" if enabled else "disabled"
        for tab in (self.display_tab, self.sequence_tab, self.console_tab):
            self._set_tree_state(tab, state)

    def _set_tree_state(self, widget, state):
        for child in widget.winfo_children():
            try:
                child.state([state]) if hasattr(child, "state") else None
            except tk.TclError:
                pass
            if isinstance(child, (tk.Scale,)):
                child.configure(state="normal" if state == "!disabled" else "disabled")
            self._set_tree_state(child, state)

    # ── button handlers ──────────────────────────────────────────────────
    def _refresh_ports(self):
        ports = [p.device for p in list_ports.comports()]
        self.port_box["values"] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])

    def _toggle_connect(self):
        if self.connected:
            self._submit("disconnect", self._do_disconnect)
        else:
            port = self.port_var.get()
            if not port:
                messagebox.showwarning("No port", "Select a serial port first.")
                return
            self.status_var.set("Connecting…")
            self.job_queue.put((self._do_connect, (port,), {}, "connect"))

    def board_status(self):
        return self.board.status()

    def board_op(self, method_name, *args, **kwargs):
        return getattr(self.board, method_name)(*args, **kwargs)

    def _set_message(self):
        self._submit("message", self.board_op, "set_message", self.message_var.get())

    def _set_speed(self):
        self._submit("speed", self.board_op, "set_speed", self.speed_var.get())

    def _set_brightness(self):
        self._submit("brightness", self.board_op, "set_brightness", self.brightness_var.get())

    def _set_spacing(self):
        self._submit("spacing", self.board_op, "set_spacing", self.spacing_var.get())

    def _set_color(self):
        v = self.color_controls.values()
        self._submit("color", self.board_op, "set_color_mode", v["mode"], preset=v["preset"], flow=v["flow"])

    def _set_duration(self):
        self._submit("sequence duration", self.board_op, "sequence_set_duration", self.duration_var.get())

    def _clear_sequence(self):
        if messagebox.askyesno("Clear sequence", "Clear all 5 sequence slots?"):
            self._submit("sequence clear", self.board_op, "sequence_clear")

    def _save_sequence_slot(self, slot, text, values):
        self._submit(
            f"sequence slot {slot}", self.board_op, "sequence_set_slot",
            slot, text, mode=values["mode"], preset=values["preset"], flow=values["flow"],
        )

    def _send_raw(self):
        line = self.raw_var.get()
        if not line:
            return
        self._submit("raw", self.board_op, "raw", line)
        self.raw_var.set("")

    def _on_close(self):
        if self.board:
            try:
                self.board.close()
            except Exception:
                pass
        self.root.destroy()


def main():
    root = tk.Tk()
    ReaderBoardApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
