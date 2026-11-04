"""Build-specific XuiAvatar records, verified in original initialization code.
Evidence: private/q11b/original-avatar-schemas.json and the 12581 live table.
Never select this by XUIB version alone: Metro removed two earlier properties.
Animation event values remain raw integers; no clip identity is implied.
"""
def properties(build, version):
    if build not in (9199,12581,13604,15574,17559):
        raise ValueError('No original XuiAvatar schema verified for build %s'%build)
    integer='i' if version==5 else 'u'
    entries=[('UseActiveUserAvatar','b'),('EnableUserControl','b')]
    if build in (9199,12581,13604):
        entries += [('QueueAnimation',integer),('PlayAnimation',integer)]
    entries += [('UseCustomView','b'),('RenderView',integer),
      ('CustomViewEyePosition','v'),('CustomViewFocusPosition','v'),
      ('CustomViewUpDirection','v'),('ShowSelfShadow','b'),('ShowShadow','b'),
      ('ShowCarryable','b'),('ShowHat','b')]
    return dict(__name__='XuiAvatar-build-%d'%build,**{}) | dict(enumerate(entries))
