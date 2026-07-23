#!/usr/bin/env python3
"""Store generated analytics credentials without exposing them to stdout."""

from __future__ import annotations

import json
import os
from pathlib import Path


secrets_path = Path(r"C:\Users\Administrator\secrets.json")
password_path = Path(__file__).resolve().parents[2] / ".build" / "analytics" / "admin_password"
temporary_path = secrets_path.with_name(secrets_path.name + ".analytics.tmp")

config = json.loads(secrets_path.read_text(encoding="utf-8-sig"))
password = password_path.read_text(encoding="utf-8").strip()
if not password:
    raise RuntimeError("Generated analytics password is empty")

config.update(
    {
        "ZPULSE_ANALYTICS_ADMIN_URL": "https://z-pulse.cn/analytics/",
        "ZPULSE_ANALYTICS_ADMIN_EMAIL": "privacy@z-pulse.cn",
        "ZPULSE_ANALYTICS_ADMIN_PASSWORD": password,
    }
)
temporary_path.write_text(
    json.dumps(config, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
)
os.replace(temporary_path, secrets_path)
password_path.unlink()
print("Analytics credentials stored in secrets.json; temporary password removed.")
