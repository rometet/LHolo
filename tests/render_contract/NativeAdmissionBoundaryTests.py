from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from NativeAdmissionBoundary import NATIVE_ADMISSION, detach_native_admission


def extract(source):
    start = source.rindex('void submitProjectedBlockActorPass(')
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class NativeAdmissionBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.body = 'void draw() {\n' + NATIVE_ADMISSION + '    native_draw();\n}'
        self.drawing = 'void draw() {\n    native_draw();\n}'

    def test_valid_body_preserves_all_drawing_statements(self):
        self.assertEqual(detach_native_admission(self.body), self.drawing)

    def test_missing_boundary_rejected(self):
        with self.assertRaises(AssertionError):
            detach_native_admission(self.drawing)

    def test_duplicate_boundary_rejected(self):
        with self.assertRaises(AssertionError):
            detach_native_admission(self.body.replace(NATIVE_ADMISSION, NATIVE_ADMISSION * 2))

    def test_boundary_after_native_work_rejected(self):
        with self.assertRaises(AssertionError):
            detach_native_admission('void draw() {\n    native_draw();\n' + NATIVE_ADMISSION + '}')

    def test_boundary_inside_conditional_rejected(self):
        with self.assertRaises(AssertionError):
            detach_native_admission('void draw() {\n    if (ready) {\n' + NATIVE_ADMISSION + '    }\n}')

    def test_different_domain_rejected(self):
        with self.assertRaises(AssertionError):
            detach_native_admission(self.body.replace('Domain::Projection', 'Domain::Icons'))

    def test_commented_boundary_rejected(self):
        with self.assertRaises(AssertionError):
            detach_native_admission(self.body.replace(NATIVE_ADMISSION, '// ' + NATIVE_ADMISSION))

    def test_drawing_mutation_still_fails_verbatim_comparison(self):
        changed = self.body.replace('native_draw();', 'native_draw_differently();')
        self.assertNotEqual(detach_native_admission(changed), self.drawing)

    def test_control_flow_mutation_still_fails_verbatim_comparison(self):
        changed = self.body.replace('native_draw();', 'if (unsafe) native_draw();')
        self.assertNotEqual(detach_native_admission(changed), self.drawing)

    def test_actual_candidate_and_original_boundary_are_identical(self):
        path = 'src/projection/mesh/ProjectionRenderer.cpp'
        current = extract((ROOT / path).read_text(encoding='utf-8-sig'))
        original = extract(subprocess.check_output([
            'git', '-C', str(ROOT), 'show',
            '25c1b223f13d69f4ea6d3b7031999cdddf055b04:' + path
        ]).decode('utf-8'))
        self.assertEqual(current, original)
        self.assertEqual(detach_native_admission(current), detach_native_admission(original))
        with self.assertRaises(AssertionError):
            detach_native_admission(current.replace(NATIVE_ADMISSION, ''))
        moved = current.replace(NATIVE_ADMISSION, '').replace('    mce::MaterialPtr', NATIVE_ADMISSION + '    mce::MaterialPtr', 1)
        with self.assertRaises(AssertionError):
            detach_native_admission(moved)


if __name__ == '__main__':
    unittest.main(verbosity=2)
