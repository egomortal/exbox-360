"""Audit marketplace hook ownership without executing guest code or fetching assets.

Classification is reviewed against the pinned source; fail closed on new names.
This inventory is not a parity or retirement receipt.
"""
from pathlib import Path
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[1]
STATEFUL = {
    "AE_MktItemFetch": "Records fetcher/name pairs shared with MktResetFetchers and XUI search.",
    "AE_MktRatingArgFix": "Copies guest stack word +0x54 to +0x58; preserve ABI workaround.",
    "AE_MktOwnStatus": "Overrides only ownership-manager object; returns handled flag.",
    "AE_MktOwnContains": "Installed closet membership, not unconditional ownership.",
    "AE_MktPurchase": "Installs missing item, then increments generation and broadcasts content-installed.",
    "AE_MktHoverTryOn": "Seeds guest generation once per process, not once per invocation.",
    "AE_MktNoParentalGate": "Returns handled=true and writes r3.u32=0.",
}
DIAGNOSTIC = set("ParserEvent SlotValidate ChannelValidate NetPoll NetHeaders NetTeardown NetClose SceneLookup PreviewLoad PageError CatFetch QueryIssue DispatchKind TitleQuery Marshal WearStart WearStep PreviewApply".split())


def audit():
    path = ROOT / "private/av2/title-port-source/patches.cpp"
    raw = path.read_bytes()
    source = raw.decode("utf-8")
    hooks = []
    for match in re.finditer(r"^(void|bool) (AE_Mkt\w+)\(([^)]*)\) \{", source, re.M):
        result, name, arguments = match.groups()
        diagnostic = name.removeprefix("AE_Mkt") in DIAGNOSTIC
        if name not in STATEFUL and not diagnostic:
            raise ValueError("Unreviewed hook: " + name)
        hooks.append(dict(name=name, result=result, arguments=arguments,
                          line=source[:match.start()].count("\n") + 1,
                          category="diagnostic" if diagnostic else "behavior",
                          contract=STATEFUL.get(name, "Trace only; preserve registers and guest bytes. Host trace counters may change.")))
    assert len(hooks) == 25, "Hook set changed; review classification"
    return dict(source=str(path.relative_to(ROOT)), sha256=hashlib.sha256(raw).hexdigest(),
                hook_count=len(hooks), diagnostic_count=sum(h["category"] == "diagnostic" for h in hooks),
                hooks=hooks, retirement_accepted=False,
                dependencies=["MktResetFetchers is called from xui_hooks.cpp; retain shared fetcher state.",
                              "MarketplaceInstallItem retains FetchRemoteItem/Icon helper names; actual offline routing must be traced before judging network behavior.",
                              "patches.cpp contains other hook groups and cannot be removed as an A5-only object.",
                              "Offline catalog Query deliberately returns an empty feed; clothing category counts must be checked against Closet, not that feed."])


if __name__ == "__main__":
    print(json.dumps(audit(), indent=2))
