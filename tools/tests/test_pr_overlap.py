import json
import os
import subprocess
import tempfile
import sys
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

    def real_pull_requests(self, main_changes, older_changes, current_changes):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name).resolve()
        self.assertTrue(root.is_relative_to(Path(tempfile.gettempdir()).resolve()))
        self.addCleanup(os.chdir, os.getcwd())
        os.chdir(root)

        def git(*args):
            return subprocess.run(
                ["git", "-c", "user.name=Overlap Test", "-c", "user.email=test@example.com", *args],
                capture_output=True, text=True, encoding="utf-8", check=True).stdout.strip()

        git("init", "-q")
        git("config", "commit.gpgsign", "false")
        for name in ("shared.cpp", "other.cpp", "unrelated.cpp"):
            (root / name).write_text("int value = 0;\n", encoding="utf-8")
        git("add", ".")
        git("commit", "-qm", "test: create base")
        base = git("rev-parse", "HEAD")

        def branch(name, parent, changes):
            git("checkout", "-qb", name, parent)
            for path, value in changes.items():
                (root / path).write_text(f"int value = {value};\n", encoding="utf-8")
            git("add", ".")
            git("commit", "-qm", "test: change values", "--allow-empty")
            return git("rev-parse", "HEAD")

        older = branch("older", base, older_changes)
        main = branch("updated-base", base, main_changes)
        current = branch("current", main, current_changes)
        git("update-ref", "refs/pr/base", main)
        prs = {1: {"ref": current, "author": "current-author", "depends": set()},
               2: {"ref": older, "author": "older-author", "depends": set()}}
        for pr in prs.values():
            pr_overlap.scan(pr)
        return prs

    def test_reports_shared_files_when_older_pr_conflicts_with_main(self):
        prs = self.real_pull_requests({"shared.cpp": 2}, {"shared.cpp": 1}, {"shared.cpp": 3})
        self.assertIsNone(prs[2]["tree"])
        self.assertIsNotNone(prs[1]["tree"])
        found = pr_overlap.overlaps(prs, 1, 2)
        self.assertIsNotNone(found)
        self.assertEqual(found["uncompared"], {"shared.cpp"})
        report = pr_overlap.report({2: found}, "https://example.invalid/blob/base")
        self.assertIn("could not be completed", report)
        self.assertIn("`shared.cpp`", report)
        self.assertNotIn("Merging one will break the other", report)

    def test_conflict_in_another_file_does_not_hide_shared_changes(self):
        prs = self.real_pull_requests({"other.cpp": 2}, {"other.cpp": 1, "shared.cpp": 1}, {"shared.cpp": 3})
        self.assertIsNone(prs[2]["tree"])
        found = pr_overlap.overlaps(prs, 1, 2)
        self.assertIsNotNone(found)
        self.assertEqual(found["uncompared"], {"shared.cpp"})

    def test_unmergeable_pr_with_no_shared_files_is_not_reported(self):
        prs = self.real_pull_requests({"other.cpp": 2}, {"other.cpp": 1}, {"unrelated.cpp": 3})
        self.assertIsNone(prs[2]["tree"])
        self.assertIsNone(pr_overlap.overlaps(prs, 1, 2))

    def test_mergeable_branches_still_report_confirmed_line_conflicts(self):
        prs = self.real_pull_requests({}, {"shared.cpp": 1}, {"shared.cpp": 2})
        self.assertTrue(all(pr["tree"] for pr in prs.values()))
        found = pr_overlap.overlaps(prs, 1, 2)
        self.assertEqual(found["conflicts"], {"shared.cpp": [(1, 1)]})
        self.assertNotIn("uncompared", found)

    def test_mixed_report_keeps_confirmed_and_uncompared_sections(self):
        rows = {2: {"added": set(), "conflicts": {"other.cpp": [(1, 1)]}},
                3: {"added": set(), "uncompared": {"shared.cpp"}}}
        report = pr_overlap.report(rows, "https://example.invalid/blob/base")
        self.assertIn("[L1]", report)
        self.assertIn("could not be completed", report)
        self.assertIn("`shared.cpp`", report)
        self.assertTrue(report.startswith(pr_overlap.MARKER))


if __name__ == "__main__":
    unittest.main()
