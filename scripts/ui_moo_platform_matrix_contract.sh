#!/usr/bin/env bash

moo_ui_platform_matrix_classify() {
  if [[ "$#" -ne 5 ]]; then
    return 1
  fi

  local local_o4_rc="$1"
  local local_o6_rc="$2"
  local windows_rc="$3"
  local windows_is_ime="$4"
  local macos_rc="$5"
  local value

  for value in "$local_o4_rc" "$local_o6_rc" "$windows_rc" \
      "$windows_is_ime" "$macos_rc"; do
    case "$value" in
      ''|*[!0-9]*) return 1 ;;
    esac
  done

  if [[ "$local_o4_rc" -ne 0 || "$local_o6_rc" -ne 0 ]]; then
    return 1
  fi

  if ! { [[ "$windows_rc" -eq 0 && "$windows_is_ime" -eq 1 ]] ||
         [[ "$windows_rc" -eq 77 && "$windows_is_ime" -eq 0 ]]; }; then
    return 1
  fi

  if [[ "$macos_rc" -ne 0 && "$macos_rc" -ne 77 ]]; then
    return 1
  fi

  if [[ "$windows_rc" -eq 0 && "$macos_rc" -eq 0 ]]; then
    return 0
  fi
  return 77
}
