//! bmc-run — start a training job.
//!
//! Replaces `scripts/deploy/deploy_training_job.sh`.
//!
//! Three defects in the shell version are the reason this binary exists, and
//! each one is a behaviour change rather than a language preference:
//!
//! 1. It exported `CUDA_VISIBLE_DEVICES` and `NCCL_DEBUG` inline (lines
//!    56–57), which is the second and third place those are defined after
//!    `/etc/profile.d/nccl.sh` and the init script. `honest-scope-and-config-generation`
//!    D8 makes `/etc/conf.d/nccl` the single source; this launcher reads it
//!    and sets nothing of its own.
//! 2. It started the job with `tmux new-session -d -s NAME "<command>"`. A
//!    detached tmux **server** that is already running supplies the new pane's
//!    environment, not the client, so on any host with a live tmux session the
//!    exports above never reached the training process. The variables are
//!    therefore written into the command string the pane runs, where they
//!    cannot be lost.
//! 3. It checked `requested > available` and nothing else: `--gpus 0,7` on a
//!    two-GPU host passed the count check, and a duplicate or a non-numeric
//!    entry passed too. Device selection is validated index by index.
//!
//! Every run appends one line to the job record naming the script, the
//! interpreter, the devices, the log and where the NCCL values came from — the
//! record `userspace-and-ml-stack` design requires, and the thing that makes
//! "which environment did that run use" answerable the next morning.
//!
//! Exit codes: 0 started (or planned, with `--dry-run`) · 1 refused ·
//! 2 the state could not be determined · 3 tmux or the driver is absent.

use bmc::{read_exports, shell_quote, Smi, Status};
use std::os::unix::fs::PermissionsExt;
use std::path::Path;
use std::process::Command;

const VENV_BASE: &str = "/opt/ai";
const LOG_DIR: &str = "/var/log/ai-training";
/// The single source of the NCCL environment (`honest-scope-and-config-generation` D8).
const NCCL_CONF: &str = "/etc/conf.d/nccl";
/// One TSV line per launched job. Path owned by `userspace-and-ml-stack`;
/// if that change names a different home, this const is the only edit.
const JOB_RECORD: &str = "/var/log/ai-training/job-record.tsv";
const DEFAULT_PROJECT: &str = "barecortex_project";

const HELP: &str = "\
bmc-run — start a training job with the host's environment

  --script PATH      training script to run (required)
  --gpus 0,1         device list; default is CUDA_VISIBLE_DEVICES from /etc/conf.d/nccl
  --project NAME     project whose venv runs the job (default barecortex_project)
  --python PATH      interpreter override; default /opt/ai/<project>/venv/bin/python
  --session NAME     tmux session name; default training_<timestamp>
  --dry-run          print the resolved plan and change nothing

exits 0 started/planned | 1 refused | 2 could not determine | 3 tmux or driver absent
";

struct Args {
    script: String,
    gpus: Option<String>,
    project: String,
    python: Option<String>,
    session: Option<String>,
    dry_run: bool,
}

fn parse_args() -> Result<Args, String> {
    let mut a = Args {
        script: String::new(),
        gpus: None,
        project: DEFAULT_PROJECT.into(),
        python: None,
        session: None,
        dry_run: false,
    };
    let mut it = std::env::args().skip(1);
    while let Some(arg) = it.next() {
        match arg.as_str() {
            "--script" => a.script = it.next().ok_or("--script needs a value")?,
            "--gpus" => a.gpus = Some(it.next().ok_or("--gpus needs a value")?),
            "--project" => a.project = it.next().ok_or("--project needs a value")?,
            "--python" => a.python = Some(it.next().ok_or("--python needs a value")?),
            "--session" => a.session = Some(it.next().ok_or("--session needs a value")?),
            "--dry-run" => a.dry_run = true,
            "-h" | "--help" => {
                print!("{HELP}");
                std::process::exit(Status::Ok.code());
            }
            other => return Err(format!("unknown argument: {other}")),
        }
    }
    if a.script.is_empty() {
        return Err("no training script named; see --help".into());
    }
    Ok(a)
}

fn main() {
    let args = match parse_args() {
        Ok(a) => a,
        Err(e) => {
            eprintln!("bmc-run: {e}");
            eprintln!("try --help");
            std::process::exit(Status::Problem.code());
        }
    };

    // Preconditions that need no driver, checked first so a typo costs nothing.
    if !Path::new(&args.script).is_file() {
        refuse(format!("training script not found: {}", args.script));
    }
    let python = args.python.clone().unwrap_or_else(|| {
        format!("{VENV_BASE}/{}/venv/bin/python", args.project)
    });
    if !is_executable(&python) {
        refuse(format!(
            "interpreter not executable: {python}\n\
             Create the environment first (ai-stack/environments/create_venv.sh {}) \
             or pass --python.",
            args.project
        ));
    }
    if !have_bin("tmux") {
        // The incumbent said "install it (BLFS) or run the job by hand" and
        // exited 1. 3 is the honest code: nothing about the job was measured.
        // A dry run reports it instead of stopping, because the host that
        // plans a job is not always the host that runs it.
        if !args.dry_run {
            println!("UNAVAILABLE  tmux not found; the job was not started");
            std::process::exit(Status::Unavailable.code());
        }
        println!("UNAVAILABLE  tmux not found on this host; the plan is still printed");
    }

    // The NCCL environment comes from the one file that owns it. Absence is
    // reported and does not stop the job — a host with no NCCL tuning still
    // trains — but the record says so, because "it ran without the tuning"
    // and "there was no tuning to apply" must not read the same later.
    let (nccl, nccl_from) = match read_exports(NCCL_CONF) {
        Ok(entries) => (
            entries
                .into_iter()
                .filter(|(k, _)| k.starts_with("NCCL_"))
                .collect::<Vec<_>>(),
            NCCL_CONF,
        ),
        Err(_) => {
            println!("INDETERMINATE  no {NCCL_CONF}; the job runs with NCCL defaults");
            (Vec::new(), "none")
        }
    };

    let gpus = match resolve_gpus(&args, NCCL_CONF) {
        Ok(g) => g,
        Err(e) => refuse(e),
    };

    let smi = match Smi::open() {
        Ok(s) => s,
        Err(_) => {
            println!("UNAVAILABLE  nvidia-smi not found; device availability was not confirmed");
            std::process::exit(Status::Unavailable.code());
        }
    };
    let available = match smi.device_count() {
        Ok(n) => n,
        Err(_) => {
            println!(
                "INDETERMINATE  device list unreadable; the job was not started \
                 against an unconfirmed device set"
            );
            std::process::exit(Status::Indeterminate.code());
        }
    };
    if let Err(e) = check_devices(&gpus, available) {
        refuse(e);
    }

    let stamp = bmc::stamp("%Y%m%d_%H%M%S");
    let session = args
        .session
        .clone()
        .unwrap_or_else(|| format!("training_{stamp}"));
    let log = format!("{LOG_DIR}/training_{stamp}.log");

    // The pane's own command line carries the environment, because a running
    // tmux server does not inherit the client's (defect 2 above).
    let mut exports = vec![format!(
        "export CUDA_VISIBLE_DEVICES={}",
        shell_quote(&gpus)
    )];
    exports.extend(
        nccl.iter()
            .map(|(k, v)| format!("export {k}={}", shell_quote(v))),
    );
    let pane = format!(
        "{}; {} 2>&1 | tee {}",
        exports.join("; "),
        [shell_quote(&python), shell_quote(&args.script)].join(" "),
        shell_quote(&log)
    );

    println!("PLAN       script  {}", args.script);
    println!("PLAN       python  {python}");
    println!("PLAN       devices {gpus} of {available} visible");
    println!("PLAN       nccl    {} variable(s) from {nccl_from}", nccl.len());
    println!("PLAN       log     {log}");
    println!("PLAN       session {session}");

    if args.dry_run {
        println!("PLAN       tmux      new-session -d -s {session} {}", shell_quote(&pane));
        println!("\nDRY RUN  nothing was started.");
        std::process::exit(Status::Ok.code());
    }

    if let Err(e) = std::fs::create_dir_all(LOG_DIR) {
        refuse(format!("cannot create log directory {LOG_DIR}: {e}"));
    }
    if session_exists(&session) {
        refuse(format!(
            "tmux session {session} already exists; pass --session to name this run \
             rather than attach to someone else's"
        ));
    }

    let out = Command::new("tmux")
        .args(["new-session", "-d", "-s", &session, &pane])
        .output();
    let started = matches!(&out, Ok(o) if o.status.success());
    if !started {
        let detail = match &out {
            Ok(o) => String::from_utf8_lossy(&o.stderr).trim().to_string(),
            Err(e) => e.to_string(),
        };
        println!("PROBLEM    tmux refused to start the job: {detail}");
        std::process::exit(Status::Problem.code());
    }

    record(&session, &args.script, &python, &gpus, &log, nccl_from);

    println!("OK         job started in tmux session {session}");
    println!("OK         log {log}");
    println!("OK         attach with: tmux attach -t {session}");
    std::process::exit(Status::Ok.code());
}

/// Device list: the flag, then the single source, then nothing. There is no
/// compiled-in default, because `userspace-and-ml-stack` 6.3 exists to delete
/// the duplicated default the shell version carried (`:8`).
fn resolve_gpus(args: &Args, conf: &str) -> Result<String, String> {
    if let Some(g) = &args.gpus {
        return Ok(g.clone());
    }
    if let Ok(entries) = read_exports(conf) {
        if let Some((_, v)) = entries.into_iter().find(|(k, _)| k == "CUDA_VISIBLE_DEVICES") {
            return Ok(v);
        }
    }
    Err(format!(
        "no device list: pass --gpus 0,1 or set CUDA_VISIBLE_DEVICES in {conf}"
    ))
}

/// Every named index must exist. The shell version compared only the count,
/// so `0,7` on two devices was accepted and failed inside the driver.
fn check_devices(gpus: &str, available: usize) -> Result<(), String> {
    let mut seen: Vec<&str> = Vec::new();
    for entry in gpus.split(',') {
        let entry = entry.trim();
        if entry.is_empty() {
            return Err(format!("empty device id in \"{gpus}\""));
        }
        if seen.contains(&entry) {
            return Err(format!("device {entry} listed twice in \"{gpus}\""));
        }
        seen.push(entry);
        match entry.parse::<usize>() {
            Ok(n) if n < available => {}
            Ok(n) => return Err(format!(
                "device {n} requested but {available} device(s) visible to the driver"
            )),
            Err(_) => {
                // A UUID or a PCI bus id is legal in CUDA_VISIBLE_DEVICES and
                // cannot be checked against a count. Say so rather than
                // pretending the check passed.
                println!("INDETERMINATE  device \"{entry}\" is not an index; not validated");
            }
        }
    }
    Ok(())
}

fn is_executable(path: &str) -> bool {
    std::fs::metadata(path)
        .map(|m| m.is_file() && m.permissions().mode() & 0o111 != 0)
        .unwrap_or(false)
}

fn have_bin(name: &str) -> bool {
    Command::new(name)
        .arg("--version")
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status()
        .is_ok()
}

fn session_exists(name: &str) -> bool {
    Command::new("tmux")
        .args(["has-session", "-t", name])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
}

/// Header written with the first record, so the columns are readable without
/// this source file.
fn record(session: &str, script: &str, python: &str, gpus: &str, log: &str, nccl_from: &str) {
    use std::io::Write;
    let fresh = std::fs::metadata(JOB_RECORD).is_err();
    let line = format!(
        "{}\t{session}\t{script}\t{python}\t{gpus}\t{log}\t{nccl_from}\t{}\n",
        bmc::stamp("%Y-%m-%dT%H:%M:%S"),
        bmc::current_user().unwrap_or_else(|| "unknown".into())
    );
    let header = "#stamp\tsession\tscript\tpython\tdevices\tlog\tnccl_source\tuser\n";
    match std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(JOB_RECORD)
    {
        Ok(mut f) => {
            if fresh && f.write_all(header.as_bytes()).is_err() {
                println!("INDETERMINATE  job record header not written to {JOB_RECORD}");
            }
            if f.write_all(line.as_bytes()).is_err() {
                println!("INDETERMINATE  job not recorded in {JOB_RECORD}");
            }
        }
        Err(_) => println!("INDETERMINATE  job not recorded; {JOB_RECORD} not writable"),
    }
}

fn refuse(msg: String) -> ! {
    println!("REFUSED    {msg}");
    std::process::exit(Status::Problem.code())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_device_that_does_not_exist_is_refused() {
        assert!(check_devices("0,1", 2).is_ok());
        assert!(check_devices("0,7", 2).is_err());
        assert!(check_devices("0,0", 2).is_err());
        assert!(check_devices("", 2).is_err());
        // A UUID is legal and only reported as unvalidated, never refused.
        assert!(check_devices("GPU-abcdef", 2).is_ok());
    }

    #[test]
    fn quoting_keeps_a_path_with_spaces_in_one_word() {
        assert!(shell_quote("/opt/a b/train.py").starts_with("'/opt/a b/train.py'"));
    }
}
