#!/usr/bin/env python3
"""Finds the scale for which sigmoid(score / scale) best predicts the game results in viriformat files.

Usage: fit_scale.py FILE...
"""

import os
import sys

import numpy as np

SAMPLE = 2_000_000
MATE_SCORE = 32767
CHUNK = 1 << 24


def scores_and_results(path, keep_prob):
    words = np.memmap(path, dtype=np.uint32, mode="r")
    ends = np.concatenate([np.flatnonzero(words[i:i + CHUNK] == 0) + i  # zero word is the end marker
                           for i in range(0, len(words), CHUNK)])
    scores, results = [], []
    start = 0
    while start < len(words):
        end = ends[np.searchsorted(ends, start + 8)]
        game_scores = (words[start + 8:end] >> 16).astype(np.uint16).view(np.int16)
        game_scores = game_scores[np.random.random(len(game_scores)) < keep_prob]
        scores.append(game_scores)
        results.append(np.full(len(game_scores), ((words[start + 7] >> 16) & 0xFF) / 2))  # 0, 0.5 or 1 for white
        start = end + 1
    return np.concatenate(scores), np.concatenate(results)


def loss(scale, scores, results):
    return np.mean((results - 1 / (1 + np.exp(-scores / scale))) ** 2)


def main():
    keep_prob = min(1, SAMPLE / (sum(os.path.getsize(path) for path in sys.argv[1:]) // 4))
    pairs = [scores_and_results(path, keep_prob) for path in sys.argv[1:]]
    scores = np.concatenate([s for s, _ in pairs]).astype(float)
    results = np.concatenate([r for _, r in pairs])
    keep = np.abs(scores) < MATE_SCORE
    scores, results = scores[keep], results[keep]

    # search through possible scale values directly, no smart stuff
    coarse = min(range(50, 2001, 10), key=lambda s: loss(s, scores, results))
    best = min(range(coarse - 10, coarse + 11), key=lambda s: loss(s, scores, results))
    
    print(f"{len(scores)} positions: best scale {best} (loss {loss(best, scores, results):.5f}, "
          f"at 400: {loss(400, scores, results):.5f})")


if __name__ == "__main__":
    main()
