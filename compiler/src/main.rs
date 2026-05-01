#![forbid(unsafe_code)]

use sloppyc::{run, CliExit};

fn main() {
    match run(std::env::args_os().skip(1)) {
        CliExit::Success => {}
        CliExit::Output(text) => {
            print!("{text}");
        }
        CliExit::Failure { code, diagnostic } => {
            eprintln!("{diagnostic}");
            std::process::exit(code);
        }
    }
}
