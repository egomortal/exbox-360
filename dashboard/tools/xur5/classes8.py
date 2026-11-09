"""Per-class property tables for XUIB versions 7 and 8 dashboard files.

Each class is a list of levels from XuiElement down to the class itself; each
level maps a mask bit to (name, type). Types: s string, f FLOT, v VECT, q QUAT,
c4 COLR, i signed int, u unsigned, b bool, c CUST path, '*t' indexed list,
'o:Kind' compound object, raw opaque packed scalar (no inferred type). Established from the v8 files themselves: every
entry below occurs in the 12625-17559 files (as a stored property or as a
timeline property), so an unlisted bit raises instead of being guessed.
Names follow XUI where the value makes the property clear; ClassNN names are
placeholders for properties whose XUI name is not established (Element25 is a
COLR colour, the others raw packed integers). Left out on purpose because no
v8 file uses them (v5 guesses, unverified for v8): Element 12 DesignTime,
Image 2 Brightness (Image 3 is a source-element string), XText 4 Font / 6 LineSpacingAdjust,
List 1/2, Slider 4, TabScene 1/3.
"""
def T(name,**kw):
    t={'__name__':name}
    for k,v in kw.items():t[int(k[1:])]=v
    return t
EMPTY={'__name__':'empty'}
E=T('Element',p0=('Id','s'),p1=('Width','f'),p2=('Height','f'),p3=('Position','v'),p4=('Scale','v'),p5=('Rotation','q'),p6=('Opacity','f'),
    p7=('Anchor','i'),p8=('Pivot','v'),p9=('Show','b'),p10=('BlendMode','i'),p11=('DisableTimelineRecursion','b'),
    p13=('ColorWriteFlags','i'),p14=('ClipChildren','b'),p16=('Element16','u'),p17=('Element17','u'),p18=('Element18','u'),p19=('Element19','u'),p21=('Element21','u'),p22=('Element22','u'),p23=('Element23','u'),p25=('Element25','c4'),p26=('Element26','u'))
CONTROL=T('Control',p0=('ClassOverride','s'),p1=('Visual','s'),p2=('Enabled','b'),p3=('UnfocussedInput','b'),p4=('NavLeft','s'),p5=('NavRight','s'),
    p6=('NavUp','s'),p7=('NavDown','s'),p10=('Text','s'),p12=('ImagePath','s'),p15=('Control15','u'),p16=('Control16','s'),p17=('Control17','u'))
SCENE=T('Scene',p0=('DefaultFocus','s'),p1=('TransFrom','s'),p2=('TransTo','s'),p3=('TransBackFrom','s'),p4=('TransBackTo','s'),p5=('InterruptTransitions','b'),p6=('IgnorePresses','b'))
BUTTON=T('Button',p0=('PressKey','i'))
FIG=T('Figure',p0=('Stroke','o:Stroke'),p1=('Fill','o:Fill'),p2=('Closed','b'),p3=('Points','c'))
STROKE=T('Stroke',p0=('StrokeWidth','f'),p1=('StrokeColor','c4'))
FILL=T('Fill',p0=('FillType','i'),p1=('FillColor','c4'),p2=('TextureFileName','s'),p3=('Gradient','o:Gradient'),p4=('Translation','v'),
    p5=('Scale','v'),p6=('Rotation','f'),p7=('WrapX','i'),p8=('WrapY','i'),p9=('BrushFlags','i'),p10=('Fill10','u'))
GRAD=T('Gradient',p0=('Radial','b'),p1=('NumStops','i'),p2=('StopColor','*c4'),p3=('StopPos','*f'))
IMAGE=T('Image',p0=('SizeMode','i'),p1=('ImagePath','s'),p4=('Image4','u'))
TEXT=T('Text',p0=('TextColor','c4'),p1=('DropShadowColor','c4'),p2=('PointSize','f'),p4=('TextStyle','i'),p5=('LineSpacingAdjust','i'),p6=('Text6','u'))
NAV=T('Nav',p0=('NavScene','s'))
TAB=T('TabScene',p0=('TabCount','i'),p2=('Wrap','b'),p4=('Tab4b','b'),p5=('DefaultTab','i'))
SLIDER=T('Slider',p0=('RangeMin','i'),p1=('RangeMax','i'),p2=('Step','i'),p5=('Slider5','i'),p6=('Slider6','i'))
XTEXT=T('XText',p0=('Text','s'),p1=('TextColor','c4'),p2=('DropShadowColor','c4'),p3=('PointSize','f'),p5=('TextStyle','i'))
NINE=T('NineGrid',p0=('ImagePath','s'),p1=('Left','i'),p2=('Top','i'),p3=('Right','i'),p4=('Bottom','i'),p5=('MeshOnly','b'))
SOUND=T('Sound',p0=('Sound0','u'),p2=('Loop','b'),p3=('Volume','f'))
OBJ={'Stroke':STROKE,'Fill':FILL,'Gradient':GRAD}
L={'XuiElement':[E],'XuiCanvas':[E,EMPTY],'XuiGroup':[E,EMPTY],'XuiVisual':[E,EMPTY],
   'XuiControl':[E,CONTROL],'XuiScene':[E,CONTROL,SCENE],'XuiFigure':[E,FIG],'XuiImage':[E,IMAGE],
   'XuiButton':[E,CONTROL,BUTTON],'XuiNavButton':[E,CONTROL,BUTTON,NAV],'XuiLabel':[E,CONTROL,TEXT],
   'XuiProgressBar':[E,CONTROL,EMPTY],
   'XuiBackButton':[E,CONTROL,BUTTON,T('BackButton')],'XuiTabScene':[E,CONTROL,SCENE,TAB],'XuiSlider':[E,CONTROL,SLIDER],
   'XuiTextPresenter':[E,TEXT],'XuiText':[E,XTEXT],'XuiImagePresenter':[E,T('ImagePresenter',p0=('SizeMode','i'),p1=('ImagePresenter1','u'))],'XuiNineGrid':[E,NINE],
   'XuiSoundXAudio':[E,SOUND,T('XAudio',p0=('File','s'))],'XuiSoundXACT':[E,SOUND,T('XACT',p0=('Cue','s'),p1=('SoundBank','s'),p2=('WaveBank','s'))],
   'XuiCheckbox':[E,CONTROL,T('Checkbox')],
   'XuiRadioButton':[E,CONTROL,T('RadioButton')],'XuiList':[E,CONTROL,T('List',p0=('Wrap','b'))],'XuiRadioGroup':[E,CONTROL,T('RadioGroup')],
   'XuiListItem':[E,CONTROL,BUTTON,T('ListItem',p0=('ItemFlags','i'),p1=('ListItem1','b'),p4=('ListItem4','u'))],'XuiEdit':[E,CONTROL,T('Edit',p0=('MaxLength','i'),p3=('ReadOnly','b'),p4=('Multiline','b'))],
   'XuiShader':[E,T('Shader',p0=('Shader0','s'),p1=('Shader1','s'),p9=('Shader9','u'),p10=('Shader10','u'))],'XuiGridPanel':[E,T('GridPanel',p0=('Columns','s'),p1=('Rows','s'))],
   'XuiCaret':[E,CONTROL,T('Caret')],'XuiScrollEnd':[E,CONTROL,T('ScrollEnd',p0=('Direction','i'))],'XuiTextureSurface':[E,T('Group'),T('TextureSurface')]}
# Title-defined classes: base class levels plus one level of their own (the
# level count is fixed by the masks in the files; ControlPackSystemGesture adds
# its level directly under XuiElement).
CUSTOM={}
def custom(name,base,level=EMPTY):
    CUSTOM[name]=base;L[name]=L[base]+[dict(level,__name__=name) if level is not EMPTY else dict(EMPTY,__name__=name)]
custom('GuideDashCommandNavButton','XuiNavButton',T('GuideDashCommand',p0=('Command','i')))
custom('ControlPackSimpleCursorScene','XuiScene')
custom('HUDScene','XuiScene',T('HUDScene',p0=('HudInt','i'),p1=('LegendA','s'),p2=('LegendB','s'),p3=('LegendX','s'),p4=('LegendY','s')))
custom('GuideMainSceneNavButton','XuiNavButton',T('GuideMainSceneNav',p0=('TargetScene','s')))
custom('AccountManagementNavButton','GuideMainSceneNavButton')
custom('ControlPackSystemGesture','XuiElement')
custom('ScriptActionNavButton','XuiNavButton',T('ScriptActionNav',p0=('ScriptAction','s'),p1=('ScriptParam','s')))
custom('VideoMarketplaceNavButton','XuiNavButton')
custom('AvatarEditorNavButton','XuiNavButton',T('AvatarEditorNav',p0=('Command','i')))

# T14: Legend0 indexes the otherwise unreferenced -2 FLOT entry in all
# eight unique v8 legends. The XUI property name is not established.
custom('ControlPackLegendScene','XuiScene',T('Legend',p0=('Legend0','f')))
L['XuiPerspectiveScene']=L['XuiScene']+[T('Perspective',p0=('UndecodedPerspective0','raw'))]
IMAGE[3]=('SourceElement','s')
L['XuiImagePresenter'][1][3]=('UndecodedImagePresenter3','raw')
L['XuiShader'][1][9]=('ShaderVector0','v')
L['XuiShader'][1][10]=('ShaderVector1','v')

# T15: packed masks/counts and full value-table accounting across the corpus.
# The scalar-only fields have no proven type; keep them opaque.
E[24]=('UndecodedElement24','raw')
L['XuiGridPanel'][1][3]=('GridPanel3','f')
L['XuiImagePresenter'][1][2]=('UndecodedImagePresenter2','raw')
L['XuiVideo']=[E,T('Video',p1=('UndecodedVideo1','raw'),p3=('UndecodedVideo3','raw'))]
L['XuiHtmlPresenter']=[E,EMPTY]
L['XuiScrollBar']=[E,CONTROL,EMPTY]
L['XuiListItem'][-1][4]=('ListItem4','v')

TEXT[3]=('Font','s')
L['XuiShader'][1].update({2:('ShaderInput','s'),4:('UndecodedShader4','raw'),
    5:('UndecodedShader5','raw'),8:('Shader8','b'),11:('ShaderVector2','v'),12:('ShaderVector3','v')})
custom('ControlPackNuiHoverButton','XuiButton',T('NuiHoverButton',p1=('UndecodedHover1','raw'),
    p2=('UndecodedHover2','raw'),p3=('UndecodedHover3','raw'),p4=('UndecodedHover4','raw')))

L['XuiTouch']=[E,EMPTY,T('Touch',**{'p'+str(i):(f'UndecodedTouch{i}','raw') for i in (1,3,5,6,7,10,11)})]
CONTROL[14]=('UndecodedControl14','raw')
for i in (3,4):L['ControlPackNuiHoverButton'][-1][i]=(f'Hover{i}','f')
custom('ControlPackVuiBling','XuiControl')

custom('MultiImage','XuiImage',T('MultiImage',p1=('UndecodedMultiImage1','raw')))
CONTROL[17]=('Control17','f')
SLIDER[3]=('UndecodedSlider3','raw')

# Q11: XuiAvatar records with only XuiElement fields have exactly two
# additional empty property levels. Strict whole-scene accounting validates
# this shape in NXE/Kinect/Metro GamerRoot and NXE sign-in panels. Their base
# class semantics are NOT inferred: reject every nonzero extension mask until
# its type is recovered. In particular this does not decode pose/camera data.
L['XuiAvatar']=[E,CONTROL,T('AvatarUnresolvedProperties',p0=('UndecodedAvatar0','raw'),p1=('UndecodedAvatar1','raw'))]

# Q11 follow-up: GamerCardSlot in 9199 and 13604 validates extension
# bits 0/1 as byte-width / packed opaque scalars respectively. The middle
# level uses Control fields (WaveIndicator stores Control16). Other avatar
# extension bits still raise; no camera/pose meaning is assigned.
