#!/usr/bin/env bash
set -euo pipefail

output_dir=${1:?Usage: run_grid_job.sh OUTPUT_DIR OUTPUT_PREFIX CONFIG_NAME}
output_prefix=${2:?Usage: run_grid_job.sh OUTPUT_DIR OUTPUT_PREFIX CONFIG_NAME}
config_name=${3:?Usage: run_grid_job.sh OUTPUT_DIR OUTPUT_PREFIX CONFIG_NAME}
process=${PROCESS:-0}

echo "@@ hostname: $(hostname)"
echo "@@ process: ${process}"
echo "@@ output destination: ${output_dir}"

payload=""
for candidate in \
    "${CONDOR_DIR_INPUT:-}/translate_payload" \
    "${INPUT_TAR_DIR_LOCAL:-}/translate_payload" \
    "${INPUT_TAR_DIR_LOCAL:-}" \
    "${CONDOR_DIR_INPUT:-}/translate_payload/translate_payload"; do
    if [[ -f "${candidate}/CMakeLists.txt" ]]; then
        payload=${candidate}
        break
    fi
done
if [[ -z "${payload}" ]]; then
    echo "Unable to find the unpacked translate_payload directory" >&2
    exit 10
fi

work_dir=${_CONDOR_SCRATCH_DIR:-$(pwd)}
build_dir="${work_dir}/translate_build"
result_dir="${work_dir}/translate_output"
mkdir -p "${build_dir}" "${result_dir}"

executable="${build_dir}/LArCADe"
if command -v cmake >/dev/null 2>&1; then
    echo "@@ building with cmake"
    cmake -S "${payload}" -B "${build_dir}" -DCMAKE_BUILD_TYPE=Release
    cmake --build "${build_dir}" --parallel "${OMP_NUM_THREADS:-1}"
else
    compiler=""
    if command -v c++ >/dev/null 2>&1; then
        compiler=$(command -v c++)
    elif command -v g++ >/dev/null 2>&1; then
        compiler=$(command -v g++)
    fi
    if [[ -z "${compiler}" ]]; then
        echo "Neither cmake nor a C++ compiler is available on this worker" >&2
        exit 12
    fi
    echo "@@ cmake unavailable; building directly with ${compiler}"
    "${compiler}" -std=c++17 -O3 -pthread \
        "${payload}/src/main.cpp" \
        "${payload}/src/Electron.cpp" \
        "${payload}/src/ProgressBar.cpp" \
        -o "${executable}"
fi

log_file="${work_dir}/${output_prefix}_job${process}.log"
set +e
"${executable}" \
    --config "${payload}/${config_name}" \
    --job-index "${process}" \
    --output-dir "${result_dir}" \
    --output-prefix "${output_prefix}_" \
    >"${log_file}" 2>&1
run_status=$?
set -e

archive="${work_dir}/${output_prefix}_job${process}.tar.gz"
tar -czf "${archive}" -C "${result_dir}" .

if ! command -v ifdh >/dev/null 2>&1; then
    if [[ -f /cvmfs/fermilab.opensciencegrid.org/products/common/etc/setups.sh ]]; then
        # shellcheck disable=SC1091
        source /cvmfs/fermilab.opensciencegrid.org/products/common/etc/setups.sh
        setup ifdhc
    fi
fi
command -v ifdh >/dev/null 2>&1 || {
    echo "ifdh is unavailable; cannot stage grid output" >&2
    exit 11
}

export IFDH_CP_MAXRETRIES=2
ifdh mkdir_p "${output_dir}"
ifdh cp -D "${archive}" "${output_dir}/${output_prefix}_job${process}.tar.gz"
ifdh cp -D "${log_file}" "${output_dir}/${output_prefix}_job${process}.log"
echo "@@ staged output for process ${process}"
exit "${run_status}"
