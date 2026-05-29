#!/usr/bin/env python3
"""
Software Thermometer
Quality = Temperature (F)
Entropy = Disorder (higher = colder/lower quality)

A lightweight, dependency-free tool to measure code quality.
Works on any text/code file. Perfect for tracking AI-generated code over time.
"""

import os
import sys
import re
from collections import Counter
from pathlib import Path

def analyze_code(content: str, filename: str = "") -> dict:
    """Calculate entropy metrics and quality temperature."""
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

    # Entropy
    entropy = (
        (complexity_score * 1.8) +
        (duplicate_lines * 4.0) +
        (loc / 80.0) +
        max(0, (50 - comment_ratio) * 0.3)
    )

    base_temp = 92
    quality_temp = max(0.0, min(100.0, base_temp - (entropy * 0.55)))
    if comment_ratio < 8:
        quality_temp -= 8

    return {
        'filename': filename or 'stdin',
        'quality_temp_f': round(quality_temp, 1),
        'loc': loc,
        'total_lines': total_lines,
        'comment_ratio': round(comment_ratio, 1),
        'complexity_score': complexity_score,
        'duplicate_lines': duplicate_lines,
        'entropy_raw': round(entropy, 1),
        'interpretation': get_interpretation(quality_temp)
    }

def get_interpretation(temp: float) -> str:
    if temp >= 90: return " Excellent  clean, maintainable, low entropy"
    elif temp >= 80: return " Good  solid code with minor room for improvement"
    elif temp >= 70: return " Acceptable  functional but watch complexity/duplication"
    elif temp >= 60: return " Lukewarm  typical first-pass AI code; needs review"
    elif temp >= 50: return " Cool  noticeable entropy; refactoring recommended"
    elif temp >= 40: return " Cold  high disorder; significant technical debt"
    else: return " Frozen  unmaintainable; high risk"

def print_report(metrics: dict):
    print("\n" + "=" * 60)
    print(f"  SOFTWARE THERMOMETER  |  {metrics['filename']}")
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
    print("  Tip: Run this regularly on AI-generated code to track if")
    print("       temperature is rising (improving) or falling over time.\n")

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
            print(f"\n DIRECTORY AVERAGE QUALITY TEMPERATURE: {avg_temp:.1f}F")
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