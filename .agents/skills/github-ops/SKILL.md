---
name: github-ops
description: GitHub operations for this machine and project - repo creation, push/pull, gh CLI usage, Git LFS for UE5 binary assets, and network diagnostics when github.com is blocked. Use whenever the user mentions GitHub, gh, pushing the repo, remote backup, creating repositories, or Git LFS; also use when git push/fetch to GitHub fails or times out.
---

# GitHub Operations (github-ops)

Operates the project's GitHub remote (org `QHXRPG-A`) with gh CLI + git + LFS, handling this machine's network reality and UE5 binary assets.

## Prerequisites check (always start here)

1. `gh --version` — if missing, install: `winget install --id GitHub.cli --silent` (or download release zip via API, see Network bypass).
2. `gh auth status` — if not logged in, the USER must run `gh auth login` interactively (browser flow). Do not attempt to auth non-interactively without a user-provided token.
3. Network probe (this machine has a history of github.com IP blocking — conditions change, always re-probe before diagnosing):

```
python - <<'PY'
import socket, ssl
for h in ["github.com", "api.github.com", "ssh.github.com",
          "media.githubusercontent.com", "github-cloud.s3.amazonaws.com"]:
    try:
        ctx = ssl.create_default_context()
        s = ctx.wrap_socket(socket.create_connection((h, 443), timeout=6), server_hostname=h)
        s.close(); print("OK  ", h)
    except Exception as e:
        print("FAIL", h, type(e).__name__)
PY
```

Interpretation (observed on this machine, 2026-08-14/15):
- `github.com` FAIL + `api.github.com` OK → only the web/git host is blocked. See Network bypass.
- `media.githubusercontent.com` / `github-cloud.s3.amazonaws.com` are what LFS actually uses — if these are OK, `git push` with LFS can still work when the main host resolves via redirect chains, but the initial `git-receive-pack` to github.com must succeed, so github.com itself must be reachable for push.
- `ssh.github.com:443` consistently FAILS with SSLError → do not use SSH-over-443 transport; use HTTPS.

## Network bypass (when github.com is blocked)

Download release assets through the API endpoint, which resolves to a different, reachable IP:

```
# 1. get asset id
curl -s https://api.github.com/repos/<owner>/<repo>/releases/latest
# 2. download via API (302 -> objects.githubusercontent.com, reachable)
curl -sL -H "Accept: application/octet-stream" -o file.zip \
  https://api.github.com/repos/<owner>/<repo>/releases/assets/<asset_id>
```

Release source zips: `https://codeload.github.com/<owner>/<repo>/zip/refs/tags/<tag>` (codeload is reachable even when github.com is not).
Do NOT retry blocked hosts in a loop; do NOT recommend `ghfast.top`-style mirrors (sandbox blocks them and they are untrusted third parties).

## UE5 project repo rules (this project: GuLiStrike)

- **LFS is mandatory** for `.uasset`, `.umap`, `.uexp`, `.ubulk`, `.dll`, `.pdb`, `.upk`. This repo has 600MB+ maps; plain git would bloat history beyond GitHub limits (2GB/file hard cap).
- `.gitattributes` LFS rules must exist BEFORE first push. Setup:
  ```
  git lfs install
  git lfs migrate import --include="*.uasset,*.umap,*.uexp,*.ubulk" --everything
  ```
  `migrate --everything` rewrites history so old versions also become LFS pointers.
- **Warning**: LFS free tier = 1GB storage + 1GB/month bandwidth. This project's map history (~2.5GB across 4 versions) exceeds it. Options: buy a $5 data pack, or `migrate` then squash history to current-state-only (keep the full 3.2GB `.git` as a local cold mirror first!).
- Never commit: `Saved/`, `Intermediate/`, `Binaries/`, `DerivedDataCache/`, `.vs/`, `*.sln`, `Downloads/`, `__pycache__/`. All already in `.gitignore` — verify with `git status --ignored | head` before pushing.
- Working-tree `git lfs ls-files | wc -l` should return ~1100+ files for this project.

## Repo creation + push (org QHXRPG-A)

```
gh repo create QHXRPG-A/<name> --private --confirm        # or --public
git remote add origin https://github.com/QHXRPG-A/<name>.git
git push -u origin main
```

- Always `--private` unless the user explicitly says public.
- Large first push (LFS): expect long upload; run in background, monitor with `git lfs ls-files` and the push output file. If bandwidth exceeded, GitHub returns 403 with a quota URL — surface it to the user, do not retry.
- If push hangs >2 min with zero bytes, re-probe network; if github.com went dark again, stop and tell the user (transport-level block, nothing to fix client-side).

## Common tasks

| Task | Command |
|---|---|
| Check auth | `gh auth status` |
| Create private repo | `gh repo create QHXRPG-A/<n> --private` |
| List org repos | `gh repo list QHXRPG-A --limit 50` |
| View repo on web | `gh repo view --web` (needs github.com reachable) |
| Release download (blocked-net) | API asset endpoint, see Network bypass |
| LFS status | `git lfs ls-files \| wc -l` / `git lfs env` |
| Prune local LFS cache | `git lfs prune` |

## Known pitfalls from this project

1. `git lfs migrate` rewrites ALL commit hashes — remote and local clones diverge. Do it BEFORE first push, never after.
2. CRLF warnings on Windows (`LF will be replaced by CRLF`) are noise; do not "fix" them mid-push.
3. The 615MB `LVL_Main.umap` history: 4 versions exist in `.git`. If quota errors occur, the cold-mirror-then-squash path is the sanctioned escape hatch — but ONLY with the user's explicit approval (destroys rollback history).
4. `ssh.github.com:443` fails with SSLError on this network — HTTPS remote URLs only, no SSH.
