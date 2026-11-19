//! Configuration.

use serde::{Deserialize, Serialize};
use std::fs;
use std::path::PathBuf;

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Config {
    pub download_dir: String,
    pub max_concurrent: u32,
    pub rpc_port: u16,
    pub aria2_path: String,
    pub auto_start_aria2: bool,
    pub clipboard_monitor: bool,
    pub notify_on_complete: bool,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            download_dir: expand_tilde("~/Downloads"),
            max_concurrent: 5,
            rpc_port: 6800,
            aria2_path: "aria2c".to_string(),
            auto_start_aria2: true,
            clipboard_monitor: true,
            notify_on_complete: true,
        }
    }
}

impl Config {
    pub fn path() -> PathBuf {
        dirs_path().join("config.json")
    }

    pub fn load() -> Self {
        let p = Self::path();
        if let Ok(s) = fs::read_to_string(&p) {
            if let Ok(mut c) = serde_json::from_str::<Config>(&s) {
                c.download_dir = expand_tilde(&c.download_dir);
                return c;
            }
        }
        let mut c = Self::default();
        c.download_dir = expand_tilde(&c.download_dir);
        c
    }

    pub fn save(&self) -> Result<(), String> {
        let p = Self::path();
        if let Some(parent) = p.parent() {
            let _ = fs::create_dir_all(parent);
        }
        let s = serde_json::to_string_pretty(self).map_err(|e| e.to_string())?;
        fs::write(&p, s).map_err(|e| e.to_string())
    }
}

fn expand_tilde(p: &str) -> String {
    if let Some(rest) = p.strip_prefix("~/") {
        if let Some(home) = std::env::var_os("HOME") {
            return PathBuf::from(home).join(rest).to_string_lossy().to_string();
        }
    } else if p == "~" {
        if let Some(home) = std::env::var_os("HOME") {
            return home.to_string_lossy().to_string();
        }
    }
    p.to_string()
}

fn dirs_path() -> PathBuf {
    if let Some(base) = std::env::var_os("XDG_CONFIG_HOME") {
        PathBuf::from(base).join("greed")
    } else if let Some(home) = std::env::var_os("HOME") {
        PathBuf::from(home).join(".config").join("greed")
    } else {
        PathBuf::from(".config").join("greed")
    }
}
