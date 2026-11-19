//! Data models parsed from aria2 JSON-RPC, plus shared formatting helpers.

use serde::Serialize;
use serde_json::Value;
use std::path::Path;

#[derive(Clone, Debug, Default, Serialize)]
pub struct Download {
    pub gid: String,
    pub status: String,
    pub total_length: u64,
    pub completed_length: u64,
    pub download_speed: u64,
    pub upload_speed: u64,
    pub connections: u32,
    pub num_seeders: u32,
    pub dir: String,
    pub name: String,
    pub url: String,
    pub info_hash: String,
    pub error_message: String,
    pub file_count: usize,
    pub is_torrent: bool,
}

impl Download {
    pub fn from_value(v: &Value) -> Self {
        let text = |key: &str| {
            v.get(key)
                .and_then(Value::as_str)
                .unwrap_or("")
                .to_string()
        };
        let number = |key: &str| {
            v.get(key)
                .and_then(Value::as_str)
                .and_then(|s| s.parse().ok())
                .unwrap_or(0)
        };

        let files = v.get("files").and_then(Value::as_array);
        let file_count = files.map(Vec::len).unwrap_or(0);
        let path = files
            .and_then(|f| f.first())
            .and_then(|f| f.get("path"))
            .and_then(Value::as_str)
            .unwrap_or("");
        let url = files
            .and_then(|f| f.first())
            .and_then(|f| f.get("uris"))
            .and_then(Value::as_array)
            .and_then(|u| u.first())
            .and_then(|u| u.get("uri"))
            .and_then(Value::as_str)
            .unwrap_or("");
        let bt_name = v
            .pointer("/bittorrent/info/name")
            .and_then(Value::as_str)
            .unwrap_or("");

        let gid = text("gid");
        let info_hash = text("infoHash");
        let is_torrent = v.get("bittorrent").is_some() || !info_hash.is_empty();

        let name = if !bt_name.is_empty() {
            bt_name.to_string()
        } else if !path.is_empty() {
            Path::new(path)
                .file_name()
                .map(|n| n.to_string_lossy().to_string())
                .unwrap_or_else(|| path.to_string())
        } else if is_torrent && !info_hash.is_empty() {
            format!("Magnet {}", &info_hash[..info_hash.len().min(8)])
        } else if !url.is_empty() {
            url.to_string()
        } else {
            gid.clone()
        };

        Self {
            gid,
            status: text("status"),
            total_length: number("totalLength"),
            completed_length: number("completedLength"),
            download_speed: number("downloadSpeed"),
            upload_speed: number("uploadSpeed"),
            connections: number("connections") as u32,
            num_seeders: number("numSeeders") as u32,
            dir: text("dir"),
            name,
            url: url.to_string(),
            info_hash,
            error_message: text("errorMessage"),
            file_count,
            is_torrent,
        }
    }

    pub fn progress(&self) -> f32 {
        if self.total_length == 0 {
            0.0
        } else {
            (self.completed_length as f64 / self.total_length as f64) as f32
        }
    }

    pub fn eta_secs(&self) -> Option<u64> {
        if self.download_speed > 0 && self.total_length > self.completed_length {
            Some((self.total_length - self.completed_length) / self.download_speed)
        } else {
            None
        }
    }

    pub fn status_label(&self) -> &'static str {
        match self.status.as_str() {
            "active" if self.is_torrent && self.total_length == 0 => "Fetching metadata",
            "active" => "Downloading",
            "waiting" => "Queued",
            "paused" => "Paused",
            "complete" => "Completed",
            "error" => "Failed",
            "removed" => "Removed",
            _ => "Unknown",
        }
    }

    pub fn is_finished(&self) -> bool {
        matches!(self.status.as_str(), "complete" | "error" | "removed")
    }
}

#[derive(Clone, Debug, Default)]
pub struct GlobalStat {
    pub download_speed: u64,
    pub upload_speed: u64,
    pub num_active: u64,
    pub num_waiting: u64,
    pub num_stopped: u64,
}

impl GlobalStat {
    pub fn from_value(v: &Value) -> Self {
        let number = |key: &str| {
            v.get(key)
                .and_then(Value::as_str)
                .and_then(|s| s.parse().ok())
                .unwrap_or(0)
        };
        Self {
            download_speed: number("downloadSpeed"),
            upload_speed: number("uploadSpeed"),
            num_active: number("numActive"),
            num_waiting: number("numWaiting"),
            num_stopped: number("numStopped"),
        }
    }
}

pub fn fmt_speed(b: u64) -> String {
    if b >= 1_073_741_824 {
        format!("{:.2} GB/s", b as f64 / 1_073_741_824.0)
    } else if b >= 1_048_576 {
        format!("{:.2} MB/s", b as f64 / 1_048_576.0)
    } else if b >= 1024 {
        format!("{:.1} KB/s", b as f64 / 1024.0)
    } else {
        format!("{b} B/s")
    }
}

pub fn fmt_bytes(b: u64) -> String {
    if b >= 1_073_741_824 {
        format!("{:.2} GB", b as f64 / 1_073_741_824.0)
    } else if b >= 1_048_576 {
        format!("{:.1} MB", b as f64 / 1_048_576.0)
    } else if b >= 1024 {
        format!("{:.1} KB", b as f64 / 1024.0)
    } else {
        format!("{b} B")
    }
}

pub fn fmt_eta(secs: u64) -> String {
    if secs < 60 {
        format!("{secs}s")
    } else if secs < 3600 {
        format!("{}m {:02}s", secs / 60, secs % 60)
    } else if secs < 86400 {
        format!("{}h {:02}m", secs / 3600, (secs % 3600) / 60)
    } else {
        format!("{}d {}h", secs / 86400, (secs % 86400) / 3600)
    }
}
