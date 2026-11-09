"""Run a test command in its own process session and clean that session up.

A private ``dbus-run-session`` bus activates desktop services on demand
(kdeconnectd, kwalletd6, ksecretd, notification daemons, ...). They outlive
the bus they were started for, so every test run used to leave another set
behind. Each command now starts a new session (setsid); when it finishes or
times out, every process still in that session is stopped. Services of the
user's real desktop session are in a different session and are never touched.
"""
import os
import signal
import subprocess
import time
from pathlib import Path


def session_members(session_id):
    """PIDs whose session id is session_id (Linux /proc)."""
    members = []
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            stat = (entry / "stat").read_text()
        except OSError:
            continue  # exited meanwhile, or not readable
        # Fields after the parenthesised command name: state ppid pgrp session ...
        fields = stat[stat.rfind(")") + 2:].split()
        if len(fields) > 3 and int(fields[3]) == session_id and int(entry.name) != os.getpid():
            members.append(int(entry.name))
    return members


def terminate_session(session_id, grace=3.0):
    """Stop every process left in session_id: SIGTERM, then SIGKILL after grace seconds."""
    def signal_all(sig):
        for pid in session_members(session_id):
            try:
                os.kill(pid, sig)
            except (ProcessLookupError, PermissionError):
                pass
    signal_all(signal.SIGTERM)
    deadline = time.monotonic() + grace
    while session_members(session_id) and time.monotonic() < deadline:
        time.sleep(0.05)
    signal_all(signal.SIGKILL)


def run(args, *, check=False, timeout=None, input=None, capture_output=False, **kwargs):
    """subprocess.run() in a new session; leftover session processes are stopped afterwards."""
    if capture_output:
        kwargs["stdout"] = subprocess.PIPE
        kwargs["stderr"] = subprocess.PIPE
    if input is not None:
        kwargs["stdin"] = subprocess.PIPE
    kwargs["start_new_session"] = True
    with subprocess.Popen(args, **kwargs) as process:
        try:
            stdout, stderr = process.communicate(input, timeout=timeout)
        except subprocess.TimeoutExpired as expired:
            terminate_session(process.pid, grace=0.5)
            process.communicate()
            raise subprocess.TimeoutExpired(process.args, timeout,
                                            output=expired.output, stderr=expired.stderr) from None
        except BaseException:
            terminate_session(process.pid, grace=0.5)
            raise
        finally:
            # The command itself has exited (or was killed); its activated
            # services are still running in its session.
            terminate_session(process.pid)
        returncode = process.poll()
    if check and returncode:
        raise subprocess.CalledProcessError(returncode, process.args, output=stdout, stderr=stderr)
    return subprocess.CompletedProcess(process.args, returncode, stdout, stderr)
