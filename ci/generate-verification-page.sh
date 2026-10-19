#!/usr/bin/env bash
# Writes docs/verification.md from this run's own job list, then commits it.
#
# The page is generated, never edited by hand. The verification job runs after the
# platform jobs on every push to the main branch, asks the GitHub API which jobs
# ran and how each one finished, reads the compiler banner each platform job
# uploaded as an artifact, and commits the result. A run whose jobs failed writes
# the page too, because that is when the record matters most; a cancelled run
# writes nothing.
#
# The banners come from artifacts rather than from the jobs' logs because a job's
# log is not served while its own run is still open, and this job is part of that
# run: reading the logs would put a dash in every row.
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
# Where the workflow downloads the banner artifacts: one directory per job.
banner_directory="banners"

# Every job of this run, all pages of the listing merged into one array, oldest
# first. The verification job removes itself: the page cannot report on the job
# that is writing it.
jobs_json="$(gh api --paginate "repos/${repository}/actions/runs/${run_id}/jobs" \
    | jq -s '[.[].jobs[]] | map(select(.name != "Verification page")) | sort_by(.started_at // "")')"
job_rows="$(jq -r '.[] | [.name, (.conclusion // "null"), (.started_at // "null"), (.completed_at // "null"), (.id | tostring)] | @tsv' <<<"${jobs_json}")"

# The compiler banner the job reported, or a dash when the job never got as far as
# reporting one.
#
# Each platform job writes its banner to a file and uploads it as an artifact named
# after the job, and this function reads that file. The job's name is turned into
# the artifact's name the same way the workflow builds it: lower case, parentheses
# removed, spaces to dashes - "Linux (gcc)" becomes "compiler-banner-linux-gcc".
# A byte order mark is stripped, because Windows PowerShell writes one by default
# and it would otherwise become part of the first word on the page.
compiler_of() {
    local job_name="$1"
    local artifact_name
    artifact_name="compiler-banner-$(printf '%s' "${job_name}" \
        | tr '[:upper:]' '[:lower:]' | tr -d '()' | tr ' ' '-')"
    local banner_path="${banner_directory}/${artifact_name}/compiler-banner.txt"
    if [[ ! -f "${banner_path}" ]]; then
        printf -- '-'
        return 0
    fi
    head -n 1 "${banner_path}" | sed $'s/^\xef\xbb\xbf//' | tr -d '\r'
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
    toolchain="$(compiler_of "${name}")"
    [[ -z "${toolchain}" ]] && toolchain="-"
    rows+="| ${name} | ${toolchain} | ${conclusion} | $(timestamp_of "${started}") | $(timestamp_of "${completed}") | $(duration_of "${started}" "${completed}") |"$'\n'
done <<<"${job_rows}"

generated_at="$(date -u '+%Y-%m-%d %H:%M:%S UTC')"
{
    cat <<'PREAMBLE'
# Verification

This page is written by the continuous integration workflow itself, not by hand.
After the platform jobs finish on a push to the main branch, the verification job
asks the GitHub API what ran, reads the compiler banner each job uploaded, and
commits the result here. A run whose jobs failed writes the page too, because that
is when the record matters most.

A dash in the toolchain column means the job did not get as far as reporting its
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
