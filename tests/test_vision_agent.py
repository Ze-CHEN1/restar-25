from __future__ import annotations

import argparse
import ast
import importlib.util
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("vision_agent", REPO_ROOT / "tools" / "vision_agent.py")
assert SPEC and SPEC.loader
vision_agent = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(vision_agent)


class VisionAgentTests(unittest.TestCase):
    def test_toml_reader_is_available(self) -> None:
        self.assertTrue(hasattr(vision_agent.tomllib, "load"))

    def test_runner_syntax_is_python_310_compatible(self) -> None:
        source = (REPO_ROOT / "tools" / "vision_agent.py").read_text(encoding="utf-8")
        ast.parse(source, feature_version=(3, 10))

    def test_slug_validation(self) -> None:
        for valid in ("tracker-outlier", "buff_v2", "latency.p95", "task1"):
            vision_agent.validate_slug(valid)
        for invalid in ("", "Upper", "has space", "../escape", "-prefix"):
            with self.assertRaises(ValueError):
                vision_agent.validate_slug(invalid)

    def test_template_rendering(self) -> None:
        rendered = vision_agent.render_text("# {{TITLE}}\n{{MISSING}}", {"TITLE": "测试"})
        self.assertEqual(rendered, "# 测试\n{{MISSING}}")

    def test_project_path_cannot_escape(self) -> None:
        old_root = vision_agent.ROOT
        try:
            with tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                projects = root / "research" / "projects"
                projects.mkdir(parents=True)
                vision_agent.ROOT = root
                inside = vision_agent._resolve_project_path("research/projects/existing", projects)
                self.assertEqual(inside, projects / "existing")
                with self.assertRaises(ValueError):
                    vision_agent._resolve_project_path("../outside", projects)
                with self.assertRaises(ValueError):
                    vision_agent._resolve_project_path("/tmp/outside", projects)
        finally:
            vision_agent.ROOT = old_root

    def test_build_prompt_separates_proposal_and_workspace_change(self) -> None:
        base = {
            "slug": "tracker-test",
            "title": "跟踪测试",
            "implementation_mode": "proposal",
        }
        project = REPO_ROOT / "research/projects/tracker-test"
        snapshot = project / "input-snapshots/abc"
        proposal = vision_agent.build_prompt(base, project, snapshot, "offline")
        self.assertIn("只能修改", proposal)
        self.assertIn("不得修改仓库源码", proposal)
        self.assertIn("不得访问公共互联网", proposal)
        changed = vision_agent.build_prompt({**base, "implementation_mode": "workspace-change"}, project, snapshot, "connected")
        self.assertIn("按冻结 task.md", changed)
        self.assertIn("可联网核查", changed)
        self.assertIn("不连接或控制真实相机", changed)

    def test_codex_command_applies_sandbox_and_search_mode(self) -> None:
        config = {"model": "", "reasoning_effort": "high"}
        output = REPO_ROOT / "last.md"
        with mock.patch.object(vision_agent, "locate_codex", return_value="/usr/bin/codex"):
            offline = vision_agent.codex_command(config, "prompt", output, "offline")
            connected = vision_agent.codex_command(config, "prompt", output, "connected")
        self.assertIn("workspace-write", offline)
        self.assertIn("never", offline)
        self.assertNotIn("--search", offline)
        self.assertIn("--search", connected)
        self.assertEqual(offline[-1], "prompt")

    def test_runner_config_rejects_unsafe_retry_values(self) -> None:
        old_config = vision_agent.RUNNER_CONFIG
        try:
            with tempfile.TemporaryDirectory() as temp:
                config = Path(temp) / "runner.toml"
                config.write_text(
                    'information_mode = "offline"\nmax_consecutive_runtime_failures = 0\n',
                    encoding="utf-8",
                )
                vision_agent.RUNNER_CONFIG = config
                with self.assertRaises(ValueError):
                    vision_agent.load_runner_config()
        finally:
            vision_agent.RUNNER_CONFIG = old_config

    def test_add_task_from_template(self) -> None:
        old_tasks = vision_agent.TASKS_ROOT
        old_template = vision_agent.TASK_TEMPLATE
        try:
            with tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                tasks = root / "tasks"
                template = root / "template"
                tasks.mkdir()
                template.mkdir()
                (template / "task.md").write_text("# {{TITLE}}\n", encoding="utf-8")
                (template / "config.toml").write_text('title = "{{TITLE}}"\nready = false\n', encoding="utf-8")
                vision_agent.TASKS_ROOT = tasks
                vision_agent.TASK_TEMPLATE = template
                result = vision_agent.add_task(argparse.Namespace(slug="demo-task", title="演示任务"))
                self.assertEqual(result, 0)
                self.assertEqual((tasks / "demo-task/task.md").read_text(encoding="utf-8"), "# 演示任务\n")
                self.assertIn("ready = false", (tasks / "demo-task/config.toml").read_text(encoding="utf-8"))
        finally:
            vision_agent.TASKS_ROOT = old_tasks
            vision_agent.TASK_TEMPLATE = old_template

    def test_execute_round_creates_recoverable_project(self) -> None:
        names = (
            "ROOT",
            "RESEARCH_ROOT",
            "TASKS_ROOT",
            "PROJECTS_ROOT",
            "RUNTIME_ROOT",
            "PROJECT_TEMPLATE",
            "STOP_REQUEST",
        )
        old = {name: getattr(vision_agent, name) for name in names}
        try:
            with tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                research = root / "research"
                tasks = research / "tasks"
                projects = research / "projects"
                runtime = research / "runtime"
                source = tasks / "integration"
                source.mkdir(parents=True)
                projects.mkdir()
                runtime.mkdir()
                (source / "task.md").write_text("# 集成测试\n", encoding="utf-8")
                (source / "config.toml").write_text('title = "集成测试"\nready = true\n', encoding="utf-8")
                template = root / "templates" / "vision-project"
                shutil.copytree(REPO_ROOT / "templates" / "vision-project", template)
                vision_agent.ROOT = root
                vision_agent.RESEARCH_ROOT = research
                vision_agent.TASKS_ROOT = tasks
                vision_agent.PROJECTS_ROOT = projects
                vision_agent.RUNTIME_ROOT = runtime
                vision_agent.PROJECT_TEMPLATE = template
                vision_agent.STOP_REQUEST = runtime / "stop-requested"
                item = {
                    "slug": "integration",
                    "dir": source,
                    "title": "集成测试",
                    "ready": True,
                    "enabled": True,
                    "implementation_mode": "proposal",
                    "information_mode": "offline",
                    "priority": 100,
                    "max_rounds": 0,
                    "project_path": "",
                }
                config = {
                    "codex_path": "/bin/true",
                    "model": "",
                    "reasoning_effort": "",
                    "attempt_timeout_minutes": 1,
                    "max_consecutive_runtime_failures": 3,
                    "information_mode": "offline",
                }
                self.assertEqual(vision_agent.execute_round(item, config), 0)
                project = projects / "integration"
                self.assertEqual((project / ".agent-status").read_text(encoding="utf-8"), "researching\n")
                self.assertTrue((project / "CURRENT_INPUT.md").is_file())
                self.assertTrue(any((project / "input-snapshots").iterdir()))
                self.assertEqual(vision_agent.read_runtime_state(item)["rounds"], 1)
        finally:
            for name, value in old.items():
                setattr(vision_agent, name, value)


if __name__ == "__main__":
    unittest.main()
