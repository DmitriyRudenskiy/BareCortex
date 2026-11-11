//! Shared plumbing for the BareCortex runtime utilities.
//!
//! Everything here exists because four binaries need the same three awkward
//! things: a way to talk to `nvidia-smi` that distinguishes "the driver said
//! no" from "the driver did not answer", a status type whose worst case
//! becomes the process exit code, and a way to ask who owns a PID.
//!
//! No dependencies. The image build has no network (build-orchestration D5),
//! so a shipped component cannot have a crate it did not bring.

use std::path::PathBuf;
use std::process::Command;

/// Every check answers one of these. The exit code is the most severe one
/// seen, so "we could not tell" can never be reported as "we looked and it
/// was fine".
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub enum Status {
    /// Condition absent. Exit 0.
    Ok,
    /// A named condition is present. Exit 1.
    Problem,
    /// A measurement could not be made. Exit 2.
    Indeterminate,
    /// The tool or driver needed to ask is absent. Exit 3.
    Unavailable,
}

impl Status {
    pub fn code(self) -> i32 {
        match self {
            Status::Ok => 0,
            Status::Problem => 1,
            Status::Indeterminate => 2,
            Status::Unavailable => 3,
        }
    }

    /// How much this outcome should worry the caller. Deliberately **not**
    /// the exit-code order: a confirmed problem outranks "we could not tell",
    /// because a run that found a hot GPU must not report itself as merely
    /// unmeasured, and the operator's next command acts on the finding. The
    /// exit code still distinguishes them — 1 is a finding, 2 is a gap.
    fn severity(self) -> u8 {
        match self {
            Status::Ok => 0,
            Status::Indeterminate => 1,
            Status::Unavailable => 2,
            Status::Problem => 3,
        }
    }

    /// Widen to the worse of two outcomes.
    pub fn worst(self, other: Status) -> Status {
        if other.severity() > self.severity() {
            other
        } else {
            self
        }
    }
}

/// A value a query was supposed to return.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Field {
    Value(String),
    /// The driver answered but has no value — `[N/A]`, `[Not Supported]`.
    /// Distinct from a query that failed, because "this card has no ECC" is a
    /// fact and "the query timed out" is not.
    NotApplicable,
}

impl Field {
    /// nvidia-smi spells "no value" several ways depending on the field and
    /// the driver branch. All of them arrive here as NotApplicable.
    pub fn parse(raw: &str) -> Field {
        let t = raw.trim();
        if t.is_empty()
            || t == "[N/A]"
            || t == "[Not Supported]"
            || t == "[Not Available]"
            || t == "N/A"
            || t == "Not Supported"
        {
            Field::NotApplicable
        } else {
            Field::Value(t.to_string())
        }
    }

    pub fn value(&self) -> Option<&str> {
        match self {
            Field::Value(v) => Some(v.as_str()),
            _ => None,
        }
    }

    /// Numeric view of a field that carries a unit, e.g. `72 W` or `65 %`.
    pub fn number(&self) -> Option<f64> {
        self.value()
            .map(|v| v.split_whitespace().next().unwrap_or("").parse::<f64>().ok())
            .flatten()
    }
}

/// A handle on the `nvidia-smi` binary.
///
/// `BMC_NVIDIA_SMI` overrides the path. That is not a convenience for
/// developers; it is how the parity tests feed this binary the same canned
/// driver output the shell incumbent was given, which is the only evidence
/// that a rewrite preserved behaviour rather than replaced it.
pub struct Smi {
    pub bin: PathBuf,
}

pub enum SmiError {
    /// The binary is not there at all.
    Missing,
    /// It ran and said something.
    Failed(String),
}

impl Smi {
    pub fn open() -> Result<Smi, SmiError> {
        let bin = std::env::var_os("BMC_NVIDIA_SMI")
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from("nvidia-smi"));
        Command::new(&bin)
            .arg("--version")
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status()
            .map_err(|_| SmiError::Missing)?;
        Ok(Smi { bin })
    }

    fn run(&self, args: &[&str]) -> Result<String, SmiError> {
        let out = Command::new(&self.bin)
            .args(args)
            .output()
            .map_err(|_| SmiError::Missing)?;
        // Measured on driver 580.159.03: a query that matches no device — an
        // out-of-range index, a bogus UUID, a host with the driver installed
        // and no GPU — answers "No devices were found" and exits 6. That is an
        // empty result, not a driver failure, and every caller below already
        // reads "no rows" as a finding rather than a crash.
        if out.status.code() == Some(6) {
            return Ok(String::new());
        }
        if !out.status.success() {
            return Err(SmiError::Failed(
                String::from_utf8_lossy(&out.stderr).trim().to_string(),
            ));
        }
        Ok(String::from_utf8_lossy(&out.stdout).to_string())
    }

    /// `--query-gpu` rows, split on `", "` the way nvidia-smi joins them.
    /// Empty input is an empty result, not an error: measured on 580.159.03, a
    /// host with no GPU answers exit 6 and one line of prose, which `run`
    /// turns into no rows — "no GPUs" is a finding, not a crash.
    pub fn gpu_rows(&self, fields: &str, gpu: Option<&str>) -> Result<Vec<Vec<Field>>, SmiError> {
        // `--query-gpu=<fields>` and `-i <n>` are the forms every nvidia-smi
        // example and manual page uses; the space-separated `--query-gpu
        // <fields>` and the packed `-i0` are not documented and are not worth
        // discovering on a `(gpu)` host.
        let query = format!("--query-gpu={fields}");
        let mut args = vec![query.as_str(), "--format=csv,noheader,nounits"];
        if let Some(g) = gpu {
            args.push("-i");
            args.push(g);
        }
        let out = self.run(&args)?;
        Ok(out
            .lines()
            .map(|l| l.split(", ").map(Field::parse).collect())
            .collect())
    }

    /// Compute processes holding device memory: `pid, name, memory`.
    pub fn compute_apps(&self) -> Result<Vec<(u32, String, String)>, SmiError> {
        let out = self.run(&[
            "--query-compute-apps=pid,process_name,used_memory",
            "--format=csv,noheader,nounits",
        ])?;
        Ok(out
            .lines()
            .filter_map(|l| {
                let mut c = l.split(", ");
                let pid = c.next()?.trim().parse::<u32>().ok()?;
                let name = c.next().unwrap_or("unknown").to_string();
                let mem = c.next().unwrap_or("unknown").to_string();
                Some((pid, name, mem))
            })
            .collect())
    }

    /// `index, pci.bus_id` for every device. The bus id is the only handle
    /// that joins a compute process to the device it is on, which is what
    /// "this device is held by someone else" has to be decided from.
    pub fn gpu_bus_ids(&self) -> Result<Vec<(String, String)>, SmiError> {
        let out = self.run(&["--query-gpu=index,pci.bus_id", "--format=csv,noheader"])?;
        Ok(out
            .lines()
            .filter_map(|l| {
                let mut c = l.split(", ");
                Some((c.next()?.trim().to_string(), c.next()?.trim().to_string()))
            })
            .collect())
    }

    /// Compute processes keyed by the bus id of the device they hold.
    pub fn holders_by_bus(&self) -> Result<Vec<(String, u32, String)>, SmiError> {
        let out = self.run(&[
            "--query-compute-apps=gpu_bus_id,pid,process_name",
            "--format=csv,noheader",
        ])?;
        Ok(out
            .lines()
            .filter_map(|l| {
                let mut c = l.split(", ");
                let bus = c.next()?.trim().to_string();
                let pid = c.next()?.trim().parse::<u32>().ok()?;
                let name = c.next().unwrap_or("unknown").to_string();
                Some((bus, pid, name))
            })
            .collect())
    }

    /// Fan speed as a percentage, when the device reports one.
    pub fn fan_speed(&self, gpu: &str) -> Field {
        match self.gpu_rows("index,fan.speed", Some(gpu)) {
            Ok(rows) => rows
                .first()
                .and_then(|r| r.get(1).cloned())
                .unwrap_or(Field::NotApplicable),
            Err(_) => Field::NotApplicable,
        }
    }

    /// Device count, from `nvidia-smi -L`. Zero is a legitimate answer.
    pub fn device_count(&self) -> Result<usize, SmiError> {
        Ok(self.run(&["-L"])?.lines().filter(|l| !l.trim().is_empty()).count())
    }

    /// Anything else, verbatim. Errors carry the driver's own words.
    pub fn raw(&self, args: &[&str]) -> Result<String, SmiError> {
        self.run(args)
    }
}

/// Owner of a process, as a name. `None` when the process is gone or the
/// system does not expose it — a missing answer must never read as "root" or
/// as "safe to signal".
pub fn process_owner(pid: u32) -> Option<String> {
    let status = std::fs::read_to_string(format!("/proc/{pid}/status")).ok()?;
    let uid = status.lines().find_map(|l| {
        let mut it = l.splitn(2, ':');
        if it.next()? == "Uid" {
            it.next()?.split_whitespace().next().map(String::from)
        } else {
            None
        }
    })?;
    Some(passwd_name(&uid).unwrap_or(uid))
}

/// The effective owner of the running process. Used to decide whether a
/// device is held by "another user" — the case the maintenance rules say must
/// be refused rather than prompted about.
pub fn current_user() -> Option<String> {
    let uid = unsafe { getuid() }.to_string();
    Some(passwd_name(&uid).unwrap_or(uid))
}

fn passwd_name(uid: &str) -> Option<String> {
    let passwd = std::fs::read_to_string("/etc/passwd").ok()?;
    passwd.lines().find_map(|l| {
        let mut c = l.split(':');
        let name = c.next()?;
        c.next()?; // password placeholder, usually `x`
        let f_uid = c.next()?;
        (f_uid == uid).then(|| name.to_string())
    })
}

// `getuid` is in libc, which we are not allowed to depend on. Declaring the
// symbol directly keeps the dependency graph at zero crates; the ABI is
// stable and the call cannot fail.
extern "C" {
    fn getuid() -> u32;
}

/// Read `KEY=value` and `export KEY=value` lines from a shell configuration
/// file — the `/etc/conf.d/*` class of file, which OpenRC sources and which
/// `honest-scope-and-config-generation` D8 makes the single source of the NCCL
/// environment.
///
/// Deliberately not a shell interpreter: no expansion, no command
/// substitution, no sourcing. A launcher that sources the file has to run it,
/// and running arbitrary root-owned shell as part of starting a job is a
/// wider surface than reading eight assignments. A value that needs expansion
/// is a value this project has not decided to put there.
pub fn read_exports(path: &str) -> Result<Vec<(String, String)>, std::io::Error> {
    let text = std::fs::read_to_string(path)?;
    Ok(text
        .lines()
        .filter_map(|line| {
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') {
                return None;
            }
            let line = line.strip_prefix("export ").unwrap_or(line).trim();
            let (key, value) = line.split_once('=')?;
            let key = key.trim();
            if key.is_empty() || !key.chars().all(|c| c.is_ascii_uppercase() || c == '_' || c.is_ascii_digit()) {
                return None;
            }
            let value = value.trim();
            let value = value
                .strip_prefix('\'')
                .and_then(|v| v.strip_suffix('\''))
                .or_else(|| value.strip_prefix('"').and_then(|v| v.strip_suffix('"')))
                .unwrap_or(value)
                .to_string();
            Some((key.to_string(), value))
        })
        .collect())
}

/// Quote one argument for a `sh -c` string. Needed because `tmux
/// new-session` runs its command through the default shell, and a project
/// path containing a space is not a hypothetical.
pub fn shell_quote(s: &str) -> String {
    format!("'{}'", s.replace('\'', r"'\''"))
}

/// Local timestamp for log and record names. Spawns `date` rather than
/// carrying a calendar: coreutils is in the base image by construction, and
/// a hand-rolled civil-time routine is 40 lines of bug surface for one
/// string.
pub fn stamp(fmt: &str) -> String {
    Command::new("date")
        .arg(format!("+{fmt}"))
        .output()
        .ok()
        .filter(|o| o.status.success())
        .map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string())
        .unwrap_or_else(|| "unknown-time".to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn severity_and_exit_code_are_different_orders() {
        assert!(Status::Unavailable.severity() > Status::Indeterminate.severity());
        assert_eq!(Status::Ok.worst(Status::Problem), Status::Problem);
        // A finding is not demoted by an unrelated gap in the same run.
        assert_eq!(Status::Problem.worst(Status::Indeterminate), Status::Problem);
        assert_eq!(
            Status::Indeterminate.worst(Status::Unavailable),
            Status::Unavailable
        );
        assert_eq!(Status::Indeterminate.code(), 2);
        assert_eq!(Status::Problem.worst(Status::Indeterminate).code(), 1);
    }

    #[test]
    fn driver_nones_are_not_values() {
        assert_eq!(Field::parse("[N/A]"), Field::NotApplicable);
        assert_eq!(Field::parse("[Not Supported]"), Field::NotApplicable);
        assert_eq!(Field::parse("  "), Field::NotApplicable);
        assert_eq!(Field::parse("0").value(), Some("0"));
    }

    #[test]
    fn conf_d_assignments_are_read_without_sourcing() {
        let dir = std::env::temp_dir().join(format!("bmc-confd-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("nccl");
        std::fs::write(
            &path,
            "# a comment\nexport NCCL_DEBUG='INFO'\nNCCL_SOCKET_IFNAME=\"eth0\"\n\nPATH=/usr/bin\nNCCL_IB_GID_INDEX=3\n",
        )
        .unwrap();
        let got = read_exports(path.to_str().unwrap()).unwrap();
        assert_eq!(got[0], ("NCCL_DEBUG".into(), "INFO".into()));
        assert_eq!(got[1], ("NCCL_SOCKET_IFNAME".into(), "eth0".into()));
        assert_eq!(got[2], ("PATH".into(), "/usr/bin".into()));
        assert_eq!(got[3], ("NCCL_IB_GID_INDEX".into(), "3".into()));
        assert_eq!(got.len(), 4);
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn quoting_survives_a_path_with_a_space() {
        assert_eq!(shell_quote("/opt/a b/x"), "'/opt/a b/x'");
        assert_eq!(shell_quote("it's"), r"'it'\''s'");
    }

    #[test]
    fn units_are_stripped_for_comparison() {
        assert_eq!(Field::parse("72 W").number(), Some(72.0));
        assert_eq!(Field::parse("65 %").number(), Some(65.0));
        assert_eq!(Field::parse("[N/A]").number(), None);
        // A zero is a reading, not an absence.
        assert_eq!(Field::parse("0").number(), Some(0.0));
    }
}
