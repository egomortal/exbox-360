"""Byte-preserving copies shared by setup, media and editor staging. Callers validate destination confinement."""
import hashlib

def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def copy_verified(source, dest):
    """Refuse changes to an existing staged file; partial copies remain for review."""
    if source.is_symlink():
        raise ValueError(f"symlink source: {source}")
    source_hash = sha256(source)
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists():
        if sha256(dest) != source_hash:
            raise ValueError(f"existing staged file differs (preserved): {dest}")
    else:
        with source.open("rb") as incoming, dest.open("xb") as outgoing:
            while chunk := incoming.read(1024 * 1024):
                outgoing.write(chunk)
        if sha256(dest) != source_hash:
            raise ValueError(f"copy verification failed (preserved): {dest}")
    return {"source": str(source), "bytes": dest.stat().st_size, "sha256": source_hash}


