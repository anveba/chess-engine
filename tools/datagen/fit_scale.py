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
        end_index = np.searchsorted(ends, start + 8)
        if end_index == len(ends):  # unfinished last game, e.g. while datagen is still writing
            break
        end = ends[end_index]
        game_scores = (words[start + 8:end] >> 16).astype(np.uint16).view(np.int16)
        game_scores = game_scores[np.random.random(len(game_scores)) < keep_prob]
        scores.append(game_scores)
        results.append(np.full(len(game_scores), ((words[start + 7] >> 16) & 0xFF) / 2))  # 0, 0.5 or 1 for white
        start = end + 1
    return np.concatenate(scores), np.concatenate(results)


def loss(scale, scores, results, weights):
    return np.average((results - 1 / (1 + np.exp(-scores / scale))) ** 2, weights=weights)


def fit(scores, results, weights):
    # search through possible scale values directly, no smart stuff
    coarse = min(range(50, 2001, 10), key=lambda s: loss(s, scores, results, weights))
    return min(range(coarse - 10, coarse + 11), key=lambda s: loss(s, scores, results, weights))


def main():
    samples = []
    for path in sys.argv[1:]:
        max_positions = os.path.getsize(path) // 4  # each position takes at least 4 bytes
        keep_prob = min(1, SAMPLE / max_positions)
        scores, results = scores_and_results(path, keep_prob)
        scores = scores.astype(float)
        keep = np.abs(scores) < MATE_SCORE
        scores, results = scores[keep], results[keep]
        weights = np.full(len(scores), 1 / keep_prob)  # each sampled position stands for 1 / keep_prob positions
        samples.append((scores, results, weights))
        best = fit(scores, results, weights)
        print(f"{path}: ~{len(scores) / keep_prob / 1e6:.0f}M positions, best scale {best} "
              f"(loss {loss(best, scores, results, weights):.5f}, at 400: {loss(400, scores, results, weights):.5f})")

    if len(samples) > 1:
        scores, results, weights = (np.concatenate(parts) for parts in zip(*samples))
        best = fit(scores, results, weights)
        print(f"Combined: best scale {best} (loss {loss(best, scores, results, weights):.5f}, "
              f"at 400: {loss(400, scores, results, weights):.5f})")


if __name__ == "__main__":
    main()
