#!/usr/bin/env bash
set -euo pipefail

repository=${1:-brendanwilliam/input-activity-obs}
repository_id=$(gh api "repos/$repository" --jq .id)

configure_ruleset() {
  local branch=$1
  local require_checks=${2:-false}
  local name="Protect $branch"
  local ruleset_id
  ruleset_id=$(gh api "repos/$repository/rulesets" --jq ".[] | select(.name == \"$name\") | .id" | head -n1 || true)

  local payload
  payload=$(jq -n \
    --arg name "$name" \
    --arg branch "refs/heads/$branch" \
    --argjson repository_id "$repository_id" \
    --argjson require_checks "$require_checks" \
    '{
      name: $name,
      target: "branch",
      enforcement: "active",
      conditions: {ref_name: {include: [$branch], exclude: []}},
      rules: [
        {type: "deletion"},
        {type: "non_fast_forward"},
        {type: "required_linear_history"},
        {type: "pull_request", parameters: {
          dismiss_stale_reviews_on_push: false,
          require_code_owner_review: false,
          require_last_push_approval: false,
          required_approving_review_count: 0,
          required_review_thread_resolution: true
        }}
        ]
      }
      | if $require_checks then
          .rules += [{type: "required_status_checks", parameters: {
            strict_required_status_checks_policy: true,
            required_status_checks: [
              {context: "formatting / clang-format"},
              {context: "formatting / gersemi"},
              {context: "build"},
              {context: "CodeQL"}
            ]
          }}]
        else . end')

  if [[ -n "$ruleset_id" ]]; then
    gh api --method PUT "repos/$repository/rulesets/$ruleset_id" --input - <<<"$payload"
  else
    gh api --method POST "repos/$repository/rulesets" --input - <<<"$payload"
  fi
}

remove_ruleset() {
  local name=$1
  local ruleset_id
  ruleset_id=$(gh api "repos/$repository/rulesets" --jq ".[] | select(.name == \"$name\") | .id" | head -n1 || true)

  if [[ -n "$ruleset_id" ]]; then
    gh api --method DELETE "repos/$repository/rulesets/$ruleset_id"
  fi
}

configure_ruleset develop false
configure_ruleset main true
