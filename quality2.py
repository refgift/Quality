#!/usr/bin/env python3
"""
Software Thermometer v2 - Negative Temperatures Edition
Quality = Temperature (F)
Entropy = Disorder (higher = colder/lower quality)

Now supports negative temperatures for truly evil code ("burning ice" territory).
"""

import os
import sys
import re
from collections import Counter

def analyze_code(content: str, filename: str = "") -> dict:
    lines = content.splitlines()
    total_lines = len(lines)
    non_empty_lines = len([l for l in lines if l.strip()])
    loc = non_empty_lines

    # Comment detection
    comment_lines = 0
    in_multiline = False
    for line in lines:
        stripped = line.strip()
        if in_multiline:
            comment_lines += 1
            if '*/' in stripped:
                in_multiline = False
            continue
        if stripped.startswith(('#', '//')) or stripped.startswith('/*'):
            comment_lines += 1
            if '/*' in stripped and '*/' not in stripped:
                in_multiline = True
        elif '/*' in stripped:
            comment_lines += 1
            if '*/' not in stripped:
                in_multiline = True

    comment_ratio = (comment_lines / max(loc, 1)) * 100

    # Complexity
    complexity_keywords = ['if', 'else if', 'elif', 'for', 'while', 'do', 'switch', 'case',
                           '&&', '||', 'try', 'catch', 'except', 'throw', 'return']
    code_lower = content.lower()
    complexity_score = sum(code_lower.count(kw) for kw in complexity_keywords)

    # Duplication
    stripped_lines = [l.strip() for l in lines if len(l.strip()) > 10]
    line_counts = Counter(stripped_lines)
    duplicate_lines = sum(count - 1 for count in line_counts.values() if count > 1)

    # Entropy (stronger weights for negative temps)
    entropy = (
        (complexity_score * 3.5) +
        (duplicate_lines * 7.0) +
        (loc / 50.0) +
        max(0, (50 - comment_ratio) * 0.8) +
        (len(re.findall(r'\b42\b|\bTODO\b|\bmagic\b|global|leak|infinite|nightmare|evil|chaos|delete|rewrite', content.lower())) * 12)
    )

    base_temp = 92
    quality_temp = base_temp - (entropy * 0.55)
    if comment_ratio < 8:
        quality_temp -= 8

    quality_temp = round(quality_temp, 1)

    return {
        'filename': filename or 'stdin',
        'quality_temp_f': quality_temp,
        'loc': loc,
        'total_lines': total_lines,
        'comment_ratio': round(comment_ratio, 1),
        'complexity_score': complexity_score,
        'duplicate_lines': duplicate_lines,
        'entropy_raw': round(entropy, 1),
        'interpretation': get_interpretation(quality_temp)
    }

def get_interpretation(temp: float) -> str:
    if temp >= 90:
        return " Excellent  clean, maintainable, low entropy"
    elif temp >= 80:
        return " Good  solid code with minor room for improvement"
    elif temp >= 70:
        return " Acceptable  functional but watch complexity/duplication"
    elif temp >= 60:
        return " Lukewarm  typical first-pass AI code; needs review"
    elif temp >= 50:
        return " Cool  noticeable entropy; refactoring recommended"
    elif temp >= 40:
        return " Cold  high disorder; significant technical debt"
    elif temp >= 20:
        return " Frozen  unmaintainable; high risk"
    elif temp >= 0:
        return " Absolute Zero  code is barely alive"
    elif temp >= -30:
        return " Burning Ice  evil entropy, actively harmful"
    elif temp >= -60:
        return " Void Code  dangerous, rewrite immediately"
    else:
        return " -77F Evil as Burning Ice  pure chaos, delete and start over"

def print_report(metrics: dict):
    print("\n" + "=" * 60)
    print(f"  SOFTWARE THERMOMETER v2  |  {metrics['filename']}")
    print("=" * 60)
    print(f"  Quality Temperature: {metrics['quality_temp_f']}F")
    print(f"  Interpretation:      {metrics['interpretation']}")
    print("-" * 60)
    print(f"  Lines of Code (LOC): {metrics['loc']}")
    print(f"  Total Lines:         {metrics['total_lines']}")
    print(f"  Comment Ratio:       {metrics['comment_ratio']}%")
    print(f"  Complexity Score:    {metrics['complexity_score']}")
    print(f"  Duplicated Lines:    {metrics['duplicate_lines']}")
    print(f"  Entropy (raw):       {metrics['entropy_raw']}")
    print("=" * 60)
    print("  Tip: Negative temps = burning ice territory. Run often.\n")

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 software_thermometer.py <file_or_directory>")
        print("       python3 software_thermometer.py -          # read from stdin")
        sys.exit(1)

    target = sys.argv[1]

    if target == "-":
        content = sys.stdin.read()
        metrics = analyze_code(content, "stdin")
        print_report(metrics)
    elif os.path.isdir(target):
        print(f"Analyzing directory: {target}\n")
        all_metrics = []
        for root, _, files in os.walk(target):
            for f in files:
                if f.endswith(('.c', '.cpp', '.h', '.hpp', '.py', '.js', '.java', '.ts', '.go', '.rs')):
                    path = os.path.join(root, f)
                    try:
                        with open(path, 'r', encoding='utf-8', errors='ignore') as fh:
                            content = fh.read()
                        m = analyze_code(content, path)
                        all_metrics.append(m)
                        print_report(m)
                    except Exception as e:
                        print(f"Error reading {path}: {e}")
        if all_metrics:
            avg_temp = sum(m['quality_temp_f'] for m in all_metrics) / len(all_metrics)
            print(f"\n DIRECTORY AVERAGE: {avg_temp:.1f}F")
    else:
        try:
            with open(target, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
            metrics = analyze_code(content, target)
            print_report(metrics)
        except FileNotFoundError:
            print(f"File not found: {target}")
            sys.exit(1)
        except Exception as e:
            print(f"Error: {e}")
            sys.exit(1)

if __name__ == "__main__":
    main()