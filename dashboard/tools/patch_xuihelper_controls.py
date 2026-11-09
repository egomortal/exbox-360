"""Preserve XML-illegal host characters in an explicit conversion sidecar."""
from pathlib import Path
import argparse
p=argparse.ArgumentParser();p.add_argument('root',type=Path);a=p.parse_args()
file=a.root/'XUIHelper.Core/XUI/XUI.cs';s=file.read_text(encoding='utf-8-sig')
old='Logger?.Here().Verbose("Wrote property {0} as {1}.", childProperty.PropertyDefinition.Name, string.Join("\\n", thisPropertyElements));'
s=s.replace(old,'Logger?.Here().Verbose("Wrote property {0}.", childProperty.PropertyDefinition.Name);')
marker='                XDocument document = new XDocument(rootElement);'
if 'ownerCharacterEscapes' not in s:
 s=s.replace(marker,marker+'''
                // XML 1.0 cannot represent these Xbox host glyph/control values.
                // Keep exact values in a JSON sidecar; never alter owner XURs.
                var ownerCharacterEscapes = new List<object>();
                int textNodeIndex = 0;
                foreach (var textNode in document.DescendantNodes().OfType<XText>()) {
                    string original = textNode.Value;
                    if (original.Any(c => !XmlConvert.IsXmlChar(c))) {
                        ownerCharacterEscapes.Add(new { index = textNodeIndex, value = original });
                        textNode.Value = string.Concat(original.Select(c => XmlConvert.IsXmlChar(c) ? c.ToString() : "\\\\u" + ((int)c).ToString("x4")));
                    }
                    textNodeIndex++;
                }
                if (ownerCharacterEscapes.Count > 0)
                    await File.WriteAllTextAsync(FilePath + ".characters.json", System.Text.Json.JsonSerializer.Serialize(ownerCharacterEscapes));
''')
file.write_text(s,encoding='utf-8')
