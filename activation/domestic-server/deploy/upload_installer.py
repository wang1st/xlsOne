#!/usr/bin/env python3
# 上传 xlsOne 安装包到国内激活服务器（z-pulse.cn）的下载目录。
# 用法：
#   export ZP_PASS='<root密码>'
#   python3 upload_installer.py /path/to/xlsone-1.0.4-windows-amd64.msi [/path/to/other.exe ...]
# 也可不传 ZP_PASS，脚本会交互式询问（输入不回显）。
#
# 上传后文件可通过以下地址下载：
#   https://z-pulse.cn/activation/downloads/<file>        (DNS 就绪前，经主站代理)
#   https://api.z-pulse.cn/downloads/<file>              (DNS A 记录 + certbot 就绪后)
# 下载页： https://z-pulse.cn/activation/downloads/

import os
import sys
import getpass
from paramiko import SSHClient, AutoAddPolicy, SFTPClient

HOST = "z-pulse.cn"
PORT = 22
USER = "root"
REMOTE_DIR = "/opt/xlsone-activation/data/downloads"

def main():
    if len(sys.argv) < 2:
        print("用法: python3 upload_installer.py <本地文件1> [<本地文件2> ...]")
        sys.exit(2)
    local_paths = sys.argv[1:]
    for p in local_paths:
        if not os.path.isfile(p):
            print(f"[错误] 本地文件不存在: {p}")
            sys.exit(1)

    passwd = os.environ.get("ZP_PASS") or getpass.getpass(f"{USER}@{HOST} 密码: ")

    c = SSHClient()
    c.set_missing_host_key_policy(AutoAddPolicy())
    c.connect(HOST, port=PORT, username=USER, password=passwd, timeout=20,
              look_for_keys=False, allow_agent=False)
    sftp = c.open_sftp()

    # 递归创建远程目录
    cur = ""
    for part in REMOTE_DIR.strip("/").split("/"):
        cur += "/" + part
        try:
            sftp.stat(cur)
        except IOError:
            sftp.mkdir(cur)

    for lp in local_paths:
        rp = REMOTE_DIR.rstrip("/") + "/" + os.path.basename(lp)
        sftp.put(lp, rp)
        try:
            sftp.chmod(rp, 0o644)
        except Exception:
            pass
        print(f"  已上传 -> {rp}")

    sftp.close()
    c.close()
    print(f"\n完成。下载页： https://z-pulse.cn/activation/downloads/")
    print(f"文件直链示例： https://z-pulse.cn/activation/downloads/{os.path.basename(local_paths[0])}")

if __name__ == "__main__":
    main()
