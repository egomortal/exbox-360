"""Link local assets into the desktop SDL test path; never contacts a console."""

from pathlib import Path


PROJECT = Path(__file__).resolve().parents[1]
TARGET = Path("/data/homebrew/ExBox360")
SOURCES = {
    "private_art": PROJECT / "private" / "ps5",
    "private_buttons": PROJECT / "private" / "xbox" / "shrdres",
    "cards": PROJECT / "assets" / "cards",
    "shiny": PROJECT / "assets" / "shiny",
    "kenney": PROJECT / "assets" / "kenney",
    "avatar": PROJECT / "assets" / "avatar",
    "trailers": PROJECT / "private" / "trailers",
    "Inter-Regular.ttf": PROJECT.parent / "native-app" / "third_party" / "fonts" / "Inter-Regular.ttf",
    "Inter-SemiBold.ttf": PROJECT.parent / "native-app" / "third_party" / "fonts" / "Inter-SemiBold.ttf",
}


def main():
    TARGET.mkdir(parents=True, exist_ok=True)
    for name, source in SOURCES.items():
        destination = TARGET / name
        if not source.exists():
            print(f"Missing optional source: {source}")
            continue
        if destination.is_symlink() and destination.resolve() == source.resolve():
            continue
        if destination.exists() or destination.is_symlink():
            raise RuntimeError(f"Refusing to replace {destination}")
        destination.symlink_to(source, target_is_directory=source.is_dir())
    print(f"Desktop assets ready at {TARGET}")


if __name__ == "__main__":
    main()
