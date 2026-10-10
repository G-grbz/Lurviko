#!/usr/bin/env python3
"""Check opt-in installs, hash locks and pinned model downloads without network."""
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

ENGINE = Path(__file__).resolve().parents[1] / "tools/subtitle-ai"
sys.path.insert(0, str(ENGINE))
from gfile_subtitle_ai import download_policy, transcription
from gfile_subtitle_ai.core import UserVisibleError, ui_text
spec = importlib.util.spec_from_file_location("ai_worker_policy_fixture", ENGINE / "worker.py")
worker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(worker)


class DownloadPolicyTests(unittest.TestCase):
    def test_unsupported_python_never_installs(self):
        with patch.dict(os.environ, {}, clear=True), \
                patch.object(worker.sys, "version_info", (3, 10)), \
                patch.object(worker.subprocess, "run") as command:
            with self.assertRaisesRegex(RuntimeError, "Python 3.11"):
                worker.bootstrap_runtime(Path("/unused"))
            command.assert_not_called()

    def test_all_runtime_requirements_are_hash_locked(self):
        for name in ("requirements.txt", "requirements-translation.txt", "requirements-gpu.txt"):
            text = (ENGINE / name).read_text()
            entries = re.split(r"\n(?=[a-zA-Z0-9])", text)
            packages = [entry for entry in entries if re.match(r"^[\w.-]+==", entry)]
            self.assertTrue(packages, name)
            for package in packages:
                self.assertRegex(package, r"--hash=sha256:[0-9a-f]{64}", package.splitlines()[0])
            self.assertNotRegex(text, r"^[a-zA-Z0-9_.-]+(?:>=|~=|<=| @ )")

    def test_missing_runtime_never_installs_without_permission(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, {}, clear=True), \
                patch.object(worker, "runtime_ready", return_value=False), \
                patch.object(worker, "_ensure_private_cuda_runtime", return_value=False), \
                patch.object(worker.subprocess, "run") as command:
            with self.assertRaisesRegex(RuntimeError, "Enable AI downloads"):
                worker.bootstrap_runtime(Path(directory), translation_only=True)
            command.assert_not_called()

    def test_approved_runtime_install_uses_only_hash_checked_wheels(self):
        with tempfile.TemporaryDirectory() as directory, \
                patch.dict(os.environ, {"LURVIKO_SUBTITLE_AI_AUTO_INSTALL": "1"}, clear=True), \
                patch.object(worker, "runtime_ready", side_effect=[False, True]), \
                patch.object(worker, "_ensure_private_cuda_runtime", return_value=False), \
                patch.object(worker.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "")) as command:
            worker.bootstrap_runtime(Path(directory), translation_only=True)
            args = command.call_args.args[0]
            self.assertIn("--require-hashes", args)
            self.assertIn("--only-binary=:all:", args)
            self.assertIn("--upgrade", args)  # Repairs partially installed target directories.
            self.assertIn(str(ENGINE / "requirements-translation.txt"), args)

    def test_ready_runtime_is_not_upgraded_even_with_permission(self):
        with patch.dict(os.environ, {"LURVIKO_SUBTITLE_AI_AUTO_INSTALL": "1"}, clear=True), \
                patch.object(worker, "runtime_ready", return_value=True), \
                patch.object(worker, "_ensure_private_cuda_runtime", return_value=False), \
                patch.object(worker.subprocess, "run") as command:
            worker.bootstrap_runtime(Path("/unused"), translation_only=True)
            command.assert_not_called()

    def test_gpu_install_requires_permission_and_a_hash_lock(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, {}, clear=True), \
                patch.object(worker.shutil, "which", return_value="nvidia-smi"), \
                patch.object(worker.platform, "machine", return_value="x86_64"), \
                patch.object(worker, "_preload_private_cuda_runtime", return_value=False), \
                patch.object(worker.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "")) as command:
            worker._ensure_private_cuda_runtime(Path(directory))
            command.assert_not_called()
            os.environ["LURVIKO_SUBTITLE_AI_AUTO_INSTALL"] = "1"
            worker._ensure_private_cuda_runtime(Path(directory))
            args = command.call_args.args[0]
            self.assertIn("--require-hashes", args)
            self.assertIn(str(ENGINE / "requirements-gpu.txt"), args)

    def test_custom_models_require_full_commit_revisions(self):
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaises(UserVisibleError):
                download_policy.model_spec("example/custom", "asr")
            os.environ["LURVIKO_SUBTITLE_AI_ASR_MODEL_REVISION"] = "main"
            with self.assertRaises(UserVisibleError):
                download_policy.model_spec("example/custom", "asr")
            os.environ["LURVIKO_SUBTITLE_AI_ASR_MODEL_REVISION"] = "a" * 40
            self.assertEqual(download_policy.model_spec("example/custom", "asr"), ("example/custom", "a" * 40))

    def test_uncached_models_do_not_download_without_permission(self):
        for kind, model, prepare in (("asr", "large-v3", transcription._prepare_local_model),
                ("translation", transcription.DEFAULT_TRANSLATION_MODEL, transcription._prepare_translation_model)):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as directory, \
                    patch.dict(os.environ, {"LURVIKO_SUBTITLE_AI_ASR_MODEL_DIR": directory,
                        "LURVIKO_SUBTITLE_AI_TRANSLATION_MODEL_DIR": directory}, clear=True), \
                    patch.object(transcription, "_saved_model_snapshot", return_value=None):
                snapshot = Mock(side_effect=FileNotFoundError)
                with patch.dict(sys.modules, {"huggingface_hub": SimpleNamespace(snapshot_download=snapshot)}):
                    with self.assertRaisesRegex(UserVisibleError, "Enable AI downloads"):
                        prepare(model, lambda text: None, None)
                    self.assertEqual(snapshot.call_count, 1)
                    kwargs = snapshot.call_args.kwargs
                    self.assertTrue(kwargs["local_files_only"])
                    self.assertRegex(kwargs["revision"], r"^[0-9a-f]{40}$")

    def test_approved_and_cached_models_keep_the_same_revision(self):
        for kind, model, prepare in (("asr", "large-v3", transcription._prepare_local_model),
                ("translation", transcription.DEFAULT_TRANSLATION_MODEL, transcription._prepare_translation_model)):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as directory, \
                    patch.dict(os.environ, {"LURVIKO_SUBTITLE_AI_ALLOW_MODEL_DOWNLOAD": "1",
                        "LURVIKO_SUBTITLE_AI_ASR_MODEL_DIR": directory,
                        "LURVIKO_SUBTITLE_AI_TRANSLATION_MODEL_DIR": directory}, clear=True), \
                    patch.object(transcription, "_saved_model_snapshot", return_value=None), \
                    patch.object(transcription, "_remember_model_snapshot"):
                repository, revision = download_policy.model_spec(model, kind)
                target = Path(directory) / ("models--" + repository.replace("/", "--")) / "snapshots" / revision
                target.mkdir(parents=True)
                snapshot = Mock(side_effect=[FileNotFoundError(), str(target)])
                with patch.dict(sys.modules, {"huggingface_hub": SimpleNamespace(snapshot_download=snapshot)}):
                    self.assertEqual(prepare(model, lambda text: None, None), target)
                for call in snapshot.call_args_list:
                    self.assertEqual(call.kwargs["revision"], revision)
                    self.assertEqual(call.kwargs["repo_id"], repository)
                os.environ["LURVIKO_SUBTITLE_AI_ALLOW_MODEL_DOWNLOAD"] = "0"
                cached = Mock(return_value=str(target))
                with patch.dict(sys.modules, {"huggingface_hub": SimpleNamespace(snapshot_download=cached)}):
                    self.assertEqual(prepare(model, lambda text: None, None), target)
                self.assertTrue(cached.call_args.kwargs["local_files_only"])

    def test_error_language_follows_explicit_ui_language(self):
        with patch.dict(os.environ, {}, clear=True):
            self.assertIn("Enable AI downloads", ui_text("error_model_permission"))
            os.environ["LURVIKO_SUBTITLE_AI_LANGUAGE"] = "tr"
            self.assertIn("izin verin", ui_text("error_model_permission"))


if __name__ == "__main__":
    unittest.main()
