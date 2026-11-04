from xur5 import *
def T(name,**kw):
    t={'__name__':name}
    for k,v in kw.items():t[int(k[1:])]=(k,v) if isinstance(v,str) else v
    return t
EMPTY={'__name__':'empty'}
NAV=T('Nav',p0=('NavScene','s'))
NINE=T('NineGrid',p0=('ImagePath','s'),p1=('Left','i'),p2=('Top','i'),p3=('Right','i'),p4=('Bottom','i'),p5=('MeshOnly','b'))
L={'XuiCanvas':[E,EMPTY],'XuiVisual':[E,EMPTY],
   'HUDScene':[E,CONTROL,SCENE,T('HUDScene',p0=('HudInt','i'),p1=('LegendA','s'),p2=('LegendB','s'),p3=('LegendX','s'),p4=('LegendY','s'))],
   'XuiScene':[E,CONTROL,SCENE],'XuiImage':[E,IMAGE],'XuiFigure':[E,FIG],'XuiNineGrid':[E,NINE],
   'XuiNavButton':[E,CONTROL,BUTTON,NAV],'XuiButton':[E,CONTROL,BUTTON],'XuiBackButton':[E,CONTROL,BUTTON,EMPTY],
   'XuiLabel':[E,CONTROL,TEXT],'XuiGamerCard':[E,CONTROL,T('GamerCard',p0=('Mode','s'),p1=('GC1','b'))],'XuiGroup':[E,EMPTY],'XuiControl':[E,CONTROL],
   'XuiTextPresenter':[E,TEXT],'XuiImagePresenter':[E,T('ImgPres',p0=('SizeMode','i'),p1=('Presenter1','i'))],'XuiElement':[E],'XuiText':[E,T('XText',p0=('Text','s'),p1=('TextColor','c4'),p2=('DropShadowColor','c4'),p3=('PointSize','f'),p4=('Font','s'),p5=('TextStyle','i'),p6=('LineSpacingAdjust','si'))],
   'XuiProgressBar':[E,CONTROL,EMPTY],'XuiSlider':[E,CONTROL,T('Slider',p0=('RangeMin','si'),p1=('RangeMax','si'),p2=('Step','si'),p3=('Value','si'),p4=('S4','si'),p5=('S5','si'),p6=('S6','si'))],'XuiList':[E,CONTROL,T('List',p0=('Wrap','b'),p1=('List1','b'),p2=('List2','b'))],'XuiCheckbox':[E,CONTROL,EMPTY],
   'XuiRadioButton':[E,CONTROL,BUTTON,EMPTY],'XuiRadioGroup':[E,CONTROL,EMPTY],'XuiCommonList':[E,CONTROL,EMPTY,EMPTY],'XuiListItem':[E,CONTROL,BUTTON,T('ListItem',p0=('ItemFlags','i'),p1=('LI1','b'),p2=('LI2','i'))],
   'XuiScrollEnd':[E,CONTROL,T('ScrollEnd',p0=('Direction','i'))],'XuiEdit':[E,CONTROL,T('Edit',p0=('MaxLength','i'),p3=('ReadOnly','b'),p4=('Multiline','b'))],'XuiCaret':[E,CONTROL,EMPTY],'XuiSoundXAudio':[E,T('Sound',p0=('File','s'),p2=('Loop','b'),p3=('Volume','f')),T('XAudio',p0=('XFile','s'))],'XuiMessageBox':[E,CONTROL,SCENE,EMPTY]}

# Later (6770 / 9199) Guide classes.
TAB=T('TabScene',p0=('TabCount','i'),p1=('Tab1','i'),p2=('Wrap','b'),p3=('Tab3','i'),p4=('Tab4b','b'),p5=('DefaultTab','i'))
L['XuiTabScene']=[E,CONTROL,SCENE,TAB]
CUSTOM=T('Custom',p0=('Custom0','i'))
class Levels(dict):
    """Custom classes (GuideDashCommandNavButton, ...) store their base
    class's levels plus one of their own."""
    BASES=[('NavButton','XuiNavButton'),('BackButton','XuiBackButton'),('Button','XuiButton'),('TabScene','XuiTabScene'),
           ('Scene','XuiScene'),('List','XuiList'),('Label','XuiLabel'),('Image','XuiImage'),('Control','XuiControl')]
    def __missing__(self,cls):
        for suffix,base in self.BASES:
            if cls.endswith(suffix) and dict.__contains__(self,base):
                b=dict.__getitem__(self,base);return [b+[CUSTOM],b,b+[EMPTY],b+[CUSTOM,EMPTY],b+[T('CustomS',p0=('CustomS0','s'))]]
        raise KeyError(cls)
    def __contains__(self,cls):
        try:self[cls];return True
        except KeyError:return False
L=Levels(L)
L['GuideMainSceneNavButton']=L['XuiNavButton']+[T('GuideMainSceneNav',p0=('TargetScene','s'))]
L['GuideDashCommandNavButton']=L['XuiNavButton']+[T('GuideDashCommand',p0=('Command','i'))]

# T14: byte widths and cross-version evidence in the task report. Unknown
# scalar semantics stay opaque; they are not guessed as floats or integers.
FILL[10]=('UndecodedFill10','raw32')
IMAGE[3]=('SourceElement','s')
L['XuiPerspectiveScene']=[E,CONTROL,SCENE,T('Perspective',p0=('UndecodedPerspective0','raw32'))]
L['XuiTextureSurface']=[E,EMPTY,EMPTY]
L['XuiShader']=[E,T('Shader',p0=('ShaderId','s'),p1=('ShaderFile','s'),p2=('ShaderInput','s'),
    p4=('UndecodedShader4','raw32'),p5=('UndecodedShader5','raw32'),p8=('Shader8','b'),
    p9=('ShaderVector0','v'),p10=('ShaderVector1','v'),p11=('ShaderVector2','v'),p12=('ShaderVector3','v'))]

# T15: Sound level bit 0 occupies four bytes, including in timeline keys;
# only the derived XAudio level's bit 0 is a two-byte string reference.
L['XuiSoundXAudio'][1][0]=('UndecodedSound0','raw32')
L['XuiHtmlPresenter']=[E,EMPTY]
L['XuiScrollBar']=[E,CONTROL,EMPTY]

E[15]=('UndecodedElement15','raw8')
E[16]=('UndecodedElement16','raw8')

L['XuiSoundXAudio'][1][1]=('UndecodedSound1','raw8')

# Q11: XuiAvatar records with only XuiElement fields have exactly two
# additional empty property levels. Strict whole-scene accounting validates
# this shape in NXE/Kinect/Metro GamerRoot and NXE sign-in panels. Their base
# class semantics are NOT inferred: reject every nonzero extension mask until
# its type is recovered. In particular this does not decode pose/camera data.
L['XuiAvatar']=[E,CONTROL,T('AvatarUnresolvedProperties',p0=('UndecodedAvatar0','raw8'),p1=('UndecodedAvatar1','raw8'))]

# Q11 follow-up: GamerCardSlot in 9199 and 13604 validates extension
# bits 0/1 as byte-width / packed opaque scalars respectively. The middle
# level uses Control fields (WaveIndicator stores Control16). Other avatar
# extension bits still raise; no camera/pose meaning is assigned.

# Q10/G03: original 1888 and 6770 consoles packages establish three string
# references after the XuiScene levels. Bit 0 names controls, bit 1 contains
# their descriptive text, bit 2 is a parallel string list. Retain neutral
# property names until the original host registration names are recovered.
# Unknown bits still raise; this is an exact class, never a suffix alias.
L['DashScene']=[E,CONTROL,SCENE,T('DashScene',
    p0=('DashSceneString0','s'),p1=('DashSceneString1','s'),p2=('DashSceneString2','s'))]
