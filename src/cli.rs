//! CLI subcommands.

use crate::aria2::{self, Client};
use crate::config::Config;
use crate::model::{Download, GlobalStat};
use pico_args::Arguments;
use serde_json::json;

pub fn run(cmd: &str, mut args: Arguments) -> Result<(), String> {
    let config = Config::load();
    let client = ensure_client(&config)?;

    match cmd {
        "add" => {
            let dir: Option<String> = args.opt_value_from_str("-d").ok().flatten();
            let name: Option<String> = args.opt_value_from_str("-o").ok().flatten();
            let conn: Option<u32> = args.opt_value_from_str("-x").ok().flatten();
            let urls: Vec<String> = args.finish().into_iter().filter_map(|x| x.into_string().ok()).collect();
            let mut opts = json!({});
            let d = dir.unwrap_or_else(|| config.download_dir.clone());
            opts["dir"] = json!(d);
            if let Some(n) = name {
                opts["out"] = json!(n);
            }
            if let Some(c) = conn {
                opts["split"] = json!(c);
                opts["max-connection-per-server"] = json!(c);
            }
            for u in urls {
                let gid = client.add_uri(&[u.clone()], &opts)?;
                println!("Added: {} ({})", gid, u);
            }
            Ok(())
        }
        "list" => {
            let json_out = args.contains("--json");
            let mut all = Vec::new();
            if let Ok(v) = client.tell_active() {
                if let Some(a) = v.as_array() {
                    all.extend(a.iter().map(Download::from_value));
                }
            }
            if let Ok(v) = client.tell_waiting(0, 1000) {
                if let Some(a) = v.as_array() {
                    all.extend(a.iter().map(Download::from_value));
                }
            }
            if let Ok(v) = client.tell_stopped(0, 1000) {
                if let Some(a) = v.as_array() {
                    all.extend(a.iter().map(Download::from_value));
                }
            }
            if json_out {
                println!("{}", serde_json::to_string_pretty(&all).unwrap_or_default());
            } else {
                println!("{:<14} {:<10} {:>7} {:>10} {}", "GID", "STATUS", "PROG", "SPEED", "NAME");
                for d in &all {
                    let name = d.files.first().cloned().unwrap_or_else(|| d.gid.clone());
                    println!(
                        "{:<14} {:<10} {:>6.1}% {:>10} {}",
                        &d.gid[..d.gid.len().min(14)],
                        d.status,
                        d.progress() * 100.0,
                        fmt_speed(d.download_speed),
                        name
                    );
                }
            }
            Ok(())
        }
        "status" => {
            let json_out = args.contains("--json");
            let rest = args.finish();
            let gid: String = rest.into_iter().next().and_then(|x| x.into_string().ok()).ok_or_else(|| "missing GID".to_string())?;
            let v = client.tell_status(&gid)?;
            if json_out {
                println!("{}", serde_json::to_string_pretty(&v).unwrap_or_default());
            } else {
                let d = Download::from_value(&v);
                println!("GID: {}", d.gid);
                println!("Status: {}", d.status);
                println!("Progress: {:.1}%", d.progress() * 100.0);
                println!("Size: {} / {}", fmt_bytes(d.completed_length), fmt_bytes(d.total_length));
                println!("Speed: {}", fmt_speed(d.download_speed));
                if d.num_seeders > 0 {
                    println!("Seeders: {}", d.num_seeders);
                }
                if !d.error_message.is_empty() {
                    println!("Error: {}", d.error_message);
                }
            }
            Ok(())
        }
        "pause" => {
            let gids: Vec<String> = args.finish().into_iter().filter_map(|x| x.into_string().ok()).collect();
            for g in gids {
                client.pause(&g)?;
                println!("Paused {}", g);
            }
            Ok(())
        }
        "resume" => {
            let gids: Vec<String> = args.finish().into_iter().filter_map(|x| x.into_string().ok()).collect();
            for g in gids {
                client.unpause(&g)?;
                println!("Resumed {}", g);
            }
            Ok(())
        }
        "remove" => {
            let delete = args.contains("--delete");
            let gids: Vec<String> = args.finish().into_iter().filter_map(|x| x.into_string().ok()).collect();
            for g in gids {
                if delete {
                    client.force_remove(&g)?;
                } else {
                    client.remove(&g)?;
                }
                println!("Removed {}", g);
            }
            Ok(())
        }
        "stats" => {
            let v = client.get_global_stat()?;
            let g = GlobalStat::from_value(&v);
            println!("Download: {}", fmt_speed(g.download_speed));
            println!("Upload: {}", fmt_speed(g.upload_speed));
            println!("Active: {}  Waiting: {}  Stopped: {}", g.num_active, g.num_waiting, g.num_stopped);
            Ok(())
        }
        "shutdown" => {
            client.shutdown()?;
            println!("aria2 shut down");
            Ok(())
        }
        other => Err(format!("unknown command: {}", other)),
    }
}

fn ensure_client(config: &Config) -> Result<Client, String> {
    let client = Client::new("127.0.0.1", config.rpc_port, None);
    if client.probe() {
        return Ok(client);
    }
    if aria2::aria2_available() && config.auto_start_aria2 {
        let _ = aria2::spawn(config.rpc_port, None, true);
        for _ in 0..20 {
            std::thread::sleep(std::time::Duration::from_millis(100));
            if client.probe() {
                return Ok(client);
            }
        }
    }
    Err("aria2 RPC not available. Start aria2c --enable-rpc or install aria2.".to_string())
}

fn fmt_speed(b: u64) -> String {
    if b >= 1_073_741_824 {
        format!("{:.2} GB/s", b as f64 / 1_073_741_824.0)
    } else if b >= 1_048_576 {
        format!("{:.2} MB/s", b as f64 / 1_048_576.0)
    } else if b >= 1024 {
        format!("{:.2} KB/s", b as f64 / 1024.0)
    } else {
        format!("{} B/s", b)
    }
}

fn fmt_bytes(b: u64) -> String {
    if b >= 1_073_741_824 {
        format!("{:.2} GB", b as f64 / 1_073_741_824.0)
    } else if b >= 1_048_576 {
        format!("{:.2} MB", b as f64 / 1_048_576.0)
    } else if b >= 1024 {
        format!("{:.2} KB", b as f64 / 1024.0)
    } else {
        format!("{} B", b)
    }
}
