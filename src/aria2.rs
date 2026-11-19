//! aria2 JSON-RPC client.

use base64::Engine;
use serde_json::{Value, json};
use std::process::{Child, Command};

#[derive(Clone)]
pub struct Client {
    pub url: String,
    pub secret: Option<String>,
}

impl Client {
    pub fn new(host: &str, port: u16, secret: Option<String>) -> Self {
        Self {
            url: format!("http://{}:{}/jsonrpc", host, port),
            secret,
        }
    }

    fn params(&self, mut p: Vec<Value>) -> Vec<Value> {
        if let Some(s) = &self.secret {
            let mut v = vec![json!(format!("token:{}", s))];
            v.append(&mut p);
            v
        } else {
            p
        }
    }

    pub fn call(&self, method: &str, params: Vec<Value>) -> Result<Value, String> {
        let body = json!({
            "jsonrpc": "2.0",
            "id": "greed",
            "method": method,
            "params": self.params(params),
        });
        let resp = ureq::post(&self.url)
            .send_json(&body)
            .map_err(|e| e.to_string())?;
        let val: Value = resp.into_body().read_json().map_err(|e| e.to_string())?;
        if let Some(err) = val.get("error") {
            return Err(err.to_string());
        }
        Ok(val.get("result").cloned().unwrap_or(Value::Null))
    }

    pub fn version(&self) -> Result<String, String> {
        let r = self.call("aria2.getVersion", vec![])?;
        Ok(r.get("version").and_then(|v| v.as_str()).unwrap_or("").to_string())
    }

    pub fn probe(&self) -> bool {
        self.version().is_ok()
    }

    pub fn get_global_stat(&self) -> Result<Value, String> {
        self.call("aria2.getGlobalStat", vec![])
    }

    pub fn add_uri(&self, urls: &[String], opts: &Value) -> Result<String, String> {
        let r = self.call("aria2.addUri", vec![json!(urls), opts.clone()])?;
        Ok(r.as_str().unwrap_or("").to_string())
    }

    pub fn add_torrent(&self, path: &str, opts: &Value) -> Result<String, String> {
        let data = std::fs::read(path).map_err(|e| e.to_string())?;
        let b64 = base64::engine::general_purpose::STANDARD.encode(&data);
        let r = self.call("aria2.addTorrent", vec![json!(b64), json!([]), opts.clone()])?;
        Ok(r.as_str().unwrap_or("").to_string())
    }

    pub fn tell_status(&self, gid: &str) -> Result<Value, String> {
        self.call("aria2.tellStatus", vec![json!(gid)])
    }

    pub fn tell_active(&self) -> Result<Value, String> {
        self.call("aria2.tellActive", vec![])
    }

    pub fn tell_waiting(&self, offset: i64, num: i64) -> Result<Value, String> {
        self.call("aria2.tellWaiting", vec![json!(offset), json!(num)])
    }

    pub fn tell_stopped(&self, offset: i64, num: i64) -> Result<Value, String> {
        self.call("aria2.tellStopped", vec![json!(offset), json!(num)])
    }

    pub fn pause(&self, gid: &str) -> Result<(), String> {
        self.call("aria2.pause", vec![json!(gid)]).map(|_| ())
    }

    pub fn unpause(&self, gid: &str) -> Result<(), String> {
        self.call("aria2.unpause", vec![json!(gid)]).map(|_| ())
    }

    pub fn remove(&self, gid: &str) -> Result<(), String> {
        self.call("aria2.remove", vec![json!(gid)]).map(|_| ())
    }

    pub fn force_remove(&self, gid: &str) -> Result<(), String> {
        self.call("aria2.forceRemove", vec![json!(gid)]).map(|_| ())
    }

    pub fn remove_result(&self, gid: &str) -> Result<(), String> {
        self.call("aria2.removeDownloadResult", vec![json!(gid)])
            .map(|_| ())
    }

    pub fn change_global_option(&self, options: &Value) -> Result<(), String> {
        self.call("aria2.changeGlobalOption", vec![options.clone()])
            .map(|_| ())
    }

    pub fn shutdown(&self) -> Result<(), String> {
        self.call("aria2.shutdown", vec![]).map(|_| ())
    }
}

pub fn bundled_binary() -> Option<std::path::PathBuf> {
    let relative = std::path::Path::new("vendor/aria2/aria2c");
    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            // installed layout: next to the binary
            let p = dir.join("aria2c");
            if p.exists() {
                return Some(p);
            }
            // target/debug and target/release sit below the repo root
            for base in dir.ancestors().take(4) {
                let p = base.join("vendor").join("aria2").join("aria2c");
                if p.exists() {
                    return Some(p);
                }
            }
        }
    }
    if relative.exists() {
        return Some(relative.to_path_buf());
    }
    None
}

pub fn aria2_binary() -> String {
    // Bundled first: same aria2 version everywhere, independent of the distro.
    if let Some(p) = bundled_binary() {
        return p.to_string_lossy().to_string();
    }
    "aria2c".to_string()
}

pub fn lib_dir_for(bin: &str) -> Option<std::path::PathBuf> {
    let p = std::path::Path::new(bin);
    if p.is_absolute() || bin.contains('/') {
        // bundled: lib next to binary or in ../lib
        if let Some(dir) = p.parent() {
            let l = dir.join("lib");
            if l.exists() {
                return Some(l);
            }
            let l = dir.join("vendor").join("aria2").join("lib");
            if l.exists() {
                return Some(l);
            }
        }
    }
    None
}

pub fn spawn(
    port: u16,
    secret: Option<&str>,
    dir: &str,
    max_concurrent: u32,
) -> Result<Child, String> {
    let bin = aria2_binary();
    let mut cmd = Command::new(&bin);
    // Keep the queue across restarts: aria2 saves its session here and reloads it on start.
    let session = crate::config::Config::path().with_file_name("aria2.session");
    cmd.arg("--enable-rpc")
        .arg(format!("--rpc-listen-port={port}"))
        .arg("--rpc-listen-all=false")
        .arg(format!("--max-concurrent-downloads={max_concurrent}"))
        .arg(format!("--dir={dir}"))
        .arg(format!("--save-session={}", session.display()))
        .arg("--save-session-interval=30")
        .arg("--continue=true")
        .arg("--daemon=true")
        .arg("--quiet");
    if session.exists() {
        cmd.arg(format!("--input-file={}", session.display()));
    }
    if let Some(s) = secret {
        cmd.arg(format!("--rpc-secret={s}"));
    }
    if let Some(ld) = lib_dir_for(&bin) {
        let existing = std::env::var("LD_LIBRARY_PATH").unwrap_or_default();
        let newv = if existing.is_empty() {
            ld.to_string_lossy().to_string()
        } else {
            format!("{}:{}", ld.to_string_lossy(), existing)
        };
        cmd.env("LD_LIBRARY_PATH", newv);
    }
    let mut child = cmd.spawn().map_err(|e| e.to_string())?;
    // --daemon=true forks; reap the short-lived launcher so it does not linger as a zombie.
    let _ = child.wait();
    Ok(child)
}

pub fn aria2_available() -> bool {
    let bin = aria2_binary();
    let mut cmd = Command::new(&bin);
    cmd.arg("--version");
    if let Some(ld) = lib_dir_for(&bin) {
        cmd.env("LD_LIBRARY_PATH", ld);
    }
    cmd.output()
        .map(|o| o.status.success())
        .unwrap_or(false)
}
