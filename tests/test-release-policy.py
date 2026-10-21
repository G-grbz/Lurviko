#!/usr/bin/env python3
"""Check reproducible source checksums and exact-commit release gates offline."""
import hashlib
import gzip
import importlib.util
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
import zipfile


REPO = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, REPO / ".github/scripts" / f"{name}.py")
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


gate = module("wait-release-checks")
archive = module("prepare-release")
SHA = "a" * 40


def run(status="completed", conclusion="success", sha=SHA, branch="main", event="push", number=1):
    return {"id": number, "head_sha": sha, "head_branch": branch, "event": event,
            "status": status, "conclusion": conclusion}


class ReleaseGateTests(unittest.TestCase):
    def test_other_commits_branches_and_events_never_satisfy_gate(self):
        for item in (run(sha="b" * 40), run(branch="other"), run(event="pull_request")):
            self.assertFalse(gate.successful_run({"workflow_runs": [item]}, SHA))
        self.assertFalse(gate.successful_run({"workflow_runs": []}, SHA))

    def test_latest_attempt_and_failed_checks(self):
        self.assertFalse(gate.successful_run({"workflow_runs": [run(), run(status="in_progress", number=2)]}, SHA))
        for conclusion in ("failure", "cancelled", "timed_out", "skipped"):
            with self.assertRaises(RuntimeError):
                gate.successful_run({"workflow_runs": [run(conclusion=conclusion)]}, SHA)

    def test_gate_waits_for_existing_runs_and_checks_alerts(self):
        requests = []
        def fetch(endpoint):
            requests.append(endpoint)
            if "/code-scanning/" in endpoint:
                return []
            first_ci = "/ci.yml/" in endpoint and sum("/ci.yml/" in item for item in requests) == 1
            return {"workflow_runs": [run(status="queued" if first_ci else "completed")]}
        gate.wait_for_checks("owner/repo", SHA, interval=0, fetch=fetch)
        self.assertEqual(sum("/ci.yml/" in item for item in requests), 2)
        self.assertEqual(sum("/codeql.yml/" in item for item in requests), 2)
        self.assertTrue(requests[-1].endswith("per_page=1"))

    def test_alerts_and_missing_checks_block_publication(self):
        with self.assertRaises(RuntimeError):
            gate.wait_for_checks("owner/repo", SHA, fetch=lambda endpoint:
                [{"number": 1}] if "/code-scanning/" in endpoint else {"workflow_runs": [run()]})
        with self.assertRaises(TimeoutError):
            gate.wait_for_checks("owner/repo", SHA, timeout=0,
                                 fetch=lambda _: {"workflow_runs": []})

    def test_malformed_versions_are_rejected(self):
        cmake = "project(Lurviko VERSION 1.3.2 LANGUAGES CXX)"
        self.assertEqual(archive.release_version("v1.3.2", cmake), "1.3.2")
        for tag in ("v1.3.1", "main", "v1.3.2-extra"):
            with self.assertRaises(ValueError):
                archive.release_version(tag, cmake)


class ReleaseArchiveTests(unittest.TestCase):
    def test_aur_checksum_can_be_finalized_before_the_single_source_commit(self):
        with tempfile.TemporaryDirectory(prefix="lurviko-release-policy-") as temporary:
            root = Path(temporary)
            env = dict(os.environ, GIT_CONFIG_GLOBAL="/dev/null", GIT_CONFIG_NOSYSTEM="1",
                       RELEASE_TAG="v1.3.2", GITHUB_OUTPUT=str(root / "outputs"))
            def git(*arguments):
                return subprocess.check_output(["git", *arguments], cwd=root, env=env, text=True).strip()
            git("init", "--quiet")
            (root / "CMakeLists.txt").write_text("project(Lurviko VERSION 1.3.2 LANGUAGES CXX)\n")
            (root / ".gitattributes").write_bytes((REPO / ".gitattributes").read_bytes())
            package = root / "packaging/aur/lurviko"
            package.mkdir(parents=True)
            (package / "PKGBUILD").write_text("sha256sums=('pending')\n")
            binary = root / "payload.bin"
            binary.write_bytes(bytes(range(256)))
            binary.chmod(0o755)
            (root / "payload-link").symlink_to("payload.bin")
            git("add", ".")
            def export(revision):
                subprocess.run(["python3", str(REPO / ".github/scripts/prepare-release.py")],
                               cwd=root, env=dict(env, RELEASE_REVISION=revision), check=True)
                return {suffix: (root / "release" / f"Lurviko-1.3.2.{suffix}").read_bytes()
                        for suffix in ("tar", "tar.gz", "zip")}
            preview = export(git("write-tree"))
            digest = hashlib.sha256(preview["tar"]).hexdigest()
            self.assertEqual(gzip.decompress(preview["tar.gz"]), preview["tar"])
            (package / "PKGBUILD").write_text(f"sha256sums=('{digest}')\n")
            (package / ".SRCINFO").write_text(f"sha256sums = {digest}\n")
            git("add", "packaging")
            self.assertEqual(preview, export(git("write-tree")))
            git("-c", "user.name=Release test", "-c", "user.email=release@example.invalid",
                "commit", "--quiet", "-m", "Release fixture")
            self.assertEqual(preview, export("HEAD"))
            with tarfile.open(root / "release/Lurviko-1.3.2.tar.gz") as source:
                names = source.getnames()
                self.assertFalse(any("packaging/aur" in name for name in names))
                self.assertEqual(source.extractfile("Lurviko-1.3.2/payload.bin").read(), bytes(range(256)))
                self.assertEqual(source.getmember("Lurviko-1.3.2/payload.bin").mode, 0o755)
                self.assertEqual(source.getmember("Lurviko-1.3.2/payload-link").linkname, "payload.bin")
            with zipfile.ZipFile(root / "release/Lurviko-1.3.2.zip") as source:
                self.assertEqual(source.read("Lurviko-1.3.2/payload.bin"), bytes(range(256)))
                self.assertFalse(any("packaging/aur" in name for name in source.namelist()))


if __name__ == "__main__":
    unittest.main()
