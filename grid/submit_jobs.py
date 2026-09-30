#!/usr/bin/env python3
"""Package and submit independent TRANSLATE replicas with Fermilab jobsub."""

import argparse
import datetime
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tarfile


REPOSITORY = Path(__file__).resolve().parents[1]


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True, type=Path,
                        help="TRANSLATE key=value configuration file")
    parser.add_argument("--jobs", required=True, type=int,
                        help="Number of independent grid processes")
    parser.add_argument("--output-prefix", required=True,
                        help="Prefix used for the submission and output files")
    parser.add_argument("--output-dir",
                        help="PNFS destination (default: $TRANSLATE_GRID_OUT_DIR/run-name)")
    parser.add_argument("--group", default="sbnd", help="jobsub group (default: sbnd)")
    parser.add_argument("--role", default="Analysis", help="jobsub role")
    parser.add_argument("--email", help="Optional job notification address")
    parser.add_argument("--expected-lifetime", default="10h")
    parser.add_argument("--dry-run", action="store_true",
                        help="Build the payload and print, but do not execute, jobsub_submit")
    return parser.parse_args()


def main():
    args = parse_args()
    if args.jobs <= 0:
        raise SystemExit("--jobs must be greater than zero")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", args.output_prefix):
        raise SystemExit("--output-prefix may contain only letters, numbers, dot, underscore, and dash")
    config = args.config.expanduser().resolve()
    if not config.is_file():
        raise SystemExit(f"Config file does not exist: {config}")

    timestamp = datetime.datetime.now().strftime("%Y_%m_%d_%H%M%S")
    run_name = f"{args.output_prefix}__{timestamp}"
    staging_root = REPOSITORY / "grid" / "submissions" / run_name
    payload = staging_root / "translate_payload"
    payload.mkdir(parents=True)

    shutil.copy2(REPOSITORY / "CMakeLists.txt", payload)
    shutil.copytree(REPOSITORY / "src", payload / "src")
    shutil.copy2(config, payload / config.name)
    shutil.copy2(REPOSITORY / "grid" / "run_grid_job.sh", staging_root)

    tar_path = staging_root / "translate_payload.tar.gz"
    with tarfile.open(tar_path, "w:gz") as archive:
        for entry in payload.iterdir():
            archive.add(entry, arcname=entry.name)

    base_output = os.environ.get("TRANSLATE_GRID_OUT_DIR")
    if args.output_dir:
        output_dir = args.output_dir
    elif base_output:
        output_dir = f"{base_output.rstrip('/')}/{run_name}"
    else:
        user = os.environ.get("USER", "unknown")
        output_dir = f"/pnfs/{args.group}/scratch/users/{user}/translate/{run_name}"

    executable = staging_root / "run_grid_job.sh"
    command = [
        "jobsub_submit", "-G", args.group,
        "--auth-methods=token", f"--role={args.role}",
        "--resource-provides=usage_model=DEDICATED,OPPORTUNISTIC",
        "--lines", "+FERMIHTC_AutoRelease=True",
        "--lines", "+FERMIHTC_GraceMemory=3000",
        "--lines", "+FERMIHTC_GraceLifetime=3600",
        "--append_condor_requirements=(TARGET.HAS_SINGULARITY=?=true)",
        f"--tar_file_name=dropbox://{tar_path}",
        "-N", str(args.jobs), "--disk", "100GB",
        "--expected-lifetime", args.expected_lifetime,
    ]
    if args.email:
        command.extend(["--email-to", args.email])
    command.extend([
        f"file://{executable}", output_dir, args.output_prefix, config.name
    ])

    print(f"Submission directory: {staging_root}")
    print(f"Output directory: {output_dir}")
    print("Command:")
    print(shlex.join(command))
    if not args.dry_run:
        subprocess.run(command, check=True, cwd=staging_root)


if __name__ == "__main__":
    main()
