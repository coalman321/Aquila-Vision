#!/usr/bin/env python3
"""
camera_cal_tui – curses TUI service client for camera_cal_node.

Keys
----
  s  –  Start calibration  (prompts for image topic & camera name)
  x  –  Stop  calibration
  c  –  Complete calibration (compute + save)
  q  –  Quit
"""

import curses
import threading
import time
from typing import List, Optional

import rclpy
from rclpy.node import Node

from calibration_msgs.msg import CamCalStatus
from calibration_msgs.srv import CamCalRequest

# ──────────────────────────────────────────────────────────────────────────────
#  ROS 2 node
# ──────────────────────────────────────────────────────────────────────────────

class CameraCalClient(Node):
    """Thin ROS 2 node: service client + status subscriber."""

    _MAX_LOG_LINES = 200

    def __init__(self) -> None:
        super().__init__("camera_cal_tui")
        self._lock = threading.Lock()

        self._client = self.create_client(CamCalRequest, "camera_cal/request")
        self._status_sub = self.create_subscription(
            CamCalStatus,
            "camera_cal/status",
            self._on_status,
            10,
        )

        self._latest_status: Optional[CamCalStatus] = None
        self._last_response: Optional[tuple] = None  # (message: str, is_error: bool)
        self._log: List[str] = []

    # ── ROS callbacks ──────────────────────────────────────────────────────

    def _on_status(self, msg: CamCalStatus) -> None:
        with self._lock:
            self._latest_status = msg

    # ── Service call (blocking, safe to call from non-spin thread) ─────────

    def send_request(
        self,
        request_id: int,
        image_topic: str = "",
        camera_name: str = "",
        board: Optional[dict] = None,
    ) -> str:
        if not self._client.wait_for_service(timeout_sec=1.0):
            msg = "ERROR: service 'camera_cal/request' not available"
            with self._lock:
                self._last_response = (msg, True)
            return msg

        req = CamCalRequest.Request()
        req.request_id = request_id
        req.image_topic = image_topic
        req.camera_name = camera_name
        for field, value in (board or {}).items():
            setattr(req, field, value)

        future = self._client.call_async(req)

        # The background spin-thread drives the future; just poll here.
        deadline = time.monotonic() + 5.0
        while not future.done():
            if time.monotonic() > deadline:
                msg = "ERROR: service call timed out after 5 s"
                with self._lock:
                    self._last_response = (msg, True)
                return msg
            time.sleep(0.05)

        resp = future.result()
        if resp is None:
            msg = "ERROR: got None response from service"
            with self._lock:
                self._last_response = (msg, True)
            return msg

        code_map = {
            CamCalRequest.Response.ERR_OK:             "OK",
            CamCalRequest.Response.ERR_UNKNOWN:        "ERR_UNKNOWN",
            CamCalRequest.Response.ERR_CAL_RUNNING:    "ERR_CAL_RUNNING",
            CamCalRequest.Response.ERR_NO_CAL_RUNNING: "ERR_NO_CAL_RUNNING",
            CamCalRequest.Response.ERR_NOT_READY:      "ERR_NOT_READY",
            CamCalRequest.Response.ERR_INVLD_EDGE_LEN: "ERR_INVLD_EDGE_LEN",
            CamCalRequest.Response.ERR_INVLD_BOARD:    "ERR_INVLD_BOARD",
        }
        code_str = code_map.get(resp.err_code, f"ERR({resp.err_code})")
        is_error = resp.err_code != CamCalRequest.Response.ERR_OK
        result = f"[{code_str}] {resp.err_msg}"
        with self._lock:
            self._last_response = (result, is_error)
        return result

    # ── Log helpers (thread-safe) ─────────────────────────────────────────

    def log(self, text: str) -> None:
        ts = time.strftime("%H:%M:%S")
        with self._lock:
            self._log.append(f"{ts}  {text}")
            if len(self._log) > self._MAX_LOG_LINES:
                self._log.pop(0)

    def snapshot(self):
        """Return (latest_status, last_response, log_lines_copy) under lock."""
        with self._lock:
            return self._latest_status, self._last_response, list(self._log)


# ──────────────────────────────────────────────────────────────────────────────
#  Curses helpers
# ──────────────────────────────────────────────────────────────────────────────

CP_TITLE = 1   # cyan bold   – section headings
CP_OK    = 2   # green       – OK responses
CP_WARN  = 3   # yellow      – warnings / waiting messages
CP_ERR   = 4   # red         – error responses
CP_BAR   = 5   # white/blue  – title / key-hint bars


def _init_colors() -> None:
    try:
        curses.start_color()
        curses.use_default_colors()
        curses.init_pair(CP_TITLE, curses.COLOR_CYAN,    -1)
        curses.init_pair(CP_OK,    curses.COLOR_GREEN,   -1)
        curses.init_pair(CP_WARN,  curses.COLOR_YELLOW,  -1)
        curses.init_pair(CP_ERR,   curses.COLOR_RED,     -1)
        curses.init_pair(CP_BAR,   curses.COLOR_WHITE,   curses.COLOR_BLUE)
    except curses.error:
        pass


def _draw_box(win, y0: int, x0: int, height: int, width: int, title: str = "") -> None:
    """Draw a box and optional title directly onto win (e.g. stdscr)."""
    try:
        # Corners
        win.addch(y0,              x0,           curses.ACS_ULCORNER)
        win.addch(y0,              x0 + width - 1, curses.ACS_URCORNER)
        win.addch(y0 + height - 1, x0,           curses.ACS_LLCORNER)
        win.addch(y0 + height - 1, x0 + width - 1, curses.ACS_LRCORNER)
        # Horizontal borders
        win.hline(y0,              x0 + 1, curses.ACS_HLINE, width - 2)
        win.hline(y0 + height - 1, x0 + 1, curses.ACS_HLINE, width - 2)
        # Vertical borders
        win.vline(y0 + 1, x0,           curses.ACS_VLINE, height - 2)
        win.vline(y0 + 1, x0 + width - 1, curses.ACS_VLINE, height - 2)
        # Title
        if title:
            label = f" {title[: width - 4]} "
            win.addstr(y0, x0 + 2, label, curses.color_pair(CP_TITLE) | curses.A_BOLD)
    except curses.error:
        pass


def _prompt(stdscr, prompt: str, y: int, x: int, max_w: int = 60) -> str:
    """Inline single-line text prompt.  Returns '' on ESC / error."""
    curses.echo()
    curses.curs_set(1)
    try:
        stdscr.addstr(y, x, prompt, curses.A_BOLD)
        stdscr.clrtoeol()
        stdscr.refresh()
        raw = stdscr.getstr(y, x + len(prompt), max_w)
        return raw.decode("utf-8", errors="replace").strip()
    except curses.error:
        return ""
    finally:
        curses.noecho()
        curses.curs_set(0)


def _prompt_board(stdscr, y: int, x: int) -> dict:
    """Prompt for the board description on rows y and y+1.

    Returns CamCalRequest.Request field values; raises ValueError on bad input.
    """
    def ask(prompt: str, default: str, row: int) -> str:
        return _prompt(stdscr, f"{prompt} [{default}]: ", row, x) or default

    kind = ask("Board type  (c = chessboard, a = charuco)", "c", y).lower()
    if kind not in ("c", "a"):
        raise ValueError(f"unknown board type '{kind}'")
    charuco = kind == "a"

    dims = "squares" if charuco else "internal corners"
    board = {
        "board_type": (CamCalRequest.Request.BOARD_CHARUCO if charuco
                       else CamCalRequest.Request.BOARD_CHESSBOARD),
        "board_rows": int(ask(f"Board rows  ({dims})", "7" if charuco else "6", y + 1)),
        "board_cols": int(ask(f"Board cols  ({dims})", "5" if charuco else "9", y)),
        "edge_len":   float(ask("Square edge (mm)", "22.86", y + 1)) / 1000.0,
    }
    if charuco:
        board["marker_len"] = float(ask("Marker edge (mm)", "17.0", y)) / 1000.0
        board["aruco_dict"] = ask("ArUco dictionary", "DICT_5X5_100", y + 1)
        board["charuco_legacy_pattern"] = ask("Legacy pattern (y/n)", "n", y).lower() == "y"
    return board


# ──────────────────────────────────────────────────────────────────────────────
#  Main TUI loop
# ──────────────────────────────────────────────────────────────────────────────

_STATUS_PANEL_H = 13   # inner rows (excl. border)
_REFRESH_HZ     = 10   # approximate screen refresh rate


def _tui(stdscr, node: CameraCalClient) -> None:
    curses.curs_set(0)
    curses.noecho()
    stdscr.nodelay(True)
    stdscr.keypad(True)
    _init_colors()

    while rclpy.ok():
        h, w = stdscr.getmaxyx()

        # ── Minimum size guard ───────────────────────────────────────────
        if h < 22 or w < 64:
            stdscr.erase()
            stdscr.addstr(0, 0, "Terminal too small – resize to ≥ 64×22")
            stdscr.refresh()
            time.sleep(0.25)
            k = stdscr.getch()
            if k in (ord("q"), ord("Q")):
                break
            continue

        stdscr.erase()

        # ── Layout ──────────────────────────────────────────────────────
        STATUS_BOX_H = _STATUS_PANEL_H + 2   # +2 for border rows
        status_y     = 1                      # below title bar
        log_y        = status_y + STATUS_BOX_H
        log_h        = h - log_y - 1          # leave 1 for key bar at bottom

        # ── Top bar ─────────────────────────────────────────────────────
        title = " Camera Calibration TUI  •  camera_cal_node "
        try:
            stdscr.attron(curses.color_pair(CP_BAR) | curses.A_BOLD)
            stdscr.addstr(0, 0, title.center(w - 1))
            stdscr.attroff(curses.color_pair(CP_BAR) | curses.A_BOLD)
        except curses.error:
            pass

        # ── Status box ──────────────────────────────────────────────────
        _draw_box(stdscr, status_y, 0, STATUS_BOX_H, w,
                  "Live Status  (camera_cal/status)")

        latest, last_resp, log_lines = node.snapshot()
        row = status_y + 1
        if latest is None:
            _waddstr(stdscr, row, 2,
                     "Waiting for status messages…",
                     curses.color_pair(CP_WARN))
            row += 1
        else:
            active  = bool(latest.camera_name)
            state_s = "● ACTIVE" if active else "○ Idle"
            state_a = (curses.color_pair(CP_OK) | curses.A_BOLD) if active \
                      else curses.color_pair(CP_WARN)
            _waddstr(stdscr, row, 2, f"State      : {state_s}", state_a); row += 1
            _waddstr(stdscr, row, 2, f"Camera     : {latest.camera_name or '—'}"); row += 1
            _waddstr(stdscr, row, 2, f"Topic      : {latest.topic_name  or '—'}"); row += 1
            _waddstr(stdscr, row, 2, f"Samples    : {latest.cal_samples}"); row += 1
            if latest.reproj_err >= 0.0:
                _waddstr(stdscr, row, 2,
                         f"Reproj err : {latest.reproj_err:.4f} px",
                         curses.color_pair(CP_OK))
            else:
                _waddstr(stdscr, row, 2, "Reproj err : — (not yet computed)")
            row += 1
            _waddstr(stdscr, row, 2,
                     f"X spread   : [{latest.min_x:+.3f} … {latest.max_x:+.3f}]"); row += 1
            _waddstr(stdscr, row, 2,
                     f"Y spread   : [{latest.min_y:+.3f} … {latest.max_y:+.3f}]"); row += 1
            _waddstr(stdscr, row, 2,
                     f"Max skew   : {latest.max_skew:.4f}    "
                     f"Max scale  : {latest.max_scale:.4f}"); row += 1

        # ── Last service response ────────────────────────────────────────
        row += 1  # blank separator
        if last_resp is None:
            _waddstr(stdscr, row, 2, "Last response : —", curses.color_pair(CP_WARN))
        else:
            resp_msg, resp_is_err = last_resp
            resp_attr = (curses.color_pair(CP_ERR) | curses.A_BOLD) if resp_is_err \
                        else (curses.color_pair(CP_OK) | curses.A_BOLD)
            _waddstr(stdscr, row, 2, f"Last response : {resp_msg}", resp_attr)

        # ── Log box ─────────────────────────────────────────────────────
        if log_h > 2:
            _draw_box(stdscr, log_y, 0, log_h, w, "Log")
            visible = log_lines[-(log_h - 2):]
            for i, line in enumerate(visible):
                attr = 0
                if "ERROR" in line:
                    attr = curses.color_pair(CP_ERR)
                elif "[OK]" in line:
                    attr = curses.color_pair(CP_OK)
                elif "[ERR" in line:
                    attr = curses.color_pair(CP_WARN)
                _waddstr(stdscr, log_y + 1 + i, 2, line[: w - 4], attr)

        # ── Key-hint bar ────────────────────────────────────────────────
        hints = "  s:Start    x:Stop    c:Complete    q:Quit  "
        try:
            stdscr.attron(curses.color_pair(CP_BAR))
            stdscr.addstr(h - 1, 0, hints.ljust(w - 1))
            stdscr.attroff(curses.color_pair(CP_BAR))
        except curses.error:
            pass

        stdscr.refresh()

        # ── Key handling ─────────────────────────────────────────────────
        key = stdscr.getch()

        if key in (ord("q"), ord("Q")):
            break

        elif key in (ord("s"), ord("S")):
            stdscr.nodelay(False)
            topic  = _prompt(stdscr, "Image topic  (blank = node default): ",
                             h - 4, 2)
            camera = _prompt(stdscr, "Camera name  (blank = node default): ",
                             h - 3, 2)
            try:
                board = _prompt_board(stdscr, h - 4, 2)
            except ValueError as e:
                board = None
                node.log(f"START    → ERROR: invalid board input: {e}")
            stdscr.nodelay(True)
            if board is not None:
                msg = node.send_request(
                    CamCalRequest.Request.REQ_START_CAL,
                    image_topic=topic,
                    camera_name=camera,
                    board=board,
                )
                node.log(f"START    → {msg}")

        elif key in (ord("x"), ord("X")):
            msg = node.send_request(CamCalRequest.Request.REQ_STOP_CAL)
            node.log(f"STOP     → {msg}")

        elif key in (ord("c"), ord("C")):
            msg = node.send_request(CamCalRequest.Request.REQ_COMPLETE_CAL)
            node.log(f"COMPLETE → {msg}")

        time.sleep(1.0 / _REFRESH_HZ)


def _waddstr(win, y: int, x: int, text: str, attr: int = 0) -> None:
    """curses addstr that silently ignores out-of-bounds errors."""
    try:
        _, w = win.getmaxyx()
        win.addstr(y, x, text[: w - x - 1], attr)
    except curses.error:
        pass


# ──────────────────────────────────────────────────────────────────────────────
#  Entry point
# ──────────────────────────────────────────────────────────────────────────────

def main(args=None) -> None:
    rclpy.init(args=args)
    node = CameraCalClient()

    # Spin the node on a daemon thread so the main thread can drive curses.
    spin_thread = threading.Thread(
        target=rclpy.spin, args=(node,), daemon=True
    )
    spin_thread.start()

    try:
        curses.wrapper(_tui, node)
    finally:
        rclpy.shutdown()
        spin_thread.join(timeout=2.0)
        node.destroy_node()


if __name__ == "__main__":
    main()
