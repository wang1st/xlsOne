#!/usr/bin/env python3
import os
import subprocess
import shutil
import glob
from PIL import Image, ImageDraw, ImageFont

VIDEO_IN = "marketing/demo-video.mp4"
VIDEO_OUT = "marketing/demo-video-subtitled.mp4"
FRAME_DIR = "marketing/frames_proc"
FONT_PATH = "/System/Library/AssetsV2/com_apple_MobileAsset_Font8/86ba2c91f017a3749571a82f2c6d890ac7ffb2fb.asset/AssetData/PingFang.ttc"

FREEZE_AT = 4.30       # seconds
FREEZE_DURATION = 2.0  # seconds

# Subtitle timings refer to the final output timeline (after freeze insertion).
# Original segments after the freeze point are shifted by FREEZE_DURATION.
SUBTITLES = [
    {"start": 2.20, "end": 4.20, "text": "导入 4 份同模板月报"},
    {"start": 4.30, "end": 6.30, "text": "自动汇总 2 个工作表"},        # during freeze
    {"start": 7.00, "end": 11.20, "text": "点击结果，追溯来源"},      # was 5.00–9.20
    {"start": 16.00, "end": 20.00, "text": "导出 Excel · 全程离线处理"}, # was 14.00–18.00
]

def get_video_info(path):
    cmd = [
        "ffprobe", "-v", "error", "-select_streams", "v:0",
        "-show_entries", "stream=r_frame_rate,width,height",
        "-show_entries", "format=duration",
        "-of", "json", path,
    ]
    import json
    out = subprocess.check_output(cmd).decode()
    data = json.loads(out)
    fps_str = data["streams"][0]["r_frame_rate"]
    num, den = map(int, fps_str.split("/"))
    fps = num / den
    width = data["streams"][0]["width"]
    height = data["streams"][0]["height"]
    duration = float(data["format"]["duration"])
    return fps, width, height, duration

def extract_frames(path, out_dir):
    if os.path.exists(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(out_dir, exist_ok=True)
    cmd = [
        "ffmpeg", "-y", "-i", path,
        "-q:v", "1",  # high quality JPEG
        os.path.join(out_dir, "frame_%06d.jpg"),
    ]
    subprocess.run(cmd, check=True)

def draw_subtitle(img, text, font):
    draw = ImageDraw.Draw(img, "RGBA")
    padding_h = 22
    padding_v = 14
    bbox = draw.textbbox((0, 0), text, font=font)
    text_w = bbox[2] - bbox[0]
    text_h = bbox[3] - bbox[1]
    bar_w = text_w + padding_h * 2
    bar_h = text_h + padding_v * 2
    # Bar anchored at the bottom center of the screen.
    bar_x = (img.width - bar_w) // 2
    bar_y = img.height - bar_h - 50
    radius = 8
    overlay = Image.new("RGBA", img.size, (0, 0, 0, 0))
    overlay_draw = ImageDraw.Draw(overlay)
    overlay_draw.rounded_rectangle(
        [bar_x, bar_y, bar_x + bar_w, bar_y + bar_h],
        radius=radius,
        fill=(0, 0, 0, 140),
    )
    img.paste(Image.alpha_composite(img.convert("RGBA"), overlay))
    # Draw text at the visual center of the bar using middle-center anchor.
    draw = ImageDraw.Draw(img)
    cx = img.width // 2
    cy = bar_y + bar_h // 2
    draw.text(
        (cx, cy), text, font=font, fill=(255, 255, 255, 255), anchor="mm"
    )

def insert_freeze_frames(frame_dir, fps, freeze_at, freeze_duration):
    import tempfile
    frames = sorted(glob.glob(os.path.join(frame_dir, "frame_*.jpg")))
    freeze_idx = int(round(freeze_at * fps))
    freeze_count = int(round(freeze_duration * fps))
    if freeze_idx >= len(frames):
        return
    freeze_source = frames[freeze_idx]

    # Build ordered list of source paths with freeze inserted after freeze_idx.
    ordered_sources = (
        frames[:freeze_idx + 1]
        + [freeze_source] * freeze_count
        + frames[freeze_idx + 1:]
    )

    # Copy to a temp directory first, then wipe frame_dir and rename sequentially.
    temp_dir = tempfile.mkdtemp()
    try:
        for i, src in enumerate(ordered_sources):
            shutil.copy2(src, os.path.join(temp_dir, f"{i:06d}.jpg"))
        for old_path in glob.glob(os.path.join(frame_dir, "*.jpg")):
            os.remove(old_path)
        temp_files = sorted(glob.glob(os.path.join(temp_dir, "*.jpg")))
        for i, src in enumerate(temp_files):
            shutil.copy2(src, os.path.join(frame_dir, f"frame_{i + 1:06d}.jpg"))
    finally:
        shutil.rmtree(temp_dir)

    print(f"Inserted {freeze_count} freeze frames at index {freeze_idx} ({freeze_at}s); total {len(ordered_sources)} frames")

def process_frames(frame_dir, fps, font):
    frames = sorted(glob.glob(os.path.join(frame_dir, "frame_*.jpg")))
    for i, frame_path in enumerate(frames):
        t = i / fps
        active = None
        for sub in SUBTITLES:
            if sub["start"] <= t < sub["end"]:
                active = sub["text"]
                break
        img = Image.open(frame_path).convert("RGBA")
        if active:
            draw_subtitle(img, active, font)
        img.convert("RGB").save(frame_path, quality=95)

def encode_video(frame_dir, fps, out_path, audio_source):
    pattern = os.path.join(frame_dir, "frame_*.jpg")
    frame_count = len(glob.glob(pattern))
    video_duration = frame_count / fps
    # Pad audio with silence so the final output matches the video length after freeze insertion.
    cmd = [
        "ffmpeg", "-y",
        "-framerate", str(fps),
        "-pattern_type", "glob",
        "-i", pattern,
        "-i", audio_source,
        "-af", f"apad=whole_dur={video_duration:.3f}",
        "-c:v", "libx264",
        "-pix_fmt", "yuv420p",
        "-crf", "18",
        "-preset", "medium",
        "-c:a", "aac",
        "-b:a", "128k",
        out_path,
    ]
    subprocess.run(cmd, check=True)

def main():
    fps, width, height, duration = get_video_info(VIDEO_IN)
    print(f"Video: {width}x{height}, {fps:.2f} fps, {duration:.2f}s")
    extract_frames(VIDEO_IN, FRAME_DIR)
    insert_freeze_frames(FRAME_DIR, fps, FREEZE_AT, FREEZE_DURATION)
    font = ImageFont.truetype(FONT_PATH, 48)
    process_frames(FRAME_DIR, fps, font)
    encode_video(FRAME_DIR, fps, VIDEO_OUT, VIDEO_IN)
    print(f"Done: {VIDEO_OUT}")

if __name__ == "__main__":
    main()
