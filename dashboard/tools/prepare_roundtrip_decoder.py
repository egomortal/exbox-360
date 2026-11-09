"""Build an isolated XUIHelper source fork with invariant float32 XML output."""
from pathlib import Path
import argparse, difflib, hashlib, json, re, shutil

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--evidence',type=Path,required=True)
a=p.parse_args()
assert not a.output.exists(), 'Use a fresh versioned fork; do not overwrite decoder sources.'
a.evidence.mkdir(parents=True,exist_ok=True)
records=[]
for folder in ('XUIHelper.Core','XUIHelper.CLI'):
    for source in sorted((a.source/folder).rglob('*')):
        relative=source.relative_to(a.source)
        if not source.is_file() or any(part in ('bin','obj','.git') for part in relative.parts):continue
        target=a.output/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
        records.append(dict(path=str(relative),sha256=hashlib.sha256(source.read_bytes()).hexdigest()))
for name in ('LICENSE','README.md'):
    shutil.copy2(a.source/name,a.output/name)
writer=a.output/'XUIHelper.Core/XUI/Versions/V12/XUI12WriteExtensions.cs'
before=writer.read_text(encoding='utf-8-sig')
after=before.replace('Convert.ToDouble(val).ToString("0.000000")','XUIFloatText.Format(Convert.ToSingle(val))')
after,n=re.subn(r'\(\(double\)([\w.]+)\)\.ToString\("0\.000000"\)',r'XUIFloatText.Format(\1)',after)
assert n==9,n
after,n=re.subn(r'(quat\.[XYZW])\.ToString\("0\.000000"\)',r'XUIFloatText.Format(\1)',after)
assert n==4,n
assert '0.000000' not in after
writer.write_text(after,encoding='utf-8')
helper=a.output/'XUIHelper.Core/XUI/Versions/V12/XUIFloatText.cs'
helper.write_text('''using System.Globalization;
using System.Text.Json;

namespace XUIHelper.Core;

internal static class XUIFloatText
{
    private static readonly string? AuditPath = Environment.GetEnvironmentVariable("XUI_NUMERIC_AUDIT");
    private static readonly Dictionary<string, (string Text, long Count)> Values = new();
    private static long Calls;
    private static long LegacyBitChanges;
    private static long NonFinite;

    static XUIFloatText()
    {
        if (!string.IsNullOrEmpty(AuditPath))
            AppDomain.CurrentDomain.ProcessExit += (_, _) => SaveAudit();
    }

    public static string Format(float value)
    {
        string text = value.ToString("R", CultureInfo.InvariantCulture);
        if (!string.IsNullOrEmpty(AuditPath))
        {
            Calls++;
            if (!float.IsFinite(value)) NonFinite++;
            else if (BitConverter.SingleToInt32Bits(value) != BitConverter.SingleToInt32Bits(float.Parse(text, CultureInfo.InvariantCulture)))
                throw new InvalidOperationException("Float32 serialization changed original bits.");
            string legacy = ((double)value).ToString("0.000000", CultureInfo.InvariantCulture);
            if (BitConverter.SingleToInt32Bits(value) != BitConverter.SingleToInt32Bits(float.Parse(legacy, CultureInfo.InvariantCulture))) LegacyBitChanges++;
            string bits = BitConverter.SingleToUInt32Bits(value).ToString("x8");
            var previous = Values.GetValueOrDefault(bits);
            Values[bits] = (text, previous.Count + 1);
        }
        return text;
    }

    private static void SaveAudit()
    {
        File.WriteAllText(AuditPath!, JsonSerializer.Serialize(new {
            calls = Calls, legacy_bit_changes = LegacyBitChanges, nonfinite = NonFinite,
            values = Values.Select(p => new { bits = p.Key, text = p.Value.Text, calls = p.Value.Count }),
            scope = "Loaded float32 values at the actual XML writer; not independent object schema validation."
        }));
    }
}
''',encoding='utf-8')
patch=''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='original/XUI12WriteExtensions.cs',tofile='roundtrip/XUI12WriteExtensions.cs'))
(a.evidence/'formatter.patch').write_text(patch,encoding='utf-8')
(a.evidence/'FORK_SOURCE.json').write_text(json.dumps(dict(source=str(a.source),fork=str(a.output),original_files=records,
    writer_sha256=hashlib.sha256(writer.read_bytes()).hexdigest(),helper_sha256=hashlib.sha256(helper.read_bytes()).hexdigest(),
    change='14 float/scalar/vector/Bezier/quaternion formatting calls; invariant round-trip float32; optional diagnostic audit.',
    original_decoder_retained=True,reader_and_schema_unchanged=True),indent=2)+'\n')
print(json.dumps(dict(copied_source_files=len(records),formatting_calls_replaced=14,fork=str(a.output))))
