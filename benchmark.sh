#!/usr/bin/env bash
# ==============================================================================
# FineTrace Overhead Benchmark Script
#
# Собирает FineTrace, cl_gemm, ze_gemm и Rodinia-бенчмарки (gaussian, nw),
# запускает каждый REPEAT_COUNT раз в 4 режимах и строит таблицу накладных
# расходов для CPU (--host-timing) и GPU (--device-timing).
#
# Флаги FineTrace:
#   --host-timing    — профилирование host-side API (CPU overhead)
#   --device-timing  — профилирование kernel execution (GPU overhead)
#
# Использование:
#   bash benchmark.sh [--no-build] [--repeat N]
# ==============================================================================
set -euo pipefail

# ============================================================
# НАСТРОЙКИ  (изменяй здесь)
# ============================================================
REPEAT_COUNT=5          # число повторений каждого замера для усреднения

CL_GEMM_DEVICE="gpu"   # cpu | gpu
CL_GEMM_SIZE=1024      # размер матрицы
CL_GEMM_REPS=4         # внутренние повторения внутри cl_gemm

ZE_GEMM_SIZE=1024      # размер матрицы для ze_gemm
ZE_GEMM_REPS=4         # внутренние повторения внутри ze_gemm

GAUSSIAN_SIZE=2048      # -s N для gaussian
GAUSSIAN_PLATFORM=0    # -p N (OpenCL platform index)
GAUSSIAN_DEVICE=0      # -d N (OpenCL device index)

NW_SEQ_LEN=8192        # длина последовательности для nw
NW_PENALTY=10          # штраф за несовпадение для nw
NW_PLATFORM=0
NW_DEVICE=0

# ============================================================
# Пути
# ============================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
SAMPLES_DIR="${SCRIPT_DIR}/samples"
RODINIA_DIR="${SAMPLES_DIR}/cl_rodinia_benchmarks"
LOG_DIR="${SCRIPT_DIR}/bench_logs"

mkdir -p "${LOG_DIR}"

# ============================================================
# Парсинг CLI
# ============================================================
OPT_NO_BUILD=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build)  OPT_NO_BUILD=1 ;;
        --repeat)    REPEAT_COUNT="$2"; shift ;;
        -h|--help)
            grep '^#' "$0" | head -20 | sed 's/^# \{0,1\}//'
            exit 0 ;;
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
section() {
    echo
    echo -e "${CBOLD}${CC}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${CNC}"
    echo -e "${CBOLD}${CC}  $* ${CNC}"
    echo -e "${CBOLD}${CC}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${CNC}"
}

# ============================================================
# Сборка
# ============================================================
FINETRACE=""
CL_GEMM_EXE=""
ZE_GEMM_EXE=""
GAUSSIAN_EXE=""
NW_EXE=""
NW_CL=""

build_finetrace() {
    section "Сборка FineTrace"
    cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
          -DCMAKE_BUILD_TYPE=Release -Wno-dev \
          2>&1 | tee "${LOG_DIR}/cmake_finetrace.log"
    cmake --build "${BUILD_DIR}" --parallel "$(nproc)" \
          2>&1 | tee "${LOG_DIR}/make_finetrace.log"
    FINETRACE="${BUILD_DIR}/finetrace"
    [[ -x "${FINETRACE}" ]] || { echo "Не найден бинарь: ${FINETRACE}"; exit 1; }
    ok "FineTrace: ${FINETRACE}"
}

build_cmake_sample() {
    local name="$1" src="$2" exe_name="$3"
    local bdir="${src}/build"
    info "Сборка ${name} ..."
    mkdir -p "${bdir}"
    if cmake -S "${src}" -B "${bdir}" \
             -DCMAKE_BUILD_TYPE=Release -Wno-dev \
             > "${LOG_DIR}/cmake_${name}.log" 2>&1 && \
       cmake --build "${bdir}" --parallel "$(nproc)" \
             >> "${LOG_DIR}/make_${name}.log" 2>&1; then
        local exe="${bdir}/${exe_name}"
        if [[ -x "${exe}" ]]; then
            ok "${name}: ${exe}"
            echo "${exe}"
            return 0
        fi
    fi
    warn "${name}: ошибка сборки — см. ${LOG_DIR}/make_${name}.log"
    echo ""
}

build_rodinia_make() {
    local name="$1" dir="$2"
    info "Сборка ${name} ..."
    pushd "${dir}" > /dev/null
    if make release > "${LOG_DIR}/make_${name}.log" 2>&1; then
        ok "${name}: собран"
        popd > /dev/null
        return 0
    fi
    warn "${name}: ошибка сборки — см. ${LOG_DIR}/make_${name}.log"
    popd > /dev/null
    return 1
}

do_build() {
    build_finetrace

    CL_GEMM_EXE=$(build_cmake_sample "cl_gemm" \
        "${SAMPLES_DIR}/cl_gemm" "cl_gemm")

    ZE_GEMM_EXE=$(build_cmake_sample "ze_gemm" \
        "${SAMPLES_DIR}/ze_gemm" "ze_gemm")

    section "Сборка Rodinia benchmarks"

    if build_rodinia_make "gaussian" "${RODINIA_DIR}/gaussian"; then
        GAUSSIAN_EXE="${RODINIA_DIR}/gaussian/gaussian.out"
    fi

    if build_rodinia_make "nw" "${RODINIA_DIR}/nw"; then
        NW_EXE="${RODINIA_DIR}/nw/nw.out"
        NW_CL="${RODINIA_DIR}/nw/nw.cl"
    fi
}

# ============================================================
# Замер времени
# ============================================================
# measure <count> <cmd> [args...]
# Выводит среднее реальное время (real) в секундах, 4 знака после запятой.
# Использует встроенный time bash с TIMEFORMAT='%R'.
measure() {
    local n="$1"; shift
    local total=0 t i

    for (( i=1; i<=n; i++ )); do
        # Весь stdout/stderr команды — в /dev/null.
        # time пишет результат в stderr своей группы {},
        # который захватываем в подстановку через 2>&1.
        t=$( { TIMEFORMAT='%R'; time "$@" > /dev/null 2>&1; } 2>&1 )
        total=$(echo "$total + $t" | bc -l)
    done

    printf "%.4f" "$(echo "scale=6; $total / $n" | bc -l)"
}

# ovhd <base_sec> <new_sec>  →  процент накладных расходов (с знаком)
ovhd() {
    printf "%.2f" "$(echo "scale=6; ($2 - $1) / $1 * 100" | bc -l)"
}

# Форматирование: "+12.34%" или "-0.12%"
fmt_pct() {
    local v="$1"
    if echo "$v >= 0" | bc -q | grep -q 1; then
        printf "+%s%%" "$v"
    else
        printf "%s%%" "$v"
    fi
}

# ============================================================
# Запуск бенчмарков
# ============================================================
# Формат строк результатов:
#   label|base|ht_time|ht_ovhd|dt_time|dt_ovhd|both_time|both_ovhd
declare -a RESULTS=()

run_benchmark() {
    local label="$1"; shift
    local cmd=("$@")

    info "Замер: ${label}  (${REPEAT_COUNT} × 4 режима = $((REPEAT_COUNT*4)) запусков)"

    printf "  %-18s" "Baseline"
    local t_base
    t_base=$(measure "${REPEAT_COUNT}" "${cmd[@]}")
    printf " %s s\n" "${t_base}"

    printf "  %-18s" "--host-timing"
    local t_ht
    t_ht=$(measure "${REPEAT_COUNT}" "${FINETRACE}" --host-timing "${cmd[@]}")
    printf " %s s\n" "${t_ht}"

    printf "  %-18s" "--device-timing"
    local t_dt
    t_dt=$(measure "${REPEAT_COUNT}" "${FINETRACE}" --device-timing "${cmd[@]}")
    printf " %s s\n" "${t_dt}"

    printf "  %-18s" "Both"
    local t_both
    t_both=$(measure "${REPEAT_COUNT}" "${FINETRACE}" --host-timing --device-timing "${cmd[@]}")
    printf " %s s\n" "${t_both}"

    local ht_o dt_o both_o
    ht_o=$(ovhd "${t_base}" "${t_ht}")
    dt_o=$(ovhd "${t_base}" "${t_dt}")
    both_o=$(ovhd "${t_base}" "${t_both}")

    RESULTS+=("${label}|${t_base}|${t_ht}|${ht_o}|${t_dt}|${dt_o}|${t_both}|${both_o}")
}

do_benchmarks() {
    section "Запуск бенчмарков"

    if [[ -x "${CL_GEMM_EXE}" ]]; then
        run_benchmark \
            "cl_gemm (${CL_GEMM_DEVICE} ${CL_GEMM_SIZE}²)" \
            "${CL_GEMM_EXE}" "${CL_GEMM_DEVICE}" "${CL_GEMM_SIZE}" "${CL_GEMM_REPS}"
    else
        warn "cl_gemm не найден — пропускаем"
    fi

    if [[ -x "${ZE_GEMM_EXE}" ]]; then
        run_benchmark \
            "ze_gemm (${ZE_GEMM_SIZE}²)" \
            "${ZE_GEMM_EXE}" "${ZE_GEMM_SIZE}" "${ZE_GEMM_REPS}"
    else
        warn "ze_gemm не найден — пропускаем"
    fi

    if [[ -x "${GAUSSIAN_EXE}" ]]; then
        run_benchmark \
            "gaussian (-s ${GAUSSIAN_SIZE})" \
            "${GAUSSIAN_EXE}" \
            -s "${GAUSSIAN_SIZE}" -p "${GAUSSIAN_PLATFORM}" -d "${GAUSSIAN_DEVICE}"
    else
        warn "gaussian не найден — пропускаем"
    fi

    if [[ -x "${NW_EXE}" ]]; then
        run_benchmark \
            "nw (${NW_SEQ_LEN}, pen=${NW_PENALTY})" \
            "${NW_EXE}" "${NW_SEQ_LEN}" "${NW_PENALTY}" "${NW_CL}" \
            -p "${NW_PLATFORM}" -d "${NW_DEVICE}"
    else
        warn "nw не найден — пропускаем"
    fi
}

# ============================================================
# Печать таблицы результатов
# ============================================================
# Ширины колонок
W_NAME=28   # Sample
W_TIME=11   # время (X.XXXX s)
W_OVHD=9    # накладные (+XX.XX%)

hr_seg() {
    local w="$1" char="${2:-─}"
    printf '%0.s'"${char}" $(seq 1 "$w")
}

print_table() {
    if [[ ${#RESULTS[@]} -eq 0 ]]; then
        warn "Нет результатов для отображения"
        return
    fi

    section "Результаты: накладные расходы FineTrace"
    echo -e "${CBOLD}Число повторений (REPEAT_COUNT): ${REPEAT_COUNT}${CNC}"
    echo

    # ── Разделители ─────────────────────────────────────────────
    local S0 S1 S2
    S0=$(hr_seg $((W_NAME+2)))
    S1=$(hr_seg $((W_TIME+2)))
    S2=$(hr_seg $((W_OVHD+2)))

    local TOP_BORDER="┌${S0}┬${S1}┬${S1}┬${S2}┬${S1}┬${S2}┬${S1}┬${S2}┐"
    local MID_BORDER="├${S0}┼${S1}┼${S1}┼${S2}┼${S1}┼${S2}┼${S1}┼${S2}┤"
    local BOT_BORDER="└${S0}┴${S1}┴${S1}┴${S2}┴${S1}┴${S2}┴${S1}┴${S2}┘"
    local DIV_BORDER="├${S0}┼${S1}┴${S1}┴${S2}┴${S1}┴${S2}┴${S1}┴${S2}┤"

    # Заголовок группы (с объединёнными ячейками — симуляция через текст)
    local H_NAME H_BASE H_HT H_HT_O H_DT H_DT_O H_BOTH H_BOTH_O
    H_NAME=$(printf "%-${W_NAME}s" "Sample")
    H_BASE=$(printf "%-${W_TIME}s" "Baseline")
    H_HT=$(printf "%-${W_TIME}s" "--host-timing")
    H_HT_O=$(printf "%-${W_OVHD}s" "CPU ovhd")
    H_DT=$(printf "%-${W_TIME}s" "--device-tmg")
    H_DT_O=$(printf "%-${W_OVHD}s" "GPU ovhd")
    H_BOTH=$(printf "%-${W_TIME}s" "Both")
    H_BOTH_O=$(printf "%-${W_OVHD}s" "Total ovhd")

    echo "${TOP_BORDER}"
    printf "│ ${CBOLD}%s${CNC} │ %s │ %s │ %s │ %s │ %s │ %s │ %s │\n" \
        "${H_NAME}" "${H_BASE}" "${H_HT}" "${H_HT_O}" \
        "${H_DT}" "${H_DT_O}" "${H_BOTH}" "${H_BOTH_O}"
    echo "${MID_BORDER}"

    local row label t_base t_ht ht_o t_dt dt_o t_both both_o
    for row in "${RESULTS[@]}"; do
        IFS='|' read -r label t_base t_ht ht_o t_dt dt_o t_both both_o <<< "${row}"

        local C_HT C_DT C_BOTH

        # Цвет накладных: зелёный < 5%, жёлтый < 20%, красный >= 20%
        color_for() {
            local v="$1"
            local abs
            abs=$(echo "${v#-}" | bc -l)   # абсолютное значение
            if echo "$abs < 5" | bc -q | grep -q 1; then
                echo "${CG}"
            elif echo "$abs < 20" | bc -q | grep -q 1; then
                echo "${CY}"
            else
                echo "${CR}"
            fi
        }

        C_HT=$(color_for "${ht_o}")
        C_DT=$(color_for "${dt_o}")
        C_BOTH=$(color_for "${both_o}")

        local F_NAME F_BASE F_HT F_HT_O F_DT F_DT_O F_BOTH F_BOTH_O
        F_NAME=$(printf "%-${W_NAME}s" "${label}")
        F_BASE=$(printf "%${W_TIME}s" "${t_base} s")
        F_HT=$(printf   "%${W_TIME}s" "${t_ht} s")
        F_DT=$(printf   "%${W_TIME}s" "${t_dt} s")
        F_BOTH=$(printf "%${W_TIME}s" "${t_both} s")

        ht_pct=$(fmt_pct "${ht_o}")
        dt_pct=$(fmt_pct "${dt_o}")
        both_pct=$(fmt_pct "${both_o}")

        F_HT_O=$(printf "%${W_OVHD}s" "${ht_pct}")
        F_DT_O=$(printf "%${W_OVHD}s" "${dt_pct}")
        F_BOTH_O=$(printf "%${W_OVHD}s" "${both_pct}")

        printf "│ %s │ %s │ %s │ ${C_HT}%s${CNC} │ %s │ ${C_DT}%s${CNC} │ %s │ ${C_BOTH}%s${CNC} │\n" \
            "${F_NAME}" "${F_BASE}" "${F_HT}" "${F_HT_O}" \
            "${F_DT}" "${F_DT_O}" "${F_BOTH}" "${F_BOTH_O}"
    done

    echo "${BOT_BORDER}"
    echo
    echo -e "${CBOLD}Легенда накладных расходов:${CNC}"
    echo -e "  ${CG}■${CNC} < 5%    незначительный overhead"
    echo -e "  ${CY}■${CNC} 5–20%   заметный overhead"
    echo -e "  ${CR}■${CNC} > 20%   высокий overhead"
    echo
    echo -e "${CBOLD}Колонки:${CNC}"
    echo "  Baseline      — запуск без FineTrace"
    echo "  --host-timing — FineTrace с профилированием host API (CPU накладные)"
    echo "  --device-tmg  — FineTrace с профилированием device kernels (GPU накладные)"
    echo "  Both          — оба флага одновременно"
    echo "  CPU ovhd      — (host_time - baseline) / baseline × 100%"
    echo "  GPU ovhd      — (device_time - baseline) / baseline × 100%"
    echo "  Total ovhd    — (both_time - baseline) / baseline × 100%"
}

# ============================================================
# Итог в CSV (опционально)
# ============================================================
save_csv() {
    local csv="${LOG_DIR}/results_$(date +%Y%m%d_%H%M%S).csv"
    {
        echo "sample,baseline_s,host_timing_s,cpu_ovhd_pct,device_timing_s,gpu_ovhd_pct,both_s,total_ovhd_pct"
        local row
        for row in "${RESULTS[@]}"; do
            echo "${row//|/,}"
        done
    } > "${csv}"
    ok "CSV сохранён: ${csv}"
}

# ============================================================
# Проверка зависимостей
# ============================================================
check_deps() {
    local missing=0
    for dep in cmake make bc; do
        if ! command -v "${dep}" &>/dev/null; then
            warn "Не найдена утилита: ${dep}"
            (( missing++ )) || true
        fi
    done
    [[ ${missing} -eq 0 ]] || { echo "Установите недостающие зависимости"; exit 1; }
}

# ============================================================
# MAIN
# ============================================================
main() {
    echo
    echo -e "${CBOLD}${CC}╔══════════════════════════════════════════════════════╗${CNC}"
    echo -e "${CBOLD}${CC}║        FineTrace Overhead Benchmark Suite            ║${CNC}"
    echo -e "${CBOLD}${CC}╚══════════════════════════════════════════════════════╝${CNC}"
    echo
    echo -e "  Число повторений : ${CBOLD}${REPEAT_COUNT}${CNC}"
    echo -e "  Лог-директория   : ${LOG_DIR}"
    echo

    check_deps

    if [[ ${OPT_NO_BUILD} -eq 0 ]]; then
        do_build
    else
        # При --no-build просто ищем уже собранные бинари
        info "Пропуск сборки (--no-build)"
        FINETRACE="${BUILD_DIR}/finetrace"
        [[ -x "${FINETRACE}" ]] || { echo "finetrace не найден: ${FINETRACE}"; exit 1; }
        [[ -x "${SAMPLES_DIR}/cl_gemm/build/cl_gemm"    ]] && CL_GEMM_EXE="${SAMPLES_DIR}/cl_gemm/build/cl_gemm"
        [[ -x "${SAMPLES_DIR}/ze_gemm/build/ze_gemm"    ]] && ZE_GEMM_EXE="${SAMPLES_DIR}/ze_gemm/build/ze_gemm"
        [[ -x "${RODINIA_DIR}/gaussian/gaussian.out"     ]] && GAUSSIAN_EXE="${RODINIA_DIR}/gaussian/gaussian.out"
        [[ -x "${RODINIA_DIR}/nw/nw.out"                 ]] && NW_EXE="${RODINIA_DIR}/nw/nw.out"
        NW_CL="${RODINIA_DIR}/nw/nw.cl"
    fi

    do_benchmarks
    print_table
    save_csv

    echo -e "${CG}Готово.${CNC}"
}

main "$@"
