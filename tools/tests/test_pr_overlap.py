import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import pr_overlap


class OpenPullRequestsTests(unittest.TestCase):
    def test_collects_pull_requests_beyond_the_previous_limit(self):
        pull_requests = [
            {"number": number, "user": {"login": f"author{number}"},
             "head": {"sha": f"sha{number}"}, "body": None}
            for number in range(1, 202)
        ]
        pull_requests[-1]["body"] = "Depends on: #12\n- #34\n"

        def response(*args):
            if args[0] == "api":
                return json.dumps(pull_requests)
            limit = int(args[args.index("-L") + 1])
            return json.dumps([
                {"number": pr["number"], "author": pr["user"], "headRefOid": pr["head"]["sha"],
                 "body": pr["body"]} for pr in pull_requests[:limit]
            ])

        with patch.object(pr_overlap, "REPO", "owner/project", create=True), \
                patch.object(pr_overlap, "gh", side_effect=response):
            found = pr_overlap.open_prs("main")
        self.assertEqual(set(found), set(range(1, 202)))
        self.assertEqual(found[201], {
            "ref": "refs/pr/201", "sha": "sha201", "author": "author201", "depends": {12, 34},
        })
        self.assertEqual(found[1]["depends"], set())

    def test_base_branch_is_encoded_in_the_paginated_request(self):
        with patch.object(pr_overlap, "REPO", "owner/project", create=True), \
                patch.object(pr_overlap, "gh", return_value="[]") as gh:
            self.assertEqual(pr_overlap.open_prs("topic/one&two"), {})
        gh.assert_called_once_with(
            "api", "--paginate",
            "repos/owner/project/pulls?state=open&base=topic%2Fone%26two&per_page=100",
        )

    def test_single_page_preserves_author_head_and_dependencies(self):
        pull_requests = [{"number": 7, "user": {"login": "contributor"}, "head": {"sha": "head-sha"},
                          "body": "Depends on: none\nUnrelated issue #42"}]
        with patch.object(pr_overlap, "REPO", "owner/project", create=True), \
                patch.object(pr_overlap, "gh", return_value=json.dumps(pull_requests)):
            found = pr_overlap.open_prs("main")
        self.assertEqual(found, {
            7: {"ref": "refs/pr/7", "sha": "head-sha", "author": "contributor", "depends": set()},
        })

    def test_collects_concatenated_json_pages_from_paginated_gh_output(self):
        pull_requests = [
            {"number": number, "user": {"login": f"author{number}"},
             "head": {"sha": f"sha{number}"}, "body": None}
            for number in range(1, 4)
        ]
        pages = [pull_requests[:2], pull_requests[2:]]
        output = "\n".join(json.dumps(page) for page in pages)

        with patch.object(pr_overlap, "REPO", "owner/project", create=True), \
                patch.object(pr_overlap, "gh", return_value=output):
            found = pr_overlap.open_prs("main")

        self.assertEqual(set(found), {1, 2, 3})
        self.assertEqual(found[3]["sha"], "sha3")

    def test_rejects_empty_paginated_output(self):
        with patch.object(pr_overlap, "REPO", "owner/project", create=True), \
                patch.object(pr_overlap, "gh", return_value="  \n"):
            with self.assertRaisesRegex(ValueError, "returned no JSON pages"):
                pr_overlap.open_prs("main")


class FetchTests(unittest.TestCase):
    def test_fetches_each_head_commit_the_api_reported(self):
        prs = {
            7: {"ref": "refs/pr/7", "sha": "a" * 40, "author": "one", "depends": set()},
            9: {"ref": "refs/pr/9", "sha": "b" * 40, "author": "two", "depends": set()},
        }
        with patch.object(pr_overlap, "REPO", "owner/project", create=True), \
                patch.object(pr_overlap, "git", return_value="") as git:
            pr_overlap.fetch("main", prs)
        git.assert_called_once_with(
            "fetch", "--no-tags", "-q", "https://github.com/owner/project.git",
            "+refs/heads/main:refs/pr/base", f"+{'a' * 40}:refs/pr/7", f"+{'b' * 40}:refs/pr/9",
        )

    def test_batches_large_sets_of_pull_request_heads(self):
        prs = {
            number: {
                "ref": f"refs/pr/{number}", "sha": f"{number:040x}",
                "author": "contributor", "depends": set(),
            }
            for number in range(250)
        }
        with patch.object(pr_overlap, "REPO", "owner/project", create=True), \
                patch.object(pr_overlap, "git", return_value="") as git:
            pr_overlap.fetch("main", prs)
        self.assertEqual(git.call_count, 3)
        self.assertEqual([len(call.args) - 4 for call in git.call_args_list], [100, 100, 51])
        self.assertEqual(git.call_args_list[0].args[4], "+refs/heads/main:refs/pr/base")
        self.assertEqual(git.call_args_list[-1].args[-1], "+00000000000000000000000000000000000000f9:refs/pr/249")


class ConflictTests(unittest.TestCase):
    def test_uses_the_shared_base_parent_without_newer_merge_tree_option(self):
        first = {"tree": "first-tree", "files": {"shared.cpp"}, "added": set()}
        second = {"tree": "second-tree", "files": {"shared.cpp"}, "added": set()}
        with patch.object(pr_overlap, "merge", return_value=("merged-tree", set())) as merge:
            self.assertEqual(pr_overlap.conflicts(first, second), {})
        merge.assert_called_once_with("first-tree", "second-tree")


class CommentStateTests(unittest.TestCase):
    def setUp(self):
        if os.name == "nt" or not all(shutil.which(tool) for tool in ("bash", "jq", "sed", "sha256sum")):
            self.skipTest("the workflow step runs in bash with jq, sed and sha256sum on Linux")
        lines = (Path(__file__).resolve().parents[2] / ".github/workflows/pr-overlap.yml").read_text(encoding="utf-8").splitlines()
        start = next(index for index, line in enumerate(lines) if "tools/pr_overlap.py" in line)
        indent = len(lines[start]) - len(lines[start].lstrip())
        while start > 0 and len(lines[start - 1]) - len(lines[start - 1].lstrip()) == indent:
            start -= 1
        step = []
        for line in lines[start:]:
            if line.strip() and len(line) - len(line.lstrip()) < indent:
                break
            step.append(line[indent:])
        self.step = "\n".join(step).replace("${{ github.event.repository.default_branch }}", "main")
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "overlap").mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def report(self, commit, lines):
        found = {7: {"conflicts": {"core/a.cpp": [lines]}, "added": set()}}
        return pr_overlap.report(found, f"https://github.com/owner/project/blob/{commit}")

    def run_step(self, report, comment):
        (self.root / "overlap" / "42.md").write_text(report, encoding="utf-8", newline="\n")
        (self.root / "comment").write_text(json.dumps([5, comment]) + "\n" if comment else "", encoding="utf-8", newline="\n")
        calls = self.root / "calls"
        calls.write_text("", encoding="utf-8")
        script = "\n".join([
            "set -eo pipefail",
            "python3() { :; }",
            'gh() { printf \'%s\\n\' "$*" >> "$RUNNER_TEMP/calls"; case "$*" in "api repos/"*) cat "$RUNNER_TEMP/comment";; esac; }',
            self.step,
        ])
        (self.root / "step.sh").write_text(script, encoding="utf-8", newline="\n")
        environment = dict(os.environ, RUNNER_TEMP=str(self.root), GH_REPO="owner/project")
        result = subprocess.run(["bash", "step.sh"], cwd=self.root, env=environment, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return [call.split(" ")[:3] for call in calls.read_text(encoding="utf-8").splitlines()]

    def test_new_main_commit_alone_makes_no_requests(self):
        first = self.report("a" * 40, (3, 4))
        self.assertEqual(self.run_step(first, None), [["api", "repos/owner/project/issues/42/comments", "--paginate"], ["pr", "comment", "42"]])
        self.assertEqual(self.run_step(self.report("b" * 40, (3, 4)), first), [])

    def test_lost_state_reads_the_comment_without_rewriting_it(self):
        (self.root / "overlap-state.json").write_text("{}", encoding="utf-8")
        self.assertEqual(self.run_step(self.report("b" * 40, (3, 4)), self.report("a" * 40, (3, 4))), [["api", "repos/owner/project/issues/42/comments", "--paginate"]])

    def test_changed_lines_still_update_the_comment(self):
        first = self.report("a" * 40, (3, 4))
        self.run_step(first, None)
        self.assertEqual(self.run_step(self.report("b" * 40, (5, 6)), first), [["api", "repos/owner/project/issues/42/comments", "--paginate"], ["api", "-X", "PATCH"]])

if __name__ == "__main__":
    unittest.main()
