#!/usr/bin/env python3
"""Real JSON-RPC 2.0 end-to-end test driver for NetRouteSwitcher.exe."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from typing import Any

BAD_STDERR_TOKENS = (
    "qml",
    "typeerror",
    "referenceerror",
    "[crit]",
    "[fatal]",
    "[warn]",
    "component is not ready",
    "invalid property assignment",
    "failed to load",
)

IGNORED_OFFSCREEN_STDERR = (
    "qfontdatabase: cannot find font directory",
    "note that qt no longer ships fonts",
    "this plugin does not support raise()",
    "this plugin does not support propagatesizehints()",
)


class JsonRpcHarness:
    def __init__(self, exe_path: Path, qt_dir: Path, plugin_dir: Path, data_dir: Path) -> None:
        env = os.environ.copy()
        qt_bin = str(qt_dir / "bin")
        env["PATH"] = qt_bin + os.pathsep + env.get("PATH", "")
        env["QT_PLUGIN_PATH"] = str(qt_dir / "plugins")
        env["QML_IMPORT_PATH"] = str(qt_dir / "qml")
        env["WS_QT_DIR"] = str(qt_dir)
        env["WS_QT_QML_DIR"] = str(qt_dir / "qml")
        env["QT_QUICK_CONTROLS_STYLE"] = "Basic"
        env["QT_QPA_PLATFORM"] = "offscreen"
        env["WS_PLUGIN_DIR"] = str(plugin_dir)
        env["WS_DATA_DIR"] = str(data_dir)

        self.proc = subprocess.Popen(
            [str(exe_path)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd=str(plugin_dir),
            env=env,
        )
        self._stdin_lock = threading.Lock()
        self._cv = threading.Condition()
        self._next_id = 1
        self.responses: dict[Any, dict[str, Any]] = {}
        self.events: list[dict[str, Any]] = []
        self.stdout_errors: list[str] = []
        self.stderr_lines: list[str] = []
        self.eof = False

        self._out_thread = threading.Thread(target=self._read_stdout, daemon=True)
        self._err_thread = threading.Thread(target=self._read_stderr, daemon=True)
        self._out_thread.start()
        self._err_thread.start()

    def _send_raw(self, payload: dict[str, Any]) -> None:
        assert self.proc.stdin is not None
        raw = (json.dumps(payload, ensure_ascii=False) + "\n").encode("utf-8")
        with self._stdin_lock:
            self.proc.stdin.write(raw)
            self.proc.stdin.flush()

    def _handle_incoming(self, msg: dict[str, Any]) -> None:
        method = msg.get("method")
        if method == "winshun/tool" and "id" in msg and msg["id"] is not None:
            self._send_raw({"jsonrpc": "2.0", "id": msg["id"], "result": {"ok": True}})

        with self._cv:
            if not method and "id" in msg:
                self.responses[msg["id"]] = msg
            else:
                self.events.append(msg)
            self._cv.notify_all()

    def _read_stdout(self) -> None:
        assert self.proc.stdout is not None
        while True:
            raw = self.proc.stdout.readline()
            if not raw:
                break
            if raw.startswith(b"\xef\xbb\xbf"):
                self.stdout_errors.append(f"UTF-8 BOM detected on stdout: {raw[:32]!r}")
                continue
            try:
                text = raw.decode("utf-8").strip()
            except UnicodeDecodeError as exc:
                self.stdout_errors.append(f"Invalid UTF-8 on stdout: {exc} ({raw!r})")
                continue
            if not text:
                continue
            try:
                obj = json.loads(text)
            except json.JSONDecodeError as exc:
                self.stdout_errors.append(f"Non-JSON line on stdout: {text!r} ({exc})")
                continue
            if not isinstance(obj, dict):
                self.stdout_errors.append(f"Non-object JSON on stdout: {text!r}")
                continue
            self._handle_incoming(obj)
        with self._cv:
            self.eof = True
            self._cv.notify_all()

    def _read_stderr(self) -> None:
        assert self.proc.stderr is not None
        for raw in self.proc.stderr:
            self.stderr_lines.append(raw.decode("utf-8", errors="replace").rstrip())

    def notify(self, method: str, params: dict[str, Any] | None = None) -> None:
        self._send_raw({"jsonrpc": "2.0", "method": method, "params": params or {}})

    def request(self, method: str, params: dict[str, Any] | None = None, timeout: float = 5.0) -> Any:
        req_id = self._next_id
        self._next_id += 1
        self._send_raw({"jsonrpc": "2.0", "id": req_id, "method": method, "params": params or {}})
        deadline = time.monotonic() + timeout
        with self._cv:
            while req_id not in self.responses:
                if self.eof:
                    raise AssertionError(f"Process closed stdout before replying to {method}")
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(f"Timed out waiting for response to {method}")
                self._cv.wait(remaining)
            msg = self.responses.pop(req_id)
        if "error" in msg:
            raise AssertionError(f"RPC {method} returned error: {msg['error']}")
        return msg.get("result")


def run_e2e(exe_path: Path, qt_dir: Path, repo_root: Path) -> None:
    if not exe_path.is_file():
        raise FileNotFoundError(f"Executable not found: {exe_path}")

    with tempfile.TemporaryDirectory(prefix="nrs-e2e-") as tmp:
        data_dir = Path(tmp) / "data"
        data_dir.mkdir(parents=True, exist_ok=True)
        harness = JsonRpcHarness(exe_path, qt_dir, repo_root, data_dir)
        try:
            init_res = harness.request("initialize", {
                "protocol": 1,
                "protocolVersion": "1",
                "winshun": "0.25.0",
                "pluginDir": str(repo_root),
                "dataDir": str(data_dir),
                "locale": "zh",
                "theme": "dark",
                "appearance": {"locale": "zh", "theme": "dark", "corners": "round"},
                "settings": {"syncIpv6": True, "showDisconnected": False, "autoSwitchByWifi": True},
            })
            assert isinstance(init_res, dict) and init_res.get("protocol") == 1, init_res

            q_empty = harness.request("query", {
                "queryId": 1,
                "command": "route",
                "trigger": "keyword",
                "keyword": "route",
                "text": "",
                "raw": "route ",
                "limit": 30,
            })
            items = q_empty.get("items", [])
            item_ids = {it.get("id") for it in items if isinstance(it, dict)}
            required_ids = {"open-panel", "wifi-rules", "test-connectivity", "restore-auto", "open-ncpa"}
            missing_ids = required_ids - item_ids
            assert not missing_ids, (
                f"Missing expected query items {sorted(missing_ids)}; got {sorted(x for x in item_ids if x)}"
            )

            q_wifi = harness.request("query", {
                "queryId": 4,
                "command": "route",
                "trigger": "keyword",
                "keyword": "route",
                "text": "wifi 规则",
                "raw": "route wifi 规则",
                "limit": 30,
            })
            wifi_items = q_wifi.get("items", [])
            wifi_ids = {it.get("id") for it in wifi_items if isinstance(it, dict)}
            assert "wifi-rules" in wifi_ids, (
                f"Expected 'wifi-rules' in filtered query items: {wifi_items}"
            )

            q_speed = harness.request("query", {
                "queryId": 2,
                "command": "route",
                "trigger": "keyword",
                "keyword": "route",
                "text": "测速",
                "raw": "route 测速",
                "limit": 30,
            })
            speed_items = q_speed.get("items", [])
            speed_ids = {it.get("id") for it in speed_items if isinstance(it, dict)}
            assert "test-connectivity" in speed_ids, (
                f"Expected 'test-connectivity' in filtered query items: {speed_items}"
            )
            assert "open-ncpa" not in speed_ids and "restore-auto" not in speed_ids, (
                f"Expected non-matching items filtered out for '测速', got {sorted(x for x in speed_ids if x)}"
            )

            tray = harness.request("trayMenu", {})
            tray_items = tray.get("items", [])
            tray_by_id = {
                it.get("id"): it for it in tray_items if isinstance(it, dict) and it.get("id")
            }
            assert {"open", "restore-auto", "toggle-wifi-auto", "wifi-rules"}.issubset(tray_by_id.keys()), (
                f"Missing required trayMenu items: {tray_items}"
            )
            assert tray_by_id["toggle-wifi-auto"].get("checked") is True, (
                f"Expected toggle-wifi-auto checked=True initially: {tray_by_id['toggle-wifi-auto']}"
            )

            harness.notify("menu", {"id": "toggle-wifi-auto"})
            tray_after_toggle = harness.request("trayMenu", {})
            toggled_items = {
                it.get("id"): it
                for it in tray_after_toggle.get("items", [])
                if isinstance(it, dict) and it.get("id")
            }
            assert toggled_items["toggle-wifi-auto"].get("checked") is False, (
                f"Expected toggle-wifi-auto checked=False after menu toggle: {toggled_items}"
            )
            harness.notify("menu", {"id": "toggle-wifi-auto"})

            settings_res = harness.request("showSettings", {})
            assert settings_res == {}, f"Expected {{}} from showSettings, got {settings_res}"
            time.sleep(0.8)
            assert harness.proc.poll() is None, (
                f"Process crashed/exited after showSettings (returncode={harness.proc.returncode})"
            )

            wifi_action_res = harness.request("action", {
                "action": "default",
                "item": {"id": "wifi-rules"},
            })
            assert isinstance(wifi_action_res, dict) and "effects" in wifi_action_res, wifi_action_res
            time.sleep(0.5)
            assert harness.proc.poll() is None, (
                f"Process crashed/exited after opening WifiRulesDialog (returncode={harness.proc.returncode})"
            )

            harness.notify("appearance", {
                "locale": "zh",
                "theme": "dark",
                "corners": "round",
                "nativeLook": {"dark": True, "rounded": True, "cardRadius": 10, "controlRadius": 6},
            })
            harness.notify("appearance", {
                "locale": "en",
                "theme": "light",
                "corners": "square",
                "nativeLook": {"dark": False, "rounded": False, "cardRadius": 0, "controlRadius": 4},
            })
            alive = harness.request("query", {
                "queryId": 3,
                "command": "route",
                "trigger": "keyword",
                "keyword": "route",
                "text": "",
                "raw": "route ",
                "limit": 20,
            })
            assert len(alive.get("items", [])) >= 4, alive

            show_env = os.environ.copy()
            show_env["PATH"] = str(qt_dir / "bin") + os.pathsep + show_env.get("PATH", "")
            subprocess.run(
                [str(exe_path), "--show", f"--data-dir={data_dir}"],
                env=show_env,
                check=True,
                timeout=5.0,
            )
            trigger_file = data_dir / "open.trigger"
            consumed = False
            deadline = time.monotonic() + 1.5
            while time.monotonic() < deadline:
                if not trigger_file.exists():
                    consumed = True
                    break
                time.sleep(0.05)
            assert consumed, f"Expected running plugin to consume {trigger_file}"
            assert harness.proc.poll() is None, (
                f"Process crashed/exited after --show trigger (returncode={harness.proc.returncode})"
            )

            shut_res = harness.request("shutdown", {}, timeout=5.0)
            assert shut_res == {}, f"Expected {{}} from shutdown, got {shut_res}"
            exit_code = harness.proc.wait(timeout=5.0)
            assert exit_code == 0, f"Expected exit code 0, got {exit_code}"
            assert not harness.stdout_errors, f"Stdout protocol violations: {harness.stdout_errors}"
            bad_stderr = [
                ln for ln in harness.stderr_lines
                if not any(ign in ln.lower() for ign in IGNORED_OFFSCREEN_STDERR)
                and any(tok in ln.lower() for tok in BAD_STDERR_TOKENS)
            ]
            assert not bad_stderr, f"QML or error output detected on stderr: {bad_stderr}"
            print("[OK] E2E JSON-RPC test suite passed (all checks, incl. QML offscreen load)")
        except Exception:
            if harness.stderr_lines:
                print("--- NetRouteSwitcher stderr ---", file=sys.stderr)
                for line in harness.stderr_lines:
                    print(line, file=sys.stderr)
            raise
        finally:
            if harness.proc.poll() is None:
                harness.proc.kill()
                harness.proc.wait(timeout=3.0)


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Run E2E JSON-RPC test against NetRouteSwitcher.exe")
    parser.add_argument("--exe", required=True, help="Path to NetRouteSwitcher.exe")
    parser.add_argument("--qt-dir", required=True, help="Path to Qt 6.12.0 MSVC2022 64-bit prefix")
    args = parser.parse_args()
    run_e2e(Path(args.exe).resolve(), Path(args.qt_dir).resolve(), repo_root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
