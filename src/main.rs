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
        Some("gui") => {
            eprintln!("GUI not yet implemented");
        }
        _ => {
            if std::env::var_os("DISPLAY").is_some()
                || std::env::var_os("WAYLAND_DISPLAY").is_some()
            {
                eprintln!("GUI not yet implemented");
            } else {
                print_help();
            }
        }
    }
}

fn print_help() {
    println!(
        "greed {}\n\nUSAGE:\n  greed [COMMAND]\n\nCOMMANDS:\n  gui        Launch the GUI\n  version    Print version\n  help       Print this help\n",
        VERSION
    );
}
