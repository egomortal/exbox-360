# ExBox360-PS5

> **Microsoft says everything is an Xbox. PlayStation says play has no limits.
> We took them both at their word.**

An Xbox 360-style dashboard, with its matching avatar editor, that runs **natively on a jailbroken PS5**.
It is built to install in one go: point the installer at your console, tick **Install everything (recommended)**
and press **Run**.

## See it running

A guided tour recorded on a PS5 running Ex-Box 360: the Blades, NXE, Kinect and Metro looks, avatars, the Guide, Videos and Theater Party (sound removed):

https://github.com/user-attachments/assets/94f58c5a-42eb-4a39-99cb-f6c0623c92a8

## What works today
- Your real avatar, with friends, on the dashboard.
- A smooth avatar closet with Sort / Filter on Y.
- An Xbox 360 loading splash.
- Continue Playing, seven Music views and full D-pad navigation.

## In progress
- Jumping from the dashboard straight into an Xbox 360 game (through PS5X360).
- The GPU / 4K renderer.

The full history is in [`CHANGELOG.md`](CHANGELOG.md).

## What's in this repository

| Folder | What it is |
|---|---|
| [`dashboard/`](dashboard/README.md) | The Ex-Box 360 dashboard title: source, build scripts, setup app and tests |
| [`avatar-editor/`](avatar-editor/README.md) | The Avatar Editor builder, which builds the editor from your own original files |
| [`games/`](games/README.md) | The Games blade: Xbox 360 games (through PS5X360) and PS5 games in one list |
| [`docs/`](docs/) | How to build ([`BUILDING.md`](docs/BUILDING.md)) and export ([`EXPORTING.md`](docs/EXPORTING.md)), plus design notes |
| [`licenses/`](licenses/) | Licences for bundled third-party components |

Licence: GPL-3.0-or-later (see [`COPYING`](COPYING) and [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)).

---

*A fan / parody project. Not affiliated with, authorized, or endorsed by Microsoft or Sony.
"Xbox" and "PlayStation" are trademarks of their respective owners. This project ships **no**
Microsoft assets or code. The look is rebuilt from scratch, and you supply your own files.*
