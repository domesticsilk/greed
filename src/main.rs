//! Greed — download manager (GUI + CLI).

mod app;
mod aria2;
mod cli;
mod config;
mod model;
mod theme;
mod ui;
mod worker;

use pico_args::Arguments;

const VERSION: &str = env!("CARGO_PKG_VERSION");

fn main() {
    let mut args = Arguments::from_env();
    let help = args.contains(["-h", "--help"]);
    let version = args.contains(["-V", "--version"]);

    if version {
        println!("greed {}", VERSION);
        return;
    }
    if help {
        print_help();
        return;
    }

    let sub = args.subcommand().ok().flatten();
    match sub.as_deref() {
        Some("version") => println!("greed {}", VERSION),
        Some("gui") => run_gui(),
        Some(cmd) => {
            if let Err(e) = crate::cli::run(cmd, args) {
                eprintln!("error: {}", e);
                std::process::exit(1);
            }
        }
        None => {
            if std::env::var_os("DISPLAY").is_some()
                || std::env::var_os("WAYLAND_DISPLAY").is_some()
            {
                run_gui();
            } else {
                print_help();
            }
        }
    }
}

fn run_gui() {
    let options = eframe::NativeOptions::default();
    if let Err(e) = eframe::run_native(
        "Greed",
        options,
        Box::new(|cc| Ok(Box::new(crate::app::GreedApp::new(cc)))),
    ) {
        eprintln!("Failed to start GUI: {}", e);
    }
}

fn print_help() {
    println!(
        "greed {}\n\nUSAGE:\n  greed [COMMAND]\n\nCOMMANDS:\n  gui        Launch the GUI\n  version    Print version\n  help       Print this help\n",
        VERSION
    );
}
