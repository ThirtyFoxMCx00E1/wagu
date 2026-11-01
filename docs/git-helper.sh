#!/usr/bin/env bash
# MP Git helper (3.40.2) - removes ONLY Claude from a GitHub repo's history. Run it in Termux
# (or any Linux / Termius SSH box). The web Termux console's bot shows which repos need it:
#   https://thirtyfoxmcx00e1.github.io/wagu/termux.html
#
#   curl -fsSL https://thirtyfoxmcx00e1.github.io/wagu/git-helper.sh | bash -s -- scan  OWNER/REPO
#   curl -fsSL https://thirtyfoxmcx00e1.github.io/wagu/git-helper.sh | bash -s -- clean OWNER/REPO          # dry run
#   curl -fsSL https://thirtyfoxmcx00e1.github.io/wagu/git-helper.sh | bash -s -- clean OWNER/REPO --push   # rewrite + force-push
#
# What "remove Claude" means here (nothing else in the history is touched):
#   - commits AUTHORED by Claude (claude / claude[bot] / Claude Code / @anthropic.com) get a human author:
#     the commit's committer when that is a person, otherwise --as "Name <email>" (default: you, from gh / git config)
#   - "Co-authored-by: Claude ..." lines and "Generated with [Claude Code]" lines are removed from messages
#   - every other commit, author, file and message stays exactly as it is (only commit ids change after
#     the first rewritten commit - that is how git works)
#   - people whose name only starts with "Claude" (e.g. Claudemirovsky) are NOT touched
# Safety: a full backup (OWNER-REPO-backup-<date>.bundle) is written before anything is pushed, the default
# is a dry run, and --push only force-pushes branches + tags (never deletes anything else).
set -euo pipefail

say()  { printf '\033[1;32m[git-helper]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[git-helper]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31m[git-helper]\033[0m %s\n' "$*" >&2; exit 1; }

CMD="${1:-}"; REPO="${2:-}"; shift 2 2>/dev/null || true
PUSH=0; AS=""
while [ $# -gt 0 ]; do
  case "$1" in
    --push) PUSH=1 ;;
    --as) AS="${2:-}"; shift ;;
    *) die "unknown option: $1" ;;
  esac; shift
done
[ -n "$CMD" ] && [ -n "$REPO" ] || { sed -n '2,10p' "$0" 2>/dev/null || true; die "usage: git-helper.sh scan|clean OWNER/REPO [--push] [--as \"Name <email>\"]"; }
case "$REPO" in */*) ;; *) die "REPO must be OWNER/REPO (e.g. ThirtyFoxMCx00E1/archify)";; esac

need() {   # Termux: pkg, Debian/Ubuntu: apt
  command -v "$1" >/dev/null 2>&1 && return 0
  say "installing $1"
  if command -v pkg >/dev/null 2>&1; then pkg install -y "$2" >/dev/null
  elif command -v apt-get >/dev/null 2>&1; then sudo apt-get install -y "$2" >/dev/null
  else die "please install $1"; fi
}
need git git; need python3 python
if ! command -v git-filter-repo >/dev/null 2>&1; then
  say "installing git-filter-repo"
  python3 -m pip install --user --quiet git-filter-repo 2>/dev/null || pip install --quiet git-filter-repo
  export PATH="$HOME/.local/bin:$PATH"
fi
command -v git-filter-repo >/dev/null 2>&1 || die "git-filter-repo is missing (pip install git-filter-repo)"

WORK="${GIT_HELPER_DIR:-$HOME/git-helper}"; mkdir -p "$WORK"
DIR="$WORK/$(echo "$REPO" | tr '/' '-').git"
URL="${GIT_HELPER_URL:-https://github.com/$REPO.git}"
if [ -d "$DIR" ]; then say "updating $DIR"; git -C "$DIR" fetch -q --prune origin '+refs/heads/*:refs/heads/*' '+refs/tags/*:refs/tags/*'
else say "cloning $URL (bare, all branches + tags)"; git clone -q --bare "$URL" "$DIR"; fi
cd "$DIR"

# --- who is Claude (exact identities only) ---
IS_CLAUDE_PY='
import re
_N = re.compile(rb"^(claude|claude\[bot\]|claude code|claude ai|anthropic claude)$", re.I)
def is_claude(name, email):
    n = (name or b"").strip(); e = (email or b"").strip().lower()
    return bool(_N.match(n)) or e.endswith(b"@anthropic.com") or b"+claude[bot]@" in e or e in (b"claude@users.noreply.github.com", b"claude[bot]@users.noreply.github.com")
_CO = re.compile(rb"^[ \t]*co-authored-by:[ \t]*(.*?)[ \t]*<([^>]*)>[ \t]*$", re.I)
_GEN = re.compile(rb"generated with \[?claude( code)?\]?|\xf0\x9f\xa4\x96 generated with", re.I)
def clean_msg(m):
    out = []
    for line in m.split(b"\n"):
        c = _CO.match(line)
        if c and is_claude(c.group(1), c.group(2)): continue
        if _GEN.search(line): continue
        out.append(line)
    m2 = b"\n".join(out)
    while m2.endswith(b"\n\n\n"): m2 = m2[:-1]
    return m2
'

say "scanning $REPO"
python3 - "$IS_CLAUDE_PY" <<'PY'
import subprocess, sys
exec(sys.argv[1])
raw = subprocess.run(["git", "log", "--all", "--format=%H%x00%an%x00%ae%x00%B%x01"], capture_output=True).stdout
a = c = 0
for rec in raw.split(b"\x01"):
    rec = rec.lstrip(b"\n")
    if not rec: continue
    h, n, e, msg = (rec.split(b"\x00", 3) + [b""] * 4)[:4]
    if is_claude(n, e): a += 1
    elif clean_msg(msg) != msg: c += 1
print("  commits authored by Claude:          %d" % a)
print("  commits with a Claude co-author line: %d" % c)
open("/tmp/.git-helper-count", "w").write(str(a + c))
PY
N=$(cat /tmp/.git-helper-count 2>/dev/null || echo 0)
if [ "$N" = "0" ]; then say "no Claude in $REPO - nothing to do, history kept as it is"; exit 0; fi
[ "$CMD" = "scan" ] && { say "run 'clean' to remove them (dry run first)"; exit 0; }
[ "$CMD" = "clean" ] || die "unknown command: $CMD"

# --- fallback human identity ---
if [ -z "$AS" ]; then
  if command -v gh >/dev/null 2>&1 && gh api user >/dev/null 2>&1; then
    AS="$(gh api user --jq '"\(.name // .login) <\(.id)+\(.login)@users.noreply.github.com>"')"
  else
    AS="$(git config --global user.name || echo "${REPO%%/*}") <$(git config --global user.email || echo "${REPO%%/*}@users.noreply.github.com")>"
  fi
fi
AS_NAME="${AS% <*}"; AS_MAIL="${AS##*<}"; AS_MAIL="${AS_MAIL%>}"
say "Claude-authored commits get this author when the committer is not a person: $AS_NAME <$AS_MAIL>"

STAMP=$(date +%Y%m%d-%H%M%S)
BUNDLE="$WORK/$(echo "$REPO" | tr '/' '-')-backup-$STAMP.bundle"
git bundle create -q "$BUNDLE" --all
say "backup: $BUNDLE  (restore: git clone $BUNDLE)"

CB="$IS_CLAUDE_PY
AS_N = b'''$AS_NAME'''.replace(b\"'\", b\"\")
AS_E = b'''$AS_MAIL'''
def human(n, e):
    return not is_claude(n, e) and e.lower() not in (b'noreply@github.com',) and not e.lower().endswith(b'[bot]@users.noreply.github.com')
if is_claude(commit.author_name, commit.author_email):
    if human(commit.committer_name, commit.committer_email):
        commit.author_name, commit.author_email = commit.committer_name, commit.committer_email
    else:
        commit.author_name, commit.author_email = AS_N, AS_E
if is_claude(commit.committer_name, commit.committer_email):
    commit.committer_name, commit.committer_email = commit.author_name, commit.author_email
commit.message = clean_msg(commit.message)
"
say "rewriting history (only Claude commits / lines change)"
git filter-repo --force --quiet --commit-callback "$CB" --refs $(git for-each-ref --format='%(refname)' refs/heads refs/tags)
git remote add origin "$URL" 2>/dev/null || git remote set-url origin "$URL"

python3 - "$IS_CLAUDE_PY" <<'PY'
import subprocess, sys
exec(sys.argv[1])
raw = subprocess.run(["git", "log", "--all", "--format=%an%x00%ae%x00%cn%x00%ce%x00%B%x01"], capture_output=True).stdout
left = 0
for rec in raw.split(b"\x01"):
    rec = rec.lstrip(b"\n")
    if not rec: continue
    an, ae, cn, ce, msg = (rec.split(b"\x00", 4) + [b""] * 5)[:5]
    if is_claude(an, ae) or is_claude(cn, ce) or clean_msg(msg) != msg: left += 1
print("  Claude left after the rewrite: %d" % left)
PY

if [ "$PUSH" != "1" ]; then
  say "dry run done. The rewritten copy is in $DIR"
  say "to publish it run the same command with --push (force-pushes branches + tags of $REPO)"
  exit 0
fi
if command -v gh >/dev/null 2>&1; then gh auth setup-git >/dev/null 2>&1 || true; fi
say "force-pushing branches + tags to $REPO"
git push --force origin 'refs/heads/*:refs/heads/*' 'refs/tags/*:refs/tags/*'
say "done. GitHub's contributors list can take a while to refresh. Backup: $BUNDLE"
