#!/usr/bin/env python3
"""Reuse the exact tagged commit's main-branch CI/CodeQL runs before publishing."""
import json
import os
import re
import subprocess
import time
from urllib.parse import urlencode


WORKFLOWS = ("ci.yml", "codeql.yml")


def successful_run(payload, sha):
    runs = [run for run in payload["workflow_runs"]
            if run["head_sha"] == sha and run["head_branch"] == "main" and run["event"] == "push"]
    if not runs:
        return False
    run = max(runs, key=lambda item: item["id"])
    if run["status"] != "completed":
        return False
    if run["conclusion"] != "success":
        raise RuntimeError(f"Required check did not pass: {run.get('html_url', run['id'])} ({run['conclusion']})")
    return True


def api(endpoint):
    result = subprocess.run(["gh", "api", endpoint], check=True, capture_output=True, text=True)
    return json.loads(result.stdout)


def wait_for_checks(repository, sha, timeout=4200, interval=20, fetch=api):
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError("Invalid repository")
    if not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", sha):
        raise ValueError("Invalid commit SHA")
    query = urlencode({"branch": "main", "event": "push", "head_sha": sha, "per_page": 100})
    deadline = time.monotonic() + timeout
    previous = None
    while True:
        pending = [workflow for workflow in WORKFLOWS if not successful_run(
            fetch(f"repos/{repository}/actions/workflows/{workflow}/runs?{query}"), sha)]
        if not pending:
            # CodeQL analysis jobs can succeed while reporting findings. Do not
            # publish over an open finding just because the workflow is green.
            alerts = fetch(f"repos/{repository}/code-scanning/alerts?state=open&ref=refs/heads/main&per_page=1")
            if alerts:
                raise RuntimeError("Open CodeQL findings require review before publication")
            print(f"Build/tests and CodeQL passed for {sha}; no open CodeQL findings.", flush=True)
            return
        if pending != previous:
            print(f"Waiting for existing commit checks: {', '.join(pending)}", flush=True)
            previous = pending
        if time.monotonic() >= deadline:
            raise TimeoutError("Commit checks were not successful within the release timeout")
        time.sleep(interval)


if __name__ == "__main__":
    if os.environ.get("GITHUB_REF_TYPE") != "tag":
        raise SystemExit("Select a version tag; publish its commit to main before creating the release.")
    wait_for_checks(os.environ["GITHUB_REPOSITORY"], os.environ["GITHUB_SHA"])
