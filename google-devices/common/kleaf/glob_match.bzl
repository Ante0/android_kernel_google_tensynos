# SPDX-License-Identifier: GPL-2.0-only

"""Glob matching library for Starlark."""

def _match_segment(seg, pattern):
    if pattern == "*":
        return True
    if "*" not in pattern:
        return seg == pattern

    parts = pattern.split("*")

    if not seg.startswith(parts[0]):
        return False
    if not seg.endswith(parts[-1]):
        return False

    start = len(parts[0])
    end = len(seg) - len(parts[-1])
    if start > end:
        return False

    for part in parts[1:-1]:
        if not part:
            continue

        idx = seg.find(part, start, end)
        if idx == -1:
            return False

        start = idx + len(part)

    return True

def glob_match(path, pattern):
    """Matches a path against a glob pattern.

    Args:
        path: The path to match.
        pattern: The glob pattern.

    Returns:
        True if the path matches the pattern, False otherwise.
    """

    path_segs = path.strip("/").split("/")
    pattern_segs = pattern.strip("/").split("/")

    for seg in path_segs:
        if not seg:
            fail("invalid path '{}'".format(path))

    for seg in pattern_segs:
        if not seg:
            fail("invalid glob pattern '{}'".format(pattern))
        if "**" in seg and seg != "**":
            fail("invalid glob pattern '{}'".format(pattern) +
                 ": recursive wildcard must be its own segment")

    # Starlark forbids recursion and while loop. Use for loop to implement DFS.
    checked = {}
    pending = [(0, 0)]  # (path_idx, pattern_idx)
    max_iters = (len(path_segs) + 1) * (len(pattern_segs) + 1) * 2
    for _ in range(max_iters):
        if not pending:
            break

        current = pending.pop()
        if current in checked:
            continue
        checked[current] = True
        path_idx, pattern_idx = current

        if path_idx >= len(path_segs) and pattern_idx >= len(pattern_segs):
            return True

        if path_idx >= len(path_segs) or pattern_idx >= len(pattern_segs):
            continue

        if pattern_segs[pattern_idx] == "**":
            if pattern_idx + 1 == len(pattern_segs):
                return True

            pending.append((path_idx + 1, pattern_idx))
            pending.append((path_idx, pattern_idx + 1))
            continue

        if _match_segment(path_segs[path_idx], pattern_segs[pattern_idx]):
            pending.append((path_idx + 1, pattern_idx + 1))
            continue

    return False

def glob_match_any(path, patterns):
    """Matches a path against a list of glob patterns.

    Args:
        path: The path to match.
        patterns: A list of glob patterns.

    Returns:
        True if the path matches any of the patterns, False otherwise.
    """
    for pattern in patterns:
        if glob_match(path, pattern):
            return True
    return False
