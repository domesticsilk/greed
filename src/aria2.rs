//! aria2 JSON-RPC client.

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

    pub fn shutdown(&self) -> Result<(), String> {
        self.call("aria2.shutdown", vec![]).map(|_| ())
    }
}

pub fn spawn(port: u16, secret: Option<&str>, daemon: bool) -> Result<Child, String> {
    let mut cmd = Command::new("aria2c");
    cmd.arg("--enable-rpc")
        .arg(format!("--rpc-listen-port={}", port))
        .arg("--rpc-listen-all=false");
    if let Some(s) = secret {
        cmd.arg(format!("--rpc-secret={}", s));
    }
    if daemon {
        cmd.arg("--daemon=true");
    } else {
        cmd.arg("--daemon=false");
    }
    cmd.arg("--quiet");
    cmd.spawn().map_err(|e| e.to_string())
}

pub fn aria2_available() -> bool {
    Command::new("aria2c")
        .arg("--version")
        .output()
        .map(|o| o.status.success())
        .unwrap_or(false)
}
