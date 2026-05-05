#!/usr/bin/env bash
# ==============================================================================
# FineTrace Overhead Benchmark Script
#
# Сборка — строго по README каждого проекта:
#   FineTrace   : cmake -DCMAKE_BUILD_TYPE=Release .. && make
#   cl_gemm     : cmake -DCMAKE_BUILD_TYPE=Release .. && make
#   ze_gemm     : cmake -DCMAKE_BUILD_TYPE=Release .. && make
#   b+tree      : make KERNEL_DIM="-DRD_WG_SIZE_0=256 -DRD_WG_SIZE_1=256"
#   bfs         : make release
#   gaussian    : make KERNEL_DIM="-DRD_WG_SIZE_0=16 -DRD_WG_SIZE_1_0=16 -DRD_WG_SIZE_1_1=16"
#   nw          : make KERNEL_DIM="-DRD_WG_SIZE_0=16"
#
# Команды запуска — из run-скриптов каждого бенчмарка.
#
# 4 режима замера:
#   1. Baseline          — без FineTrace
#   2. --host-timing     — CPU overhead (host API profiling)
#   3. --device-timing   — GPU overhead (kernel execution profiling)
#   4. Оба флага         — суммарный overhead
#
# Использование:
#   bash benchmark.sh [--no-build] [--repeat N]
# ==============================================================================
set -euo pipefail

# ============================================================
# НАСТРОЙКИ — меняй здесь
# ============================================================
REPEAT_COUNT=5      # число повторений каждого замера (для усреднения)

# Аргументы бенчмарков (совпадают с run-скриптами)
CL_GEMM_ARGS=("gpu" "1024" "4")   # ./cl_gemm gpu 1024 4
ZE_GEMM_ARGS=("1024" "4")         # ./ze_gemm 1024 4

# Для Rodinia-бенчмарков, требующих внешних данных (b+tree, bfs),
# укажи путь к директории с данными Rodinia:
RODINIA_DATA_DIR="${RODINIA_DATA_DIR:-}"
# При пустой строке — b+tree и bfs будут пропущены.

# ============================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
SAMPLES_DIR="${SCRIPT_DIR}/samples"
RODINIA_DIR="${SAMPLES_DIR}/cl_rodinia_benchmarks"
LOG_DIR="${SCRIPT_DIR}/bench_logs"

mkdir -p "${LOG_DIR}"

# ============================================================
# CLI
# ============================================================
OPT_NO_BUILD=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build) OPT_NO_BUILD=1 ;;
        --repeat)   REPEAT_COUNT="$2"; shift ;;
        -h|--help)  grep '^#' "$0" | sed 's/^# \?//' | head -30; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
    shift
done

# ============================================================
# Цвета
# ============================================================
if [[ -t 1 ]]; then
    CR='\033[0;31m' CG='\033[0;32m' CY='\033[1;33m'
    CB='\033[0;34m' CC='\033[0;36m' CBOLD='\033[1m' CNC='\033[0m'
else
    CR='' CG='' CY='' CB='' CC='' CBOLD='' CNC=''
fi

info()    { echo -e "${CB}[INFO]${CNC} $*"; }
ok()      { echo -e "${CG}[ OK ]${CNC} $*"; }
warn()    { echo -e "${CY}[WARN]${CNC} $*"; }
fail()    { echo -e "${CR}[FAIL]${CNC} $*" >&2; }
section() {
    echo
    echo -e "${CBOLD}${CC}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${CNC}"
    echo -e "${CBOLD}${CC}  $* ${CNC}"
    echo -e "${CBOLD}${CC}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${CNC}"
}

# ============================================================
# СБОРКА
# ============================================================

FINETRACE=""

# -- FineTrace (главный README: cmake + make из build/) ------
build_finetrace() {
    section "Сборка FineTrace"
    # cd <finetrace>/build && cmake -DCMAKE_BUILD_TYPE=Release .. && make
    mkdir -p "${BUILD_DIR}"
    (
        cd "${BUILD_DIR}"
        cmake -DCMAKE_BUILD_TYPE=Release .. -Wno-dev \
              2>&1 | tee "${LOG_DIR}/cmake_finetrace.log"
        make -j"$(nproc)" \
              2>&1 | tee -a "${LOG_DIR}/cmake_finetrace.log"
    )
    FINETRACE="${BUILD_DIR}/finetrace"
    [[ -x "${FINETRACE}" ]] || { fail "finetrace не найден после сборки"; exit 1; }
    ok "FineTrace → ${FINETRACE}"
}

# -- cl_gemm (README: cd samples/cl_gemm/build && cmake .. && make) --
CL_GEMM_EXE=""
build_cl_gemm() {
    section "Сборка cl_gemm"
    local bdir="${SAMPLES_DIR}/cl_gemm/build"
    mkdir -p "${bdir}"
    if (
        cd "${bdir}"
        cmake -DCMAKE_BUILD_TYPE=Release .. -Wno-dev \
              > "${LOG_DIR}/cmake_cl_gemm.log" 2>&1
        make -j"$(nproc)" \
              >> "${LOG_DIR}/cmake_cl_gemm.log" 2>&1
    ); then
        CL_GEMM_EXE="${bdir}/cl_gemm"
        [[ -x "${CL_GEMM_EXE}" ]] && ok "cl_gemm → ${CL_GEMM_EXE}" \
                                   || warn "cl_gemm: бинарь не найден"
    else
        warn "cl_gemm: ошибка сборки (лог: ${LOG_DIR}/cmake_cl_gemm.log)"
    fi
}

# -- ze_gemm (README: cd samples/ze_gemm/build && cmake .. && make) --
ZE_GEMM_EXE=""
build_ze_gemm() {
    section "Сборка ze_gemm"
    local bdir="${SAMPLES_DIR}/ze_gemm/build"
    mkdir -p "${bdir}"
    if (
        cd "${bdir}"
        cmake -DCMAKE_BUILD_TYPE=Release .. -Wno-dev \
              > "${LOG_DIR}/cmake_ze_gemm.log" 2>&1
        make -j"$(nproc)" \
              >> "${LOG_DIR}/cmake_ze_gemm.log" 2>&1
    ); then
        ZE_GEMM_EXE="${bdir}/ze_gemm"
        [[ -x "${ZE_GEMM_EXE}" ]] && ok "ze_gemm → ${ZE_GEMM_EXE}" \
                                   || warn "ze_gemm: бинарь не найден"
    else
        warn "ze_gemm: ошибка сборки (лог: ${LOG_DIR}/cmake_ze_gemm.log)"
    fi
}

# -- Rodinia: b+tree -----------------------------------------
# README: make KERNEL_DIM="-DRD_WG_SIZE_0=256 -DRD_WG_SIZE_1=256"
BTREE_EXE=""
build_btree() {
    section "Сборка b+tree"
    local dir="${RODINIA_DIR}/b+tree"
    if (
        cd "${dir}"
        make clean > "${LOG_DIR}/make_btree.log" 2>&1 || true
        make KERNEL_DIM="-DRD_WG_SIZE_0=256 -DRD_WG_SIZE_1=256" \
             >> "${LOG_DIR}/make_btree.log" 2>&1
    ); then
        BTREE_EXE="${dir}/b+tree.out"
        [[ -x "${BTREE_EXE}" ]] && ok "b+tree → ${BTREE_EXE}" \
                                 || warn "b+tree: бинарь не найден"
    else
        warn "b+tree: ошибка сборки (лог: ${LOG_DIR}/make_btree.log)"
    fi
}

# -- Rodinia: bfs --------------------------------------------
# README: make release  (KERNEL_DIM не задан в README)
BFS_EXE=""
build_bfs() {
    section "Сборка bfs"
    local dir="${RODINIA_DIR}/bfs"
    if (
        cd "${dir}"
        make clean > "${LOG_DIR}/make_bfs.log" 2>&1 || true
        make release >> "${LOG_DIR}/make_bfs.log" 2>&1
    ); then
        BFS_EXE="${dir}/bfs.out"
        [[ -x "${BFS_EXE}" ]] && ok "bfs → ${BFS_EXE}" \
                               || warn "bfs: бинарь не найден"
    else
        warn "bfs: ошибка сборки (лог: ${LOG_DIR}/make_bfs.log)"
    fi
}

# -- Rodinia: gaussian ----------------------------------------
# README: make KERNEL_DIM="-DRD_WG_SIZE_0=16 -DRD_WG_SIZE_1_0=16 -DRD_WG_SIZE_1_1=16"
GAUSSIAN_EXE=""
build_gaussian() {
    section "Сборка gaussian"
    local dir="${RODINIA_DIR}/gaussian"
    if (
        cd "${dir}"
        make clean > "${LOG_DIR}/make_gaussian.log" 2>&1 || true
        make KERNEL_DIM="-DRD_WG_SIZE_0=16 -DRD_WG_SIZE_1_0=16 -DRD_WG_SIZE_1_1=16" \
             release >> "${LOG_DIR}/make_gaussian.log" 2>&1
    ); then
        GAUSSIAN_EXE="${dir}/gaussian.out"
        [[ -x "${GAUSSIAN_EXE}" ]] && ok "gaussian → ${GAUSSIAN_EXE}" \
                                    || warn "gaussian: бинарь не найден"
    else
        warn "gaussian: ошибка сборки (лог: ${LOG_DIR}/make_gaussian.log)"
    fi
}

# -- Rodinia: nw ---------------------------------------------
# README: make KERNEL_DIM="-DRD_WG_SIZE_0=16"
NW_EXE=""
build_nw() {
    section "Сборка nw"
    local dir="${RODINIA_DIR}/nw"
    if (
        cd "${dir}"
        make clean > "${LOG_DIR}/make_nw.log" 2>&1 || true
        make KERNEL_DIM="-DRD_WG_SIZE_0=16" \
             >> "${LOG_DIR}/make_nw.log" 2>&1
    ); then
        NW_EXE="${dir}/nw.out"
        [[ -x "${NW_EXE}" ]] && ok "nw → ${NW_EXE}" \
                              || warn "nw: бинарь не найден"
    else
        warn "nw: ошибка сборки (лог: ${LOG_DIR}/make_nw.log)"
    fi
}

do_build() {
    build_finetrace
    build_cl_gemm
    build_ze_gemm
    build_gaussian
    build_nw
    build_btree
    build_bfs
}

# ============================================================
# ЗАМЕР ВРЕМЕНИ
#
# Использует bash-builtin time с TIMEFORMAT='%R'.
# Весь stdout/stderr команды → /dev/null.
# time пишет в stderr группы {}, 2>&1 захватывает его отдельно.
# Возвращает среднее время в секундах (4 знака), N повторений.
# ============================================================

# measure <N> <workdir> <cmd> [args...]
# workdir — директория, из которой запускать (важно для ./nw.cl и т.п.)
measure() {
    local n="$1" workdir="$2"; shift 2
    local total=0 t i
    for (( i=1; i<=n; i++ )); do
        t=$(
            cd "${workdir}"
            { TIMEFORMAT='%R'; time "$@" > /dev/null 2>&1; } 2>&1
        )
        total=$(echo "$total + $t" | bc -l)
    done
    printf "%.4f" "$(echo "scale=6; $total / $n" | bc -l)"
}

# ovhd <base> <new>  →  процент накладных расходов
ovhd() { printf "%.2f" "$(echo "scale=6; ($2 - $1) / $1 * 100" | bc -l)"; }

# форматирование: "+12.34%" / "-0.12%"
fmt_pct() {
    local v="$1"
    if echo "$v >= 0" | bc -q | grep -q 1; then printf "+%s%%" "$v"
    else printf "%s%%" "$v"; fi
}

# ============================================================
# ЗАПУСК БЕНЧМАРКОВ
# ============================================================
# Команды запуска — точная копия того, что делают run-скрипты,
# только без лишних env-переменных, которых там нет.
#
# run-скрипты:
#   gaussian/run : ./gaussian.out -s 2048 $@
#   nw/run       : ./nw.out 8192 10 ./nw.cl -p 0 -d 0
#   b+tree/run   : ./b+tree.out file ${DATA_DIR}/b+tree/mil.txt \
#                              command ${DATA_DIR}/b+tree/command.txt $@
#   bfs/run      : ./bfs.out ${DATA_DIR}/bfs/graph1MW_6.txt $@
# ============================================================

declare -a RESULTS=()  # label|t_base|t_ht|ht_o|t_dt|dt_o|t_both|both_o

# run_bench <label> <workdir> <exe> [args...]
# FineTrace вызывается с абсолютным путём к exe для нужды в workdir.
run_bench() {
    local label="$1" workdir="$2"; shift 2
    local cmd=("$@")

    info "Замер: ${label}  (${REPEAT_COUNT} повт. × 4 режима)"

    local t_base t_ht t_dt t_both

    printf "  %-20s" "baseline"
    t_base=$(measure "${REPEAT_COUNT}" "${workdir}" "${cmd[@]}")
    printf "%s s\n" "${t_base}"

    printf "  %-20s" "--host-timing"
    t_ht=$(measure "${REPEAT_COUNT}" "${workdir}" "${FINETRACE}" --host-timing "${cmd[@]}")
    printf "%s s\n" "${t_ht}"

    printf "  %-20s" "--device-timing"
    t_dt=$(measure "${REPEAT_COUNT}" "${workdir}" "${FINETRACE}" --device-timing "${cmd[@]}")
    printf "%s s\n" "${t_dt}"

    printf "  %-20s" "both"
    t_both=$(measure "${REPEAT_COUNT}" "${workdir}" "${FINETRACE}" --host-timing --device-timing "${cmd[@]}")
    printf "%s s\n" "${t_both}"

    local ht_o dt_o both_o
    ht_o=$(ovhd "${t_base}" "${t_ht}")
    dt_o=$(ovhd "${t_base}" "${t_dt}")
    both_o=$(ovhd "${t_base}" "${t_both}")

    RESULTS+=("${label}|${t_base}|${t_ht}|${ht_o}|${t_dt}|${dt_o}|${t_both}|${both_o}")
}

do_benchmarks() {
    section "Замеры (${REPEAT_COUNT} повторений)"

    # cl_gemm: run из директории build, где лежит бинарь
    if [[ -x "${CL_GEMM_EXE}" ]]; then
        run_bench "cl_gemm (gpu 1024²)" \
            "${SAMPLES_DIR}/cl_gemm/build" \
            "./cl_gemm" "${CL_GEMM_ARGS[@]}"
    else
        warn "cl_gemm: пропускаем"
    fi

    # ze_gemm: аналогично
    if [[ -x "${ZE_GEMM_EXE}" ]]; then
        run_bench "ze_gemm (1024²)" \
            "${SAMPLES_DIR}/ze_gemm/build" \
            "./ze_gemm" "${ZE_GEMM_ARGS[@]}"
    else
        warn "ze_gemm: пропускаем"
    fi

    # gaussian: запуск из директории gaussian/ (как в run-скрипте)
    # run-скрипт: ./gaussian.out -s 2048 $@
    if [[ -x "${GAUSSIAN_EXE}" ]]; then
        run_bench "gaussian (-s 2048)" \
            "${RODINIA_DIR}/gaussian" \
            "./gaussian.out" -s 2048
    else
        warn "gaussian: пропускаем"
    fi

    # nw: запуск из директории nw/ — важно! run-скрипт использует ./nw.cl
    # run-скрипт: ./nw.out 8192 10 ./nw.cl -p 0 -d 0
    if [[ -x "${NW_EXE}" ]]; then
        run_bench "nw (8192, pen=10)" \
            "${RODINIA_DIR}/nw" \
            "./nw.out" 8192 10 "./nw.cl" -p 0 -d 0
    else
        warn "nw: пропускаем"
    fi

    # b+tree: нужен RODINIA_DATA_DIR
    # run-скрипт: ./b+tree.out file ${DATA_DIR}/b+tree/mil.txt command ${DATA_DIR}/b+tree/command.txt $@
    if [[ -x "${BTREE_EXE}" ]]; then
        if [[ -n "${RODINIA_DATA_DIR}" && \
              -f "${RODINIA_DATA_DIR}/b+tree/mil.txt" && \
              -f "${RODINIA_DATA_DIR}/b+tree/command.txt" ]]; then
            run_bench "b+tree" \
                "${RODINIA_DIR}/b+tree" \
                "./b+tree.out" \
                file    "${RODINIA_DATA_DIR}/b+tree/mil.txt" \
                command "${RODINIA_DATA_DIR}/b+tree/command.txt"
        else
            warn "b+tree: задайте RODINIA_DATA_DIR — пропускаем"
        fi
    else
        warn "b+tree: пропускаем"
    fi

    # bfs: нужен RODINIA_DATA_DIR
    # run-скрипт: ./bfs.out ${DATA_DIR}/bfs/graph1MW_6.txt $@
    if [[ -x "${BFS_EXE}" ]]; then
        if [[ -n "${RODINIA_DATA_DIR}" && \
              -f "${RODINIA_DATA_DIR}/bfs/graph1MW_6.txt" ]]; then
            run_bench "bfs" \
                "${RODINIA_DIR}/bfs" \
                "./bfs.out" "${RODINIA_DATA_DIR}/bfs/graph1MW_6.txt"
        else
            warn "bfs: задайте RODINIA_DATA_DIR — пропускаем"
        fi
    else
        warn "bfs: пропускаем"
    fi
}

# ============================================================
# ТАБЛИЦА РЕЗУЛЬТАТОВ
# ============================================================
# Колонки: Sample | Baseline | --host-timing | CPU ovhd | --device-timing | GPU ovhd | Both | Total ovhd

W_NAME=24   # ширина колонки Sample
W_TIME=11   # ширина колонки времени (X.XXXX s)
W_OVHD=10   # ширина колонки overhead (+XX.XX%)

# цвет накладных
color_ovhd() {
    local v="$1"
    local abs="${v#-}"
    if echo "$abs < 5"  | bc -q | grep -q 1; then echo "${CG}"
    elif echo "$abs < 20" | bc -q | grep -q 1; then echo "${CY}"
    else echo "${CR}"; fi
}

hr() { printf '%0.s─' $(seq 1 "$1"); }

print_table() {
    [[ ${#RESULTS[@]} -eq 0 ]] && { warn "Нет результатов."; return; }

    section "Результаты"
    echo -e "${CBOLD}Повторений на режим: ${REPEAT_COUNT}${CNC}"
    echo

    local S0 S1 S2
    S0=$(hr $((W_NAME+2)))
    S1=$(hr $((W_TIME+2)))
    S2=$(hr $((W_OVHD+2)))

    local TOP="┌${S0}┬${S1}┬${S1}┬${S2}┬${S1}┬${S2}┬${S1}┬${S2}┐"
    local MID="├${S0}┼${S1}┼${S1}┼${S2}┼${S1}┼${S2}┼${S1}┼${S2}┤"
    local BOT="└${S0}┴${S1}┴${S1}┴${S2}┴${S1}┴${S2}┴${S1}┴${S2}┘"

    echo "${TOP}"
    printf "│ ${CBOLD}%-${W_NAME}s${CNC} │ %-${W_TIME}s │ %-${W_TIME}s │ %-${W_OVHD}s │ %-${W_TIME}s │ %-${W_OVHD}s │ %-${W_TIME}s │ %-${W_OVHD}s │\n" \
        "Sample" \
        "Baseline" "--host-timing" "CPU ovhd" \
        "--device-tmg"  "GPU ovhd" \
        "Both"    "Total ovhd"
    echo "${MID}"

    local row
    for row in "${RESULTS[@]}"; do
        IFS='|' read -r label t_base t_ht ht_o t_dt dt_o t_both both_o <<< "${row}"

        local C_HT C_DT C_BOTH
        C_HT=$(color_ovhd "${ht_o}")
        C_DT=$(color_ovhd "${dt_o}")
        C_BOTH=$(color_ovhd "${both_o}")

        local ht_pct dt_pct both_pct
        ht_pct=$(fmt_pct "${ht_o}")
        dt_pct=$(fmt_pct "${dt_o}")
        both_pct=$(fmt_pct "${both_o}")

        printf "│ %-${W_NAME}s │ %${W_TIME}s │ %${W_TIME}s │ ${C_HT}%${W_OVHD}s${CNC} │ %${W_TIME}s │ ${C_DT}%${W_OVHD}s${CNC} │ %${W_TIME}s │ ${C_BOTH}%${W_OVHD}s${CNC} │\n" \
            "${label}" \
            "${t_base} s" "${t_ht} s" "${ht_pct}" \
            "${t_dt} s"   "${dt_pct}" \
            "${t_both} s" "${both_pct}"
    done

    echo "${BOT}"
    echo
    echo -e "${CBOLD}Накладные расходы = (время_с_FineTrace − baseline) / baseline × 100%${CNC}"
    echo -e "  ${CG}■${CNC} < 5%   незначительные"
    echo -e "  ${CY}■${CNC} 5–20%  заметные"
    echo -e "  ${CR}■${CNC} > 20%  высокие"
}

# ============================================================
# CSV
# ============================================================
save_csv() {
    local csv="${LOG_DIR}/results_$(date +%Y%m%d_%H%M%S).csv"
    {
        echo "sample,baseline_s,host_timing_s,cpu_ovhd_pct,device_timing_s,gpu_ovhd_pct,both_s,total_ovhd_pct"
        local row
        for row in "${RESULTS[@]}"; do echo "${row//|/,}"; done
    } > "${csv}"
    ok "CSV → ${csv}"
}

# ============================================================
# Проверка зависимостей
# ============================================================
check_deps() {
    local dep missing=0
    for dep in cmake make bc; do
        command -v "${dep}" &>/dev/null || { warn "Не найдено: ${dep}"; (( missing++ )) || true; }
    done
    (( missing == 0 )) || { fail "Установите недостающие утилиты"; exit 1; }
}

# ============================================================
# MAIN
# ============================================================
main() {
    echo
    echo -e "${CBOLD}${CC}╔══════════════════════════════════════════════════════╗${CNC}"
    echo -e "${CBOLD}${CC}║       FineTrace Overhead Benchmark Suite             ║${CNC}"
    echo -e "${CBOLD}${CC}╚══════════════════════════════════════════════════════╝${CNC}"
    echo -e "  Повторений : ${CBOLD}${REPEAT_COUNT}${CNC} на режим"
    echo -e "  Логи       : ${LOG_DIR}"
    [[ -n "${RODINIA_DATA_DIR}" ]] \
        && echo -e "  Rodinia    : ${RODINIA_DATA_DIR}" \
        || echo -e "  Rodinia    : ${CY}не задан RODINIA_DATA_DIR — b+tree и bfs пропускаются${CNC}"
    echo

    check_deps

    if [[ ${OPT_NO_BUILD} -eq 0 ]]; then
        do_build
    else
        info "Сборка пропущена (--no-build), ищем готовые бинари..."
        FINETRACE="${BUILD_DIR}/finetrace"
        [[ -x "${FINETRACE}" ]]                                 || { fail "finetrace не найден"; exit 1; }
        [[ -x "${SAMPLES_DIR}/cl_gemm/build/cl_gemm"   ]]      && CL_GEMM_EXE="${SAMPLES_DIR}/cl_gemm/build/cl_gemm"
        [[ -x "${SAMPLES_DIR}/ze_gemm/build/ze_gemm"   ]]      && ZE_GEMM_EXE="${SAMPLES_DIR}/ze_gemm/build/ze_gemm"
        [[ -x "${RODINIA_DIR}/gaussian/gaussian.out"    ]]      && GAUSSIAN_EXE="${RODINIA_DIR}/gaussian/gaussian.out"
        [[ -x "${RODINIA_DIR}/nw/nw.out"                ]]      && NW_EXE="${RODINIA_DIR}/nw/nw.out"
        [[ -x "${RODINIA_DIR}/b+tree/b+tree.out"        ]]      && BTREE_EXE="${RODINIA_DIR}/b+tree/b+tree.out"
        [[ -x "${RODINIA_DIR}/bfs/bfs.out"              ]]      && BFS_EXE="${RODINIA_DIR}/bfs/bfs.out"
    fi

    do_benchmarks
    print_table
    save_csv

    echo -e "\n${CG}Готово.${CNC}"
}

main "$@"
