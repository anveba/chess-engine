#!/usr/bin/env python3
"""Finds the scale for which sigmoid(score / scale) best predicts the game results in viriformat files.

Usage: fit_scale.py FILE...
"""

import sys

import numpy as np

SAMPLE = 2_000_000
MATE_SCORE = 32767


def scores_and_results(path):
    words = np.fromfile(path, dtype=np.uint32)
    ends = np.flatnonzero(words == 0) # zero word is the end marker
    scores, results = [], []
    start = 0
    while start < len(words):
        end = ends[np.searchsorted(ends, start + 8)]
        game_scores = (words[start + 8:end] >> 16).astype(np.uint16).view(np.int16)
        scores.append(game_scores)
        results.append(np.full(len(game_scores), ((words[start + 7] >> 16) & 0xFF) / 2))  # 0, 0.5 or 1 for white
        start = end + 1
    return np.concatenate(scores), np.concatenate(results)


def loss(scale, scores, results):
    return np.mean((results - 1 / (1 + np.exp(-scores / scale))) ** 2)


def main():
    pairs = [scores_and_results(path) for path in sys.argv[1:]]
    scores = np.concatenate([s for s, _ in pairs]).astype(float)
    results = np.concatenate([r for _, r in pairs])
    keep = np.abs(scores) < MATE_SCORE
    scores, results = scores[keep], results[keep]

    sample = np.random.choice(len(scores), min(SAMPLE, len(scores)), replace=False)
    scores, results = scores[sample], results[sample]

    # search through possible scale values directly, no smart stuff
    coarse = min(range(50, 2001, 10), key=lambda s: loss(s, scores, results))
    best = min(range(coarse - 10, coarse + 11), key=lambda s: loss(s, scores, results))
    
    print(f"{len(scores)} positions: best scale {best} (loss {loss(best, scores, results):.5f}, "
          f"at 400: {loss(400, scores, results):.5f})")


if __name__ == "__main__":
    main()
