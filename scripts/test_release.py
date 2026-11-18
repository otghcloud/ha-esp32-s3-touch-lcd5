import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from contextlib import contextmanager

import release


class ReleaseTests(unittest.TestCase):
    @contextmanager
    def repository(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "VERSION").write_text("0.0.1\n")
            (root / ".gitignore").write_text("/build/\n")
            (root / "wallpanel_blueprint.yaml").write_text(
                'blueprint:\n  source_url: https://example.com/main/wallpanel_blueprint.yaml\n'
                'variables:\n  blueprint_release: "0.0.1"\n'
            )
            (root / "esphome/components/wallpanel").mkdir(parents=True)
            release.synchronize(root, False)
            release.git(root, "init", "--initial-branch=main")
            release.git(root, "config", "user.name", "Release tests")
            release.git(root, "config", "user.email", "tests@example.invalid")
            release.git(root, "add", ".")
            self.commit(root, "Legacy initial commit")
            yield root

    def commit(self, root, message):
        source = root / "source.txt"
        source.write_text((source.read_text() if source.exists() else "") + message + "\n")
        release.git(root, "add", "source.txt")
        release.git(root, "commit", "--quiet", "-m", message)
        release.git(root, "update-ref", "refs/remotes/origin/main", "HEAD")
        return release.git(root, "rev-parse", "HEAD")

    def test_stable_versions_are_strict_and_numeric(self):
        self.assertGreater(release.parse_version("1.10.0"), release.parse_version("1.2.9"))
        self.assertEqual(release.parse_version("4294967295.4294967295.4294967295"), (4294967295,) * 3)
        for value in ("v1.2.3", "01.2.3", "1.2", "1.2.3.4", "1.2.3-beta.1", "1.2.4294967296"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                release.parse_version(value)

    def test_sync_detects_drift_and_packages_latest_channel_assets_with_checksums(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "VERSION").write_text("0.1.0\n")
            (root / "wallpanel_blueprint.yaml").write_text(
                'blueprint:\n  source_url: https://example.com/main/wallpanel_blueprint.yaml\n'
                'variables:\n  blueprint_release: "0.0.1"\n'
            )
            (root / "esphome/components/wallpanel").mkdir(parents=True)
            with self.assertRaises(ValueError):
                release.synchronize(root, True)
            release.synchronize(root, False)
            self.assertEqual(release.synchronize(root, True), "0.1.0")
            output = root / "assets"
            with self.assertRaises(ValueError):
                release.package(root, output, "v0.0.1")
            release.package(root, output, "v0.1.0")
            self.assertEqual(json.loads((output / "version.json").read_text())["version"], "0.1.0")
            example = (output / "example.yaml").read_text()
            self.assertNotIn("wallpanel_component_source:", example)
            self.assertIn("ref: latest", example)
            self.assertIn("refresh: 300s", example)
            self.assertNotIn("ref: main", example)
            self.assertIn("/refs/tags/latest/", (output / "wallpanel_blueprint.yaml").read_text())
            for line in (output / "SHA256SUMS").read_text().splitlines():
                digest, name = line.split("  ")
                self.assertEqual(hashlib.sha256((output / name).read_bytes()).hexdigest(), digest)
            (root / "esphome/project_version.yaml").write_text('"0.0.1"\n')
            with self.assertRaises(ValueError):
                release.package(root, output, "v0.1.0")

    def test_conventional_commit_headers_and_breaking_footers(self):
        for message, expected in (
                ("feat(ui): Add controls", ("feat", False)),
                ("fix!: Remove legacy support", ("fix", True)),
                ("refactor: Simplify\n\nBREAKING CHANGE: Configure a new endpoint", ("refactor", True)),
                ("chore: Cleanup\n\nBREAKING-CHANGE: Remove option", ("chore", True))):
            with self.subTest(message=message):
                self.assertEqual(release.conventional_commit(message), expected)
        for message in ("Fix something", "feature: Something", "feat: ", "fix(): Broken", "fix: \nOops"):
            with self.subTest(message=message), self.assertRaises(ValueError):
                release.conventional_commit(message)

    def test_first_release_handles_legacy_history_and_updates_only_a_tag_commit(self):
        with self.repository() as root:
            source = release.git(root, "rev-parse", "main")
            plan = release.plan_release(root)
            self.assertEqual(plan["version"], "0.0.1")
            with self.assertRaisesRegex(ValueError, "detached checkout"):
                release.prepare_candidate(root, plan["version"], "")
            release.git(root, "checkout", "--quiet", "--detach", "main")
            release.prepare_candidate(root, plan["version"], "")
            self.assertEqual(release.git(root, "rev-parse", "main"), source)
            self.assertEqual(release.git(root, "rev-parse", "HEAD^"), source)
            self.assertEqual(release.validate_tag(root, "v0.0.1"), "0.0.1")
            self.assertIn("Legacy initial commit", (root / "build/release-notes.md").read_text())
            with self.assertRaises(ValueError):
                release.validate_tag(root, "v0.0.2")
            release.git(root, "checkout", "--quiet", "main")
            with self.assertRaisesRegex(ValueError, "No new commits"):
                release.plan_release(root)
            self.commit(root, "feat: Add controls")
            self.assertEqual(release.plan_release(root)["version"], "0.1.0")
            release.git(root, "checkout", "--quiet", "--detach", "main")
            release.prepare_candidate(root, "0.1.0", "v0.0.1")
            release.package(root, root / "build/assets", "v0.1.0")
            self.assertEqual(release.synchronize(root, True), "0.1.0")
            self.assertEqual(release.git(root, "show", "main:VERSION"), "0.0.1")
            self.assertIn("/refs/tags/latest/", (root / "wallpanel_blueprint.yaml").read_text())
            self.assertEqual(
                set(release.git(root, "diff", "--name-only", "HEAD^", "HEAD").splitlines()),
                set(release.METADATA_PATHS),
            )
            self.assertIn("Add controls", (root / "build/release-notes.md").read_text())
            self.assertNotIn("Legacy initial commit", (root / "build/release-notes.md").read_text())

    def test_bump_uses_all_commits_and_numeric_tags(self):
        cases = (
            (["fix: Repair", "docs: Explain"], "1.2.10"),
            (["feat: Add", "fix: Repair"], "1.3.0"),
            (["feat: Add", "refactor!: Remove", "fix: Repair"], "2.0.0"),
            (["fix: Repair\n\nBREAKING CHANGE: Replace option"], "2.0.0"),
        )
        for messages, expected in cases:
            with self.subTest(messages=messages), self.repository() as root:
                release.synchronize(root, False, "1.2.9")
                release.git(root, "add", ".")
                self.commit(root, "chore: Establish previous release")
                release.git(root, "tag", "v1.2.9")
                release.git(root, "tag", "not-a-release")
                for message in messages:
                    self.commit(root, message)
                self.assertEqual(release.plan_release(root)["version"], expected)
                self.assertEqual(release.plan_release(root, "patch")["version"], "1.2.10")
                self.assertEqual(release.plan_release(root, override="3.0.0")["version"], "3.0.0")
                for value in ("1.2.9", "1.0.0", "v3.0.0", "3.0.0-beta.1", "3.0.4294967296"):
                    with self.assertRaises(ValueError):
                        release.plan_release(root, override=value)
                with self.assertRaises(ValueError):
                    release.plan_release(root, "minor", "3.0.0")

    def test_pr_and_push_checks_validate_new_commits_but_not_legacy_history(self):
        with self.repository() as root:
            base = release.git(root, "rev-parse", "HEAD")
            head = self.commit(root, "fix: Repair hold detection")
            pr = {"title": "fix(ui): Repair holds", "base": {"sha": base}, "head": {"sha": head}}
            release.validate_commits(root, {"pull_request": pr})
            release.validate_commits(root, {"before": base, "after": head})
            release.validate_commits(root, {"merge_group": {"base_sha": base, "head_sha": head}})
            with self.assertRaises(ValueError):
                release.validate_commits(root, {"pull_request": {**pr, "title": "Repair holds"}})
            head = self.commit(root, "Bad commit message")
            with self.assertRaisesRegex(ValueError, head):
                release.validate_commits(root, {"before": base, "after": head})

    def test_candidate_rejects_dirty_checkout_and_tags_changed_during_validation(self):
        with self.repository() as root:
            (root / "unexpected.txt").write_text("Do not overwrite")
            with self.assertRaisesRegex(ValueError, "clean checkout"):
                release.prepare_candidate(root, "0.0.1", "")
            (root / "unexpected.txt").unlink()
            release.git(root, "tag", "v0.0.1")
            self.commit(root, "fix: Repair")
            release.git(root, "checkout", "--quiet", "--detach", "main")
            with self.assertRaisesRegex(ValueError, "tags changed"):
                release.prepare_candidate(root, "0.0.2", "")
            self.assertEqual(release.git(root, "tag", "--list"), "v0.0.1")

    def test_release_commit_cannot_include_source_changes_or_come_from_outside_main(self):
        with self.repository() as root:
            release.git(root, "checkout", "--quiet", "--detach", "main")
            (root / "unexpected.txt").write_text("Source change")
            release.git(root, "add", ".")
            release.git(root, "commit", "--quiet", "-m", "chore(release): Release v0.0.1")
            with self.assertRaisesRegex(ValueError, "only generated"):
                release.validate_tag(root, "v0.0.1")
            with self.assertRaisesRegex(ValueError, "on main"):
                release.plan_release(root)
            release.git(root, "commit", "--quiet", "--allow-empty", "-m", "chore(release): Release v0.0.1")
            with self.assertRaisesRegex(ValueError, "based on main"):
                release.validate_tag(root, "v0.0.1")

    def test_release_notes_include_breaking_messages_and_reject_new_nonconventional_commits(self):
        with self.repository() as root:
            release.git(root, "tag", "v0.0.1")
            self.commit(root, "feat(ui): New controls")
            self.commit(root, "fix: Repair")
            self.commit(root, "ci: Update tools")
            self.commit(root, "refactor!: Remove option\n\nBREAKING CHANGE: Select the replacement option")
            notes = release.release_notes(root, "v0.0.1")
            for content in ("## Breaking changes", "## Features", "## Fixes", "## Other changes",
                            "Select the replacement option"):
                self.assertIn(content, notes)
            self.assertNotIn("Legacy initial commit", notes)
            self.commit(root, "Not conventional")
            with self.assertRaises(ValueError):
                release.release_notes(root, "v0.0.1")
            with self.assertRaises(ValueError):
                release.plan_release(root)

    def test_numeric_latest_tag_and_bump_overflow(self):
        with self.repository() as root:
            release.synchronize(root, False, "1.10.0")
            release.git(root, "add", ".")
            self.commit(root, "chore: Establish previous release")
            release.git(root, "tag", "v1.2.9")
            release.git(root, "tag", "v1.10.0")
            self.commit(root, "fix: Repair")
            self.assertEqual(release.plan_release(root)["version"], "1.10.1")
            release.synchronize(root, False, "4294967295.0.0")
            release.git(root, "add", ".")
            self.commit(root, "chore: Establish upper bound")
            release.git(root, "tag", "v4294967295.0.0")
            self.commit(root, "feat!: Replace everything")
            with self.assertRaisesRegex(ValueError, "32-bit"):
                release.plan_release(root)

    def test_explicit_version_or_bump_can_publish_without_new_code(self):
        with self.repository() as root:
            release.git(root, "tag", "v0.0.1")
            with self.assertRaisesRegex(ValueError, "No new commits"):
                release.plan_release(root)
            self.assertEqual(release.plan_release(root, "minor")["version"], "0.1.0")
            release.git(root, "checkout", "--quiet", "--detach", "main")
            release.prepare_candidate(root, "1.0.0", "v0.0.1")
            self.assertEqual(release.validate_tag(root, "v1.0.0"), "1.0.0")
            self.assertIn("No code changes since v0.0.1", (root / "build/release-notes.md").read_text())


if __name__ == "__main__":
    unittest.main()
