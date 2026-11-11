#!/usr/bin/env python3
"""test_check_no_systemd.py — does the scan fail on the right thing?

Run: python3 test_check_no_systemd.py
Exit 0 = the dynamic-section parser, the configuration scan, and the refusal to
scan nothing all behave. The ELF walk itself needs `readelf` and a built image,
which is build-orchestration's side of 5.2; what is proven here is that the
decision the scan makes is a decision about `DT_NEEDED`, not about strings.
"""

import importlib.util
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).parent
SPEC = importlib.util.spec_from_file_location(
    "scan", HERE / "scripts/build/check-no-systemd.py")
scan = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(scan)

UDEVADM = """\
  0x0000000000000001 (NEEDED)    Shared library: [libudev.so.1]
  0x0000000000000001 (NEEDED)    Shared library: [libc.so.6]
  0x000000000000000e (SONAME)    Library soname: [udevadm]
 0x0000000000000001 (NEEDED)     Shared library: [libsystemd.so.0]
"""

CLEAN = """\
  0x0000000000000001 (NEEDED)    Shared library: [libudev.so.1]
  0x0000000000000001 (NEEDED)    Shared library: [librt.so.1]
"""

# A string-table mention is not a runtime resolution: the edition installs
# systemd-hwdb as /usr/bin/udev-hwdb and it names libudev only.
STRINGS_ONLY = """\
  0x000000000000000e (SONAME)    Library soname: [libudev.so.1]
  0x0000000000000001 (NEEDED)    Shared library: [libc.so.6]
"""


def main():
    fails = []
    check = lambda label, cond: None if cond else fails.append(label)  # noqa: E731

    check("a udev binary linking only libudev was reported",
          scan.parse_needed(CLEAN) == [])
    check("a systemd-hwdb-style binary was reported",
          scan.parse_needed(STRINGS_ONLY) == [])
    check("libsystemd in the dynamic section was missed",
          scan.parse_needed(UDEVADM) == ["libsystemd.so.0"])
    check("libsystemd-shared was missed — the library the edition's udev build "
          "option exists to prevent",
          scan.parse_needed(" (NEEDED) Shared library: [libsystemd-shared-257.so]")
          == ["libsystemd-shared-257.so"])
    check("libelogind was missed",
          scan.parse_needed(" (NEEDED) Shared library: [libelogind.so.0]") == ["libelogind.so.0"])
    check("libnss_systemd was missed",
          scan.parse_needed(" (NEEDED) Shared library: [libnss_systemd.so.2]")
          == ["libnss_systemd.so.2"])

    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        check("a clean image root was reported as dirty", scan.scan_configs(root) == [])
        pam = root / "etc/pam.d"
        pam.mkdir(parents=True)
        (pam / "system-remote-login").write_text(
            "auth required pam_unix.so\nsession optional pam_systemd.so\n")
        (root / "etc/nsswitch.conf").write_text(
            "passwd: files systemd\n")
        hits = scan.scan_configs(root)
        check("pam_systemd named by a PAM config was missed",
              any("pam_systemd" in why and f.name == "system-remote-login"
                  for f, why in hits))
        check("an nsswitch.conf naming systemd was missed",
              any("nsswitch.conf" in str(f) for f, _ in hits))

    r = subprocess.run([sys.executable, str(HERE / "scripts/build/check-no-systemd.py")],
                       capture_output=True, text=True)
    check("scanning with no --root passed",
          r.returncode != 0 and "no --root" in r.stderr)
    r = subprocess.run([sys.executable, str(HERE / "scripts/build/check-no-systemd.py"),
                        "--root", str(HERE / "openrc")], capture_output=True, text=True)
    check("a tree of no ELF files was not reported as binaries=0",
          r.returncode == 0 and "binaries=0" in r.stdout)

    if fails:
        print(f"FAIL {len(fails)}")
        for f in fails:
            print("  " + f)
        return 1
    print("check-no-systemd: ok — DT_NEEDED is what is scanned, PAM and NSS "
          "configuration are scanned too, and a scan with no image root refuses")
    return 0


if __name__ == "__main__":
    sys.exit(main())
