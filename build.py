#!/usr/bin/env python3
"""Small Zephyr build helper for the zephyr-esp32-learning repository.

Expected layout:
  build.py
  zephyr/                 (Git submodule)
  .west/                  (created by west init -l zephyr)
  app/00_blinky/          (CMakeLists.txt, prj.conf, src/main.c)
  .venv/                  (created by --setup)
  build/                  (generated, safe to delete)

This script does not change the caller's shell environment. Instead it runs
West and the serial monitor with the project-local virtual environment.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import venv

ROOT = Path(__file__).resolve().parent
VENV_DIR = ROOT / ".venv"
APPS_DIR = ROOT / "app"
BUILD_ROOT = ROOT / "build"
DEFAULT_BOARD = "heltec_wifi_lora32_v2/esp32/procpu"


def venv_python():
    return VENV_DIR / ("Scripts/python.exe" if os.name == "nt" else "bin/python")


def venv_bin():
    return VENV_DIR / ("Scripts" if os.name == "nt" else "bin")


def cmd_env():
    env = os.environ.copy()
    env["VIRTUAL_ENV"] = str(VENV_DIR)
    env["PATH"] = str(venv_bin()) + os.pathsep + env.get("PATH", "")
    env["ZEPHYR_BASE"] = str(ROOT / "zephyr")
    env.pop("PYTHONHOME", None)
    return env


def run(command, *, env=None):
    print("\n$ " + " ".join(str(p) for p in command), flush=True)
    try:
        subprocess.run([str(p) for p in command], cwd=ROOT, env=env, check=True)
    except FileNotFoundError as exc:
        raise RuntimeError(f"Executable not found: {command[0]}") from exc
    except subprocess.CalledProcessError as exc:
        raise RuntimeError(f"Command failed with exit code {exc.returncode}") from exc


def west_exe():
    executable = venv_bin() / ("west.exe" if os.name == "nt" else "west")
    if not executable.is_file():
        raise RuntimeError(
            "West is not installed in .venv. Run: python build.py --setup"
        )
    return executable


def needs_workspace():
    if not (ROOT / "zephyr" / "west.yml").is_file():
        raise RuntimeError(
            "Missing Zephyr submodule. Check: git submodule update --init"
        )
    if not (ROOT / ".west" / "config").is_file():
        raise RuntimeError("West workspace missing. Run: python build.py --setup")
    west_exe()


def setup(fetch_deps=False, fetch_blobs=False):
    if not (ROOT / "zephyr" / "west.yml").is_file():
        raise RuntimeError(
            "Initialize the Zephyr Git submodule first: git submodule update --init"
        )
    if not venv_python().is_file():
        print("Creating project-local Python environment at .venv/ ...")
        venv.EnvBuilder(with_pip=True).create(VENV_DIR)
    py = venv_python()
    requirements = ROOT / "zephyr" / "scripts" / "requirements.txt"
    run(
        [py, "-m", "pip", "install", "west", "pyserial", "-r", requirements],
        env=cmd_env(),
    )
    west = west_exe()
    if not (ROOT / ".west" / "config").is_file():
        run([west, "init", "-l", "zephyr"], env=cmd_env())
    if fetch_deps:
        run([west, "update"], env=cmd_env())
    if fetch_blobs:
        run([west, "blobs", "fetch", "hal_espressif"], env=cmd_env())
    print(
        "\nEnvironment ready. Commands use .venv automatically; activation is optional."
    )


def activate_shell():
    if not venv_python().is_file():
        raise RuntimeError("Environment missing. Run: python build.py --setup")
    if os.name != "nt":
        print(f"Run this in your shell: source '{venv_bin() / 'activate'}'")
        return
    script = venv_bin() / "Activate.ps1"
    # Spawn a *new* PowerShell: no Python program can alter its parent shell.
    escaped_script = str(script).replace("'", "''")
    escaped_root = str(ROOT).replace("'", "''")
    run(
        [
            "powershell.exe",
            "-NoExit",
            "-ExecutionPolicy",
            "Bypass",
            "-Command",
            f"& '{escaped_script}'; Set-Location -LiteralPath '{escaped_root}'",
        ]
    )


def get_paths(app_name, board):
    if not app_name or Path(app_name).name != app_name or app_name in (".", ".."):
        raise RuntimeError(
            "--app must be a single folder name under app/, e.g. 00_blinky"
        )
    app_path = APPS_DIR / app_name
    if not (app_path / "CMakeLists.txt").is_file():
        raise RuntimeError(f"Application missing: {app_path / 'CMakeLists.txt'}")
    if not board or any(
        c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_./-"
        for c in board
    ):
        raise RuntimeError("Invalid board target")
    safe_board = board.replace("/", "_")
    build_dir = BUILD_ROOT / app_name / safe_board
    return app_path, build_dir


def build_command(west, app_path, build_dir, board, pristine, configure_only, jobs):
    command = [
        west,
        "build",
        "-b",
        board,
        str(app_path),
        "-d",
        str(build_dir),
        "-p",
        "always" if pristine else "auto",
    ]

    if configure_only:
        command.append("--cmake-only")
    else:
        command.append(f"-o=-j{jobs}")

    return command


def list_ports():
    if not venv_python().is_file():
        raise RuntimeError("Environment missing. Run: python build.py --setup")
    try:
        # Serial may not be importable into the Python which invoked this script.
        # Use the project venv to query it instead.
        code = (
            "from serial.tools import list_ports\n"
            "ports = list(list_ports.comports())\n"
            "for p in ports: print(f'{p.device} | {p.description}')\n"
            "if not ports: print('No serial ports found.')"
        )
        run([venv_python(), "-c", code], env=cmd_env())
    except RuntimeError:
        raise


def monitor(port, baud):
    if not port:
        print("Specify --port COM3 (replace COM3 with your device). Available ports:")
        list_ports()
        return
    print("Serial monitor: exit with Ctrl+] (control + right bracket).")
    run([venv_python(), "-m", "serial.tools.miniterm", port, str(baud)], env=cmd_env())


def detect_esp32_port(explicit_port):
    """Use an explicit port, or a single recognized CP210x adapter."""
    if explicit_port:
        return explicit_port
    code = (
        "from serial.tools import list_ports\n"
        "ports = [p.device for p in list_ports.comports() "
        "if any(x in (p.description + ' ' + str(p.manufacturer or '')).lower() "
        "for x in ('cp210', 'silicon labs', 'silabs'))]\n"
        "print('\\n'.join(ports))\n"
    )
    try:
        result = subprocess.run(
            [str(venv_python()), "-c", code],
            cwd=ROOT,
            env=cmd_env(),
            text=True,
            capture_output=True,
            check=True,
        )
    except (FileNotFoundError, subprocess.CalledProcessError) as exc:
        raise RuntimeError(
            "Cannot detect serial ports. Run: python build.py --ports"
        ) from exc
    matches = [p.strip() for p in result.stdout.splitlines() if p.strip()]
    if len(matches) == 1:
        print(f"[all] Found one CP210x USB serial adapter: {matches[0]}")
        return matches[0]
    if not matches:
        raise RuntimeError(
            "Build succeeded, but no CP210x COM port was detected. "
            "Connect your ESP32 and use --port COM5 (your actual port). "
            "Check ports with: python build.py --ports"
        )
    raise RuntimeError(
        "Build succeeded, but multiple CP210x ports were found: "
        + ", ".join(matches)
        + ". Supply --port COMx explicitly."
    )


def all_pipeline(app_path, build_dir, board, port, baud, pristine, jobs):
    """Perform each necessary step in sequence; abort on the first failure."""
    print("\n[all] Step 1/5: Checking Python environment and West workspace...")
    if (
        not venv_python().is_file()
        or not (venv_bin() / ("west.exe" if os.name == "nt" else "west")).is_file()
        or not (ROOT / ".west" / "config").is_file()
    ):
        setup()
    needs_workspace()
    west = west_exe()
    env = cmd_env()
    serial_check = subprocess.run(
        [str(venv_python()), "-c", "import serial"],
        cwd=ROOT,
        env=env,
        capture_output=True,
    )
    if serial_check.returncode != 0:
        print("[all] pyserial missing; installing it in .venv...")
        run([venv_python(), "-m", "pip", "install", "pyserial"], env=env)

    # Modules are West-managed, not Git submodules of the application.
    if ("esp32" in board.lower() or "heltec_wifi_lora32" in board.lower()) and not (
        ROOT / "modules" / "hal" / "espressif" / "zephyr" / "module.yml"
    ).is_file():
        print("[all] Espressif HAL module not found; fetching it with West...")
        run([west, "update", "hal_espressif"], env=env)

    print("\n[all] Step 2/5: Checking CMake configuration...")
    if pristine or not (build_dir / "CMakeCache.txt").is_file():
        run(
            build_command(west, app_path, build_dir, board, pristine, True, jobs),
            env=env,
        )
    else:
        print("[all] Existing configuration detected; no separate configure needed.")

    print("\n[all] Step 3/5: Building firmware (incremental when possible)...")
    run(build_command(west, app_path, build_dir, board, False, False, jobs), env=env)

    if board.startswith("qemu_"):
        print("\n[all] Step 4/5: Starting QEMU (no flashing required)...")
        run([west, "build", "-d", str(build_dir), "-t", "run"], env=env)
        return

    if not ("esp32" in board.lower() or "heltec_wifi_lora32" in board.lower()):
        raise RuntimeError(
            "--all can flash/monitor ESP32 targets only. "
            "For another board, use --build and --flash separately."
        )
    print("\n[all] Step 4/5: Finding ESP32 serial port and flashing firmware...")
    chosen_port = detect_esp32_port(port)
    run(
        [west, "flash", "-d", str(build_dir), "--", "--esp-device", chosen_port],
        env=env,
    )

    print("\n[all] Step 5/5: Opening serial monitor (exit with Ctrl+])...")
    monitor(chosen_port, baud)


def main():
    parser = argparse.ArgumentParser(
        description="Zephyr ESP32 learning helper (app outside Zephyr submodule).",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    actions = parser.add_mutually_exclusive_group(required=True)
    actions.add_argument(
        "--all",
        action="store_true",
        help="Auto-check prerequisites, configure if needed, build, flash and monitor",
    )
    actions.add_argument(
        "--setup",
        action="store_true",
        help="Create .venv, install west/requirements, initialize workspace",
    )
    actions.add_argument(
        "-a",
        "--activate",
        action="store_true",
        help="Open a NEW PowerShell with project venv activated",
    )
    actions.add_argument(
        "--configure",
        action="store_true",
        help="Run CMake configuration only (no compilation)",
    )
    actions.add_argument(
        "-b", "--build", action="store_true", help="Build selected app and board"
    )
    actions.add_argument(
        "-c",
        "--clean",
        action="store_true",
        help="Delete selected app/board build output",
    )
    actions.add_argument(
        "-f", "--flash", action="store_true", help="Flash already built firmware"
    )
    actions.add_argument(
        "-m",
        "--monitor",
        action="store_true",
        help="Open serial console using pyserial",
    )
    actions.add_argument(
        "--ports", action="store_true", help="List available serial ports"
    )
    actions.add_argument(
        "-r", "--run", action="store_true", help="Run QEMU build (QEMU boards only)"
    )

    parser.add_argument("--app", default="00_blinky", help="Folder under app/")
    parser.add_argument("--board", default=DEFAULT_BOARD, help="Zephyr board target")
    parser.add_argument(
        "--port", help="ESP32 serial port, e.g. COM5; used for flash and monitor"
    )
    parser.add_argument(
        "--baud", type=int, default=115200, help="Monitor serial baud rate"
    )
    parser.add_argument(
        "--pristine",
        action="store_true",
        help="Discard old CMake cache before configuring or building",
    )
    parser.add_argument(
        "--fetch-deps", action="store_true", help="With --setup: also run west update"
    )
    parser.add_argument(
        "--fetch-blobs",
        action="store_true",
        help="With --setup: fetch Espressif HAL blobs",
    )
    parser.add_argument(
        "-j", "--jobs", type=int, default=8, help="Number of parallel compilation jobs"
    )

    args = parser.parse_args()
    if (args.fetch_deps or args.fetch_blobs) and not args.setup:
        parser.error("--fetch-deps and --fetch-blobs require --setup")
    if args.baud < 1:
        parser.error("--baud must be positive")

    try:
        if args.setup:
            setup(args.fetch_deps, args.fetch_blobs)
        elif args.activate:
            activate_shell()
        elif args.ports:
            list_ports()
        elif args.monitor:
            monitor(args.port, args.baud)
        else:
            app_path, build_dir = get_paths(args.app, args.board)
            if args.all:
                all_pipeline(
                    app_path,
                    build_dir,
                    args.board,
                    args.port,
                    args.baud,
                    args.pristine,
                    args.jobs,
                )
                return 0
            if args.clean:
                if build_dir.exists():
                    print(f"Removing: {build_dir}")
                    shutil.rmtree(build_dir)
                else:
                    print(f"Already clean: {build_dir}")
                return 0

            needs_workspace()
            west = west_exe()
            env = cmd_env()
            if args.configure or args.build:
                run(
                    build_command(
                        west,
                        app_path,
                        build_dir,
                        args.board,
                        args.pristine,
                        args.configure,
                        args.jobs,
                    ),
                    env=env,
                )
            elif args.flash:
                if not (build_dir / "CMakeCache.txt").is_file():
                    raise RuntimeError("No configured build. Run -b before flashing.")
                cmd = [west, "flash", "-d", str(build_dir)]
                if args.port:
                    if (
                        "esp32" not in args.board.lower()
                        and "heltec_wifi_lora32" not in args.board.lower()
                    ):
                        raise RuntimeError(
                            "--port on flash is currently supported only for ESP32 targets"
                        )
                    cmd += ["--", "--esp-device", args.port]
                run(cmd, env=env)
            elif args.run:
                if not args.board.startswith("qemu_"):
                    raise RuntimeError(
                        "--run is for QEMU boards. Use --flash for your ESP32."
                    )
                if not (build_dir / "CMakeCache.txt").is_file():
                    raise RuntimeError("No configured build. Run -b first.")
                run([west, "build", "-d", str(build_dir), "-t", "run"], env=env)
        return 0
    except (RuntimeError, OSError) as exc:
        print(f"\nERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
