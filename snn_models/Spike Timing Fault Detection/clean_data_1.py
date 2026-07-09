import csv
from pathlib import Path

INPUT_CSV = Path("D:\\桌面\\比赛或项目\\26嵌入式大赛\\data\\data\\industry_data.csv")
OUTPUT_CSV = Path("D:\\桌面\\比赛或项目\\26嵌入式大赛\\data\\data\\industry_data_cleaned.csv")

UINT16_MOD = 65536
INT16_SIGN_BIT = 32768


def convert_uint16_to_int16(value: str) -> str:
    value = value.strip()

    if value == "":
        return value

    try:
        number = float(value)
    except ValueError:
        return value

    if not number.is_integer():
        return value

    raw = int(number)

    if INT16_SIGN_BIT <= raw <= UINT16_MOD - 1:
        corrected = raw - UINT16_MOD

        if "." in value:
            return f"{corrected:.1f}"
        return str(corrected)

    return value


def clean_industry_data(input_path: Path, output_path: Path) -> None:
    with input_path.open("r", encoding="utf-8-sig", newline="") as f:
        reader = csv.reader(f)
        rows = list(reader)

    if not rows:
        raise ValueError("CSV 文件为空")

    header = rows[0]
    cleaned_rows = [header]

    for row in rows[1:]:
        cleaned_row = []

        for col_index, cell in enumerate(row):
            if col_index == len(row) - 1:
                cleaned_row.append(cell)
            else:
                cleaned_row.append(convert_uint16_to_int16(cell))

        cleaned_rows.append(cleaned_row)

    with output_path.open("w", encoding="utf-8-sig", newline="") as f:
        writer = csv.writer(f)
        writer.writerows(cleaned_rows)


if __name__ == "__main__":
    clean_industry_data(INPUT_CSV, OUTPUT_CSV)
    print(f"清洗完成，已生成：{OUTPUT_CSV}")