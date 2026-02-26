"""
xlsOneCore - Excel 汇总引擎
"""

import re
from collections import Counter
from dataclasses import dataclass
from enum import Enum
from typing import Optional
from . import (
    CellData, CellPosition, CellDataType, 
    MergedCell, ConflictMarkers, MergeStatistics,
    recognize_data_type, extract_numeric_value
)


class NumericMergeStrategy(Enum):
    SUM = "sum"
    AVERAGE = "average"
    MAX = "max"
    MIN = "min"
    FIRST = "first"
    LAST = "last"


class TextMergeStrategy(Enum):
    MAJORITY = "majority"
    FIRST = "first"
    LAST = "last"
    CONCAT = "concat"
    EMPTY = "empty"


class CodeMergeStrategy(Enum):
    PLACEHOLDER = "placeholder"
    FIRST = "first"
    LAST = "last"
    EMPTY = "empty"


@dataclass
class MergeStrategy:
    numeric_strategy: NumericMergeStrategy = NumericMergeStrategy.SUM
    text_strategy: TextMergeStrategy = TextMergeStrategy.MAJORITY
    code_strategy: CodeMergeStrategy = CodeMergeStrategy.PLACEHOLDER
    empty_as_zero: bool = False


class MergerEngine:
    """Excel 汇总引擎"""
    
    def __init__(self, strategy: Optional[MergeStrategy] = None):
        self.strategy = strategy or MergeStrategy()
    
    def merge(self, data_sets: list[list[list[CellData]]]) -> tuple[list[list[MergedCell]], MergeStatistics]:
        """汇总多个表格"""
        if not data_sets:
            return [], MergeStatistics(
                total_files=0, total_cells=0,
                numeric_cells=0, text_cells=0,
                code_cells=0, empty_cells=0, conflict_cells=0
            )
        
        # 验证结构一致性
        if not self._validate_structure(data_sets):
            raise ValueError("数据集结构不一致，无法汇总")
        
        merged_cells: list[list[MergedCell]] = []
        stats = MergeStatistics(
            total_files=len(data_sets),
            total_cells=0,
            numeric_cells=0,
            text_cells=0,
            code_cells=0,
            empty_cells=0,
            conflict_cells=0
        )
        
        # 获取最大行数和列数
        max_rows = max(len(ds) for ds in data_sets)
        max_cols = max(len(row) for ds in data_sets for row in ds)
        
        for row in range(max_rows):
            merged_row: list[MergedCell] = []
            
            for col in range(max_cols):
                position = CellPosition(row=row, column=col)
                
                # 收集该位置的所有数据
                cell_data_list: list[CellData] = []
                for data_set in data_sets:
                    if row < len(data_set) and col < len(data_set[row]):
                        cell_data_list.append(data_set[row][col])
                
                # 汇总该单元格
                merged_cell = self._merge_cell(cell_data_list, position)
                merged_row.append(merged_cell)
                
                # 统计
                stats.total_cells += 1
                markers = merged_cell.conflict_markers
                if markers.has_numeric:
                    stats.numeric_cells += 1
                elif markers.has_text:
                    stats.text_cells += 1
                elif markers.has_code:
                    stats.code_cells += 1
                elif markers.has_empty:
                    stats.empty_cells += 1
            
            merged_cells.append(merged_row)
        
        return merged_cells, stats
    
    def _merge_cell(self, data: list[CellData], position: CellPosition) -> MergedCell:
        """汇总单个单元格"""
        non_empty = [d for d in data if d.raw_value and d.raw_value.strip()]
        empty = [d for d in data if not d.raw_value or not d.raw_value.strip()]
        
        has_numeric = any(d.cell_type in [
            CellDataType.NUMERIC, CellDataType.CURRENCY, CellDataType.PERCENTAGE
        ] for d in non_empty)
        has_text = any(d.cell_type == CellDataType.TEXT for d in non_empty)
        has_code = any(d.cell_type == CellDataType.CODE for d in non_empty)
        has_empty = len(empty) > 0
        
        conflict_markers = ConflictMarkers(
            has_numeric=has_numeric,
            has_text=has_text,
            has_code=has_code,
            has_empty=has_empty
        )
        
        # 如果全为空
        if not non_empty:
            return MergedCell(
                position=position,
                result_type="empty",
                result_value=None,
                numeric_value=None,
                source_values=[],
                conflict_markers=conflict_markers
            )
        
        # 根据类型汇总
        if has_numeric and not has_text and not has_code:
            return self._merge_numeric(non_empty, position, conflict_markers)
        elif has_code and not has_numeric and not has_text:
            return self._merge_code(non_empty, position, conflict_markers)
        elif has_text and not has_numeric and not has_code:
            return self._merge_text(non_empty, position, conflict_markers)
        else:
            # 混合类型
            return self._merge_mixed(non_empty, position, conflict_markers)
    
    def _merge_numeric(self, data: list[CellData], position: CellPosition, 
                       markers: ConflictMarkers) -> MergedCell:
        """汇总数值"""
        values = [d.numeric_value for d in data if d.numeric_value is not None]
        raw_values = [d.raw_value for d in data if d.raw_value]
        
        if self.strategy.numeric_strategy == NumericMergeStrategy.SUM:
            result_value = str(sum(values))
            numeric_value = sum(values)
        elif self.strategy.numeric_strategy == NumericMergeStrategy.AVERAGE:
            result_value = str(sum(values) / len(values))
            numeric_value = sum(values) / len(values)
        elif self.strategy.numeric_strategy == NumericMergeStrategy.MAX:
            result_value = str(max(values))
            numeric_value = max(values)
        elif self.strategy.numeric_strategy == NumericMergeStrategy.MIN:
            result_value = str(min(values))
            numeric_value = min(values)
        elif self.strategy.numeric_strategy == NumericMergeStrategy.FIRST:
            result_value = data[0].raw_value
            numeric_value = data[0].numeric_value
        else:  # LAST
            result_value = data[-1].raw_value
            numeric_value = data[-1].numeric_value
        
        return MergedCell(
            position=position,
            result_type="numericSum",
            result_value=result_value,
            numeric_value=numeric_value,
            source_values=raw_values,
            conflict_markers=markers
        )
    
    def _merge_code(self, data: list[CellData], position: CellPosition,
                    markers: ConflictMarkers) -> MergedCell:
        """汇总编码"""
        raw_values = [d.raw_value for d in data if d.raw_value]
        
        if self.strategy.code_strategy == CodeMergeStrategy.PLACEHOLDER:
            result_value = self._generate_placeholder(raw_values)
            result_type = "codePlaceholder"
        elif self.strategy.code_strategy == CodeMergeStrategy.FIRST:
            result_value = raw_values[0]
            result_type = "firstValue"
        elif self.strategy.code_strategy == CodeMergeStrategy.LAST:
            result_value = raw_values[-1]
            result_type = "lastValue"
        else:
            result_value = ""
            result_type = "empty"
        
        return MergedCell(
            position=position,
            result_type=result_type,
            result_value=result_value,
            numeric_value=None,
            source_values=raw_values,
            conflict_markers=markers
        )
    
    def _merge_text(self, data: list[CellData], position: CellPosition,
                    markers: ConflictMarkers) -> MergedCell:
        """汇总文本"""
        raw_values = [d.raw_value for d in data if d.raw_value]
        
        if self.strategy.text_strategy == TextMergeStrategy.MAJORITY:
            # 取多数
            from collections import Counter
            counts = Counter(raw_values)
            result_value = counts.most_common(1)[0][0]
            result_type = "textMajority"
        elif self.strategy.text_strategy == TextMergeStrategy.FIRST:
            result_value = raw_values[0]
            result_type = "firstValue"
        elif self.strategy.text_strategy == TextMergeStrategy.LAST:
            result_value = raw_values[-1]
            result_type = "lastValue"
        elif self.strategy.text_strategy == TextMergeStrategy.CONCAT:
            result_value = ", ".join(raw_values)
            result_type = "textConcat"
        else:
            result_value = ""
            result_type = "empty"
        
        return MergedCell(
            position=position,
            result_type=result_type,
            result_value=result_value,
            numeric_value=None,
            source_values=raw_values,
            conflict_markers=markers
        )
    
    def _merge_mixed(self, data: list[CellData], position: CellPosition,
                     markers: ConflictMarkers) -> MergedCell:
        """汇总混合类型"""
        numeric_data = [d for d in data if d.cell_type in [
            CellDataType.NUMERIC, CellDataType.CURRENCY, CellDataType.PERCENTAGE
        ]]
        text_data = [d for d in data if d.cell_type in [CellDataType.TEXT, CellDataType.CODE]]
        
        numeric_values = [d.numeric_value for d in numeric_data if d.numeric_value is not None]
        raw_values = [d.raw_value for d in data if d.raw_value]
        
        # 汇总数值
        if numeric_values:
            result_value = str(sum(numeric_values))
            numeric_value = sum(numeric_values)
        else:
            result_value = text_data[0].raw_value if text_data else ""
            numeric_value = None
        
        return MergedCell(
            position=position,
            result_type="numericSum",
            result_value=result_value,
            numeric_value=numeric_value,
            source_values=raw_values,
            conflict_markers=markers
        )
    
    def _generate_placeholder(self, values: list[str]) -> str:
        """生成编码占位符"""
        if not values:
            return ""
        
        prefix = ""
        numbers: list[int] = []
        
        for value in values:
            number_part = re.sub(r"[^0-9]", "", value)
            letter_part = re.sub(r"[0-9]", "", value)
            
            prefix = letter_part
            if number_part:
                try:
                    numbers.append(int(number_part))
                except ValueError:
                    pass
        
        if numbers:
            placeholder_num = f"{sum(numbers):03d}"
            return f"{prefix}{placeholder_num}"
        return ""
    
    def _validate_structure(self, data_sets: list[list[list[CellData]]]) -> bool:
        """验证数据结构一致性"""
        if len(data_sets) <= 1:
            return True
        
        ref_rows = len(data_sets[0])
        ref_cols = max(len(row) for row in data_sets[0]) if data_sets[0] else 0
        
        for data_set in data_sets[1:]:
            if len(data_set) != ref_rows:
                return False
            for row in data_set:
                if len(row) != ref_cols:
                    return False
        
        return True
