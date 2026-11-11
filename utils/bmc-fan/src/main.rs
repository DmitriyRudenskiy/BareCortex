//! bmc-fan — set GPU fan speed and report whether the driver accepted it.
//!
//! The mechanism half of `openrc/init.d/gpu-fancontrol`, which is a shell
//! wrapper around `nvidia-smi -f` with `2>/dev/null || true` on every write
//! (lines 41, 56). That combination is the defect: on the cards where the
//! driver ignores manual fan control — most GeForce and many Quadro, as the
//! init script's own comment admits — the service reported a successful start
//! and had changed nothing. `gpu-stack` 7.4 makes "success requires an
//! accepted write" a requirement; this binary is what satisfies it.
//!
//! What is deliberately **not** here: no temperature threshold, no polling
//! loop, no fan curve, no safe-failure state. `CHECK_INTERVAL` in the shipped
//! `conf.d` file is read by nothing in the incumbent and stays unread. Those
//! are `gpu-stack` 7.1–7.3's decisions to make, and a cooling policy invented
//! in a rewrite is the destructive case with no owner.
//!
//! Persistence mode is not touched either: the service already declares
//! `need nvidia-persistenced`, and `nvidia-smi -pm 1` beside a running daemon
//! is a second mechanism for one job.
//!
//! Exit codes: 0 every write accepted · 1 a write was refused ·
//! 2 a state could not be read · 3 the driver is absent.

use bmc::{Field, Smi, SmiError, Status};

const HELP: &str = "\
bmc-fan — set GPU fan speed and report what the driver accepted

  --percent N    target fan speed 0-100; 0 returns control to the driver
  --gpu IDX      target one device; repeatable, default every device
  --check        report the fan speed the driver reports, write nothing

exits 0 accepted | 1 refused | 2 could not read | 3 driver absent

No threshold and no loop: the policy belongs to gpu-stack 7.1-7.3.
";

struct Args {
    percent: Option<u32>,
    gpus: Vec<String>,
    check: bool,
}

fn parse_args() -> Result<Args, String> {
    parse_from(std::env::args().skip(1))
}

// Split from `parse_args` so the range check is tested against the code that
// runs, not a copy of it.
fn parse_from<I: Iterator<Item = String>>(mut it: I) -> Result<Args, String> {
    let mut a = Args {
        percent: None,
        gpus: Vec::new(),
        check: false,
    };
    while let Some(arg) = it.next() {
        match arg.as_str() {
            "--percent" => {
                let v = it.next().ok_or("--percent needs a value")?;
                let n: u32 = v.parse().map_err(|_| format!("--percent: not a number: {v}"))?;
                if n > 100 {
                    return Err(format!("--percent {n} is out of range 0-100"));
                }
                a.percent = Some(n);
            }
            "--gpu" => {
                let v = it.next().ok_or("--gpu needs a value")?;
                if !v.chars().all(|c| c.is_ascii_digit()) || v.is_empty() {
                    return Err(format!("--gpu: not a device index: {v}"));
                }
                a.gpus.push(v);
            }
            "--check" => a.check = true,
            "-h" | "--help" => {
                print!("{HELP}");
                std::process::exit(Status::Ok.code());
            }
            other => return Err(format!("unknown argument: {other}")),
        }
    }
    if !a.check && a.percent.is_none() {
        return Err("nothing to do: pass --percent N or --check".into());
    }
    Ok(a)
}

fn main() {
    let args = match parse_args() {
        Ok(a) => a,
        Err(e) => {
            eprintln!("bmc-fan: {e}");
            eprintln!("try --help");
            std::process::exit(Status::Problem.code());
        }
    };

    let smi = match Smi::open() {
        Ok(s) => s,
        Err(_) => {
            println!("UNAVAILABLE  nvidia-smi not found; no fan state was read or written");
            std::process::exit(Status::Unavailable.code());
        }
    };

    let targets: Vec<String> = if args.gpus.is_empty() {
        match smi.device_count() {
            Ok(n) => (0..n).map(|i| i.to_string()).collect(),
            Err(_) => {
                println!("INDETERMINATE  device list unavailable; nothing was written");
                std::process::exit(Status::Indeterminate.code());
            }
        }
    } else {
        args.gpus.clone()
    };

    if targets.is_empty() {
        println!("INDETERMINATE  no devices visible to the driver; nothing was written");
        std::process::exit(Status::Indeterminate.code());
    }

    let mut status = Status::Ok;

    if args.check {
        for gpu in &targets {
            match smi.fan_speed(gpu) {
                f @ Field::Value(_) => println!(
                    "OK           gpu {gpu} fan speed {}",
                    text(&f)
                ),
                Field::NotApplicable => println!(
                    "OK           gpu {gpu} reports no fan speed; the driver does not expose one"
                ),
            }
        }
        std::process::exit(status.code());
    }

    let percent = args.percent.expect("checked in parse_args");
    for gpu in &targets {
        match smi.raw(&["-i", gpu, "-f", &percent.to_string()]) {
            Ok(_) => {
                // The write was accepted. Whether the fan moved is the
                // read-back below, and a driver that exposes no value there
                // is a capability gap, not a failure of this write.
                println!("OK           gpu {gpu} accepted fan speed {percent}%");
                match smi.fan_speed(gpu) {
                    Field::NotApplicable => println!(
                        "OK           gpu {gpu} does not report fan speed back; \
                         acceptance is the strongest statement available"
                    ),
                    f => println!("OK           gpu {gpu} now reports {}", text(&f)),
                }
            }
            Err(SmiError::Failed(m)) => {
                println!("REFUSED      gpu {gpu} rejected fan speed {percent}%: {m}");
                status = status.worst(Status::Problem);
            }
            Err(SmiError::Missing) => {
                println!("UNAVAILABLE  nvidia-smi disappeared mid-run");
                std::process::exit(Status::Unavailable.code());
            }
        }
    }

    std::process::exit(status.code());
}

fn text(f: &Field) -> String {
    f.value().unwrap_or("n/a").to_string()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse(argv: &[&str]) -> Result<Args, String> {
        parse_from(argv.iter().map(|s| s.to_string()))
    }

    #[test]
    fn an_out_of_range_speed_is_refused_before_the_driver_sees_it() {
        assert!(parse(&["--percent", "101"]).is_err());
        assert!(parse(&["--percent", "80"]).unwrap().percent == Some(80));
        // 0 is the write that returns control to the driver, not a typo.
        assert!(parse(&["--percent", "0"]).unwrap().percent == Some(0));
        assert!(parse(&[]).is_err());
        assert!(parse(&["--check"]).unwrap().check);
        assert!(parse(&["--gpu", "0a"]).is_err());
        assert_eq!(parse(&["--gpu", "1", "--percent", "50"]).unwrap().gpus, ["1"]);
    }
}
