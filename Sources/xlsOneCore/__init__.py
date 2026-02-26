"""
xlsOneCore - Excel 报表合并核心库
"""

import re
from dataclasses import dataclass
from typing import Optional
from enum import Enum


class CellDataType(Enum):
    NUMERIC = "numeric"
    CURRENCY = "currency"
    PERCENTAGE = "percentage"
    TEXT = "text"
    CODE = "code"
    EMPTY = "empty"
    MIXED = "mixed"


@dataclass
class CellPosition:
    row: int
    column: int
    
    def a1_notation(self) -> str:
        """转换为 A1 表示法 (如 A1, B2)"""
        letters = ""
        col = self.column
        while col > 0:
            col -= 1
            letter = chr(65 + (col % 26))
            letters = letter + letters
            col //= 26
        return f"{letters}{self.row + 1}"


@dataclass
class CellData:
    position: CellPosition
    raw_value: Optional[str]
    cell_type: CellDataType
    numeric_value: Optional[float]


@dataclass
class ConflictMarkers:
    has_numeric: bool = False
    has_text: bool = False
    has_code: bool = False
    has_empty: bool = False
    
    @property
    def status_color(self) -> str:
        if self.has_numeric and not self.has_text and not self.has_code:
            return "green"
        if self.has_text or self.has_code:
            return "yellow"
        return "gray"


@dataclass
class MergedCell:
    position: CellPosition
    result_type: str
    result_value: Optional[str]
    numeric_value: Optional[float]
    source_values: list[str]
    conflict_markers: ConflictMarkers


@dataclass
class MergeStatistics:
    total_files: int
    total_cells: int
    numeric_cells: int
    text_cells: int
    code_cells: int
    empty_cells: int
    conflict_cells: int


def recognize_data_type(raw_value: Optional[str]) -> CellDataType:
    """识别数据类型"""
    if not raw_value or raw_value.strip() == "":
        return CellDataType.EMPTY
    
    value = raw_value.strip()
    
    if value.endswith("%"):
        return CellDataType.PERCENTAGE
    
    currency_symbols = ["¥", "$", "€", "£", "₩", "₹"]
    for symbol in currency_symbols:
        if symbol in value:
            return CellDataType.CURRENCY
    
    try:
        float(value)
        return CellDataType.NUMERIC
    except ValueError:
        pass
    
    if is_code_pattern(value):
        return CellDataType.CODE
    
    return CellDataType.TEXT


def is_code_pattern(value: str) -> bool:
    """判断是否为编码模式"""
    patterns = [
        r"^[A-Za-z]+-\d+$",
        r"^[A-Za-z]+_\d+$",
        r"^[A-Za-z]+\d+$",
        r"^订单\d+$",
        r"^No\.\d+$",
        r"^编号\d+$"
    ]
    
    for pattern in patterns:
        if re.match(pattern, value):
            return True
    
    has_letters = bool(re.search(r"[A-Za-z]", value))
    has_numbers = bool(re.search(r"\d", value))
    
    return has_letters and has_numbers and len(value) > 3


def extract_numeric_value(raw_value: Optional[str], cell_type: CellDataType) -> Optional[float]:
    """提取数值"""
    if not raw_value:
        return None
    
    value = raw_value.strip()
    
    if cell_type == CellDataType.NUMERIC:
        try:
            return float(value)
        except ValueError:
            return None
    
    if cell_type == CellDataType.CURRENCY:
        cleaned = re.sub(r"[^0-9.]", "", value)
        try:
            return float(cleaned)
        except ValueError:
            return None
    
    if cell_type == CellDataType.PERCENTAGE:
        cleaned = value.replace("%", "")
        try:
            return float(cleaned) / 100.0
        except ValueError:
            return None
    
    return None


def a1_to_position(a1: str) -> CellPosition:
    """将 A1 表示法转换为行列索引"""
    match = re.match(r"^([A-Z]+)(\d+)$", a1.upper())
    if not match:
        raise ValueError(f"无效的 A1 格式: {a1}")
    
    column_str = match.group(1)
    row_str = match.group(2)
    
    column = 0
    for char in column_str:
        column = column * 26 + (ord(char) - ord('A') + 1)
    
    row = int(row_str) - 1
    
    return CellPosition(row=row, column=column)


# 导出子模块
from .parser import ExcelParser
from .merger import (
    MergerEngine, MergeStrategy,
    NumericMergeStrategy, TextMergeStrategy, CodeMergeStrategy
)

__all__ = [
    'CellDataType', 'CellPosition', 'CellData',
    'ConflictMarkers', 'MergedCell', 'MergeStatistics',
    'recognize_data_type', 'is_code_pattern', 
    'extract_numeric_value', 'a1_to_position',
    'ExcelParser', 'MergerEngine', 'MergeStrategy',
    'NumericMergeStrategy', 'TextMergeStrategy', 'CodeMergeStrategy'
]
