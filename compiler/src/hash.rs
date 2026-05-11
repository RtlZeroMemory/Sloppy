use sha2::{Digest, Sha256};

pub(crate) fn sha256_hex(contents: &str) -> String {
    let digest = Sha256::digest(contents.as_bytes());
    sha256_digest_hex(digest)
}

pub(crate) fn sha256_bytes_hex(contents: &[u8]) -> String {
    let digest = Sha256::digest(contents);
    sha256_digest_hex(digest)
}

fn sha256_digest_hex(digest: impl IntoIterator<Item = u8>) -> String {
    let mut output = String::with_capacity("sha256:".len() + 64);
    output.push_str("sha256:");
    for byte in digest {
        output.push_str(&format!("{byte:02x}"));
    }
    output
}
