#[derive(Debug, Eq, PartialEq)]
enum CliCommand {
    Help,
    Version,
    Invalid,
}

fn command_from_arg(arg: Option<&str>) -> CliCommand {
    match arg {
        Some("--version") => CliCommand::Version,
        Some("--help") | Some("-h") | None => CliCommand::Help,
        Some(_) => CliCommand::Invalid,
    }
}

fn print_version() {
    println!("sloppyc 0.0.0-foundation");
}

fn print_help() {
    print_version();
    println!("Foundation build: Oxc and Sloppy Plan emission are not implemented yet.");
    println!();
    println!("Usage:");
    println!("  sloppyc --help");
    println!("  sloppyc --version");
}

fn main() {
    let mut args = std::env::args();
    let _program = args.next();

    match command_from_arg(args.next().as_deref()) {
        CliCommand::Version => print_version(),
        CliCommand::Help => print_help(),
        CliCommand::Invalid => {
            print_help();
            std::process::exit(2);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{command_from_arg, CliCommand};

    #[test]
    fn no_argument_prints_help() {
        assert_eq!(command_from_arg(None), CliCommand::Help);
    }

    #[test]
    fn help_flags_print_help() {
        assert_eq!(command_from_arg(Some("--help")), CliCommand::Help);
        assert_eq!(command_from_arg(Some("-h")), CliCommand::Help);
    }

    #[test]
    fn version_flag_prints_version() {
        assert_eq!(command_from_arg(Some("--version")), CliCommand::Version);
    }

    #[test]
    fn unknown_argument_is_invalid() {
        assert_eq!(command_from_arg(Some("build")), CliCommand::Invalid);
    }
}
