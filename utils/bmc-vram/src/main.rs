//! bmc-vram — release device memory held by processes.
//!
//! Replaces `scripts/maintenance/clean_vram.sh`.
//!
//! What the shell version did, in order: list every compute process on the
//! host, ask one yes/no question, then signal all of them regardless of who
//! owns them, then offer to reset devices that might still be busy. On a
//! shared training host that is a way to kill a colleague's week-long run
//! with one keystroke, and `read -p` as the only path means it cannot be run
//! from a script or a cron at all.
//!
//! Here:
//!   * dry run is the default — listing is what you get for free,
//!   * acting requires `--apply` **and** named targets,
//!   * another user's process is refused, not prompted about, and the refusal
//!     is the point of the exit code,
//!   * `--override` exists, is loud, and is recorded in the output so the
//!     action is attributable afterwards,
//!   * a device reset is refused while any process still holds the device.
//!
//! Exit codes: 0 nothing to do or acted as asked · 1 refused or an action
//! failed · 2 the state could not be determined.

use bmc::{current_user, process_owner, Smi, Status};
use std::process::Command;
use std::thread::sleep;
use std::time::Duration;

const TERM_GRACE: Duration = Duration::from_secs(3);

struct Args {
    apply: bool,
    all: bool,
    pids: Vec<u32>,
    override_foreign: bool,
    reset: bool,
}

const HELP: &str = "\
bmc-vram — release GPU memory held by processes

  (no action flags)   list every compute process, its owner, and its memory
  --pid N             target one process; repeatable
  --all               target every compute process (needs --apply)
  --apply             actually signal; without it nothing is touched
  --override          permit signalling processes owned by another user
  --reset             reset device state after releasing (refused if still held)

exits 0 nothing to do / acted | 1 refused or failed | 2 could not determine
";

fn parse_args() -> Result<Args, String> {
    let mut a = Args {
        apply: false,
        all: false,
        pids: Vec::new(),
        override_foreign: false,
        reset: false,
    };
    let mut it = std::env::args().skip(1);
    while let Some(arg) = it.next() {
        match arg.as_str() {
            "--apply" => a.apply = true,
            "--all" => a.all = true,
            "--override" => a.override_foreign = true,
            "--reset" => a.reset = true,
            "--pid" => {
                let v = it.next().ok_or("--pid needs a value")?;
                a.pids.push(v.parse().map_err(|_| format!("--pid: not a pid: {v}"))?);
            }
            "-h" | "--help" => {
                print!("{HELP}");
                std::process::exit(0);
            }
            other => return Err(format!("unknown argument: {other}")),
        }
    }
    if a.all && !a.pids.is_empty() {
        return Err("--all and --pid are alternatives; name one".into());
    }
    Ok(a)
}

fn main() {
    let args = match parse_args() {
        Ok(a) => a,
        Err(e) => {
            eprintln!("bmc-vram: {e}");
            std::process::exit(Status::Problem.code());
        }
    };

    let smi = match Smi::open() {
        Ok(s) => s,
        Err(_) => {
            println!("UNAVAILABLE  nvidia-smi not found; no device state was read");
            std::process::exit(Status::Unavailable.code());
        }
    };

    let holders = match smi.compute_apps() {
        Ok(h) => h,
        Err(_) => {
            println!("INDETERMINATE  compute process list unavailable; nothing was signalled");
            std::process::exit(Status::Indeterminate.code());
        }
    };

    let me = current_user();
    let me = me.as_deref().unwrap_or("unknown");

    if holders.is_empty() {
        println!("OK           no processes hold device memory");
        std::process::exit(Status::Ok.code());
    }

    // The listing is the same information the old script printed before it
    // asked to destroy everything, now with the owner attached — the one
    // column whose absence made the old prompt meaningless.
    println!("{:<8} {:<12} {:<14} {}", "PID", "OWNER", "MEMORY", "PROCESS");
    for (pid, name, mem) in &holders {
        let owner = process_owner(*pid).unwrap_or_else(|| "gone".into());
        let mark = if owner != me && owner != "gone" { " other user" } else { "" };
        println!("{pid:<8} {owner:<12} {mem:<14} {name}{mark}");
    }

    if !args.apply {
        println!(
            "\nDRY RUN  {} process(es) hold device memory; nothing was signalled.\n\
             Re-run with --apply and --pid <n> or --all to act.",
            holders.len()
        );
        std::process::exit(Status::Ok.code());
    }

    // Decide the target set, then refuse before acting. Refusing after
    // signalling half the list is the failure this ordering prevents.
    let targets: Vec<u32> = if args.all {
        holders.iter().map(|(p, _, _)| *p).collect()
    } else if !args.pids.is_empty() {
        args.pids.clone()
    } else {
        println!(
            "REFUSED  --apply needs a target: --pid <n> (repeatable) or --all.\n\
             A prompt is not offered, because a maintenance command that\n\
             only works when a person is watching cannot be scripted or audited."
        );
        std::process::exit(Status::Problem.code());
    };

    let mut status = Status::Ok;
    let mut to_signal: Vec<u32> = Vec::new();

    for pid in &targets {
        let present = holders.iter().any(|(p, _, _)| p == pid);
        if !present {
            println!("OK           pid {pid} holds no device memory; nothing to do");
            continue;
        }
        let owner = process_owner(*pid);
        match owner.as_deref() {
            None => {
                println!("INDETERMINATE  pid {pid} owner unknown; not signalled");
                status = status.worst(Status::Indeterminate);
            }
            Some(o) if o != me && !args.override_foreign => {
                println!(
                    "REFUSED      pid {pid} is owned by {o}, not {me}; \
                     pass --override to signal another user's process"
                );
                status = status.worst(Status::Problem);
            }
            Some(o) => {
                if o != me {
                    println!("OVERRIDE     pid {pid} owned by {o} signalled by {me}");
                }
                to_signal.push(*pid);
            }
        }
    }

    if to_signal.is_empty() {
        println!("\nNothing signalled.");
        std::process::exit(status.code());
    }

    for pid in &to_signal {
        match signal(*pid, "TERM") {
            Ok(()) => println!("OK           TERM sent to pid {pid}"),
            Err(e) => {
                println!("PROBLEM      could not signal pid {pid}: {e}");
                status = status.worst(Status::Problem);
            }
        }
    }

    sleep(TERM_GRACE);

    // Escalate only against the pids this run targeted. The old script
    // re-read the whole list here and force-killed whoever had appeared in
    // the meantime, including processes started after the prompt.
    let still = smi
        .compute_apps()
        .unwrap_or_default()
        .into_iter()
        .map(|(p, _, _)| p)
        .collect::<Vec<_>>();
    for pid in &to_signal {
        if still.contains(pid) {
            match signal(*pid, "KILL") {
                Ok(()) => println!("OK           KILL sent to pid {pid}, it had ignored TERM"),
                Err(e) => {
                    println!("PROBLEM      could not force pid {pid}: {e}");
                    status = status.worst(Status::Problem);
                }
            }
        }
    }

    if args.reset {
        status = status.worst(reset_devices(&smi));
    }

    println!(
        "\n{}  vram cleanup finished at {}",
        label(status),
        bmc::stamp("%Y-%m-%d %H:%M:%S")
    );
    std::process::exit(status.code());
}

fn label(s: Status) -> &'static str {
    match s {
        Status::Ok => "OK",
        Status::Problem => "PROBLEM",
        Status::Indeterminate => "INDETERMINATE",
        Status::Unavailable => "UNAVAILABLE",
    }
}

fn signal(pid: u32, name: &str) -> Result<(), String> {
    let out = Command::new("kill")
        .arg(format!("-{name}"))
        .arg(pid.to_string())
        .output()
        .map_err(|e| e.to_string())?;
    if out.status.success() {
        Ok(())
    } else {
        Err(String::from_utf8_lossy(&out.stderr).trim().to_string())
    }
}

/// A reset while a context is live is the case where `nvidia-smi -r` fails in
/// a way that is hard to read, so the precondition is checked rather than
/// attempted.
fn reset_devices(smi: &Smi) -> Status {
    match smi.compute_apps() {
        Ok(still) if !still.is_empty() => {
            let pids: Vec<String> = still.iter().map(|(p, _, _)| p.to_string()).collect();
            println!(
                "REFUSED      device reset needs every device free; still held by {}",
                pids.join(", ")
            );
            Status::Problem
        }
        Err(_) => {
            println!("INDETERMINATE  could not confirm the devices are free; not resetting");
            Status::Indeterminate
        }
        Ok(_) => {
            let count = smi.device_count().unwrap_or(0);
            let mut status = Status::Ok;
            for i in 0..count {
                let idx = i.to_string();
                match smi.raw(&["-i", &idx, "-r"]) {
                    Ok(_) => println!("OK           gpu {idx} reset"),
                    Err(bmc::SmiError::Failed(m)) => {
                        println!("PROBLEM      gpu {idx} reset failed: {m}");
                        status = status.worst(Status::Problem);
                    }
                    Err(bmc::SmiError::Missing) => {
                        println!("UNAVAILABLE  nvidia-smi disappeared mid-run");
                        return Status::Unavailable;
                    }
                }
            }
            status
        }
    }
}
