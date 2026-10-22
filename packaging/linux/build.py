import argparse
import json
import os
from pathlib import Path

from common import DEPENDENCIES, ROOT, checkout, output, run, ubuntu_version, version


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    version()
    ubuntu = ubuntu_version()
    prefix, work = args.prefix.resolve(), args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    qt = prefix / "qt"
    yaml = prefix / "yaml"
    parallel = str(min(4, os.cpu_count() or 2))
    marker = prefix / "complete.json"
    expected = {"ubuntu": ubuntu, "qt": DEPENDENCIES["qt"], "yamlCpp": DEPENDENCIES["yamlCpp"]}
    if not marker.exists() or json.loads(marker.read_text()) != expected:
        for module, commit in DEPENDENCIES["qt"].items():
            source = work / module
            checkout("https://github.com/qt/" + module + ".git", commit, source)
            run("cmake", "-S", source, "-B", work / (module + "-build"), "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_INSTALL_PREFIX=" + str(qt),
                "-DCMAKE_PREFIX_PATH=" + str(qt), "-DQT_BUILD_TESTS=OFF",
                "-DQT_BUILD_EXAMPLES=OFF", "-DQT_BUILD_BENCHMARKS=OFF")
            run("cmake", "--build", work / (module + "-build"), "--parallel", parallel)
            run("cmake", "--install", work / (module + "-build"))
        source = work / "yaml-source"
        checkout("https://github.com/jbeder/yaml-cpp.git", DEPENDENCIES["yamlCpp"], source)
        run("cmake", "-S", source, "-B", work / "yaml-build", "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_INSTALL_PREFIX=" + str(yaml),
            "-DYAML_BUILD_SHARED_LIBS=ON", "-DYAML_CPP_BUILD_TESTS=OFF", "-DYAML_CPP_BUILD_TOOLS=OFF")
        run("cmake", "--build", work / "yaml-build", "--parallel", parallel)
        run("cmake", "--install", work / "yaml-build")
        marker.write_text(json.dumps(expected))
    if output(qt / "bin/qmake", "-query", "QT_VERSION") != DEPENDENCIES["qtVersion"]:
        raise ValueError("Cached Qt version mismatch")
    application = work / "application"
    run("cmake", "-S", ROOT, "-B", application, "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=ON", "-DCMAKE_BUILD_RPATH_USE_ORIGIN=ON",
        "-DCMAKE_PREFIX_PATH=" + str(qt) + ";" + str(yaml))
    run("cmake", "--build", application, "--parallel", parallel)
    run("ctest", "--test-dir", application, "--output-on-failure", "--no-tests=error",
        "--output-junit", "ctest-results.xml")


if __name__ == "__main__":
    main()
