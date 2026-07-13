# Archive Directory

## Purpose

This directory contains files that are NOT part of the active source tree but are preserved for reference. Files are moved here rather than deleted when they contain potentially useful information but do not belong in the main project structure.

## Archival Policy

| Criteria | Action |
|----------|--------|
| Personal/developer-specific content | Move to `archive/` |
| Outdated documentation superseded by newer files | Move to `archive/` |
| Auto-generated configs with absolute/machine-specific paths | Move to `archive/` |
| Truly obsolete or empty files | Delete permanently |
| Duplicated content | Delete after merging into canonical location |

## Contents

| File | Origin | Date Archived | Reason |
|------|--------|--------------|--------|
| `CV_SECTION.txt` | Root | 2026-07-07 | Personal resume content, not project source |
| `flash_central.bat` | Root | 2026-07-07 | Hardcoded user paths (`%USERPROFILE%`), machine-specific |
| `vscode_c_cpp_properties.json` | `.vscode/` | 2026-07-07 | Auto-generated, platform-dependent, absolute paths |

## Restoration

To restore an archived file to its original location:
```bash
git mv archive/<filename> <original_path>
git commit -m "restore: bring <filename> back from archive"
```

## Notes

- Files in `archive/` are NOT compiled, deployed, or maintained.
- No guarantees are made about accuracy or completeness of archived content.
- The `archive/` directory is tracked in git but excluded from PlatformIO builds by default.
