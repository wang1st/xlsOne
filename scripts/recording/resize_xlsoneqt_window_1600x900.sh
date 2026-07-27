#!/usr/bin/env bash
set -euo pipefail

readonly app_name="xlsOneQt"
readonly target_width=1600
readonly target_height=900
readonly target_x=160
readonly target_y=72

if ! command -v osascript >/dev/null 2>&1; then
    echo "错误：未找到 macOS 的 osascript。" >&2
    exit 1
fi

app_pid="$(pgrep -x "$app_name" | head -n 1 || true)"
if [[ -z "$app_pid" ]]; then
    app_pid="$(
        pgrep -f '/xlsOneQt\.app/Contents/MacOS/xlsOneQt$' |
            head -n 1 ||
            true
    )"
fi

if [[ -z "$app_pid" ]]; then
    echo "错误：没有找到正在运行的 xlsOneQt。" >&2
    exit 1
fi

open -a "$app_name"
sleep 1

if ! resize_result="$(
    osascript - "$app_pid" "$target_width" "$target_height" "$target_x" "$target_y" 2>&1 <<'APPLESCRIPT'
on run arguments
    set appPid to item 1 of arguments as integer
    set targetWidth to item 2 of arguments as integer
    set targetHeight to item 3 of arguments as integer
    set targetX to item 4 of arguments as integer
    set targetY to item 5 of arguments as integer

    tell application "System Events"
        set targetProcess to first application process whose unix id is appPid
        set frontmost of targetProcess to true

        repeat 20 times
            if (count of windows of targetProcess) is greater than 0 then exit repeat
            delay 0.1
        end repeat

        if (count of windows of targetProcess) is 0 then
            error "xlsOneQt 当前没有可调整的窗口。"
        end if

        set targetWindow to front window of targetProcess

        set wasFullScreen to false
        try
            set wasFullScreen to value of attribute "AXFullScreen" of targetWindow
        end try

        set previousPosition to position of targetWindow
        set previousSize to size of targetWindow

        if wasFullScreen is true then
            set value of attribute "AXFullScreen" of targetWindow to false
            delay 2
            set targetWindow to front window of targetProcess
        end if

        set position of targetWindow to {targetX, targetY}
        set size of targetWindow to {targetWidth, targetHeight}
        delay 0.5

        set finalPosition to position of targetWindow
        set finalSize to size of targetWindow
        set finalWidth to item 1 of finalSize
        set finalHeight to item 2 of finalSize

        if finalWidth is not targetWidth or finalHeight is not targetHeight then
            error "系统将窗口限制为 " & finalWidth & "×" & finalHeight & "，未达到目标尺寸。"
        end if

        return "pid=" & appPid & ¬
            ",previous_position=" & item 1 of previousPosition & "," & item 2 of previousPosition & ¬
            ",previous_size=" & item 1 of previousSize & "x" & item 2 of previousSize & ¬
            ",current_position=" & item 1 of finalPosition & "," & item 2 of finalPosition & ¬
            ",current_size=" & finalWidth & "x" & finalHeight & ¬
            ",mode=windowed"
    end tell
end run
APPLESCRIPT
)"; then
    echo "调整窗口失败：$resize_result" >&2
    if [[ "$resize_result" == *"辅助"* ||
        "$resize_result" == *"assistive"* ||
        "$resize_result" == *"-1719"* ]]; then
        echo "请在“系统设置 → 隐私与安全性 → 辅助功能”中允许终端或 Codex 控制电脑。" >&2
    fi
    exit 1
fi

echo "xlsOneQt 窗口已按 16:9 比例调整为 ${target_width}×${target_height}。"
echo "$resize_result"
