"""Retain opaque DBUG bytes while decoding the independent scene sections."""
from pathlib import Path
import argparse
p=argparse.ArgumentParser();p.add_argument('root',type=Path);a=p.parse_args();core=a.root/'XUIHelper.Core'
target=core/'XUR/V8/XUR8.cs';s=target.read_text(encoding='utf-8-sig')
if 'DBUGOwnerSection' not in s:s=s.replace('switch (Magic)\n                {','switch (Magic)\n                {\n                    case 0x44425547: return new DBUGOwnerSection();')
target.write_text(s,encoding='utf-8')
(core/'XUR/V8/Sections/DBUGOwnerSection.cs').write_text('''namespace XUIHelper.Core {
    // Debug symbol bytes are not required to evaluate independent DATA/KEYD.
    // Preserve the entire section and its length, without interpreting it.
    public class DBUGOwnerSection : IXURSection {
        public int Magic => 0x44425547;
        public byte[] Data = System.Array.Empty<byte>();
        public System.Threading.Tasks.Task<bool> TryReadAsync(IXUR xur, System.IO.BinaryReader reader) {
            var entry = xur.TryGetXURSectionTableEntryForMagic(Magic);
            if (entry == null) return System.Threading.Tasks.Task.FromResult(false);
            Data = reader.ReadBytes(entry.Length);
            return System.Threading.Tasks.Task.FromResult(Data.Length == entry.Length);
        }
        public System.Threading.Tasks.Task<bool> TryBuildAsync(IXUR xur, XUObject obj) => System.Threading.Tasks.Task.FromResult(true);
        public System.Threading.Tasks.Task<int?> TryWriteAsync(IXUR xur, XUObject obj, System.IO.BinaryWriter writer) {
            writer.Write(Data);return System.Threading.Tasks.Task.FromResult<int?>(Data.Length);
        }
    }
}''',encoding='utf-8')
