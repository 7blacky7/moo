#!/usr/bin/env bash
# Gemeinsamer Sicherheits-Harness fuer UI-Prozesse.
# Jeder gestartete Befehl erhaelt eine eigene Prozessgruppe. Timeouts und
# Abbrueche terminieren die gesamte Gruppe, warten begrenzt und reapen sie.

UI_HARNESS_ACTIVE_PID=""
UI_HARNESS_ACTIVE_PGID=""
UI_HARNESS_XVFB_PID=""
UI_HARNESS_LOCK_MODE=""
UI_HARNESS_LOCK_DIR=""
UI_HARNESS_LOCK_FILE=""
UI_HARNESS_RUN_DIR=""
UI_HARNESS_KEEP_RUN_DIR=0
UI_HARNESS_SPAWNING=0
UI_HARNESS_PENDING_SIGNAL=0

ui_harness_is_windows() {
    [ "${OS:-}" = "Windows_NT" ]
}

ui_harness_group_alive() {
    local pgid="${1:-}"
    [ -n "$pgid" ] || return 1
    if ui_harness_is_windows; then
        kill -0 "$pgid" 2>/dev/null
    else
        kill -0 -- "-$pgid" 2>/dev/null
    fi
}

ui_harness_stop_group() {
    local pid="${1:-}"
    local pgid="${2:-}"
    [ -n "$pgid" ] || return 0

    if ui_harness_is_windows; then
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            taskkill.exe //PID "$pid" //T //F >/dev/null 2>&1 || true
        fi
        if [ -n "$pid" ]; then
            wait "$pid" 2>/dev/null || true
        fi
        ! ui_harness_group_alive "$pgid"
        return
    fi

    if ui_harness_group_alive "$pgid"; then
        kill -TERM -- "-$pgid" 2>/dev/null || true
        local i
        for i in {1..20}; do
            ui_harness_group_alive "$pgid" || break
            sleep 0.1
        done
    fi
    if ui_harness_group_alive "$pgid"; then
        kill -KILL -- "-$pgid" 2>/dev/null || true
        local i
        for i in {1..20}; do
            ui_harness_group_alive "$pgid" || break
            sleep 0.1
        done
    fi
    if ui_harness_group_alive "$pgid"; then
        return 1
    fi
    if [ -n "$pid" ]; then
        wait "$pid" 2>/dev/null || true
    fi
    return 0
}

ui_harness_stop_xvfb() {
    local pid="${UI_HARNESS_XVFB_PID:-}"
    [ -n "$pid" ] || return 0
    if kill -0 "$pid" 2>/dev/null; then
        kill -TERM "$pid" 2>/dev/null || true
        local i
        for i in {1..20}; do
            kill -0 "$pid" 2>/dev/null || break
            sleep 0.1
        done
    fi
    if kill -0 "$pid" 2>/dev/null; then
        kill -KILL "$pid" 2>/dev/null || true
    fi
    wait "$pid" 2>/dev/null || true
    UI_HARNESS_XVFB_PID=""
}

ui_harness_cleanup() {
    if [ -n "${UI_HARNESS_ACTIVE_PGID:-}" ]; then
        if ui_harness_stop_group "${UI_HARNESS_ACTIVE_PID:-}" "$UI_HARNESS_ACTIVE_PGID"; then
            UI_HARNESS_ACTIVE_PID=""
            UI_HARNESS_ACTIVE_PGID=""
        fi
    fi
    ui_harness_stop_xvfb

    if [ "${UI_HARNESS_LOCK_MODE:-}" = "flock" ]; then
        flock -u 9 2>/dev/null || true
        exec 9>&-
    elif [ "${UI_HARNESS_LOCK_MODE:-}" = "mkdir" ] && [ -n "${UI_HARNESS_LOCK_DIR:-}" ]; then
        rm -f -- "$UI_HARNESS_LOCK_DIR/owner" 2>/dev/null || true
        rmdir -- "$UI_HARNESS_LOCK_DIR" 2>/dev/null || true
    fi
    UI_HARNESS_LOCK_MODE=""
    UI_HARNESS_LOCK_DIR=""
    UI_HARNESS_LOCK_FILE=""

    if [ "${UI_HARNESS_KEEP_RUN_DIR:-0}" -eq 1 ]; then
        if [ -n "${UI_HARNESS_RUN_DIR:-}" ]; then
            echo "[harness] Diagnoseverzeichnis erhalten: $UI_HARNESS_RUN_DIR" >&2
        fi
    else
        case "${UI_HARNESS_RUN_DIR:-}" in
            "${TMPDIR:-/tmp}"/moo-ui-*)
                rm -rf -- "$UI_HARNESS_RUN_DIR"
                ;;
        esac
        UI_HARNESS_RUN_DIR=""
    fi
}

ui_harness_on_exit() {
    local rc=$?
    trap - EXIT INT TERM HUP
    ui_harness_cleanup
    exit "$rc"
}

ui_harness_on_signal() {
    local rc="$1"
    if [ "${UI_HARNESS_SPAWNING:-0}" -eq 1 ]; then
        UI_HARNESS_PENDING_SIGNAL="$rc"
        return 0
    fi
    trap - EXIT INT TERM HUP
    ui_harness_cleanup
    exit "$rc"
}

ui_harness_finish_spawn_window() {
    UI_HARNESS_SPAWNING=0
    if [ "${UI_HARNESS_PENDING_SIGNAL:-0}" -ne 0 ]; then
        local rc="$UI_HARNESS_PENDING_SIGNAL"
        UI_HARNESS_PENDING_SIGNAL=0
        ui_harness_on_signal "$rc"
    fi
}

ui_harness_install_traps() {
    trap ui_harness_on_exit EXIT
    trap 'ui_harness_on_signal 130' INT
    trap 'ui_harness_on_signal 143' TERM
    trap 'ui_harness_on_signal 129' HUP
}

ui_harness_init() {
    local name="${1:-run}"
    local lock_root="${TMPDIR:-/tmp}/moo-ui-harness.lock"
    ui_harness_install_traps

    if command -v flock >/dev/null 2>&1; then
        UI_HARNESS_LOCK_FILE="$lock_root"
        if ! eval 'exec 9>"$UI_HARNESS_LOCK_FILE"'; then
            echo "FEHLER: UI-Harness-Lockdatei konnte nicht geoeffnet werden ($lock_root)." >&2
            return 2
        fi
        if ! flock -n 9; then
            exec 9>&-
            echo "FEHLER: UI-Harness bereits aktiv (Lock $lock_root)." >&2
            return 3
        fi
        UI_HARNESS_LOCK_MODE="flock"
    else
        # Portabler Fail-closed-Fallback. Ein numerischer, nicht mehr lebender
        # Owner darf zurueckgewonnen werden; unklare/aktive Locks blockieren.
        if ! mkdir "$lock_root" 2>/dev/null; then
            local owner=""
            owner=$(cat "$lock_root/owner" 2>/dev/null || true)
            if [[ "$owner" =~ ^[0-9]+$ ]] && ! kill -0 "$owner" 2>/dev/null; then
                rm -f -- "$lock_root/owner" 2>/dev/null || true
                rmdir -- "$lock_root" 2>/dev/null || true
                if ! mkdir "$lock_root" 2>/dev/null; then
                    echo "FEHLER: UI-Harness-Lock wurde gleichzeitig uebernommen ($lock_root)." >&2
                    return 3
                fi
            else
                echo "FEHLER: UI-Harness bereits aktiv/unklar (PID ${owner:-unbekannt}, Lock $lock_root)." >&2
                return 3
            fi
        fi
        UI_HARNESS_LOCK_MODE="mkdir"
        UI_HARNESS_LOCK_DIR="$lock_root"
        printf '%s\n' "$$" >"$UI_HARNESS_LOCK_DIR/owner"
    fi

    if ! UI_HARNESS_RUN_DIR=$(mktemp -d "${TMPDIR:-/tmp}/moo-ui-${name}.XXXXXX"); then
        echo "FEHLER: temporaeres UI-Harness-Verzeichnis konnte nicht angelegt werden." >&2
        ui_harness_cleanup
        return 2
    fi
}

ui_harness_start_display() {
    if [ "${MOO_UI_BACKEND:-gtk}" != "gtk" ]; then
        if [ "${CI:-}" != "true" ] && [ "${MOO_UI_ALLOW_NATIVE_WINDOW:-0}" != "1" ]; then
            echo "FEHLER: Native UI-Fenster sind lokal gesperrt; explizit MOO_UI_ALLOW_NATIVE_WINDOW=1 setzen." >&2
            return 2
        fi
        echo "[harness] Nativer Backend-Lauf explizit/CI-freigegeben: ${MOO_UI_BACKEND}"
        return 0
    fi

    if [ "${MOO_UI_ALLOW_HOST_DISPLAY:-0}" = "1" ] && [ -n "${DISPLAY:-}" ]; then
        echo "[harness] Explizit freigegebenes Host-DISPLAY=$DISPLAY"
        return 0
    fi
    if ! command -v Xvfb >/dev/null 2>&1; then
        echo "FEHLER: GTK-UI-Tests verlangen einen privaten Xvfb." >&2
        return 2
    fi

    local display_file="$UI_HARNESS_RUN_DIR/xvfb-display"
    UI_HARNESS_SPAWNING=1
    Xvfb -displayfd 3 -screen 0 1024x768x24         >"$UI_HARNESS_RUN_DIR/xvfb.log" 2>&1 3>"$display_file" &
    UI_HARNESS_XVFB_PID=$!
    ui_harness_finish_spawn_window

    local i display_no=""
    for i in {1..50}; do
        if [ -s "$display_file" ]; then
            display_no=$(tr -d '[:space:]' <"$display_file")
            [ -n "$display_no" ] && break
        fi
        if ! kill -0 "$UI_HARNESS_XVFB_PID" 2>/dev/null; then
            echo "FEHLER: Xvfb endete vor der Bereitschaft." >&2
            return 2
        fi
        sleep 0.1
    done
    if [ -z "$display_no" ]; then
        echo "FEHLER: Xvfb meldete innerhalb von 5s kein Display." >&2
        return 2
    fi
    export DISPLAY=":$display_no"
    # DISPLAY allein reicht unter einer Wayland-Sitzung nicht: GTK kann sonst
    # den echten Wayland-Compositor bevorzugen. Private Xvfb-Laeufe erzwingen
    # deshalb X11 und entfernen den Host-Wayland-Socket aus der Umgebung.
    export GDK_BACKEND=x11
    export SDL_VIDEODRIVER=x11
    unset WAYLAND_DISPLAY
    echo "[harness] Privater Xvfb PID=$UI_HARNESS_XVFB_PID DISPLAY=$DISPLAY GDK_BACKEND=$GDK_BACKEND"
}

ui_harness_run() {
    local timeout_seconds="$1"
    local log="$2"
    shift 2

    if [ -n "${UI_HARNESS_ACTIVE_PGID:-}" ]; then
        echo "[harness] FATAL: Active-Slot bereits belegt (PID=${UI_HARNESS_ACTIVE_PID:-?} PGID=$UI_HARNESS_ACTIVE_PGID)" >>"$log"
        UI_HARNESS_KEEP_RUN_DIR=1
        return 125
    fi

    local monitor_was_on=0
    case "$-" in
        *m*) monitor_was_on=1 ;;
    esac

    UI_HARNESS_SPAWNING=1
    if ui_harness_is_windows; then
        "$@" >"$log" 2>&1 &
    else
        set -m
        "$@" >"$log" 2>&1 &
    fi
    local pid=$!
    local pgid="$pid"
    UI_HARNESS_ACTIVE_PID="$pid"
    UI_HARNESS_ACTIVE_PGID="$pgid"
    if ! ui_harness_is_windows && [ "$monitor_was_on" -eq 0 ]; then
        set +m
    fi
    ui_harness_finish_spawn_window

    local deadline=$((SECONDS + timeout_seconds))
    while kill -0 "$pid" 2>/dev/null; do
        if [ "$SECONDS" -ge "$deadline" ]; then
            echo "[harness] TIMEOUT nach ${timeout_seconds}s (PID=$pid PGID=$pgid)" >>"$log"
            if ! ui_harness_stop_group "$pid" "$pgid"; then
                echo "[harness] FEHLER: Prozessgruppe blieb nach KILL bestehen (PGID=$pgid)" >>"$log"
                UI_HARNESS_KEEP_RUN_DIR=1
                return 125
            fi
            UI_HARNESS_ACTIVE_PID=""
            UI_HARNESS_ACTIVE_PGID=""
            return 124
        fi
        sleep 0.1
    done

    local rc=0
    wait "$pid" || rc=$?
    if ui_harness_group_alive "$pgid"; then
        echo "[harness] FEHLER: verwaiste Prozesse nach Leader-RC=$rc (PGID=$pgid)" >>"$log"
        if ui_harness_stop_group "" "$pgid"; then
            UI_HARNESS_ACTIVE_PID=""
            UI_HARNESS_ACTIVE_PGID=""
        else
            echo "[harness] FEHLER: verwaiste Prozessgruppe blieb nach KILL bestehen (PGID=$pgid)" >>"$log"
            UI_HARNESS_KEEP_RUN_DIR=1
        fi
        return 125
    fi
    UI_HARNESS_ACTIVE_PID=""
    UI_HARNESS_ACTIVE_PGID=""
    return "$rc"
}
