#!/usr/bin/env python3
import os
import sys
import time
import ftplib
import argparse

def parse_args():
    parser = argparse.ArgumentParser(description="Upload PS4 build directory to an FTP server.")
    parser.add_argument("--host", default="192.168.10.8", help="FTP server host (default: 192.168.10.8)")
    parser.add_argument("--port", type=int, default=21, help="FTP server port (default: 21)")
    parser.add_argument("--user", default="anonymous", help="FTP username (default: anonymous)")
    parser.add_argument("--password", default="", help="FTP password (default: empty)")
    parser.add_argument("--local", default=r"D:\MyEngine\PS4Build", help="Local directory to upload")
    parser.add_argument("--remote", default="/PS4Build", help="Remote destination directory on FTP")
    parser.add_argument("--exclude-obj", action="store_true", help="Exclude intermediate obj/ directory")
    return parser.parse_args()

def ensure_remote_dir(ftp, remote_path):
    parts = [p for p in remote_path.replace("\\", "/").strip("/").split("/") if p]
    cur = ""
    for p in parts:
        cur += "/" + p
        try:
            ftp.cwd(cur)
        except Exception:
            try:
                ftp.mkd(cur)
                ftp.cwd(cur)
            except Exception as e:
                print(f"[FTP] Warning creating directory {cur}: {e}")

def main():
    args = parse_args()
    local_dir = os.path.abspath(args.local)

    if not os.path.isdir(local_dir):
        print(f"[Error] Local directory does not exist: {local_dir}")
        sys.exit(1)

    print("=" * 60)
    print(f"PS4 Build FTP Uploader")
    print(f"  Target:    ftp://{args.host}:{args.port}{args.remote}")
    print(f"  Local Dir: {local_dir}")
    print("=" * 60)

    # Collect files
    file_list = []
    total_bytes = 0
    for root, dirs, files in os.walk(local_dir):
        if args.exclude_obj:
            dirs[:] = [d for d in dirs if d.lower() != "obj"]
        for f in files:
            full_path = os.path.join(root, f)
            rel_path = os.path.relpath(full_path, local_dir)
            size = os.path.getsize(full_path)
            file_list.append((full_path, rel_path, size))
            total_bytes += size

    print(f"Found {len(file_list)} files to upload ({total_bytes / (1024 * 1024):.2f} MB)")

    # Connect to FTP
    try:
        ftp = ftplib.FTP(timeout=30)
        print(f"Connecting to {args.host}:{args.port}...")
        ftp.connect(args.host, args.port)
        welcome = ftp.getwelcome()
        print(f"[FTP] Banner: {welcome}")
        ftp.login(user=args.user, passwd=args.password)
        print(f"[FTP] Logged in as {args.user}")
        ftp.set_pasv(True)
    except Exception as e:
        print(f"[Error] Failed to connect to FTP: {e}")
        sys.exit(1)

    start_time = time.time()
    uploaded_bytes = 0

    for idx, (full_path, rel_path, size) in enumerate(file_list, 1):
        remote_rel = rel_path.replace("\\", "/")
        remote_file_dir = os.path.dirname(remote_rel)
        target_dir = args.remote.rstrip("/") + ("/" + remote_file_dir if remote_file_dir else "")

        ensure_remote_dir(ftp, target_dir)

        filename = os.path.basename(full_path)
        size_mb = size / (1024 * 1024)
        print(f"[{idx}/{len(file_list)}] {remote_rel} ({size_mb:.2f} MB)... ", end="", flush=True)

        file_start = time.time()
        try:
            with open(full_path, "rb") as fp:
                ftp.storbinary(f"STOR {filename}", fp, blocksize=65536)
            file_elapsed = max(time.time() - file_start, 0.001)
            file_speed = (size / (1024 * 1024)) / file_elapsed
            uploaded_bytes += size
            print(f"OK ({file_speed:.2f} MB/s)")
        except Exception as e:
            print(f"FAILED ({e})")

    total_elapsed = max(time.time() - start_time, 0.001)
    avg_speed = (uploaded_bytes / (1024 * 1024)) / total_elapsed

    print("=" * 60)
    print("Upload Completed!")
    print(f"  Total Uploaded: {uploaded_bytes / (1024 * 1024):.2f} MB")
    print(f"  Total Time:     {total_elapsed:.2f} s")
    print(f"  Average Speed:  {avg_speed:.2f} MB/s")
    print("=" * 60)

    # Verification / listing
    print(f"\nVerifying remote contents of {args.remote}:")
    try:
        ftp.cwd(args.remote)
        ftp.dir()
    except Exception as e:
        print(f"[FTP] Could not list remote dir: {e}")

    try:
        ftp.quit()
    except Exception:
        ftp.close()
    print("\nFTP connection closed.")

if __name__ == "__main__":
    main()
