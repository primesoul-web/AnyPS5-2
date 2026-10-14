import sys
import unittest
from pathlib import Path

from test_symbol_versions import build_image, patch, read_dynsym, IMPORTS, SHN_UNDEF, STT_FUNC


def image(names):
    entries = [(None, 1, SHN_UNDEF, 0, 0)]
    entries += [(name, 1, SHN_UNDEF, 0, 0) for name in IMPORTS]
    entries += [(name, 1, 9, STT_FUNC, 0x1200 + index * 0x20) for index, name in enumerate(names)]
    return bytes(build_image(entries, False))


class ElfDuplicateTests(unittest.TestCase):
    def test_exports_patched_to_one_name_are_rejected(self):
        cases = (
            ["sceAlphaComputeLength", "sceAlphaComputeLength_nid_disambig1", "sceBetaCopyString"],
            ["sceAlphaComputeLength_nid_postfix", "sceAlphaComputeLength_nid_disambig12", "sceBetaCopyString"],
            ["sceBetaCopyString", "sceAlphaComputeLength_nid_no_patch", "sceAlphaComputeLength_nid_no_patch_nid_no_patch_cut"],
        )
        for names in cases:
            with self.subTest(names=names):
                before = image(names)
                result, after = patch(PATCHER, before, "duplicates.so")
                self.assertEqual(result.returncode, 2, (result.returncode, result.stderr))
                self.assertIn(b"duplicate patched export name", result.stderr)
                self.assertEqual(after, before)

    def test_distinct_exports_are_still_patched(self):
        names = ["sceAlphaComputeLength_nid_disambig1", "sceBetaCopyString_nid_postfix", "sceGammaKeep_nid_no_patch_cut"]
        result, after = patch(PATCHER, image(names), "distinct.so")
        self.assertEqual(result.returncode, 0, result.stderr)
        patched = [entry[0] for entry in read_dynsym(after) if entry[1] == 9]
        self.assertEqual(len(patched), 3)
        self.assertEqual(len(set(patched)), 3)
        self.assertIn("sceGammaKeep", patched)


if __name__ == "__main__":
    PATCHER = Path(sys.argv.pop(1)).resolve()
    unittest.main()
