"""Testes da implementacao: python -m unittest discover -s tests -v"""
import os
import sys
import unittest

import numpy as np
from scipy import stats

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "python"))
import friedman_analysis as fa  # noqa: E402

TOY = np.array([[120, 95, 80], [140, 110, 90], [100, 105, 70],
                [160, 120, 100], [130, 100, 115], [150, 125, 95]], dtype=float)


class TestFriedman(unittest.TestCase):
    def test_toy_example(self):
        r = fa.friedman(TOY)
        self.assertEqual(list(r["R"]), [17.0, 12.0, 7.0])
        self.assertAlmostEqual(r["Q"], 8.3333, places=3)
        self.assertAlmostEqual(r["p"], 0.0155, places=4)
        self.assertAlmostEqual(r["F"], 11.3636, places=3)
        self.assertAlmostEqual(r["W"], 0.6944, places=3)

    def test_matches_scipy_with_and_without_ties(self):
        rng = np.random.default_rng(0)
        for _ in range(30):
            X = rng.integers(0, 6, size=(rng.integers(5, 40), rng.integers(3, 8))).astype(float)  # muitos empates
            r = fa.friedman(X)
            sc = stats.friedmanchisquare(*X.T)
            self.assertAlmostEqual(r["Q"], sc.statistic, places=8)
            self.assertAlmostEqual(r["p"], sc.pvalue, places=8)

    def test_exact_p_small_case(self):
        # n=6, k=3, Q=9.333: p exato = 0.00553 (enumeracao de 6^6 tabelas), menor que o 0.0094 do qui-quadrado
        r = fa.friedman(np.array([[3, 2, 1], [2, 3, 1], [2, 3, 1], [2, 3, 1], [3, 2, 1], [2, 3, 1]], dtype=float))
        self.assertAlmostEqual(r["Q"], 9.3333, places=3)
        self.assertAlmostEqual(fa.exact_pvalue(r["Q"], 6, 3), 0.00553, places=5)

    def test_demsar_2006_table6(self):
        # Demsar (2006), Tab. 6: N = 14, k = 4, postos medios -> chi2_F = 9.28 e F_F = 3.69
        chi2_f, f_f = fa.friedman_from_mean_ranks([3.143, 2.000, 2.893, 1.964], 14)
        self.assertAlmostEqual(chi2_f, 9.28, places=2)
        self.assertAlmostEqual(f_f, 3.69, places=2)

    def test_nemenyi_cd_demsar(self):
        cd05, _ = fa.nemenyi_cd(4, 14, 0.05)
        cd10, _ = fa.nemenyi_cd(4, 14, 0.10)
        self.assertAlmostEqual(cd05, 1.25, places=2)
        self.assertAlmostEqual(cd10, 1.12, places=2)

    def test_holm(self):
        adj = fa.holm([0.01, 0.04, 0.03])
        np.testing.assert_allclose(adj, [0.03, 0.06, 0.06])


if __name__ == "__main__":
    unittest.main()
