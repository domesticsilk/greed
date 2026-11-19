//! Data models.

use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
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
    pub files: Vec<String>,
    pub error_code: String,
    pub error_message: String,
}

impl Download {
    pub fn from_value(v: &serde_json::Value) -> Self {
        let g = |k: &str| v.get(k).cloned().unwrap_or(serde_json::Value::Null);
        let files = v
            .get("files")
            .and_then(|f| f.as_array())
            .map(|arr| {
                arr.iter()
                    .filter_map(|f| {
                        f.get("path")
                            .and_then(|p| p.as_str())
                            .map(|s| s.to_string())
                    })
                    .collect()
            })
            .unwrap_or_default();
        Self {
            gid: g("gid").as_str().unwrap_or("").to_string(),
            status: g("status").as_str().unwrap_or("").to_string(),
            total_length: g("totalLength").as_str().unwrap_or("0").parse().unwrap_or(0),
            completed_length: g("completedLength").as_str().unwrap_or("0").parse().unwrap_or(0),
            download_speed: g("downloadSpeed").as_str().unwrap_or("0").parse().unwrap_or(0),
            upload_speed: g("uploadSpeed").as_str().unwrap_or("0").parse().unwrap_or(0),
            connections: g("connections").as_str().unwrap_or("0").parse().unwrap_or(0),
            num_seeders: g("numSeeders").as_str().unwrap_or("0").parse().unwrap_or(0),
            dir: g("dir").as_str().unwrap_or("").to_string(),
            files,
            error_code: g("errorCode").as_str().unwrap_or("").to_string(),
            error_message: g("errorMessage").as_str().unwrap_or("").to_string(),
        }
    }

    pub fn progress(&self) -> f32 {
        if self.total_length == 0 {
            0.0
        } else {
            self.completed_length as f32 / self.total_length as f32
        }
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
    pub fn from_value(v: &serde_json::Value) -> Self {
        let g = |k: &str| v.get(k).and_then(|x| x.as_str()).unwrap_or("0");
        Self {
            download_speed: g("downloadSpeed").parse().unwrap_or(0),
            upload_speed: g("uploadSpeed").parse().unwrap_or(0),
            num_active: g("numActive").parse().unwrap_or(0),
            num_waiting: g("numWaiting").parse().unwrap_or(0),
            num_stopped: g("numStopped").parse().unwrap_or(0),
        }
    }
}
