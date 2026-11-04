"""Clean generated avatar texture and mesh while preserving the source-facing art."""
from pathlib import Path
import sys
import numpy as np
import trimesh
import moderngl
from scipy.ndimage import distance_transform_edt, median_filter
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent / "private/avatar"
sys.path.insert(0, str(Path(sys.argv[1])))
from tsr.bake_texture import rasterize_position_atlas
SOURCE = ROOT / "bayonetta1-image-model-source.obj"
TEXTURE = ROOT / "bayonetta1-front-projected.png"
OUT = ROOT
OUT.mkdir(exist_ok=True)

mesh = trimesh.load(SOURCE, process=False)
print("mesh", len(mesh.vertices), len(mesh.faces), mesh.bounds, mesh.is_watertight)
components = mesh.split(only_watertight=False)
print("components", len(components), sorted((len(c.faces) for c in components), reverse=True)[:30])

rgba = np.asarray(Image.open(TEXTURE).convert("RGBA")).copy()
base = np.asarray(Image.open(ROOT / "bayonetta1-image-model-source-texture.png").convert("RGBA"))
projected = np.max(np.abs(rgba[..., :3].astype(np.int16) - base[..., :3].astype(np.int16)), axis=-1) > 7
rgb = rgba[..., :3].astype(np.float32)
r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
red = (r > g * 1.35) & (r > b * 1.35) & (g < 120)
gold = (r > g * 1.04) & (g > b * 1.25) & (g > 60)
skin = (r > g * 1.1) & (g > b * 1.15) & (g > 70) & ~red
keep = red | gold | skin
lum = r * 0.2126 + g * 0.7152 + b * 0.0722
neutral = np.stack([lum * .91, lum * .9, lum * .91], axis=-1)
# Keep intentional red, gold and skin; pull color fringing in black material to charcoal.
clean = ~keep & ~projected
rgb[clean] = rgb[clean] * .18 + neutral[clean] * .82
rgba[..., :3] = np.clip(rgb, 0, 255).astype(np.uint8)

# Project the designed back view onto surfaces that are actually visible from
# behind. The source image and reconstruction share the same height scale.
original = np.asarray(mesh.vertices)
scale = 1.73 / (original[:, 2].max() - original[:, 2].min())
mid_y = (original[:, 1].max() + original[:, 1].min()) / 2
mid_x = (original[:, 0].max() + original[:, 0].min()) / 2
posed = np.column_stack(((original[:, 1] - mid_y) * scale,
                          (original[:, 2] - original[:, 2].min()) * scale,
                          (original[:, 0] - mid_x) * scale)).astype(np.float32)
atlas_mesh = trimesh.Trimesh(vertices=posed, faces=mesh.faces, process=False)
atlas_mesh.visual = trimesh.visual.TextureVisuals(uv=mesh.visual.uv)
side = rgba.shape[0]
position = rasterize_position_atlas(
    atlas_mesh, np.arange(len(mesh.vertices), dtype=np.int32),
    np.asarray(mesh.faces, dtype=np.int32), np.asarray(mesh.visual.uv, dtype=np.float32), side, 0)
position = np.flipud(position)
xmin, ymin = posed[:, :2].min(axis=0)
xmax, ymax = posed[:, :2].max(axis=0)
ctx = moderngl.create_context(standalone=True)
ctx.enable(moderngl.DEPTH_TEST)
prog = ctx.program(
    vertex_shader="""#version 330
        in vec3 in_pos; out vec3 v_pos; uniform vec4 bounds;
        void main() {
            v_pos=in_pos;
            float x=(in_pos.x-bounds.x)/(bounds.z-bounds.x)*2.0-1.0;
            float y=(in_pos.y-bounds.y)/(bounds.w-bounds.y)*2.0-1.0;
            gl_Position=vec4(x,y,in_pos.z*0.7,1.0);
        }""",
    fragment_shader="""#version 330
        in vec3 v_pos; out vec4 o_col;
        void main() { o_col=vec4(v_pos,1.0); }""")
prog["bounds"].value = (float(xmin), float(ymin), float(xmax), float(ymax))
vao = ctx.vertex_array(prog, [(ctx.buffer(posed.tobytes()), "3f", "in_pos")],
                       ctx.buffer(np.asarray(mesh.faces, dtype=np.int32).tobytes()))
color = ctx.texture((side, side), 4, dtype="f4")
depth = ctx.depth_renderbuffer((side, side))
fbo = ctx.framebuffer(color_attachments=[color], depth_attachment=depth)
fbo.use(); fbo.clear(0, 0, 0, 0, depth=1)
vao.render()
back_position = np.flipud(np.frombuffer(color.read(), dtype=np.float32).reshape(side, side, 4))
valid = position[..., 3] > .5
px, py, pz = position[..., 0], position[..., 1], position[..., 2]
ix = np.clip(((px-xmin)/(xmax-xmin)*(side-1)).astype(np.int32), 0, side-1)
iy = np.clip(((ymax-py)/(ymax-ymin)*(side-1)).astype(np.int32), 0, side-1)
back_visible = valid & (back_position[iy, ix, 3] > .5) & (pz <= back_position[iy, ix, 2] + .017)
reference = np.asarray(Image.open(ROOT / "bayonetta1-back-reference.png").convert("RGBA"))
ys, xs = np.where(reference[..., 3] > 100)
left, right, top, bottom = xs.min(), xs.max(), ys.min(), ys.max()
u = left + (xmax-px)/(xmax-xmin)*(right-left)
v = top + (ymax-py)/(ymax-ymin)*(bottom-top)
ru = np.clip(np.rint(u).astype(np.int32), 0, reference.shape[1]-1)
rv = np.clip(np.rint(v).astype(np.int32), 0, reference.shape[0]-1)
paint = reference[rv, ru]
back_visible &= paint[..., 3] > 220
rgba[back_visible, :3] = (paint[back_visible, :3].astype(np.float32) * .9
                          + rgba[back_visible, :3].astype(np.float32) * .1).astype(np.uint8)
print("painted back texels", back_visible.sum())

side_sheet = np.asarray(Image.open(ROOT / "bayonetta1-side-references.png").convert("RGBA"))
zmin, zmax = posed[:, 2].min(), posed[:, 2].max()
for is_left, panel in ((True, side_sheet[:, :512]), (False, side_sheet[:, 1024:])):
    side_prog = ctx.program(
        vertex_shader="""#version 330
            in vec3 in_pos; out vec3 v_pos; uniform vec4 bounds; uniform float side;
            void main() {
                v_pos=in_pos;
                float h=(in_pos.z-bounds.x)/(bounds.z-bounds.x);
                if (side < 0.0) h=1.0-h;
                float y=(in_pos.y-bounds.y)/(bounds.w-bounds.y)*2.0-1.0;
                gl_Position=vec4(h*2.0-1.0,y,in_pos.x*side*0.7,1.0);
            }""",
        fragment_shader="""#version 330
            in vec3 v_pos; out vec4 o_col;
            void main() { o_col=vec4(v_pos,1.0); }""")
    side_prog["bounds"].value = (float(zmin), float(ymin), float(zmax), float(ymax))
    side_prog["side"].value = 1.0 if is_left else -1.0
    side_vao = ctx.vertex_array(side_prog, [(ctx.buffer(posed.tobytes()), "3f", "in_pos")],
                                ctx.buffer(np.asarray(mesh.faces, dtype=np.int32).tobytes()))
    fbo.use(); fbo.clear(0, 0, 0, 0, depth=1)
    side_vao.render()
    side_position = np.flipud(np.frombuffer(color.read(), dtype=np.float32).reshape(side, side, 4))
    h = (pz-zmin)/(zmax-zmin)
    if not is_left:
        h = 1.0-h
    six = np.clip(np.rint(h*(side-1)).astype(np.int32), 0, side-1)
    near_x = side_position[iy, six, 0]
    exposed = valid & (side_position[iy, six, 3] > .5)
    exposed &= (px <= near_x+.012) if is_left else (px >= near_x-.012)
    # The source front and back have better registration. Use the generated
    # profile art only for the trailing hair behind the character.
    exposed &= (pz < -.03) & (py > .55)
    sy, sx = np.where(panel[..., 3] > 100)
    sl, sr, st, sb = sx.min(), sx.max(), sy.min(), sy.max()
    su = np.clip(np.rint(sl+h*(sr-sl)).astype(np.int32), 0, panel.shape[1]-1)
    sv = np.clip(np.rint(st+(ymax-py)/(ymax-ymin)*(sb-st)).astype(np.int32), 0, panel.shape[0]-1)
    side_paint = panel[sv, su]
    exposed &= side_paint[..., 3] > 220
    # The generated profiles exaggerate the red strands across the large
    # inferred hair volume. Keep that volume black; the narrow ribbon remains
    # supplied by the more reliable front and back references.
    side_rgb = side_paint[..., :3].copy()
    sr, sg, sb = (side_rgb[..., i].astype(np.float32) for i in range(3))
    stray_red = (sr > sg * 1.35) & (sr > sb * 1.35) & (sr > 75)
    hair_gray = sr * .25 + sg * .60 + sb * .15
    side_rgb[stray_red] = np.stack((hair_gray*.50, hair_gray*.45, hair_gray*.48), axis=-1)[stray_red]
    # Side art is a design guide; blend it so the original front and back retain
    # their sharper source-based detail where the projections meet.
    rgba[exposed, :3] = (side_rgb[exposed].astype(np.float32)*.72
                           + rgba[exposed, :3].astype(np.float32)*.28).astype(np.uint8)
    print("painted", "left" if is_left else "right", "texels", exposed.sum())

# The UV atlas has many small islands with transparent black around them.
# Nearest texture sampling can hit those empty texels and produce black freckles.
empty = rgba[..., 3] == 0
_, closest = distance_transform_edt(empty, return_indices=True)
rgba[empty] = rgba[closest[0][empty], closest[1][empty]]
rgba[..., 3] = 255
# Remove isolated reconstruction freckles on broad skin areas. Keep connected
# dark features such as glasses, eyelashes and brows by requiring a skin median.
median = median_filter(rgba[..., :3], size=(5, 5, 1))
mr, mg, mb = (median[..., i].astype(np.float32) for i in range(3))
skin_area = (mr > mg * 1.08) & (mg > mb * 1.08) & (mg > 75)
dark_dot = np.max(median.astype(np.int16) - rgba[..., :3].astype(np.int16), axis=-1) > 45
clean_dots = skin_area & dark_dot & valid & (py > .75) & (pz > .0)
rgba[clean_dots, :3] = median[clean_dots]
print("skin freckles repaired", clean_dots.sum())
Image.fromarray(rgba).save(OUT / "bayonetta1-image-model-texture.png")

# Weld only the geometric copy, then transfer positions back to the UV mesh.
# The picture-facing half stays fixed so facial features and costume edges hold.
unique, inverse = np.unique(np.round(mesh.vertices, 6), axis=0, return_inverse=True)
geo = trimesh.Trimesh(vertices=unique.copy(), faces=inverse[np.asarray(mesh.faces)], process=False)
print("welded components", len(geo.split(only_watertight=False)))
trimesh.smoothing.filter_humphrey(geo, alpha=.12, beta=.55, iterations=8)
front = np.clip((mesh.vertices[:, 0] - .025) / .10, 0, 1)
strength = (1 - front) * .55
hair = (posed[:, 1] > .55) & (posed[:, 2] < -.12)
strength[hair] = 1.0
mesh.vertices = mesh.vertices * (1 - strength[:, None]) + geo.vertices[inverse] * strength[:, None]
# The comparison mask is wider at shoulder and upper-hair height than the
# reconstruction. Expand only that band, then taper back to the original face
# and waist widths.
upper_width = np.interp(posed[:, 1],
                        [.0, .95, 1.10, 1.20, 1.40, 1.73],
                        [1.0, 1.0, 1.10, 1.20, 1.0, 1.0])
mesh.vertices[:, 1] = mid_y + (mesh.vertices[:, 1] - mid_y) * upper_width
# Pixel-level front-outline comparison places the source head and shoulders
# about 30 pixels right of the reconstruction while the hips already line up.
# Shift the upper silhouette smoothly without moving the legs or foot contact.
upper_shift = np.interp(posed[:, 1],
                        [0.0, .95, 1.10, 1.37, 1.73],
                        [0.0, 0.0, .07, .09, .105])
lower_shift = np.interp(posed[:, 1],
                        [0.0, .20, .38, .50, 1.73],
                        [-.04, -.035, -.012, 0.0, 0.0])
mesh.vertices[:, 1] += (upper_shift + lower_shift) / scale
mesh.visual = trimesh.visual.TextureVisuals(uv=mesh.visual.uv, image=Image.open(OUT / "bayonetta1-image-model-texture.png"))
mesh.export(OUT / "bayonetta1-image-model.obj")
