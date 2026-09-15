#!/usr/bin/env bash

manifest_condition_enabled() {
  local condition="$1"
  local var expected actual

  case "${condition}" in
    ""|always)
      return 0
      ;;
    *"!="*)
      var="${condition%%!=*}"
      expected="${condition#*!=}"
      actual="${!var:-}"
      [[ "${actual}" != "${expected}" ]]
      ;;
    *"="*)
      var="${condition%%=*}"
      expected="${condition#*=}"
      actual="${!var:-}"
      [[ "${actual}" == "${expected}" ]]
      ;;
    *)
      echo "Unsupported manifest condition: ${condition}" >&2
      return 1
      ;;
  esac
}

manifest_source_exists() {
  local repo_root="$1"
  local kind="$2"
  local source_path="$3"
  local abs_source="${repo_root}/${source_path}"

  case "${kind}" in
    file)
      [[ -f "${abs_source}" ]]
      ;;
    dir)
      [[ -d "${abs_source}" ]]
      ;;
    glob)
      compgen -G "${abs_source}" >/dev/null
      ;;
    generated|derived)
      [[ "${source_path}" == "-" ]] || [[ -e "${abs_source}" ]]
      ;;
    *)
      echo "Unsupported manifest kind: ${kind}" >&2
      return 1
      ;;
  esac
}

manifest_selected_destinations() {
  local manifest="$1"
  local kind required source_path destination producer verification condition note extra

  if [[ ! -f "${manifest}" ]]; then
    echo "Missing manifest: ${manifest}" >&2
    return 1
  fi

  while IFS='|' read -r kind required source_path destination producer verification condition note extra; do
    [[ -n "${kind}" ]] || continue
    [[ "${kind}" == \#* ]] && continue

    if [[ -n "${extra:-}" ]]; then
      echo "Malformed manifest entry in ${manifest}: too many fields: ${source_path}" >&2
      return 1
    fi

    if manifest_condition_enabled "${condition:-always}"; then
      printf '%s\n' "${destination}"
    fi
  done < "${manifest}"
}

manifest_selected_sources() {
  local manifest="$1"
  local kind required source_path destination producer verification condition note extra

  if [[ ! -f "${manifest}" ]]; then
    echo "Missing manifest: ${manifest}" >&2
    return 1
  fi

  while IFS='|' read -r kind required source_path destination producer verification condition note extra; do
    [[ -n "${kind}" ]] || continue
    [[ "${kind}" == \#* ]] && continue

    if [[ -n "${extra:-}" ]]; then
      echo "Malformed manifest entry in ${manifest}: too many fields: ${source_path}" >&2
      return 1
    fi

    if manifest_condition_enabled "${condition:-always}"; then
      printf '%s\n' "${source_path}"
    fi
  done < "${manifest}"
}

manifest_source_selected() {
  local requested_source="$1"
  shift
  local manifest selected_sources selected_source

  if [[ $# -eq 0 ]]; then
    return 0
  fi

  for manifest in "$@"; do
    selected_sources="$(manifest_selected_sources "${manifest}")"
    while IFS= read -r selected_source; do
      [[ -n "${selected_source}" ]] || continue
      case "${requested_source}" in
        ${selected_source})
          return 0
          ;;
      esac
    done <<< "${selected_sources}"
  done

  return 1
}
