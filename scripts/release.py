"""Validate Conventional Commits and generate immutable, versioned releases."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
METADATA_PATHS = (
    "VERSION", "esphome/project_version.yaml",
    "esphome/components/wallpanel/release_version.h", "wallpanel_blueprint.yaml",
)
VERSION_PATTERN = r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
COMMIT_PATTERN = re.compile(
    r"^(feat|fix|perf|revert|docs|style|refactor|test|build|ci|chore)"
    r"(?:\([a-z0-9._/-]+\))?(!)?: (\S[^\r\n]*)$"
)


def parse_version(value):
    if not re.fullmatch(VERSION_PATTERN, value):
        raise ValueError(f"Expected a stable SemVer X.Y.Z, got {value!r}")
    parts = tuple(map(int, value.split(".")))
    if any(part > 4294967295 for part in parts):
        raise ValueError("Version segments must fit unsigned 32-bit integers")
    return parts


def conventional_commit(message):
    subject = message.splitlines()[0] if message else ""
    match = COMMIT_PATTERN.fullmatch(subject)
    if not match:
        raise ValueError(f"Expected type(scope)!: description, got {subject!r}")
    breaking = bool(match[2] or re.search(r"^BREAKING[ -]CHANGE:\s*\S", message, re.MULTILINE))
    return match[1], breaking


def git(root, *arguments):
    return subprocess.run(
        ["git", *arguments], cwd=root, check=True, text=True, stdout=subprocess.PIPE,
    ).stdout.strip()


def is_ancestor(root, ancestor, descendant):
    result = subprocess.run(
        ["git", "merge-base", "--is-ancestor", ancestor, descendant], cwd=root, check=False,
    )
    if result.returncode not in (0, 1):
        result.check_returncode()
    return result.returncode == 0


def commits(root, base="", head="HEAD"):
    output = git(root, "log", "--no-merges", "--reverse", "--format=%H%x00%B%x00",
                 f"{base}..{head}" if base else head).split("\0")
    return [(sha.strip(), message.strip()) for sha, message in zip(output[::2], output[1::2])]


def validate_commits(root, event):
    if "pull_request" in event:
        pr = event["pull_request"]
        conventional_commit(pr["title"])
        base, head = pr["base"]["sha"], pr["head"]["sha"]
    elif "merge_group" in event:
        base, head = event["merge_group"]["base_sha"], event["merge_group"]["head_sha"]
    else:
        head = event["after"]
        base = event["before"]
        if base == "0" * 40:
            parents = git(root, "show", "-s", "--format=%P", head).split()
            base = parents[0] if parents else ""
    for sha, message in commits(root, base, head):
        try:
            conventional_commit(message)
        except ValueError as error:
            raise ValueError(f"Commit {sha}: {error}") from error


def generated_files(root, version):
    parse_version(version)
    parts = ", ".join(version.split("."))
    blueprint = root / "wallpanel_blueprint.yaml"
    text, count = re.subn(
        r'^  blueprint_release: "[^"]*"$', f'  blueprint_release: "{version}"',
        blueprint.read_text(), flags=re.MULTILINE,
    )
    if count != 1:
        raise ValueError("Expected exactly one blueprint_release variable")
    text, count = re.subn(
        r"^  source_url: .+$",
        "  source_url: https://raw.githubusercontent.com/otghcloud/"
        f"ha-esp32-s3-touch-lcd5/refs/tags/v{version}/wallpanel_blueprint.yaml",
        text, flags=re.MULTILINE,
    )
    if count != 1:
        raise ValueError("Expected exactly one blueprint source_url")
    return {
        root / "VERSION": f"{version}\n",
        root / "esphome/project_version.yaml": f'"{version}"\n',
        root / "esphome/components/wallpanel/release_version.h": (
            '#pragma once\n#include <array>\n#include <cstdint>\n\nnamespace wallpanel {\n'
            f'inline constexpr char PROJECT_VERSION[] = "{version}";\n'
            f'inline constexpr std::array<uint32_t, 3> PROJECT_VERSION_PARTS{{{parts}}};\n'
            '}  // namespace wallpanel\n'
        ),
        blueprint: text,
    }


def synchronize(root, check, version=None):
    version = version if version is not None else (root / "VERSION").read_text().strip()
    for path, content in generated_files(root, version).items():
        if check:
            if not path.exists() or path.read_text() != content:
                raise ValueError(f"{path.relative_to(root)} is inconsistent with VERSION")
        else:
            path.write_text(content)
    return version


def release_source(root, ref, version):
    if git(root, "show", f"{ref}:VERSION") != version:
        raise ValueError(f"{ref} has incorrect VERSION metadata")
    if is_ancestor(root, ref, "origin/main"):
        return git(root, "rev-parse", f"{ref}^{{commit}}")
    parents = git(root, "rev-list", "--parents", "-n", "1", ref).split()
    if len(parents) != 2 or not is_ancestor(root, parents[1], "origin/main"):
        raise ValueError(f"{ref} must be based on main")
    changed = set(git(root, "diff", "--name-only", parents[1], ref).splitlines())
    subject = git(root, "log", "-1", "--format=%s", ref)
    if not changed.issubset(METADATA_PATHS) or subject != f"chore(release): Release v{version}":
        raise ValueError("A release-only commit may change only generated version metadata")
    return parents[1]


def validate_tag(root, tag):
    version = synchronize(root, True)
    if tag != f"v{version}":
        raise ValueError(f"Tag {tag!r} must match VERSION (v{version})")
    release_source(root, "HEAD", version)
    return version


def plan_release(root, bump="auto", override=""):
    if not is_ancestor(root, "HEAD", "origin/main"):
        raise ValueError("Releases must start from a commit on main")
    tags = [tag for tag in git(root, "tag", "--list", "v*").splitlines()
            if re.fullmatch("v" + VERSION_PATTERN, tag)]
    previous_tag = max(tags, key=lambda tag: parse_version(tag[1:]), default="")
    previous = parse_version(previous_tag[1:]) if previous_tag else (0, 0, 0)
    base = release_source(root, previous_tag, previous_tag[1:]) if previous_tag else ""
    if base and not is_ancestor(root, base, "HEAD"):
        raise ValueError("The previous release is not in this main snapshot's history")
    changes = commits(root, base)
    if not changes and bump == "auto" and not override:
        raise ValueError("No new commits to release")
    if override and bump != "auto":
        raise ValueError("Choose either an exact version or a bump override, not both")
    if override:
        version = override
    elif not previous_tag and bump == "auto":
        version = "0.0.1"
    else:
        if bump == "auto":
            kinds = [conventional_commit(message) for _, message in changes]
            bump = ("major" if any(breaking for _, breaking in kinds) else
                    "minor" if any(kind == "feat" for kind, _ in kinds) else "patch")
        major, minor, patch = previous
        version = {"major": f"{major + 1}.0.0", "minor": f"{major}.{minor + 1}.0",
                   "patch": f"{major}.{minor}.{patch + 1}"}[bump]
    if parse_version(version) <= previous:
        raise ValueError("The release version must be newer than every existing stable tag")
    return {"version": version, "tag": f"v{version}", "previous_tag": previous_tag,
            "source_sha": git(root, "rev-parse", "HEAD")}


def release_notes(root, previous_tag):
    base = release_source(root, previous_tag, previous_tag[1:]) if previous_tag else ""
    groups = {name: [] for name in ("Breaking changes", "Features", "Fixes", "Other changes")}
    for sha, message in commits(root, base):
        subject = message.splitlines()[0]
        if COMMIT_PATTERN.fullmatch(subject):
            kind, breaking = conventional_commit(message)
            group = ("Breaking changes" if breaking else
                     "Features" if kind == "feat" else "Fixes" if kind in ("fix", "perf") else
                     "Other changes")
        elif previous_tag:
            raise ValueError(f"Commit {sha} is not a Conventional Commit")
        else:
            group = "Other changes"
        entry = (f"- {subject} ([{sha[:7]}](https://github.com/otghcloud/"
                 f"ha-esp32-s3-touch-lcd5/commit/{sha}))")
        if group == "Breaking changes" and "\n" in message:
            entry += "\n\n" + "\n".join(f"  {line}" for line in message.split("\n", 1)[1].strip().splitlines())
        groups[group].append(entry)
    sections = [f"## {name}\n\n" + "\n".join(entries)
                for name, entries in groups.items() if entries]
    return ("\n\n".join(sections) if sections else
            f"No code changes since {previous_tag}; this release updates version metadata.") + "\n"


def prepare_candidate(root, version, previous_tag):
    if git(root, "status", "--porcelain"):
        raise ValueError("Release preparation requires a clean checkout")
    if git(root, "rev-parse", "--abbrev-ref", "HEAD") != "HEAD":
        raise ValueError("Release preparation requires a detached checkout; branches must not change")
    plan = plan_release(root, override=version)
    if plan["previous_tag"] != previous_tag:
        raise ValueError("Release tags changed during validation; start a new release run")
    notes = release_notes(root, previous_tag)
    synchronize(root, False, version)
    git(root, "add", "--", *METADATA_PATHS)
    # The first release can already match the checked-in bootstrap version.
    git(root, "commit", "--allow-empty", "-m", f"chore(release): Release v{version}")
    validate_tag(root, f"v{version}")
    git(root, "tag", "--annotate", f"v{version}", "--message", f"Release v{version}")
    output = root / "build/release-notes.md"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(notes)


def package(root, destination, tag):
    version = synchronize(root, True)
    if tag != f"v{version}":
        raise ValueError("Release tag does not match VERSION")
    destination.mkdir(parents=True, exist_ok=True)
    assets = [destination / "wallpanel_blueprint.yaml", destination / "version.json",
              destination / "example.yaml"]
    assets[0].write_bytes((root / "wallpanel_blueprint.yaml").read_bytes())
    assets[1].write_text(json.dumps({
        "version": version,
        "url": f"https://github.com/otghcloud/ha-esp32-s3-touch-lcd5/releases/tag/{tag}",
    }, indent=2) + "\n")
    assets[2].write_text(
        'substitutions:\n  device_name: wallpanel\n  friendly_name: Wallpanel\n'
        '  wifi_ssid: !secret panel_wifi_ssid\n  wifi_password: !secret panel_wifi_password\n'
        f'  wallpanel_component_source: github://otghcloud/ha-esp32-s3-touch-lcd5@{tag}\n'
        'api:\n  encryption:\n    key: !secret panel_api_key\n'
        'packages:\n  wallpanel:\n'
        '    url: https://github.com/otghcloud/ha-esp32-s3-touch-lcd5\n'
        f'    ref: {tag}\n    files: [ha-esp32-s3-touch-lcd5.yaml]\n'
    )
    (destination / "SHA256SUMS").write_text("".join(
        f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n" for path in assets
    ))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=[
        "sync", "check", "validate-tag", "package", "commits", "plan", "candidate",
    ])
    parser.add_argument("--tag")
    parser.add_argument("--version")
    parser.add_argument("--bump", choices=["auto", "patch", "minor", "major"], default="auto")
    parser.add_argument("--previous-tag", default="")
    parser.add_argument("--event", type=Path)
    parser.add_argument("--github-output", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "build/release")
    args = parser.parse_args()
    if args.command in ("sync", "check"):
        synchronize(ROOT, args.command == "check", args.version)
    elif args.command == "commits":
        if not args.event:
            parser.error("--event is required")
        validate_commits(ROOT, json.loads(args.event.read_text()))
    elif args.command == "plan":
        plan = plan_release(ROOT, args.bump, args.version or "")
        print(json.dumps(plan, indent=2))
        if args.github_output:
            with args.github_output.open("a") as output:
                output.write("".join(f"{key}={value}\n" for key, value in plan.items()))
    elif args.command == "candidate":
        if not args.version:
            parser.error("--version is required")
        prepare_candidate(ROOT, args.version, args.previous_tag)
    elif not args.tag:
        parser.error("--tag is required")
    elif args.command == "validate-tag":
        validate_tag(ROOT, args.tag)
    else:
        package(ROOT, args.output, args.tag)


if __name__ == "__main__":
    main()
