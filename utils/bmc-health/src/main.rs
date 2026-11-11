//! bmc-health — GPU health check.
//!
//! Replaces `scripts/maintenance/check_gpu_health.sh`.
//!
//! Exit-code contract, stated here because the whole point of the rewrite is
//! that the old script printed a temperature warning and exited 0:
//!
//! ```text
//! 0  every check ran and nothing was wrong
//! 1  a named condition is present (hot GPU, ECC errors, persistence off, ...)
//! 2  a check could not be performed — we do not know, which is not a pass
//! 3  nvidia-smi is absent, so nothing could be asked
//! ```
//!
//! The exit status is the worst outcome seen. A query the driver refused is
//! reported as such and widens the status to 2; it is never swallowed into a
//! blank field the way `|| echo "N/A"` did.
//!
//! Non-interactive by construction: no prompts, one line per finding, so the
//! operator's shell — the only consumer this project has, since it ships no
//! scheduler and no monitor — can act on the code.

use bmc::{Field, Smi, SmiError, Status};

const DEFAULT_THRESHOLD: f64 = 85.0;
const KERNEL_RECORD: &str = "/etc/barecortex/driver-kernel";

const QUERY: &str = "index,name,driver_version,persistence_mode,temperature.gpu,\
utilization.gpu,memory.used,memory.total,power.draw,power.limit";

struct Args {
    threshold: f64,
    quiet: bool,
    topo: bool,
}

fn parse_args() -> Result<Args, String> {
    let mut a = Args { threshold: DEFAULT_THRESHOLD, quiet: false, topo: false };
    let mut it = std::env::args().skip(1);
    while let Some(arg) = it.next() {
        match arg.as_str() {
            "--threshold" => {
                let v = it.next().ok_or("--threshold needs a value")?;
                a.threshold = v.parse().map_err(|_| format!("--threshold: not a number: {v}"))?;
            }
            "--quiet" => a.quiet = true,
            "--topo" => a.topo = true,
            "-h" | "--help" => {
                print!("{}", HELP);
                std::process::exit(0);
            }
            other => return Err(format!("unknown argument: {other}")),
        }
    }
    Ok(a)
}

const HELP: &str = "\
bmc-health — GPU health check

  --threshold N   temperature in C above which the run fails (default 85)
  --quiet         print only findings, not the healthy lines
  --topo          include the nvidia-smi topology table

exits 0 ok | 1 condition found | 2 could not measure | 3 driver absent
";

/// One line per finding, worst status accumulated.
struct Report {
    status: Status,
    lines: Vec<(Status, String)>,
}

impl Report {
    fn add(&mut self, s: Status, line: String) {
        self.status = self.status.worst(s);
        self.lines.push((s, line));
    }
}

fn main() {
    let args = match parse_args() {
        Ok(a) => a,
        Err(e) => {
            eprintln!("bmc-health: {e}");
            eprintln!("try --help");
            std::process::exit(Status::Problem.code());
        }
    };

    let smi = match Smi::open() {
        Ok(s) => s,
        Err(_) => {
            // The old script said this in prose and exited 1 via set -e. The
            // distinction that matters is 3, not 1: nothing was measured, so
            // no caller may read this as "healthy".
            println!("UNAVAILABLE  nvidia-smi not found; no measurement was made");
            std::process::exit(Status::Unavailable.code());
        }
    };

    let mut r = Report { status: Status::Ok, lines: Vec::new() };
    check_visibility(&smi, &mut r);
    check_gpus(&smi, &mut r, args.threshold);
    check_ecc(&smi, &mut r);
    check_pcie(&smi, &mut r);
    check_module_matches_kernel(&mut r);

    for (s, line) in &r.lines {
        if args.quiet && *s == Status::Ok {
            continue;
        }
        println!("{line}");
    }

    if args.topo {
        match smi.raw(&["topo", "-m"]) {
            Ok(t) => print!("{t}"),
            Err(_) => println!("INDETERMINATE  topology table unavailable"),
        }
    }

    if !args.quiet {
        println!(
            "{}  health check finished at {}",
            label(r.status),
            bmc::stamp("%Y-%m-%d %H:%M:%S")
        );
    }
    std::process::exit(r.status.code());
}

fn label(s: Status) -> &'static str {
    match s {
        Status::Ok => "OK",
        Status::Problem => "PROBLEM",
        Status::Indeterminate => "INDETERMINATE",
        Status::Unavailable => "UNAVAILABLE",
    }
}

fn check_visibility(smi: &Smi, r: &mut Report) {
    match smi.device_count() {
        Ok(0) => r.add(
            Status::Problem,
            "PROBLEM  no GPU devices visible to the driver".into(),
        ),
        Ok(n) => r.add(Status::Ok, format!("OK           {n} device(s) visible")),
        Err(_) => r.add(
            Status::Indeterminate,
            "INDETERMINATE  device list unavailable; driver may be loaded with no devices".into(),
        ),
    }
}

fn check_gpus(smi: &Smi, r: &mut Report, threshold: f64) {
    let rows = match smi.gpu_rows(QUERY, None) {
        Ok(rows) => rows,
        Err(SmiError::Failed(m)) => {
            r.add(Status::Indeterminate, format!("INDETERMINATE  gpu query failed: {m}"));
            return;
        }
        Err(SmiError::Missing) => {
            r.add(Status::Unavailable, "UNAVAILABLE  nvidia-smi disappeared".into());
            return;
        }
    };

    for row in rows {
        let get = |i: usize| row.get(i).cloned().unwrap_or(Field::NotApplicable);
        let idx = get(0).value().unwrap_or("?").to_string();
        let name = get(1).value().unwrap_or("unknown").to_string();
        let driver = get(2).value().unwrap_or("unknown").to_string();

        r.add(
            Status::Ok,
            format!("OK           gpu {idx} {name} driver {driver}"),
        );

        // Persistence matters here because the project ships
        // nvidia-persistenced; a disabled mode means the driver is torn down
        // between jobs and the first kernel launch pays for it.
        match get(3).value() {
            Some("Enabled") => {}
            Some(other) => r.add(
                Status::Problem,
                format!("PROBLEM      gpu {idx} persistence mode is {other}, expected Enabled"),
            ),
            None => r.add(
                Status::Indeterminate,
                format!("INDETERMINATE  gpu {idx} persistence mode field unavailable"),
            ),
        }

        // Temperature is the one field this check cannot do without.
        match get(4).number() {
            Some(t) if t > threshold => r.add(
                Status::Problem,
                format!("PROBLEM      gpu {idx} temperature {t:.0}C exceeds {threshold:.0}C"),
            ),
            Some(t) => r.add(
                Status::Ok,
                format!("OK           gpu {idx} temperature {t:.0}C of {threshold:.0}C limit"),
            ),
            None => r.add(
                Status::Indeterminate,
                format!("INDETERMINATE  gpu {idx} temperature unreadable; cannot conclude"),
            ),
        }

        let mem = format!("{}/{}", text(&get(6)), text(&get(7)));
        let util = text(&get(5));
        let power = format!("{} of {}", text(&get(8)), text(&get(9)));
        r.add(Status::Ok, format!("OK           gpu {idx} memory {mem} util {util} power {power}"));
    }
}

fn text(f: &Field) -> String {
    f.value().unwrap_or("n/a").to_string()
}

fn check_ecc(smi: &Smi, r: &mut Report) {
    const ECC: &str = "index,ecc.errors.corrected.volatile.total,ecc.errors.uncorrected.volatile.total";
    let rows = match smi.gpu_rows(ECC, None) {
        Ok(rows) => rows,
        // A driver that rejects the query outright is a driver without ECC
        // reporting, which is a fact about the card, not a failed measurement.
        Err(_) => {
            r.add(Status::Ok, "OK           ECC reporting not supported by these devices".into());
            return;
        }
    };
    for row in rows {
        let idx = row
            .first()
            .and_then(|f| f.value())
            .unwrap_or("?")
            .to_string();
        match (
            row.get(1).and_then(|f| f.number()),
            row.get(2).and_then(|f| f.number()),
        ) {
            (Some(corr), Some(unc)) if unc > 0.0 => r.add(
                Status::Problem,
                format!("PROBLEM      gpu {idx} ECC uncorrected {unc:.0} (corrected {corr:.0})"),
            ),
            (Some(corr), Some(_)) if corr > 0.0 => r.add(
                Status::Ok,
                format!("OK           gpu {idx} ECC corrected {corr:.0}, none uncorrected"),
            ),
            (Some(_), Some(_)) => {}
            _ => r.add(
                Status::Ok,
                format!("OK           gpu {idx} ECC not applicable"),
            ),
        }
    }
}

fn check_pcie(smi: &Smi, r: &mut Report) {
    // The field the shell version masked with `|| echo "N/A"` at line 33.
    // Unavailable now widens the exit status instead of printing a reassuring
    // blank, which is the difference between a check and a decoration.
    let rows = match smi.gpu_rows("index,pcie.replay.counter", None) {
        Ok(rows) => rows,
        Err(_) => {
            r.add(
                Status::Indeterminate,
                "INDETERMINATE  pcie.replay.counter unavailable on these devices".into(),
            );
            return;
        }
    };
    for row in rows {
        let idx = row.first().and_then(|f| f.value()).unwrap_or("?").to_string();
        match row.get(1).and_then(|f| f.number()) {
            Some(n) if n > 0.0 => r.add(
                Status::Problem,
                format!("PROBLEM      gpu {idx} PCIe replay counter {n:.0}, link is retraining"),
            ),
            Some(_) => {}
            None => r.add(
                Status::Indeterminate,
                format!("INDETERMINATE  gpu {idx} pcie.replay.counter unavailable"),
            ),
        }
    }
}

/// The out-of-tree module and the running kernel move together or not at all
/// (`maintenance-and-updates` D3). The record is written at install time; if
/// it is not there, the pairing is simply unknown, and unknown is not ok.
///
/// `BMC_DRIVER_RECORD` overrides the path for the same reason `BMC_NVIDIA_SMI`
/// does: the binary reads a fact about an installed machine, and a test host is
/// not one. Production never sets it.
fn check_module_matches_kernel(r: &mut Report) {
    let record = std::env::var("BMC_DRIVER_RECORD").unwrap_or_else(|_| KERNEL_RECORD.into());
    let running = match std::fs::read_to_string("/proc/sys/kernel/osrelease") {
        Ok(v) => v.trim().to_string(),
        Err(_) => {
            r.add(Status::Indeterminate, "INDETERMINATE  running kernel release unknown".into());
            return;
        }
    };
    match std::fs::read_to_string(&record) {
        Ok(v) => {
            let built_for = v.trim().to_string();
            if built_for == running {
                r.add(Status::Ok, format!("OK           module built for {built_for}"))
            } else {
                r.add(
                    Status::Problem,
                    format!(
                        "PROBLEM      kernel is {running} but the GPU module was built for {built_for}"
                    ),
                )
            }
        }
        Err(_) => r.add(
            Status::Indeterminate,
            format!("INDETERMINATE  no record at {record}; module/kernel pairing unknown"),
        ),
    }
}
