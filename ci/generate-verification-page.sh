#!/usr/bin/env bash
# Writes docs/verification.md from this run's own job list, then commits it.
#
# The page is generated, never edited by hand. The verification job runs after the
# platform jobs on every push to the main branch, asks the GitHub API which jobs
# ran and how each one finished, reads each job's compiler banner back from its
# log, and commits the result. A run whose jobs failed writes the page too,
# because that is when the record matters most; a cancelled run writes nothing.
#
# The script runs on a GitHub hosted ubuntu runner, which carries bash, gh, jq, and
# git, and it expects the workflow to have set GH_TOKEN and to have checked the
# repository out. The push uses that checkout's credentials.

set -euo pipefail

repository="${GITHUB_REPOSITORY:?run this from the workflow; GITHUB_REPOSITORY is not set}"
run_id="${GITHUB_RUN_ID:?run this from the workflow; GITHUB_RUN_ID is not set}"
commit="${GITHUB_SHA:?run this from the workflow; GITHUB_SHA is not set}"
branch="${GITHUB_REF_NAME:?run this from the workflow; GITHUB_REF_NAME is not set}"
server_url="${GITHUB_SERVER_URL:-https://github.com}"
run_attempt="${GITHUB_RUN_ATTEMPT:-1}"
run_url="${server_url}/${repository}/actions/runs/${run_id}"
page_path="docs/verification.md"
# The line each platform job prints twice: into its own job summary and into its
# log, which is where this script reads it back from.
compiler_marker="agent-db-doctor compiler:"

# Every job of this run, all pages of the listing merged into one array, oldest
# first. The verification job removes itself: the page cannot report on the job
# that is writing it.
jobs_json="$(gh api --paginate "repos/${repository}/actions/runs/${run_id}/jobs" \
    | jq -s '[.[].jobs[]] | map(select(.name != "Verification page")) | sort_by(.started_at // "")')"
job_rows="$(jq -r '.[] | [.name, (.conclusion // "null"), (.started_at // "null"), (.completed_at // "null"), (.id | tostring)] | @tsv' <<<"${jobs_json}")"

# The compiler banner the job printed, or nothing at all when the job never got
# as far as printing one (which the page then shows as a dash rather than a
# guess). The per-job view is tried first; the job logs endpoint is the fallback
# because a finished job's log can be readable through one and not the other.
#
# The read is retried, because this job runs while its own run is still open and a
# job's log can take a moment to become readable. The marker is anchored to the end
# of the log's timestamp so that the runner's own echo of the command cannot match
# it: that echo contains the same words as the output line, and matching it would
# put the workflow's source text on the page instead of the compiler's name.
compiler_of() {
    local job_id="$1"
    local attempt=0
    local log=""
    while (( attempt < 6 )); do
        log="$(gh run view "${run_id}" --job "${job_id}" --log 2>/dev/null || true)"
        if [[ -z "${log}" ]]; then
            log="$(gh api "repos/${repository}/actions/jobs/${job_id}/logs" 2>/dev/null || true)"
        fi
        if [[ -n "${log}" ]]; then
            break
        fi
        attempt=$(( attempt + 1 ))
        sleep 10
    done
    printf '%s\n' "${log}" \
        | sed $'s/\033\\[[0-9;]*m//g' \
        | grep -m1 -E "(^|Z )${compiler_marker} " \
        | sed "s/.*${compiler_marker} //" \
        | tr -d '\r' \
        || true
}

# An ISO 8601 instant in a form a person can compare, or a dash.
timestamp_of() {
    local instant="$1"
    if [[ -z "${instant}" || "${instant}" == "null" ]]; then
        printf -- '-'
    else
        date -u -d "${instant}" '+%Y-%m-%d %H:%M:%S'
    fi
}

# The wall clock the job occupied, from the two instants above.
duration_of() {
    local started="$1"
    local completed="$2"
    if [[ -z "${started}" || -z "${completed}" || "${started}" == "null" || "${completed}" == "null" ]]; then
        printf -- '-'
        return
    fi
    local seconds=$(( $(date -u -d "${completed}" +%s) - $(date -u -d "${started}" +%s) ))
    if (( seconds >= 60 )); then
        printf '%dm %02ds' $(( seconds / 60 )) $(( seconds % 60 ))
    else
        printf '%ds' "${seconds}"
    fi
}

rows=""
while IFS=$'\t' read -r name conclusion started completed job_id; do
    [[ -z "${name}" ]] && continue
    if [[ -z "${conclusion}" || "${conclusion}" == "null" ]]; then
        conclusion="in progress"
    fi
    toolchain="$(compiler_of "${job_id}")"
    [[ -z "${toolchain}" ]] && toolchain="-"
    rows+="| ${name} | ${toolchain} | ${conclusion} | $(timestamp_of "${started}") | $(timestamp_of "${completed}") | $(duration_of "${started}" "${completed}") |"$'\n'
done <<<"${job_rows}"

generated_at="$(date -u '+%Y-%m-%d %H:%M:%S UTC')"
{
    cat <<'PREAMBLE'
# Verification

This page is written by the continuous integration workflow itself, not by hand.
After the platform jobs finish on a push to the main branch, the verification job
asks the GitHub API what ran, reads each job's compiler banner back from that
job's log, and commits the result here. A run whose jobs failed writes the page
too, because that is when the record matters most.

A dash in the toolchain column means the job did not get as far as printing its
compiler banner.

PREAMBLE
    printf '| Job | Toolchain | Result | Started (UTC) | Finished (UTC) | Duration |\n'
    printf '|---|---|---|---|---|---|\n'
    printf '%s' "${rows}"
    printf '\n'
    printf 'Run: [%s](%s), attempt %s.\n\n' "${run_url}" "${run_url}" "${run_attempt}"
    printf 'Commit: `%s` on `%s`.\n\n' "${commit}" "${branch}"
    printf 'Generated at %s.\n' "${generated_at}"
} > "${page_path}"

# The bot identity, exactly as GitHub names the action account, so the commit is
# attributed rather than anonymous.
git config user.name "github-actions[bot]"
git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
git add "${page_path}"
if git diff --cached --quiet; then
    echo "${page_path} did not change; nothing to commit."
    exit 0
fi
git commit -m "docs: refresh the verification page"
# HEAD is pushed by name because a workflow checkout can be detached; the branch
# the page belongs to is the one the run was started for.
git push origin "HEAD:${branch}"
