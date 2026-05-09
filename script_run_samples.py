import subprocess
import pandas as pd
from xlsxwriter.utility import xl_rowcol_to_cell

# Palette: least saturated (pale) → most saturated (vivid), single blue hue.
_PALETTE = [
    "#D6E4F5",  # very pale blue
    "#A8C8EC",  # light blue
    "#74A9DE",  # medium-light blue
    "#4080C8",  # medium blue
    "#1F5BAF",  # medium-dark blue
    "#0D3478",  # deep navy
]


def _pick_colors(n):
    """Return n colors evenly spread across _PALETTE, light → dark."""
    if n == 1:
        return [_PALETTE[-1]]
    palette = _PALETTE
    if n >= len(palette):
        return palette[:]
    indices = [round(i * (len(palette) - 1) / (n - 1)) for i in range(n)]
    return [palette[idx] for idx in indices]


def run_benchmarks():
    print("Running bash script... Please wait until benchmarks complete.\n")
    process = subprocess.Popen(
        ['./script_run_samples.sh'],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )

    results = []
    current_mode = None
    current_headers = None
    capture = False

    for line in process.stdout:
        print(line, end='')

        if "Summary" in line:
            capture = True

        if not capture:
            continue

        if "--- Mode:" in line:
            current_mode = line.split("Mode:")[1].strip(" -#\n")
            current_headers = None
            continue

        if "|" not in line or "---" in line:
            continue

        parts = [p.strip() for p in line.split("|")]

        # Header row — "Benchmark" in first cell.
        if parts and parts[0] == "Benchmark":
            # Strip the trailing " (s)" suffix from each column label.
            current_headers = [
                h.replace(" (s)", "").strip()
                for h in parts[1:]
                if h.strip()
            ]
            continue

        # Data row — non-empty benchmark name.
        if current_headers and parts and parts[0]:
            bench_name = parts[0].removeprefix("bench_")
            row = {"Mode": current_mode, "Benchmark": bench_name}
            for i, col in enumerate(current_headers):
                row[col] = parts[i + 1].strip() if i + 1 < len(parts) else ""
            results.append(row)

    process.wait()
    return results


def create_excel(data, filename="finetrace_overhead_stat.xlsx"):
    df = pd.DataFrame(data)

    writer = pd.ExcelWriter(filename, engine='xlsxwriter')

    for mode in df['Mode'].unique():
        mode_df = df[df['Mode'] == mode].drop(columns=['Mode']).copy()

        # CPU charts: exclude ze_gemm (it only runs in GPU suite).
        if mode == "cpu":
            mode_df = mode_df[mode_df["Benchmark"] != "ze_gemm"].copy()

        # All columns except "Benchmark" hold timing values.
        timing_cols = [c for c in mode_df.columns if c != "Benchmark"]
        for col in timing_cols:
            mode_df[col] = pd.to_numeric(mode_df[col], errors='coerce')

        # Overhead relative to the "clean" baseline (if present).
        pct_columns = []
        if "clean" in mode_df.columns:
            for col in timing_cols:
                if col == "clean":
                    continue
                pct_name = f"{col} (%)"
                pct_columns.append(pct_name)
                mode_df[pct_name] = (
                    (mode_df[col] - mode_df["clean"]) / mode_df["clean"] * 100
                )

        sheet_name = f"Results_{mode}"
        mode_df.to_excel(writer, sheet_name=sheet_name, index=False)

        workbook  = writer.book
        worksheet = writer.sheets[sheet_name]

        fmt_pct = workbook.add_format({'num_format': '0.00'})
        pct_start_col = len(timing_cols) + 1  # 0-based column index
        if pct_columns:
            worksheet.set_column(
                pct_start_col, pct_start_col + len(pct_columns) - 1,
                None, fmt_pct
            )

        max_row = len(mode_df)

        mode_ru = "CPU" if mode == "cpu" else "GPU"

        # Charts go below the table: 1 header row + max_row data rows + 2 gap rows.
        chart_anchor_row = max_row + 2   # 0-based row index
        chart_row_step   = 20            # rows per chart (approx chart height)

        # ---- Chart 1: Absolute execution time ----
        colors_abs = _pick_colors(len(timing_cols))
        chart_abs = workbook.add_chart({'type': 'column'})
        for i, col_name in enumerate(timing_cols):
            col_idx = i + 1
            chart_abs.add_series({
                'name':       [sheet_name, 0, col_idx],
                'categories': [sheet_name, 1, 0, max_row, 0],
                'values':     [sheet_name, 1, col_idx, max_row, col_idx],
                'fill':       {'color': colors_abs[i]},
                'border':     {'color': colors_abs[i]},
            })
        chart_abs.set_title({'name': f'Время выполнения — {mode_ru}'})
        chart_abs.set_x_axis({'name': 'Приложение'})
        chart_abs.set_y_axis({'name': 'Время (с)'})
        chart_abs.set_style(11)
        worksheet.insert_chart(
            xl_rowcol_to_cell(chart_anchor_row, 0),
            chart_abs, {'x_scale': 1.6, 'y_scale': 1.2},
        )

        # ---- Chart 2: Overhead (%) relative to clean ----
        if pct_columns:
            colors_pct = _pick_colors(len(pct_columns))
            chart_pct = workbook.add_chart({'type': 'column'})
            for i, col_name in enumerate(pct_columns):
                col_idx = pct_start_col + i
                chart_pct.add_series({
                    'name':       [sheet_name, 0, col_idx],
                    'categories': [sheet_name, 1, 0, max_row, 0],
                    'values':     [sheet_name, 1, col_idx, max_row, col_idx],
                    'fill':       {'color': colors_pct[i]},
                    'border':     {'color': colors_pct[i]},
                })
            chart_pct.set_title({'name': f'Накладные расходы vs clean — {mode_ru}'})
            chart_pct.set_x_axis({'name': 'Приложение'})
            chart_pct.set_y_axis({'name': 'Накладные расходы (%)'})
            chart_pct.set_style(12)
            worksheet.insert_chart(
                xl_rowcol_to_cell(chart_anchor_row + chart_row_step, 0),
                chart_pct, {'x_scale': 1.6, 'y_scale': 1.2},
            )

        # Widen columns for readability.
        worksheet.set_column('A:A', 22)
        worksheet.set_column(1, len(timing_cols), 14)
        if pct_columns:
            worksheet.set_column(pct_start_col, pct_start_col + len(pct_columns) - 1, 16)

    writer.close()
    print(f"\n[Success] {filename} created.")


if __name__ == "__main__":
    raw_data = run_benchmarks()
    if raw_data:
        create_excel(raw_data)
    else:
        print("No data found. Check the bash script output.")
