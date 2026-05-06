import subprocess
import pandas as pd

def run_benchmarks():
    print("Running bash script... Please wait until benchmarks complete.")
    # Run the bash script and read its output
    process = subprocess.Popen(['./script_run_samples.sh'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    
    results = []
    current_mode = None
    capture = False
    
    for line in process.stdout:
        print(line, end='')  # Mirror output to the console
        
        if "Summary" in line:
            capture = True
        
        if capture and "--- Mode:" in line:
            current_mode = line.split("Mode:")[1].strip(" -#\n")
            
        if capture and "|" in line and "Benchmark" not in line and "---" not in line:
            parts = [p.strip() for p in line.split("|")]
            if len(parts) > 1:
                results.append({
                    "Mode": current_mode,
                    "Benchmark": parts[0],
                    "clean": parts[1],
                    "call-logging": parts[2],
                    "device-timeline": parts[3],
                    "both": parts[4]
                })

    process.wait()
    return results

def create_excel(data, filename="finetrace_overhead_stat.xlsx"):
    df = pd.DataFrame(data)
    
    # Convert absolute values to numbers
    abs_columns = ["clean", "call-logging", "device-timeline", "both"]
    for col in abs_columns:
        df[col] = pd.to_numeric(df[col], errors='coerce')

    # Compute overhead as a percentage
    pct_columns = []
    for col in ["call-logging", "device-timeline", "both"]:
        pct_col_name = f"{col} (%)"
        pct_columns.append(pct_col_name)
        # Formula: (Current - Clean) / Clean * 100
        df[pct_col_name] = ((df[col] - df["clean"]) / df["clean"]) * 100

    # Create the Excel file
    writer = pd.ExcelWriter(filename, engine='xlsxwriter')
    
    for mode in df['Mode'].unique():
        # Filter data by mode
        mode_df = df[df['Mode'] == mode].drop(columns=['Mode'])
        sheet_name = f"Results_{mode}"
        
        # Write to the sheet
        mode_df.to_excel(writer, sheet_name=sheet_name, index=False)
        
        workbook  = writer.book
        worksheet = writer.sheets[sheet_name]
        
        # Round percentages for cleaner display in the table
        format_pct = workbook.add_format({'num_format': '0.00'})
        worksheet.set_column(5, 7, None, format_pct) 
        
        max_row = len(mode_df)
        
        # ==========================================
        # CHART 1: Absolute time (milliseconds)
        # ==========================================
        chart_abs = workbook.add_chart({'type': 'column'})
        for i, col_name in enumerate(abs_columns):
            chart_abs.add_series({
                'name':       [sheet_name, 0, i + 1],
                'categories': [sheet_name, 1, 0, max_row, 0],
                'values':     [sheet_name, 1, i + 1, max_row, i + 1],
            })
        chart_abs.set_title({'name': f'Absolute Time - {mode.upper()}'})
        chart_abs.set_x_axis({'name': 'Benchmark'})
        chart_abs.set_y_axis({'name': 'Time (s)'})
        chart_abs.set_style(11)
        worksheet.insert_chart('J2', chart_abs, {'x_scale': 1.2, 'y_scale': 1.2})
        
        # ==========================================
        # CHART 2: Overhead (percentage)
        # ==========================================
        chart_pct = workbook.add_chart({'type': 'column'})
        for i, col_name in enumerate(pct_columns):
            col_idx = i + 5  # Indices of percentage columns (starting at F = 5)
            chart_pct.add_series({
                'name':       [sheet_name, 0, col_idx],
                'categories': [sheet_name, 1, 0, max_row, 0],
                'values':     [sheet_name, 1, col_idx, max_row, col_idx],
            })
        chart_pct.set_title({'name': f'Overhead Percentage - {mode.upper()}'})
        chart_pct.set_x_axis({'name': 'Benchmark'})
        chart_pct.set_y_axis({'name': 'Overhead (%)'})
        chart_pct.set_style(12)  # Different style for visual contrast
        
        # Place the overhead chart below the first chart
        worksheet.insert_chart('J20', chart_pct, {'x_scale': 1.2, 'y_scale': 1.2})
        
        # Widen columns slightly for readability
        worksheet.set_column('A:A', 20)
        worksheet.set_column('B:E', 12)
        worksheet.set_column('F:H', 18)

    writer.close()
    print(f"\n[Success] File created: {filename}. Absolute time and percentage overhead charts have been added.")

if __name__ == "__main__":
    raw_data = run_benchmarks()
    if raw_data:
        create_excel(raw_data)
    else:
        print("No data found. Please check the bash script output.")
