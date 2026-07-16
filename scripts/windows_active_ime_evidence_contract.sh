#!/usr/bin/env bash

moo_windows_active_ime_evidence_classify() {
  if [[ "$#" -ne 6 ]]; then
    return 1
  fi

  local ime_evidence="$1"
  local ime_samples="$2"
  local ime_value_a="$3"
  local ime_value_b="$4"
  local ime_value_c="$5"
  local active_layout_is_ime="$6"
  local value

  for value in "$ime_evidence" "$ime_samples" "$ime_value_a" \
      "$ime_value_b" "$ime_value_c" "$active_layout_is_ime"; do
    case "$value" in
      ''|*[!0-9]*) return 1 ;;
    esac
  done

  if [[ "$ime_evidence" -eq 1 && "$ime_samples" -eq 4 &&
        "$ime_value_a" -eq 2 && "$ime_value_b" -eq 1 &&
        "$ime_value_c" -eq 1 && "$active_layout_is_ime" -eq 1 ]]; then
    return 0
  fi

  if [[ "$ime_evidence" -eq 3 && "$ime_samples" -eq 0 &&
        "$ime_value_a" -eq 0 && "$ime_value_b" -eq 0 &&
        "$ime_value_c" -eq 0 && "$active_layout_is_ime" -eq 0 ]]; then
    return 77
  fi

  return 1
}
