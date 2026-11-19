//! Greed — download manager (GUI + CLI).

mod app;
mod aria2;
mod cli;
mod config;
mod model;
mod theme;
mod ui;

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
    let options = eframe::NativeOptions {
        viewport: eframe::egui::ViewportBuilder::default()
            .with_inner_size([1100.0, 700.0])
            .with_min_inner_size([1000.0, 620.0])
            .with_title("Greed"),
        ..Default::default()
    };
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
        "greed {VERSION}\n\n\
USAGE:\n  greed [COMMAND] [OPTIONS]\n\n\
COMMANDS:\n\
  gui                          Launch the GUI (default when a display is present)\n\
  add <URL|MAGNET|FILE>…       Add downloads (-d DIR, -o NAME, -x CONNECTIONS)\n\
  list [--json]                List downloads\n\
  status <GID> [--json]        Show one download\n\
  pause <GID>…                 Pause downloads\n\
  resume <GID>…                Resume downloads\n\
  remove <GID>… [--delete]     Remove downloads\n\
  stats                        Global transfer statistics\n\
  shutdown                     Stop the aria2 engine\n\
  version                      Print version\n\
  help                         Print this help\n"
    );
}
