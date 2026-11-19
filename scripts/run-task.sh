#!/usr/bin/env bash
# run-task.sh N [PLAN] — give task N of a plan to dsh (headless), then run the
# gate. The orchestrator (a Claude session) reviews every commit afterwards;
# this script makes no judgement calls.
#
#   scripts/run-task.sh 3
#   DRY=1 scripts/run-task.sh 3        print the prompt only
#   DSH_PROFILE=headless-off scripts/run-task.sh 3   another dsh profile
#                                      (headless-off: thinking off, 10-17 trial)
#   RESTARTS=8 scripts/run-task.sh 3   re-dispatch up to 8 times while dsh
#                                      exits without committing
#
# Gate: make test && make uvm2 (no warning:/error: lines from src/, tests/ or
# tools/; the SDK's Rust warnings are not ours). No make clean: it deletes
# build_uvm2 and the picotool rebuild takes minutes. (Copied from
# vectrex-veecat 2026-11-07.)
# Logs: ~/.dsh/scratch/tasks/<repo>/task-N-{dsh,gate}.log, OUTSIDE the repo
# (2026-10-16: with them in .tasks/, dsh found its own live log while
# exploring, took it for "a previous attempt" and re-read it for hours).
# A re-run rotates the previous dsh/gate logs to task-N-*.attemptK.log.
set -uo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO" || exit 1
N="${1:?usage: run-task.sh N [PLAN]}"
PLAN="${2:-$(ls -1 docs/superpowers/plans/*.md | tail -1)}"
LOGS="${TASK_LOGS:-$HOME/.dsh/scratch/tasks/$(basename "$REPO")}"

task_text() {
    awk -v n="$1" '
        $0 ~ "^#+ Task " n ":"            { on = 1; print; next }
        on && $0 ~ "^#+ Task [0-9]+:"     { exit }
        on && $0 ~ "^## Milestone"        { exit }
        on                                { print }
    ' "$PLAN"
}

header() {   # the plan's preamble: goal, constraints, review focus, file map
    awk '/^### Task 1:/ { exit } { print }' "$PLAN"
}

TASK="$(task_text "$N")"
[[ -n "${TASK// }" ]] || { echo "task $N not found in $PLAN" >&2; exit 2; }
TITLE="$(head -1 <<< "$TASK" | sed -E 's/^#+ +//')"

PROMPT="Implement $TITLE

You are implementing ONE task of an approved plan for uvmc2-menu, the game
menu for the Ultimate Vectrex Multicart 2, in $REPO. Read AGENTS.md first.

RULES
- The task says what to build and how to prove it works; the design and
  code are yours. Write the tests the task asks for first and watch them
  fail on the current code (a test that does not compile yet counts).
- Facts the plan states as confirmed ('What is wrong', numbers, file and
  function names) were checked by the orchestrator: build on them, don't
  re-derive them. If one turns out wrong, say so in a Plan-deviation trailer.
- Where the task does give code, use it as written. If it is wrong (does not
  compile, a test fails on a real case, or it contradicts the plan), you may
  correct it, but end the commit message with a trailer:
      Plan-deviation: <what the task said> -> <what you did>, and why.
- Stop instead of researching: if a proof or fix would need reverse-
  engineering an emulator, the toolchain or a library (reading their source,
  fetching it from the web), or you have tried three approaches to the same
  sub-problem, write docs/superpowers/blocked-task-$N.md with the exact
  question and what you tried, and do not commit. A short blocked note is a
  good outcome; hours of research are not.
- Working loop: while you work, run only make test. Run the full gate
  once, when you believe you are done:
      make test && make uvm2
  (make test: '0 failed' from every test program, no warnings; make uvm2: no 'warning:' or
  'error:' line for a file under src/, tests/ or tools/). Never run make clean.
  It must pass before you commit.
- Keep tests fast: a new test takes seconds, and make test stays under two
  minutes. Cache or vectorise rather than recompute.
- If you learn something about the SDK or the toolchain that AGENTS.md
  doesn't say, add it to docs/superpowers/lessons.md (one short entry:
  symptom, cause, how you confirmed it) and include it in the commit.
- Never weaken, skip or delete a test to pass the gate. If a test looks
  wrong, stop: write docs/superpowers/blocked-task-$N.md explaining why, and
  do not commit.
- Touch only the files the task names.
- Every number in the commit message (test counts, sizes) comes from
  a command you ran in this session. Don't state anything about earlier
  commits or other code that you did not check.
- Commit once, at the end, with the message the task gives as its title line, then a
  body with the proof: the tests added, what each checks, and the commands
  you ran with their results (a commit with no body is reported as missing
  its proof). Add no
  Co-Authored-By or other attribution trailers (earlier commits' trailers
  are the orchestrator's, not yours).

=========================== PLAN PREAMBLE ===========================
$(header)
============================== YOUR TASK ==============================
$TASK"

BASE_PROMPT="$PROMPT"
RESUME_NOTE="

=========================== RESUMING ===========================
An earlier attempt at this task stopped before committing and left its work
in the tree (see git status / git diff). Its notes are in
$LOGS/task-$N-dsh.attempt*.log (newest last; read the last few hundred lines,
not the whole file) and $LOGS/task-$N-gate.attempt*.log. Continue from that
work instead of starting over: read it, finish it, and get the gate to pass."

rotate_logs() {   # an earlier attempt's logs -> attemptK (so a resume can point at them)
    mkdir -p "$LOGS"
    if [[ -f "$LOGS/task-$N-dsh.log" ]]; then
        local k=1
        while [[ -e "$LOGS/task-$N-dsh.attempt$k.log" ]]; do k=$((k + 1)); done
        mv "$LOGS/task-$N-dsh.log" "$LOGS/task-$N-dsh.attempt$k.log"
        [[ -f "$LOGS/task-$N-gate.log" ]] && mv "$LOGS/task-$N-gate.log" "$LOGS/task-$N-gate.attempt$k.log"
    fi
    return 0
}

prompt_now() {
    if [[ -n "$(git status --porcelain -- . ':!.tasks')" ]]; then
        printf '%s%s' "$BASE_PROMPT" "$RESUME_NOTE"
    else
        printf '%s' "$BASE_PROMPT"
    fi
}

if [[ "${DRY:-0}" == "1" ]]; then prompt_now; echo; exit 0; fi

# RESTARTS=n: when dsh exits without a commit or a blocked note (a session
# that ends early, e.g. on a DSML cutoff or, with thinking off, on a turn
# that only narrates), re-dispatch it with the resume note, up to n more
# times, before running the gate (2026-10-17, the thinking-off trial).
before="$(git rev-parse HEAD)"
PROFILE="${DSH_PROFILE:-headless}"
RESTARTS="${RESTARTS:-0}"
attempt=0
t_all=$SECONDS
while :; do
    rotate_logs
    attempt=$((attempt + 1))
    echo "── task $N: $TITLE — dispatching to dsh, profile $PROFILE, attempt $attempt ($(date '+%m-%d %H:%M'))"
    t0=$SECONDS
    dsh --profile "$PROFILE" "$(prompt_now)" > "$LOGS/task-$N-dsh.log" 2>&1
    echo "   dsh exit $? after $(( (SECONDS - t0) / 60 )) min (log: $LOGS/task-$N-dsh.log)"
    grep -q 'DSML' "$LOGS/task-$N-dsh.log" && echo "   ⚠ the log ends on a malformed tool call (DSML): the session was cut off"
    if [[ -f "docs/superpowers/blocked-task-$N.md" ]]; then
        echo "   BLOCKED — dsh stopped with a question (docs/superpowers/blocked-task-$N.md):"
        sed 's/^/     /' "docs/superpowers/blocked-task-$N.md" | head -40
        exit 4
    fi
    [[ "$(git rev-parse HEAD)" != "$before" ]] && break
    (( attempt > RESTARTS )) && break
    echo "   no commit yet: restarting ($attempt of $((RESTARTS + 1)))"
done
echo "   dsh total $(( (SECONDS - t_all) / 60 )) min over $attempt attempt(s)"

gate() {
    { make test && make uvm2; } > "$LOGS/task-$N-gate.log" 2>&1 || return 1
    # every test program says "N passed, 0 failed" (3 of them since test_cart, more as tests are added)
    (( $(grep -c ' 0 failed' "$LOGS/task-$N-gate.log") >= 3 )) || return 1
    ! grep -qE ' [1-9][0-9]* failed' "$LOGS/task-$N-gate.log" || return 1
    ! grep -qE '(^|/)(src|tests|tools)/[^ :]+:[0-9]+:([0-9]+:)? (warning|error):' "$LOGS/task-$N-gate.log" || return 1
}
t1=$SECONDS
if gate; then
    echo "   gate took $(( (SECONDS - t1) / 60 )) min"
    if [[ "$(git rev-parse HEAD)" == "$before" ]]; then
        echo "   GATE PASS but nothing committed"; exit 3
    fi
    echo "   GATE PASS — $(git log -1 --oneline)"
    [[ -z "$(git log -1 --format=%b | tr -d '[:space:]')" ]] \
        && echo "   ⚠ the commit message has no body: no proof was written"
    git log -1 --format=%B | grep -q '^Plan-deviation:' && { echo "   ⚠ plan deviation:"; git log -1 --format=%B | sed -n '/^Plan-deviation:/,$p' | sed 's/^/     /'; }
    exit 0
fi
echo "   GATE FAIL after $(( (SECONDS - t1) / 60 )) min — see $LOGS/task-$N-gate.log"; tail -15 "$LOGS/task-$N-gate.log"
exit 1
